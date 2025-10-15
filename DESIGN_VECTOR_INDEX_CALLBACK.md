# Vector Index Background Loading & Callback API Design

## Overview

HNSWインデックスのロードをDatabase初期化から分離し、バックグラウンドで非同期実行することで起動時間を短縮する。ロード完了時にコールバックで通知し、UI側でベクトル検索の使用可否を表示可能にする。

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│ Database::Database()                                        │
│   ├─ initMembers()                                          │
│   ├─ autoLoadLinkedExtensions()                             │
│   │   └─ VectorExtension::load()                            │
│   │       ├─ Register functions                             │
│   │       └─ std::thread::detach([]{                        │
│   │           initHNSWEntries();  // ← バックグラウンド実行 │
│   │           database->notifyVectorIndexLoadComplete();    │
│   │         })                                               │
│   └─ return (即座に完了)                                    │
└─────────────────────────────────────────────────────────────┘
        ↓ (非同期)
┌─────────────────────────────────────────────────────────────┐
│ Background Thread                                           │
│   ├─ initHNSWEntries() (parallel loading)                   │
│   └─ notifyVectorIndexLoadComplete(success, error)          │
│       └─ invoke callback → UI update                        │
└─────────────────────────────────────────────────────────────┘
```

## API Design

### 1. Callback Type Definition

**File**: `database.h` (line ~33, after SystemConfig)

```cpp
/**
 * @brief Callback function type for vector index loading completion
 * @param userData User-provided data pointer
 * @param success True if all vector indexes loaded successfully
 * @param errorMessage Error description if failed, nullptr if succeeded
 */
typedef void (*VectorIndexLoadCompletionCallback)(
    void* userData,
    bool success,
    const char* errorMessage
);
```

### 2. Database Class API

**File**: `database.h`

#### Public Methods (line ~148, after existing getters)

```cpp
/**
 * @brief Register callback for vector index loading completion
 *
 * If indexes are already loaded when this is called, the callback
 * will be invoked immediately on the calling thread.
 *
 * @param callback Function to call on completion (nullptr to unregister)
 * @param userData Opaque pointer passed to callback
 */
KUZU_API void setVectorIndexLoadCallback(
    VectorIndexLoadCompletionCallback callback,
    void* userData
);

/**
 * @brief Check if vector indexes have finished loading
 * @return true if loading completed (success or failure), false if still loading
 */
KUZU_API bool isVectorIndexesLoaded() const {
    return vectorIndexesLoaded.load(std::memory_order_acquire);
}

/**
 * @brief Check if vector indexes are ready for use
 * @return true if loaded successfully and ready for queries
 */
KUZU_API bool isVectorIndexesReady() const {
    return vectorIndexesLoaded.load(std::memory_order_acquire) &&
           vectorIndexesLoadSuccess.load(std::memory_order_acquire);
}
```

#### Internal Method (for VectorExtension)

```cpp
// Called by VectorExtension to notify loading completion
void notifyVectorIndexLoadComplete(bool success, const std::string& errorMsg = "");
```

#### Private Members (line ~200, end of private section)

```cpp
// Vector index loading state
std::atomic<bool> vectorIndexesLoaded{false};
std::atomic<bool> vectorIndexesLoadSuccess{false};
std::mutex vectorIndexCallbackMutex;
VectorIndexLoadCompletionCallback vectorIndexCallback = nullptr;
void* vectorIndexCallbackUserData = nullptr;
std::string vectorIndexLoadErrorMessage;
```

#### Required Headers (line ~1, top of file)

```cpp
#include <atomic>
#include <mutex>
```

### 3. Implementation

**File**: `database.cpp` (end of file)

```cpp
void Database::setVectorIndexLoadCallback(
    VectorIndexLoadCompletionCallback callback,
    void* userData
) {
    std::lock_guard<std::mutex> lock(vectorIndexCallbackMutex);
    vectorIndexCallback = callback;
    vectorIndexCallbackUserData = userData;

    // Already loaded? Invoke callback immediately
    if (vectorIndexesLoaded.load(std::memory_order_acquire)) {
        if (callback) {
            bool success = vectorIndexesLoadSuccess.load(std::memory_order_acquire);
            const char* errMsg = success ? nullptr : vectorIndexLoadErrorMessage.c_str();
            callback(userData, success, errMsg);
        }
    }
}

