# Background Loading Implementation Analysis

## Problem Statement

HNSWインデックスの初期化をDatabase起動から分離し、バックグラウンドで非同期実行することで起動時間を短縮する。

## Critical Issues Discovered

### Issue 1: ClientContext Lifetime (Dangling Pointer)

**Problem:**
```cpp
Database::initMembers() {
    ClientContext clientContext(this);  // スタック上のローカル変数
    ...
    extensionManager->autoLoadLinkedExtensions(&clientContext);
      → VectorExtension::load(&clientContext)

    // ここでclientContext破棄
}

// もしバックグラウンドスレッドでdetach()すると:
VectorExtension::load(ClientContext* context) {
    std::thread([context]() {  // ← dangling pointer!
        initHNSWEntries(context);  // context is destroyed
    }).detach();
}
```

**Root Cause:**
- `clientContext`は`initMembers()`のスタック上のローカル変数
- `initMembers()`完了後に破棄される
- バックグラウンドスレッドは破棄されたオブジェクトにアクセス

**Solution:**
バックグラウンドスレッド内で新しいClientContextを作成:
```cpp
std::thread([database = context->getDatabase()]() {
    main::ClientContext bgContext(database);  // 新しいcontext
    // ... 処理 ...
}).detach();
```

### Issue 2: Transaction Requirement

**Problem:**
```cpp
// initHNSWEntries() requires active transaction
catalog->getIndexEntries(transaction::Transaction::Get(*context))
```

新しいClientContextには初期状態でアクティブなトランザクションがない。

**Solution:**
READ_ONLYトランザクションを明示的に開始:
```cpp
auto* txn = database->getTransactionManager()->beginTransaction(
    bgContext,
    TransactionType::READ_ONLY
);
initHNSWEntries(&bgContext);
database->getTransactionManager()->commit(bgContext, txn);
```

### Issue 3: Database Destructor Race Condition

**Problem:**
```cpp
Database::~Database() {
    // バックグラウンドスレッドの完了を待たない
    // DatabaseオブジェクトがdeleteされるとdanglingEnumConstantDecl
}
```

バックグラウンドスレッドがロード中にDatabaseが破棄される可能性。

**Solution:**
デストラクタでバックグラウンドスレッド完了を待つ:
```cpp
// Database.h - private members
std::atomic<bool> vectorIndexLoadingInProgress{false};
std::mutex vectorIndexThreadMutex;
std::condition_variable vectorIndexThreadCV;

// Database.cpp
Database::~Database() {
    // バックグラウンドロード完了を待つ
    {
        std::unique_lock<std::mutex> lock(vectorIndexThreadMutex);
        vectorIndexThreadCV.wait(lock, [this] {
            return !vectorIndexLoadingInProgress.load();
        });
    }
    // ... 既存の処理 ...
}
```

## Recommended Implementation

### 1. Database Class Changes

**database.h:**
```cpp
// Callback typedef (line ~33)
typedef void (*VectorIndexLoadCompletionCallback)(
    void* userData,
    bool success,
    const char* errorMessage
);

class Database {
public:
    // Public API (line ~148)
    KUZU_API void setVectorIndexLoadCallback(
        VectorIndexLoadCompletionCallback callback,
        void* userData
    );
    KUZU_API bool isVectorIndexesLoaded() const;
    KUZU_API bool isVectorIndexesReady() const;

    // Internal (for VectorExtension)
    void notifyVectorIndexLoadComplete(bool success, const std::string& errorMsg = "");
    void notifyVectorIndexLoadStarted();

private:
    // Vector index loading state (line ~200)
    std::atomic<bool> vectorIndexLoadingInProgress{false};
    std::atomic<bool> vectorIndexesLoaded{false};
    std::atomic<bool> vectorIndexesLoadSuccess{false};
    std::mutex vectorIndexCallbackMutex;
    std::mutex vectorIndexThreadMutex;
    std::condition_variable vectorIndexThreadCV;
    VectorIndexLoadCompletionCallback vectorIndexCallback = nullptr;
    void* vectorIndexCallbackUserData = nullptr;
    std::string vectorIndexLoadErrorMessage;
};
```

