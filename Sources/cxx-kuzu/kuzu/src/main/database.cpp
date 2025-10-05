#include "main/database.h"

#include "extension/binder_extension.h"
#include "extension/extension_manager.h"
#include "extension/mapper_extension.h"
#include "extension/planner_extension.h"
#include "extension/transformer_extension.h"
#include "main/client_context.h"
#include "main/database_manager.h"
#include "storage/buffer_manager/buffer_manager.h"

#include <iostream>

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
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
    fprintf(stderr, "[KUZU DEBUG] Database constructor called\n");
    fprintf(stderr, "[KUZU DEBUG]   Path: %s\n", std::string(databasePath).c_str());
    fprintf(stderr, "[KUZU DEBUG]   readOnly: %d\n", systemConfig.readOnly);
    fprintf(stderr, "[KUZU DEBUG]   autoCheckpoint: %d\n", systemConfig.autoCheckpoint);
    fprintf(stderr, "[KUZU DEBUG]   checkpointThreshold: %llu\n", systemConfig.checkpointThreshold);
    fprintf(stderr, "[KUZU DEBUG]   forceCheckpointOnClose: %d\n", systemConfig.forceCheckpointOnClose);
    fflush(stderr);

    initMembers(databasePath, constructBMFunc);

    fprintf(stderr, "[KUZU DEBUG] Database initialized successfully\n");
    fflush(stderr);
}

std::unique_ptr<BufferManager> Database::initBufferManager(const Database& db) {
    return std::make_unique<BufferManager>(db.databasePath,
        StorageUtils::getTmpFilePath(db.databasePath), db.dbConfig.bufferPoolSize,
        db.dbConfig.maxDBSize, db.vfs.get(), db.dbConfig.readOnly);
}

void Database::initMembers(std::string_view dbPath, construct_bm_func_t initBmFunc) {
    fprintf(stderr, "[KUZU DEBUG] initMembers() START\n");
    fflush(stderr);

    // To expand a path with home directory(~), we have to pass in a dummy clientContext which
    // handles the home directory expansion.
    const auto dbPathStr = std::string(dbPath);
    auto clientContext = ClientContext(this);
    databasePath = StorageUtils::expandPath(&clientContext, dbPathStr);

    fprintf(stderr, "[KUZU DEBUG] initMembers() - expanded path: %s\n", databasePath.c_str());
    fprintf(stderr, "[KUZU DEBUG] initMembers() - checking if path exists...\n");
    fflush(stderr);

    bool pathExists = std::filesystem::exists(databasePath);
    bool isDirectory = pathExists && std::filesystem::is_directory(databasePath);
    bool isFile = pathExists && std::filesystem::is_regular_file(databasePath);

    fprintf(stderr, "[KUZU DEBUG] initMembers() - exists=%d, isDirectory=%d, isFile=%d\n",
        pathExists, isDirectory, isFile);
    fflush(stderr);

    if (std::filesystem::is_directory(databasePath)) {
        throw RuntimeException("Database path cannot be a directory: " + databasePath);
    }

    fprintf(stderr, "[KUZU DEBUG] initMembers() - creating VirtualFileSystem...\n");
    fflush(stderr);
    vfs = std::make_unique<VirtualFileSystem>(databasePath);

    fprintf(stderr, "[KUZU DEBUG] initMembers() - validating path in read-only mode...\n");
    fflush(stderr);
    validatePathInReadOnly();

    fprintf(stderr, "[KUZU DEBUG] initMembers() - creating BufferManager...\n");
    fflush(stderr);
    bufferManager = initBmFunc(*this);

    fprintf(stderr, "[KUZU DEBUG] initMembers() - creating MemoryManager...\n");
    fflush(stderr);
    memoryManager = std::make_unique<MemoryManager>(bufferManager.get(), vfs.get());

#if defined(__APPLE__)
    fprintf(stderr, "[KUZU DEBUG] initMembers() - creating QueryProcessor (Apple)...\n");
    fflush(stderr);
    queryProcessor =
        std::make_unique<processor::QueryProcessor>(dbConfig.maxNumThreads, dbConfig.threadQos);
#else
    fprintf(stderr, "[KUZU DEBUG] initMembers() - creating QueryProcessor...\n");
    fflush(stderr);
    queryProcessor = std::make_unique<processor::QueryProcessor>(dbConfig.maxNumThreads);
#endif

    fprintf(stderr, "[KUZU DEBUG] initMembers() - creating Catalog...\n");
    fflush(stderr);
    catalog = std::make_unique<Catalog>();

    fprintf(stderr, "[KUZU DEBUG] initMembers() - creating StorageManager...\n");
    fflush(stderr);
    storageManager = std::make_unique<StorageManager>(databasePath, dbConfig.readOnly,
        dbConfig.enableChecksums, *memoryManager, dbConfig.enableCompression, vfs.get());

    fprintf(stderr, "[KUZU DEBUG] initMembers() - creating TransactionManager...\n");
    fflush(stderr);
    transactionManager = std::make_unique<TransactionManager>(storageManager->getWAL());

    fprintf(stderr, "[KUZU DEBUG] initMembers() - creating DatabaseManager...\n");
    fflush(stderr);
    databaseManager = std::make_unique<DatabaseManager>();

    fprintf(stderr, "[KUZU DEBUG] initMembers() - creating ExtensionManager...\n");
    fflush(stderr);
    extensionManager = std::make_unique<extension::ExtensionManager>();
    dbLifeCycleManager = std::make_shared<DatabaseLifeCycleManager>();

    if (clientContext.isInMemory()) {
        fprintf(stderr, "[KUZU DEBUG] initMembers() - in-memory mode, initializing data file handle...\n");
        fflush(stderr);
        storageManager->initDataFileHandle(vfs.get(), &clientContext);
        extensionManager->autoLoadLinkedExtensions(&clientContext);
        return;
    }

    fprintf(stderr, "[KUZU DEBUG] initMembers() - calling StorageManager::recover()...\n");
    fflush(stderr);
    StorageManager::recover(clientContext, dbConfig.throwOnWalReplayFailure,
        dbConfig.enableChecksums);

    fprintf(stderr, "[KUZU DEBUG] initMembers() COMPLETE\n");
    fflush(stderr);
}

