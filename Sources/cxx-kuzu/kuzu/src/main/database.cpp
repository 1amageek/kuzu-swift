#include "main/database.h"

#include <chrono>
#include "extension/binder_extension.h"
#include "extension/extension_manager.h"
#include "extension/mapper_extension.h"
#include "extension/planner_extension.h"
#include "extension/transformer_extension.h"
#include "main/client_context.h"
#include "main/database_manager.h"
#include "storage/buffer_manager/buffer_manager.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#if defined(__linux__)
#include <sys/resource.h>
#endif
#endif

#include "common/exception/exception.h"
#include "common/file_system/virtual_file_system.h"
#include "main/db_config.h"
#include "processor/processor.h"
#include "storage/storage_extension.h"
#include "storage/storage_manager.h"
#include "storage/storage_utils.h"
#include "transaction/transaction_manager.h"

using namespace kuzu::catalog;
using namespace kuzu::common;
using namespace kuzu::storage;
using namespace kuzu::transaction;

namespace kuzu {
namespace main {

SystemConfig::SystemConfig(uint64_t bufferPoolSize_, uint64_t maxNumThreads, bool enableCompression,
    bool readOnly, uint64_t maxDBSize, bool autoCheckpoint, uint64_t checkpointThreshold,
    bool forceCheckpointOnClose, bool throwOnWalReplayFailure, bool enableChecksums
#if defined(__APPLE__)
    ,
    uint32_t threadQos
#endif
    )
    : maxNumThreads{maxNumThreads}, enableCompression{enableCompression}, readOnly{readOnly},
      autoCheckpoint{autoCheckpoint}, checkpointThreshold{checkpointThreshold},
      forceCheckpointOnClose{forceCheckpointOnClose},
      throwOnWalReplayFailure(throwOnWalReplayFailure), enableChecksums(enableChecksums) {
#if defined(__APPLE__)
    this->threadQos = threadQos;
#endif
    if (bufferPoolSize_ == -1u || bufferPoolSize_ == 0) {
#if defined(_WIN32)
        MEMORYSTATUSEX status;
        status.dwLength = sizeof(status);
        GlobalMemoryStatusEx(&status);
        auto systemMemSize = (std::uint64_t)status.ullTotalPhys;
#else
        auto systemMemSize = static_cast<std::uint64_t>(sysconf(_SC_PHYS_PAGES)) *
                             static_cast<std::uint64_t>(sysconf(_SC_PAGESIZE));
#endif
        bufferPoolSize_ = static_cast<uint64_t>(
            BufferPoolConstants::DEFAULT_PHY_MEM_SIZE_RATIO_FOR_BM *
            static_cast<double>(std::min(systemMemSize, static_cast<uint64_t>(UINTPTR_MAX))));
        // On 32-bit systems or systems with extremely large memory, the buffer pool size may
        // exceed the maximum size of a VMRegion. In this case, we set the buffer pool size to
        // 80% of the maximum size of a VMRegion.
        bufferPoolSize_ = static_cast<uint64_t>(std::min(static_cast<double>(bufferPoolSize_),
            BufferPoolConstants::DEFAULT_VM_REGION_MAX_SIZE *
                BufferPoolConstants::DEFAULT_PHY_MEM_SIZE_RATIO_FOR_BM));
    }
    bufferPoolSize = bufferPoolSize_;
#ifndef __SINGLE_THREADED__
    if (maxNumThreads == 0) {
        this->maxNumThreads = std::thread::hardware_concurrency();
    }
#else
    // In single-threaded mode, even if the user specifies a number of threads,
    // it will be ignored and set to 0.
    this->maxNumThreads = 1;
#endif
    if (maxDBSize == -1u) {
        maxDBSize = BufferPoolConstants::DEFAULT_VM_REGION_MAX_SIZE;
    }
    this->maxDBSize = maxDBSize;
}

Database::Database(std::string_view databasePath, SystemConfig systemConfig)
    : Database(databasePath, systemConfig, initBufferManager) {}

Database::Database(std::string_view databasePath, SystemConfig systemConfig,
    construct_bm_func_t constructBMFunc)
    : dbConfig(systemConfig) {
    // Phase 1: Lightweight initialization (synchronous, 20-50ms)
    // Expand path
    const auto dbPathStr = std::string(databasePath);
    auto clientContext = ClientContext(this);
    this->databasePath = StorageUtils::expandPath(&clientContext, dbPathStr);

    if (std::filesystem::is_directory(this->databasePath)) {
        throw RuntimeException("Database path cannot be a directory: " + this->databasePath);
    }

    // Fast initialization
    vfs = std::make_unique<VirtualFileSystem>(this->databasePath);
    validatePathInReadOnly();
    bufferManager = constructBMFunc(*this);
    memoryManager = std::make_unique<MemoryManager>(bufferManager.get(), vfs.get());
#if defined(__APPLE__)
    queryProcessor = std::make_unique<processor::QueryProcessor>(dbConfig.maxNumThreads, dbConfig.threadQos);
#else
    queryProcessor = std::make_unique<processor::QueryProcessor>(dbConfig.maxNumThreads);
#endif
    catalog = std::make_unique<Catalog>();
    storageManager = std::make_unique<StorageManager>(this->databasePath, dbConfig.readOnly,
        dbConfig.enableChecksums, *memoryManager, dbConfig.enableCompression, vfs.get());
    transactionManager = std::make_unique<TransactionManager>(storageManager->getWAL());
    databaseManager = std::make_unique<DatabaseManager>();
    extensionManager = std::make_unique<extension::ExtensionManager>();
    dbLifeCycleManager = std::make_shared<DatabaseLifeCycleManager>();

    // In-memory database: complete immediately
    if (clientContext.isInMemory()) {
        storageManager->initDataFileHandle(vfs.get(), &clientContext);
        extensionManager->autoLoadLinkedExtensions(&clientContext);
        walReplayComplete_.store(true, std::memory_order_release);
        initComplete_.store(true, std::memory_order_release);
        return;  // Done in 20-50ms
    }

    // ========================================================================
    // DATABASE INITIALIZATION GUIDE
    // ========================================================================
    //
    // The initialization thread (initThread_) performs heavy I/O operations:
    //   - Phase 2: WAL replay and checkpoint reading (7+ seconds)
    //   - Phase 3-4: Extension loading including HNSW vector indexes
    //
    // These operations MUST run at low priority on ALL platforms to prevent:
    //   - iOS: UIApplicationMain blocking during app launch (7+ second delay)
    //   - macOS: Application startup appearing frozen to users
    //   - Linux: UI thread starvation during database initialization
    //   - Windows: Main thread blocking and poor responsiveness
    //
    // REQUIRED: Set thread priority IMMEDIATELY after thread creation
    //
    // Platform-specific APIs:
    //   - iOS/macOS:  pthread_set_qos_class_self_np(QOS_CLASS_UTILITY, 0)
    //                 Uses Apple's QoS system to signal background work
    //   - Linux:      setpriority(PRIO_PROCESS, 0, 10)
    //                 Increases nice value by +10 (lower CPU priority)
    //   - Windows:    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL)
    //                 Sets thread priority below normal execution level
    //
    // Testing checklist:
    //   - Measure app startup time on real devices (not simulators)
    //   - Profile with platform tools (Instruments/perf/ETW)
    //   - Verify UI remains responsive during database initialization
    //   - Test with large databases (1GB+) to ensure scalability
    //
    // ========================================================================

    // Persistent database: spawn background thread for Phases 2-4
    initThread_ = std::thread([this]() {
        // ====================================================================
        // CRITICAL: Set thread priority to background on ALL platforms
        // This prevents application startup blocking and UI thread starvation
        // ====================================================================
#if defined(__APPLE__)
        // macOS/iOS: QOS_CLASS_UTILITY signals this is background work
        // iOS will NOT wait for this thread during UIApplicationMain launch
        pthread_set_qos_class_self_np(QOS_CLASS_UTILITY, 0);
#elif defined(__linux__)
        // Linux: Increase nice value to deprioritize this thread
        // UI threads will get CPU time ahead of database initialization
        setpriority(PRIO_PROCESS, 0, 10);
#elif defined(_WIN32)
        // Windows: Lower thread priority to keep UI responsive
        // Main thread will not be blocked by database I/O operations
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
#endif
        auto clientContext = ClientContext(this);
        try {
            // ================================================================
            // Phase 2: WAL Replay & Checkpoint Reading (7+ seconds typically)
            // ================================================================
            // This phase performs heavy I/O operations:
            //   1. Replay write-ahead log (WAL) entries
            //   2. Read checkpoint (includes PrimaryKeyIndex loading)
            //   3. Deserialize catalog and storage metadata
            //
            // On iOS with large databases (50k+ photos), this takes 7+ seconds
            // Running at QOS_CLASS_UTILITY prevents UIApplicationMain blocking
            // ================================================================
            dbLifeCycleManager->isRecoveryInProgress.store(true, std::memory_order_release);

            StorageManager::recover(clientContext,
                dbConfig.throwOnWalReplayFailure,
                dbConfig.enableChecksums);

            // WAL replay complete - notify waiting threads
            // Queries can now execute (but vector indexes may still be loading)
            {
                std::lock_guard<std::mutex> lock(walReplayMutex_);
                walReplayComplete_.store(true, std::memory_order_release);
            }
            walReplayCV_.notify_all();

            // ================================================================
            // Phase 3 + 4: Extension & Vector Index Loading
            // ================================================================
            // Loads extensions including HNSW vector indexes
            // Since isRecoveryInProgress=true, HNSW loads synchronously here
            // This ensures vector indexes are ready before accepting queries
            // ================================================================
            extensionManager->autoLoadLinkedExtensions(&clientContext);

            // All phases complete - database is fully initialized
            dbLifeCycleManager->isRecoveryInProgress.store(false, std::memory_order_release);

            // Mark initialization complete and notify all waiting threads
            {
                std::lock_guard<std::mutex> lock(initMutex_);
                initComplete_.store(true, std::memory_order_release);
            }
            initCV_.notify_all();

        } catch (const Exception& e) {
            // ================================================================
            // Error Handling: Store error and notify waiting threads
            // ================================================================
            // Determine which phase failed for appropriate error reporting
            bool walReplayFailed = !walReplayComplete_.load(std::memory_order_acquire);

            if (walReplayFailed) {
                // Phase 2 (WAL replay) failed - store error and notify
                std::lock_guard<std::mutex> lock(walReplayMutex_);
                walReplayError_ = e.what();
                walReplayComplete_.store(true, std::memory_order_release);
                walReplayCV_.notify_all();
            }

            // Mark overall initialization as failed
            {
                std::lock_guard<std::mutex> lock(initMutex_);
                initError_ = e.what();
                initComplete_.store(true, std::memory_order_release);
            }
            dbLifeCycleManager->isRecoveryInProgress.store(false, std::memory_order_release);
            initCV_.notify_all();
        }
    });

    // ====================================================================
    // Constructor returns immediately (20-50ms on iOS)
    // The application can proceed with UI initialization while the
    // background thread handles database loading asynchronously
    // ====================================================================
}

std::unique_ptr<BufferManager> Database::initBufferManager(const Database& db) {
    return std::make_unique<BufferManager>(db.databasePath,
        StorageUtils::getTmpFilePath(db.databasePath), db.dbConfig.bufferPoolSize,
        db.dbConfig.maxDBSize, db.vfs.get(), db.dbConfig.readOnly);
}

void Database::waitForWALReplay() const {
    // Fast path: WAL replay already complete
    if (walReplayComplete_.load(std::memory_order_acquire)) {
        std::lock_guard<std::mutex> lock(walReplayMutex_);
        if (!walReplayError_.empty()) {
            throw Exception(walReplayError_);
        }
        return;
    }

    // Slow path: wait for WAL replay
    std::unique_lock<std::mutex> lock(walReplayMutex_);
    walReplayCV_.wait(lock, [this]() {
        return walReplayComplete_.load(std::memory_order_acquire);
    });

    if (!walReplayError_.empty()) {
        throw Exception(walReplayError_);
    }
}

void Database::waitForVectorIndexes() const {
    // Wait for both WAL replay AND HNSW loading
    waitForWALReplay();  // First ensure WAL is done

    // Then wait for full initialization (includes HNSW)
    if (initComplete_.load(std::memory_order_acquire)) {
        std::lock_guard<std::mutex> lock(initMutex_);
        if (!initError_.empty()) {
            throw Exception(initError_);
        }
        return;
    }

    std::unique_lock<std::mutex> lock(initMutex_);
    initCV_.wait(lock, [this]() {
        return initComplete_.load(std::memory_order_acquire);
    });

    if (!initError_.empty()) {
        throw Exception(initError_);
    }
}

void Database::waitForInitialization() const {
    // Fast path: already initialized
    if (initComplete_.load(std::memory_order_acquire)) {
        if (!initError_.empty()) {
            throw Exception(initError_);
        }
        return;
    }

    // Slow path: wait for initialization
    std::unique_lock<std::mutex> lock(initMutex_);
    initCV_.wait(lock, [this]() {
        return initComplete_.load(std::memory_order_acquire);
    });

    if (!initError_.empty()) {
        throw Exception(initError_);
    }
}

Database::~Database() {
    // Signal cancellation to background threads
    {
        std::lock_guard<std::mutex> lock(backgroundThreadStartMutex);
        vectorIndexLoadCancelled.store(true, std::memory_order_release);
        dbLifeCycleManager->isDatabaseClosed = true;
    }

    // Wait for initialization thread to finish
    if (initThread_.joinable()) {
        initThread_.join();
    }

    joinVectorIndexLoaderThread();

    if (!dbConfig.readOnly && dbConfig.forceCheckpointOnClose) {
        try {
            ClientContext clientContext(this);
            transactionManager->checkpoint(clientContext);
        } catch (...) {} // NOLINT
    }
}

void Database::setVectorIndexLoadCallback(
    VectorIndexLoadCompletionCallback callback,
    void* userData
) {
    std::lock_guard<std::mutex> lock(vectorIndexCallbackMutex);
    vectorIndexCallback = callback;
    vectorIndexCallbackUserData = userData;

    // Already loaded? Invoke immediately on calling thread
    if (vectorIndexesLoaded.load(std::memory_order_acquire)) {
        if (callback) {
            bool success = vectorIndexesLoadSuccess.load(std::memory_order_acquire);
            const char* errMsg = success ? nullptr : vectorIndexLoadErrorMessage.c_str();
            callback(userData, success, errMsg);
        }
    }
}

void Database::notifyVectorIndexLoadComplete(bool success, const std::string& errorMsg) {
    // Check vectorIndexLoadCancelled (atomic), not isDatabaseClosed
    if (vectorIndexLoadCancelled.load(std::memory_order_acquire)) {
        return;
    }

    // Store results with release semantics
    vectorIndexesLoadSuccess.store(success, std::memory_order_release);
    if (!success) {
        vectorIndexLoadErrorMessage = errorMsg;
    }
    vectorIndexesLoaded.store(true, std::memory_order_release);

    // Invoke callback if registered
    std::lock_guard<std::mutex> lock(vectorIndexCallbackMutex);
    if (vectorIndexCallback) {
        const char* errorMsgPtr = success ? nullptr : vectorIndexLoadErrorMessage.c_str();
        vectorIndexCallback(vectorIndexCallbackUserData, success, errorMsgPtr);
    }
}

// NOLINTNEXTLINE(readability-make-member-function-const): Semantically non-const function.
void Database::registerFileSystem(std::unique_ptr<FileSystem> fs) {
    vfs->registerFileSystem(std::move(fs));
}

// NOLINTNEXTLINE(readability-make-member-function-const): Semantically non-const function.
void Database::registerStorageExtension(std::string name,
    std::unique_ptr<StorageExtension> storageExtension) {
    extensionManager->registerStorageExtension(std::move(name), std::move(storageExtension));
}

// NOLINTNEXTLINE(readability-make-member-function-const): Semantically non-const function.
void Database::addExtensionOption(std::string name, LogicalTypeID type, Value defaultValue,
    bool isConfidential) {
    extensionManager->addExtensionOption(std::move(name), type, std::move(defaultValue),
        isConfidential);
}

void Database::addTransformerExtension(
    std::unique_ptr<extension::TransformerExtension> transformerExtension) {
    transformerExtensions.push_back(std::move(transformerExtension));
}

std::vector<extension::TransformerExtension*> Database::getTransformerExtensions() {
    std::vector<extension::TransformerExtension*> transformers;
    for (auto& transformerExtension : transformerExtensions) {
        transformers.push_back(transformerExtension.get());
    }
    return transformers;
}

void Database::addBinderExtension(
    std::unique_ptr<extension::BinderExtension> transformerExtension) {
    binderExtensions.push_back(std::move(transformerExtension));
}

std::vector<extension::BinderExtension*> Database::getBinderExtensions() {
    std::vector<extension::BinderExtension*> binders;
    for (auto& binderExtension : binderExtensions) {
        binders.push_back(binderExtension.get());
    }
    return binders;
}

void Database::addPlannerExtension(std::unique_ptr<extension::PlannerExtension> plannerExtension) {
    plannerExtensions.push_back(std::move(plannerExtension));
}

std::vector<extension::PlannerExtension*> Database::getPlannerExtensions() {
    std::vector<extension::PlannerExtension*> planners;
    for (auto& plannerExtension : plannerExtensions) {
        planners.push_back(plannerExtension.get());
    }
    return planners;
}

void Database::addMapperExtension(std::unique_ptr<extension::MapperExtension> mapperExtension) {
    mapperExtensions.push_back(std::move(mapperExtension));
}

std::vector<extension::MapperExtension*> Database::getMapperExtensions() {
    std::vector<extension::MapperExtension*> mappers;
    for (auto& mapperExtension : mapperExtensions) {
        mappers.push_back(mapperExtension.get());
    }
    return mappers;
}

std::vector<StorageExtension*> Database::getStorageExtensions() {
    return extensionManager->getStorageExtensions();
}

void Database::validatePathInReadOnly() const {
    if (dbConfig.readOnly) {
        if (DBConfig::isDBPathInMemory(databasePath)) {
            throw Exception("Cannot open an in-memory database under READ ONLY mode.");
        }
        if (!vfs->fileOrPathExists(databasePath)) {
            throw Exception("Cannot create an empty database under READ ONLY mode.");
        }
    }
}

uint64_t Database::getNextQueryID() {
    std::unique_lock lock(queryIDGenerator.queryIDLock);
    return queryIDGenerator.queryID++;
}

void Database::startVectorIndexLoader(std::thread loaderThread) {
    if (!loaderThread.joinable()) {
        return;
    }

    std::thread previous;
    {
        std::lock_guard<std::mutex> lock(vectorIndexLoaderMutex);
        previous = std::move(vectorIndexLoaderThread);
        vectorIndexLoaderThread = std::move(loaderThread);
    }

    if (previous.joinable()) {
        previous.join();
    }
}

void Database::joinVectorIndexLoaderThread() {
    std::thread loader;
    {
        std::lock_guard<std::mutex> lock(vectorIndexLoaderMutex);
        loader = std::move(vectorIndexLoaderThread);
    }
    if (loader.joinable()) {
        loader.join();
    }
}

} // namespace main
} // namespace kuzu