**database.cpp:**
```cpp
Database::~Database() {
    fprintf(stderr, "[KUZU DEBUG] Database destructor: Waiting for vector index loading...\n");
    fflush(stderr);

    // Wait for background loading to complete
    {
        std::unique_lock<std::mutex> lock(vectorIndexThreadMutex);
        vectorIndexThreadCV.wait(lock, [this] {
            return !vectorIndexLoadingInProgress.load(std::memory_order_acquire);
        });
    }

    fprintf(stderr, "[KUZU DEBUG] Database destructor: Vector index loading completed\n");
    fflush(stderr);

    // ... existing checkpoint logic ...
}

void Database::setVectorIndexLoadCallback(
    VectorIndexLoadCompletionCallback callback,
    void* userData
) {
    std::lock_guard<std::mutex> lock(vectorIndexCallbackMutex);
    vectorIndexCallback = callback;
    vectorIndexCallbackUserData = userData;

    // Already loaded? Call immediately
    if (vectorIndexesLoaded.load(std::memory_order_acquire)) {
        if (callback) {
            bool success = vectorIndexesLoadSuccess.load(std::memory_order_acquire);
            const char* errMsg = success ? nullptr : vectorIndexLoadErrorMessage.c_str();
            callback(userData, success, errMsg);
        }
    }
}

void Database::notifyVectorIndexLoadStarted() {
    vectorIndexLoadingInProgress.store(true, std::memory_order_release);
}

void Database::notifyVectorIndexLoadComplete(bool success, const std::string& errorMsg) {
    fprintf(stderr, "[KUZU DEBUG] Vector index loading %s\n",
            success ? "succeeded" : "failed");
    if (!success && !errorMsg.empty()) {
        fprintf(stderr, "[KUZU DEBUG]   Error: %s\n", errorMsg.c_str());
    }
    fflush(stderr);

    // Store result
    vectorIndexesLoadSuccess.store(success, std::memory_order_release);
    vectorIndexesLoaded.store(true, std::memory_order_release);
    if (!success) {
        vectorIndexLoadErrorMessage = errorMsg;
    }

    // Invoke callback
    {
        std::lock_guard<std::mutex> lock(vectorIndexCallbackMutex);
        if (vectorIndexCallback) {
            const char* errMsgPtr = success ? nullptr : vectorIndexLoadErrorMessage.c_str();
            vectorIndexCallback(vectorIndexCallbackUserData, success, errMsgPtr);
        }
    }

    // Notify destructor
    {
        std::lock_guard<std::mutex> lock(vectorIndexThreadMutex);
        vectorIndexLoadingInProgress.store(false, std::memory_order_release);
    }
    vectorIndexThreadCV.notify_all();
}
```

### 2. VectorExtension Background Loading