Database::~Database() {
    fprintf(stderr, "[KUZU DEBUG] ========== Database destructor called ==========\n");
    fflush(stderr);

    if (!dbConfig.readOnly && dbConfig.forceCheckpointOnClose) {
        fprintf(stderr, "[KUZU DEBUG] Attempting checkpoint on close...\n");
        fflush(stderr);
        try {
            ClientContext clientContext(this);
            fprintf(stderr, "[KUZU DEBUG] ClientContext created, calling checkpoint...\n");
            fflush(stderr);
            transactionManager->checkpoint(clientContext);
            fprintf(stderr, "[KUZU DEBUG] Checkpoint on close succeeded\n");
            fflush(stderr);
        } catch (Exception& e) {
            fprintf(stderr, "[KUZU ERROR] Checkpoint on close failed: %s\n", e.what());
            fflush(stderr);
        } catch (std::exception& e) {
            fprintf(stderr, "[KUZU ERROR] Checkpoint on close failed (std::exception): %s\n", e.what());
            fflush(stderr);
        } catch (...) {
            fprintf(stderr, "[KUZU ERROR] Checkpoint on close failed: Unknown exception\n");
            fflush(stderr);
        }
    } else {
        fprintf(stderr, "[KUZU DEBUG] Checkpoint skipped (readOnly=%d, forceCheckpointOnClose=%d)\n",
                dbConfig.readOnly, dbConfig.forceCheckpointOnClose);
        fflush(stderr);
    }

    fprintf(stderr, "[KUZU DEBUG] Setting isDatabaseClosed = true\n");
    fflush(stderr);
    dbLifeCycleManager->isDatabaseClosed = true;
    fprintf(stderr, "[KUZU DEBUG] ========== Database destructor finished ==========\n");
    fflush(stderr);
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

} // namespace main
} // namespace kuzu