void Database::notifyVectorIndexLoadComplete(bool success, const std::string& errorMsg) {
    fprintf(stderr, "[KUZU DEBUG] Vector index loading %s\n",
            success ? "succeeded" : "failed");
    if (!success && !errorMsg.empty()) {
        fprintf(stderr, "[KUZU DEBUG]   Error: %s\n", errorMsg.c_str());
    }
    fflush(stderr);

    // Store result with memory barriers
    vectorIndexesLoadSuccess.store(success, std::memory_order_release);
    vectorIndexesLoaded.store(true, std::memory_order_release);
    if (!success) {
        vectorIndexLoadErrorMessage = errorMsg;
    }

    // Invoke callback under lock
    std::lock_guard<std::mutex> lock(vectorIndexCallbackMutex);
    if (vectorIndexCallback) {
        const char* errMsgPtr = success ? nullptr : vectorIndexLoadErrorMessage.c_str();
        vectorIndexCallback(vectorIndexCallbackUserData, success, errMsgPtr);
    }
}
```

### 4. VectorExtension Background Loading

**File**: `vector_extension.cpp`

```cpp
void VectorExtension::load(main::ClientContext* context) {
    auto& db = *context->getDatabase();

    // Register vector extension functions
    extension::ExtensionUtils::addTableFunc<QueryVectorIndexFunction>(db);
    extension::ExtensionUtils::addInternalStandaloneTableFunc<InternalCreateHNSWIndexFunction>(db);
    extension::ExtensionUtils::addInternalStandaloneTableFunc<InternalFinalizeHNSWIndexFunction>(db);
    extension::ExtensionUtils::addStandaloneTableFunc<CreateVectorIndexFunction>(db);
    extension::ExtensionUtils::addInternalStandaloneTableFunc<InternalDropHNSWIndexFunction>(db);
    extension::ExtensionUtils::addStandaloneTableFunc<DropVectorIndexFunction>(db);
    extension::ExtensionUtils::registerIndexType(db, OnDiskHNSWIndex::getIndexType());

    // Start background HNSW index loading
    fprintf(stderr, "[KUZU DEBUG] Starting HNSW index loading in background\n");
    fflush(stderr);

    std::thread([context]() {
        try {
            fprintf(stderr, "[KUZU DEBUG] Background thread: Loading HNSW indexes...\n");
            fflush(stderr);

            initHNSWEntries(context);

            fprintf(stderr, "[KUZU DEBUG] Background thread: Notifying success\n");
            fflush(stderr);
            context->getDatabase()->notifyVectorIndexLoadComplete(true);

        } catch (const std::exception& e) {
            fprintf(stderr, "[KUZU ERROR] Background thread: Loading failed: %s\n", e.what());
            fflush(stderr);
            context->getDatabase()->notifyVectorIndexLoadComplete(false, e.what());

        } catch (...) {
            fprintf(stderr, "[KUZU ERROR] Background thread: Unknown exception\n");
            fflush(stderr);
            context->getDatabase()->notifyVectorIndexLoadComplete(false, "Unknown error");
        }
    }).detach();

    fprintf(stderr, "[KUZU DEBUG] VectorExtension::load() completed, background loading started\n");
    fflush(stderr);
}
```

## Thread Safety

### Memory Ordering

- `std::atomic<bool>` with `memory_order_acquire/release` for lock-free reads
- Ensures visibility of `vectorIndexesLoadSuccess` when `vectorIndexesLoaded` is true

### Mutex Protection

- `vectorIndexCallbackMutex` protects:
  - Callback registration (`setVectorIndexLoadCallback`)
  - Callback invocation (`notifyVectorIndexLoadComplete`)
- Prevents race conditions when callback is changed during background loading

### Lifetime Guarantees

- Background thread uses `detach()` - must complete before Database destruction
- `ClientContext*` captured by value (pointer) - safe as Database outlives thread
- Error message stored in `std::string` before callback invocation

## Usage Examples

### C++ API

```cpp
// Create database
Database db("/path/to/db");

