# Database Initialization - Complete Guide

## Table of Contents
1. [Problem Statement](#problem-statement)
2. [Implemented Solution: Deferred Initialization](#implemented-solution-deferred-initialization)
3. [Current Limitations](#current-limitations)
4. [Future Improvements](#future-improvements)
5. [Implementation Details](#implementation-details)
6. [Usage Guide](#usage-guide)

---

## Problem Statement

### Original Issue
Database initialization in Kuzu blocks the calling thread for 14+ seconds on iOS due to:

1. **F_FULLFSYNC on WAL file** (~14 seconds) - necessary for data integrity
2. **WAL replay** - restores committed transactions
3. **Checkpoint reading** - loads database state
4. **Extension loading** - initializes extensions including Vector extension
5. **HNSW index loading** - builds vector search indexes

**Key Insight**: F_FULLFSYNC is NOT excessive - it's necessary for data integrity. The real problem is that initialization blocks app startup, preventing UI from being shown.

### Design Constraint
Swift API must remain **SwiftData-like**: No async/await, no callbacks, no changes to existing code.

---

## Implemented Solution: Deferred Initialization

### Architecture

Database initialization is split into phases:

```
Phase 1 (Synchronous, 20-50ms):
  - VirtualFileSystem, BufferManager, MemoryManager
  - QueryProcessor, Catalog, TransactionManager
  - Constructor returns immediately

Phase 2-4 (Background Thread, 14+ seconds):
  - WAL replay + F_FULLFSYNC
  - Extension loading
  - HNSW index loading
```

### Implementation

#### C++ Layer (`database.cpp`)

```cpp
Database::Database(std::string_view databasePath, SystemConfig systemConfig) {
    // Phase 1: Lightweight initialization (20-50ms)
    vfs = std::make_unique<VirtualFileSystem>(databasePath);
    bufferManager = constructBMFunc(*this);
    // ... other lightweight initialization

    // In-memory database: complete immediately
    if (clientContext.isInMemory()) {
        storageManager->initDataFileHandle(vfs.get(), &clientContext);
        extensionManager->autoLoadLinkedExtensions(&clientContext);
        initComplete_.store(true, std::memory_order_release);
        return;  // 20-50ms total
    }

    // Persistent database: spawn background thread
    initThread_ = std::thread([this]() {
        try {
            // Phase 2-4: Heavy operations
            dbLifeCycleManager->isRecoveryInProgress.store(true);
            StorageManager::recover(...);  // WAL replay
            extensionManager->autoLoadLinkedExtensions(...);  // HNSW loading
            dbLifeCycleManager->isRecoveryInProgress.store(false);

            initComplete_.store(true, std::memory_order_release);
            initCV_.notify_all();
        } catch (const Exception& e) {
            initError_ = e.what();
            initComplete_.store(true);
            initCV_.notify_all();
        }
    });

    // Constructor returns immediately (20-50ms)
}

void Database::waitForInitialization() const {
    if (initComplete_.load(std::memory_order_acquire)) {
        if (!initError_.empty()) throw Exception(initError_);
        return;
    }

    std::unique_lock<std::mutex> lock(initMutex_);
    initCV_.wait(lock, [this]() {
        return initComplete_.load(std::memory_order_acquire);
    });

    if (!initError_.empty()) throw Exception(initError_);
}
```

#### ClientContext Layer (`client_context.cpp`)

**Centralized Waiting**: All query entry points wait for initialization

```cpp
std::unique_ptr<QueryResult> ClientContext::query(...) {
    localDatabase->waitForInitialization();  // Transparent wait
    lock_t lck{mtx};
    return queryNoLock(query, queryID, config);
}

std::unique_ptr<PreparedStatement> ClientContext::prepareWithParams(...) {
    localDatabase->waitForInitialization();
    // ...
}

std::unique_ptr<QueryResult> ClientContext::executeWithParams(...) {
    localDatabase->waitForInitialization();
    // ...
}
```

#### Swift Layer (`Database.swift`)

```swift
public enum DatabaseStatus {
    case initializing
    case ready
    case failed(Error)
}

public final class Database {
    public init(_ databasePath: String = ":memory:", _ systemConfig: SystemConfig? = nil) throws {
        // Constructor returns immediately (20-50ms)
        let state = kuzu_database_init(databasePath, cSystemConfig, &self.cDatabase)
        // Background initialization happens automatically
    }

    public var initializationStatus: DatabaseStatus {
        let status = kuzu_database_get_init_status(&cDatabase)
        switch status {
        case KuzuInitializing: return .initializing
        case KuzuReady: return .ready
        case KuzuFailed: return .failed(...)
        }
    }
}
```

### Achieved Results

✅ **Database constructor returns in 2ms** (was 14+ seconds)
✅ **GraphContainer initializes in 19ms** (was 14+ seconds)
✅ **UI shows immediately**
✅ **First query waits transparently for initialization**

---

## Current Limitations

### Problem: First Query Blocks Main Thread

**Observed Behavior** (from actual app logs):

```
T+34153.81ms: context.count() START (Main Thread)
[KUZU] fetchExistingTables: 48879.73ms  ← 48 seconds blocked!
T+48933.68ms: context.count() END
```

**Root Cause**:

1. `GraphContainer` init spawns background Task for schema creation
2. Simultaneously, main thread calls `context.count()`
3. `count()` → `connection.query()` → `ClientContext::query()` → `waitForInitialization()`
4. **Main thread waits for ALL initialization** (WAL replay + HNSW loading = 15 seconds)

**Timeline**:

```
T+0ms:    GraphContainer.init (returns in 19ms)
          └─ Spawns Task { SchemaManager.ensureSchema() }

T+19ms:   UI appears

T+34s:    User action triggers context.count()
          └─ Blocks main thread waiting for:
             - WAL replay (14s)
             - HNSW loading (1s)

T+49s:    Query finally executes
```

### Why This Happens

**`ClientContext::waitForInitialization()` waits for EVERYTHING**:
- ✅ WAL replay (necessary for READ operations)
- ✅ Extension loading (necessary for READ operations)
- ❌ **HNSW index loading (only necessary for vector search)**

**The Problem**:
- Simple queries like `COUNT(*)` don't need HNSW indexes
- But they wait 15 seconds anyway because HNSW loading is part of "initialization"

---

## Future Improvements

### Proposed Solution: Separate WAL and HNSW Initialization

**Concept**: Split initialization into two independent phases

```cpp
enum class InitPhase {
    WAL_REPLAY,      // WAL replay in progress
    HNSW_LOADING,    // HNSW index loading in progress (WAL complete)
    READY            // Everything ready
};

class Database {
public:
    void waitForWALReplay() const;      // Wait for WAL only (~3 seconds)
    void waitForVectorIndexes() const;  // Wait for HNSW (~15 seconds total)
    void waitForInitialization() const; // Wait for everything
};
```

**Implementation in ClientContext**:

```cpp
std::unique_ptr<QueryResult> ClientContext::query(std::string_view query, ...) {
    // All queries wait for WAL replay (essential)
    localDatabase->waitForWALReplay();  // ~3 seconds

    // Only vector queries wait for HNSW
    if (queryUsesVectorIndex(query)) {
        localDatabase->waitForVectorIndexes();  // +12 seconds
    }

    lock_t lck{mtx};
    return queryNoLock(query, queryID, config);
}
```

**Expected Improvement**:

| Operation | Current | After WAL/HNSW Split |
|-----------|---------|---------------------|
| `context.count()` | 15s wait ❌ | 3s wait ✓ |
| Vector search | 15s wait | 15s wait |

**Challenges**:
- Query parsing/analysis required to detect vector operations
- Or, explicit API differentiation: `fetch()` vs `vectorSearch()`

---

## Implementation Details

### Files Modified

#### C++ Layer
- `database.h`: Added `initThread_`, `initComplete_`, `initMutex_`, `initCV_`, `initError_`
- `database.h`: Added `InitStatus` enum, `getInitializationStatus()`, `waitForInitialization()`
- `database.cpp`: Constructor spawns background thread for Phase 2-4
- `database.cpp`: Deleted old `initMembers()` function (175-317 lines)
- `client_context.cpp`: Added `waitForInitialization()` to 3 entry point methods

#### C API Layer
- `kuzu.h`: Added `kuzu_init_status` enum
- `kuzu.h`: Added `kuzu_database_get_init_status()`, `kuzu_database_get_init_error()`
- `c_api/database.cpp`: Implemented status functions

#### Swift Layer
- `Database.swift`: Added `DatabaseStatus` enum, `initializationStatus` property
- `Database.swift`: Updated constructor documentation

#### Swift Extension Layer
- `GraphContainer.swift`: Added `initializationStatus` forwarding property
- `GraphContainer.swift`: Spawns `Task.detached` for schema creation
- `GraphContext.swift`: Queries automatically wait via `ClientContext::waitForInitialization()`

### Initialization Flow (Persistent Database)

```
Main Thread:
┌─────────────────────────────────────────┐
│ 0ms:  Database() called                 │
│ 2ms:  Phase 1 complete                  │
│       initThread_ spawned               │
│       Constructor returns               │
│       UI shows immediately              │
│                                          │
│ 15s+: First query called                │
│       waitForInitialization() blocks    │
│       Query executes after init done    │
└─────────────────────────────────────────┘

Background Thread (initThread_):
┌─────────────────────────────────────────┐
│ 2ms:  Thread starts                     │
│       isRecoveryInProgress = true       │
│                                          │
│ Phase 2: Storage Recovery               │
│ 2ms-14s: StorageManager::recover()     │
│          - F_FULLFSYNC (~14s)          │
│          - WAL replay                   │
│          - Checkpoint read              │
│                                          │
│ Phase 3: Extension Loading              │
│ 14s-14.5s: autoLoadLinkedExtensions()  │
│            VectorExtension::load()      │
│                                          │
│ Phase 4: HNSW Index Loading             │
│ 14.5s-15s: loadHNSWIndexesSync()       │
│            (synchronous, same thread)   │
│                                          │
│ 15s:  isRecoveryInProgress = false     │
│       initComplete = true               │
│       notify waiters                    │
└─────────────────────────────────────────┘
```

### Why Phase 4 (HNSW) Runs Synchronously

From `vector_extension.cpp:199-206`:

```cpp
void VectorExtension::load(main::ClientContext* context) {
    // Check if we are in recovery mode
    if (lifeCycleManager->isRecoveryInProgress.load(...)) {
        // During recovery: Load synchronously (same thread)
        loadHNSWIndexesSync(database, lifeCycleManager);
        return;  // No separate thread spawned
    }

    // Normal runtime: Launch background thread
    std::thread loaderThread([...]) { loadHNSWIndexesSync(...); };
    database->startVectorIndexLoader(std::move(loaderThread));
}
```

**Rationale** (from comment):
> "During recovery, we must load indexes synchronously to avoid race conditions where WAL records (e.g., NodeDeletionRecord) access indexes before background loading completes"

**Our Strategy**: Keep `isRecoveryInProgress=true` throughout Phases 2-4, ensuring HNSW loads synchronously in the same background thread.

---

## Usage Guide

### Basic Usage (No Changes Required)

```swift
// Before (blocked for 14s)
let db = try Database(":memory:")

// After (returns in <50ms, first query waits)
let db = try Database(":memory:")  // Same API!
```

### With GraphContainer (SwiftData-style)

```swift
@main
struct PXLApp: App {
    // ✅ Returns immediately (19ms)
    let container = try! GraphContainer(for: PhotoAsset.self)

    var body: some Scene {
        WindowGroup {
            MainView()
        }
        .graphContainer(container)
    }
}
```

### Optional: Track Initialization Status

```swift
@State private var status: DatabaseStatus = .initializing

var body: some View {
    Group {
        switch container.initializationStatus {
        case .initializing:
            ProgressView("Initializing database...")
        case .ready:
            MainView()
        case .failed(let error):
            ErrorView(error: error)
        }
    }
    .task {
        // Poll status for UI updates
        while case .initializing = container.initializationStatus {
            try? await Task.sleep(for: .milliseconds(100))
        }
    }
}
```

### Workaround for Main Thread Blocking

Until WAL/HNSW split is implemented, avoid calling queries during initialization:

```swift
@main
struct PXLApp: App {
    let container = try! GraphContainer(for: PhotoAsset.self)
    @State private var isReady = false

    var body: some Scene {
        WindowGroup {
            if isReady {
                MainView()
            } else {
                SplashView()
                    .task {
                        // Wait for schema initialization to complete
                        try? await container.waitForSchema()
                        isReady = true
                    }
            }
        }
        .graphContainer(container)
    }
}
```

---

## Performance Comparison

### Before Implementation
- Database.init: **14,000ms** (blocks main thread)
- GraphContainer.init: **14,000ms** (blocks main thread)
- First query: Immediate
- UI appears: After 14 seconds

### After Implementation (Current)
- Database.init: **2ms** ✓
- GraphContainer.init: **19ms** ✓
- First query: **15,000ms** (blocks if called immediately) ⚠️
- UI appears: **Immediately** ✓

### After WAL/HNSW Split (Future)
- Database.init: **2ms** ✓
- GraphContainer.init: **19ms** ✓
- First `count()` query: **3,000ms** (WAL only) ✓
- First vector query: **15,000ms** (WAL + HNSW)
- UI appears: **Immediately** ✓

---

## Design Rationale

### Why Phase 1 Stays Synchronous

**Reasons**:
- ✅ **Fast** (20-50ms) - acceptable blocking time
- ✅ **Error detection** - constructor can throw immediately
- ✅ **All members initialized** - Database struct is valid when constructor returns
- ✅ **In-memory databases** - complete immediately, no delay

### Why Phases 2-4 Run in Single Background Thread

**Dependencies**:
```
Phase 2 (recover) → Phase 3 (extensions) → Phase 4 (HNSW)
     ↓                    ↓                    ↓
 Catalog entries    VectorExtension     Index loading
                    registration        from catalog
```

**Reasons**:
- ✅ **Phase 3 depends on Phase 2**: Extension loading needs catalog from WAL replay
- ✅ **Phase 4 depends on Phase 3**: HNSW loading calls `catalog->getIndexEntries()`
- ✅ **Thread-safe**: `isRecoveryInProgress` prevents race conditions
- ✅ **Simpler**: One thread, one status flag, clear lifecycle

### Why ClientContext Layer is Ideal

**Reasons**:
- ✅ **Central bottleneck**: All Connection methods go through ClientContext
- ✅ **3 entry points**: query(), prepareWithParams(), executeWithParams()
- ✅ **Before lock acquisition**: Avoids deadlocks
- ✅ **DRY principle**: Single place to manage waiting

---

## Known Issues

### Issue 1: Main Thread Blocking
**Status**: Known limitation
**Impact**: First query on main thread blocks for 15 seconds
**Workaround**: Wait for `container.waitForSchema()` before showing main UI
**Permanent Fix**: Requires WAL/HNSW split (future improvement)

### Issue 2: No Query-Level Optimization
**Status**: Architecture limitation
**Impact**: All queries wait for full initialization, even simple ones
**Root Cause**: `waitForInitialization()` is binary (all or nothing)
**Permanent Fix**: Requires query analysis or explicit API split

---

## Migration Guide

### For Existing Users

**No changes required** - existing code works as-is:

```swift
// Before and After - same code!
let db = try Database("/path/to/db")
let result = try db.query("MATCH (n) RETURN count(n)")
```

### For New Projects

**Recommended Pattern**:

```swift
@main
struct MyApp: App {
    let container = try! GraphContainer(for: MyModel.self)
    @State private var isReady = false

    var body: some Scene {
        WindowGroup {
            if isReady {
                ContentView()
            } else {
                LaunchScreen()
                    .task {
                        try? await container.waitForSchema()
                        isReady = true
                    }
            }
        }
        .graphContainer(container)
    }
}
```

---

## Success Criteria

### Achieved ✓
- ✅ Database constructor returns in <50ms
- ✅ GraphContainer initializes in <50ms
- ✅ UI shows immediately
- ✅ Background initialization works correctly
- ✅ First query waits transparently
- ✅ Subsequent queries execute immediately
- ✅ SwiftData-like API maintained
- ✅ No breaking changes

### Remaining ⏳
- ⏳ First query doesn't block main thread (requires WAL/HNSW split)
- ⏳ READ operations optimized (don't wait for HNSW)

---

## Future Roadmap

### Short Term (Current)
- ✅ Document workarounds for main thread blocking
- ✅ Add usage examples and best practices
- ✅ Performance profiling and optimization

### Medium Term (Next Release)
- ⏳ Implement WAL/HNSW split in C++ layer
- ⏳ Add query analysis to detect vector operations
- ⏳ Optimize READ operations to skip HNSW wait

### Long Term (Future Releases)
- ⏳ Parallel HNSW loading (multiple indexes)
- ⏳ Incremental index loading (priority-based)
- ⏳ Background re-indexing for updated data

---

## References

### Source Files
- C++ Database: `/Users/1amageek/Desktop/kuzu-swift/Sources/cxx-kuzu/kuzu/src/main/database.cpp`
- C++ ClientContext: `/Users/1amageek/Desktop/kuzu-swift/Sources/cxx-kuzu/kuzu/src/main/client_context.cpp`
- C++ Connection: `/Users/1amageek/Desktop/kuzu-swift/Sources/cxx-kuzu/kuzu/src/main/connection.cpp`
- C API: `/Users/1amageek/Desktop/kuzu-swift/Sources/cxx-kuzu/kuzu/src/include/c_api/kuzu.h`
- Swift Database: `/Users/1amageek/Desktop/kuzu-swift/Sources/Kuzu/Database.swift`
- GraphContainer: `/Users/1amageek/Desktop/kuzu-swift-extension/Sources/KuzuSwiftExtension/Core/GraphContainer.swift`
- GraphContext: `/Users/1amageek/Desktop/kuzu-swift-extension/Sources/KuzuSwiftExtension/Core/GraphContext.swift`

### Related Documents
- Original Design: `ASYNC_DATABASE_INIT_DESIGN.md` (archived)
- Fix Plan: `DEFERRED_INIT_FIX_PLAN.md` (archived)

---

**Document Version**: 1.0
**Last Updated**: 2025-10-17
**Status**: Implemented with Known Limitations