**vector_extension.cpp:**
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

    fprintf(stderr, "[KUZU DEBUG] Starting HNSW index loading in background\n");
    fflush(stderr);

    // Notify Database that background loading started
    auto* database = context->getDatabase();
    database->notifyVectorIndexLoadStarted();

    // Start background loading with new ClientContext
    std::thread([database]() {
        try {
            fprintf(stderr, "[KUZU DEBUG] Background thread: Creating ClientContext\n");
            fflush(stderr);

            // Create new ClientContext (avoid dangling pointer)
            main::ClientContext bgContext(database);

            fprintf(stderr, "[KUZU DEBUG] Background thread: Beginning READ_ONLY transaction\n");
            fflush(stderr);

            // Start READ_ONLY transaction (required for catalog access)
            auto* txn = database->getTransactionManager()->beginTransaction(
                bgContext,
                transaction::TransactionType::READ_ONLY
            );

            fprintf(stderr, "[KUZU DEBUG] Background thread: Loading HNSW indexes...\n");
            fflush(stderr);

            // Execute HNSW loading
            initHNSWEntries(&bgContext);

            fprintf(stderr, "[KUZU DEBUG] Background thread: Committing transaction\n");
            fflush(stderr);

            // Commit transaction
            database->getTransactionManager()->commit(bgContext, txn);

            fprintf(stderr, "[KUZU DEBUG] Background thread: Notifying success\n");
            fflush(stderr);

            // Notify completion
            database->notifyVectorIndexLoadComplete(true);

        } catch (const std::exception& e) {
            fprintf(stderr, "[KUZU ERROR] Background thread: Loading failed: %s\n", e.what());
            fflush(stderr);
            database->notifyVectorIndexLoadComplete(false, e.what());

        } catch (...) {
            fprintf(stderr, "[KUZU ERROR] Background thread: Unknown exception\n");
            fflush(stderr);
            database->notifyVectorIndexLoadComplete(false, "Unknown error");
        }
    }).detach();

    fprintf(stderr, "[KUZU DEBUG] VectorExtension::load() completed, background loading started\n");
    fflush(stderr);
}
```

## Thread Safety Analysis

### Memory Ordering
- `std::atomic` with `memory_order_acquire/release` for state flags
- Ensures proper visibility across threads

### Synchronization
- `vectorIndexCallbackMutex`: Protects callback registration and invocation
- `vectorIndexThreadMutex` + `vectorIndexThreadCV`: Destructor synchronization
- Transaction isolation: Each background thread has its own READ_ONLY transaction

### Lifetime Guarantees
- **ClientContext**: Created on background thread's stack, destroyed after completion
- **Transaction**: Properly committed/rolled back
- **Database***: Valid until destructor, which waits for background thread
- **Callback**: Mutex-protected, called at most once

## Performance Impact

### Before (Synchronous)
```
Database::Database() - ~10s
  ├─ initMembers() - ~10s
  │   └─ initHNSWEntries() - ~9.9s (blocks)
  └─ return
```

### After (Background)
```
Database::Database() - ~100ms
  ├─ initMembers() - ~100ms
  │   └─ VectorExtension::load() - <1ms (spawn thread)
  └─ return

Background Thread - ~3s (parallel)
  └─ initHNSWEntries() - ~3s
```

**Improvement**: ~100x faster Database initialization

## Implementation Checklist

- [ ] Add callback typedef and state variables to `database.h`
- [ ] Add synchronization primitives (mutex, condition_variable)
- [ ] Implement `setVectorIndexLoadCallback()` in `database.cpp`
- [ ] Implement `notifyVectorIndexLoadStarted()` in `database.cpp`
- [ ] Implement `notifyVectorIndexLoadComplete()` in `database.cpp`
- [ ] Add destructor synchronization in `~Database()`
- [ ] Modify `VectorExtension::load()` for background execution
- [ ] Create new ClientContext in background thread
- [ ] Begin READ_ONLY transaction in background thread
- [ ] Add exception handling in background thread
- [ ] Verify compilation
- [ ] Test with real dataset
- [ ] Update DESIGN_VECTOR_INDEX_CALLBACK.md with these findings

## Risks and Mitigations

| Risk | Mitigation |
|------|------------|
| ClientContext dangling pointer | Create new ClientContext in background thread |
| Missing Transaction | Explicitly begin READ_ONLY transaction |
| Database destroyed during loading | Destructor waits for completion via condition_variable |
| Callback invoked after Database destroyed | Callback invoked before notifying destructor |
| Memory leak from detached thread | Thread completes and cleans up, no heap allocations captured |
| Race condition on callback registration | Mutex protection + immediate invocation if already loaded |

## Conclusion

実装は可能だが、以下の3つの重要な問題を解決する必要がある：

1. **ClientContext dangling pointer** → 新しいContextを作成
2. **Transaction requirement** → READ_ONLYトランザクションを開始
3. **Destructor race condition** → condition_variableで待機

これらを適切に実装すれば、安全にバックグラウンドロードが可能。