// Register callback
db.setVectorIndexLoadCallback([](void* userData, bool success, const char* error) {
    if (success) {
        printf("Vector indexes ready!\n");
    } else {
        printf("Vector index loading failed: %s\n", error);
    }
}, nullptr);

// Database is ready immediately, indexes load in background
```

### Swift API (Future Integration)

```swift
// GraphContainer.swift
public struct GraphContainer {
    @Published public private(set) var isVectorIndexReady = false

    init(for types: any _KuzuGraphModel.Type..., configuration: GraphConfiguration) throws {
        // ... initialization ...

        database.setVectorIndexLoadCallback { [weak self] success, error in
            DispatchQueue.main.async {
                if success {
                    self?.isVectorIndexReady = true
                } else {
                    print("Vector index loading failed: \(error ?? "unknown")")
                }
            }
        }
    }
}

// SwiftUI
struct ContentView: View {
    @Environment(\.graphContainer) var container

    var body: some View {
        if container.isVectorIndexReady {
            Text("✅ Vector search available")
        } else {
            ProgressView("Loading vector indexes...")
        }
    }
}
```

## Error Handling

### Success Case
```
Database init → VectorExtension::load() → Background thread starts → Database ready
                                                ↓ (async)
                                           initHNSWEntries() succeeds
                                                ↓
                                           notifyVectorIndexLoadComplete(true, "")
                                                ↓
                                           callback(userData, true, nullptr)
```

### Failure Case
```
Database init → VectorExtension::load() → Background thread starts → Database ready
                                                ↓ (async)
                                           initHNSWEntries() throws exception
                                                ↓
                                           catch (std::exception& e)
                                                ↓
                                           notifyVectorIndexLoadComplete(false, e.what())
                                                ↓
                                           callback(userData, false, "error message")
```

### Late Registration
```
Database init → Background loading completes → isVectorIndexesLoaded() == true
                                                ↓
                                           setVectorIndexLoadCallback(...)
                                                ↓
                                           Callback invoked immediately (synchronous)
```

## Performance Impact

### Before (Synchronous Loading)
```
Database::Database() - 10s
  ├─ initMembers() - 100ms
  ├─ VectorExtension::load() - 9.9s
  │   └─ initHNSWEntries() - 9.9s (blocks)
  └─ return
```

### After (Background Loading)
```
Database::Database() - 100ms
  ├─ initMembers() - 100ms
  ├─ VectorExtension::load() - <1ms
  │   └─ std::thread::detach() - <1ms
  └─ return (immediately)

Background Thread - 3s (parallel)
  └─ initHNSWEntries() - 3s (thread pool)
```

**Improvement**: Database initialization ~100x faster (10s → 100ms)

## Implementation Checklist

- [ ] Add callback typedef to `database.h`
- [ ] Add atomic state variables to Database class
- [ ] Add mutex for callback protection
- [ ] Add public API methods (`setVectorIndexLoadCallback`, `isVectorIndexesLoaded`, `isVectorIndexesReady`)
- [ ] Add internal notification method (`notifyVectorIndexLoadComplete`)
- [ ] Implement methods in `database.cpp`
- [ ] Modify `VectorExtension::load()` for background execution
- [ ] Add exception handling in background thread
- [ ] Verify compilation
- [ ] Test with real dataset
- [ ] Document in kuzu-swift README

## Future Enhancements

### Progress Reporting
```cpp
typedef void (*VectorIndexLoadProgressCallback)(void* userData, size_t loaded, size_t total);
void setVectorIndexLoadProgressCallback(VectorIndexLoadProgressCallback callback, void* userData);
```

### Cancellation Support
```cpp
void cancelVectorIndexLoading();
```

### Per-Index Callbacks
```cpp
typedef void (*VectorIndexSingleLoadCallback)(void* userData, const char* indexName, bool success);
void setVectorIndexSingleLoadCallback(VectorIndexSingleLoadCallback callback, void* userData);
```
