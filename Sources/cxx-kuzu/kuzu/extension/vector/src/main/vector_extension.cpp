#include "main/vector_extension.h"

#include "catalog/hnsw_index_catalog_entry.h"
#include "function/hnsw_index_functions.h"
#include "main/client_context.h"
#include "main/database.h"
#include "storage/storage_manager.h"
#include "transaction/transaction_manager.h"

#include <atomic>
#include <thread>
#include <vector>
#include <mutex>

namespace kuzu {
namespace vector_extension {

static void initHNSWEntries(main::ClientContext* context) {
    auto storageManager = storage::StorageManager::Get(*context);
    auto catalog = catalog::Catalog::Get(*context);
    auto* database = context->getDatabase();

    // Collect HNSW indexes
    std::vector<catalog::IndexCatalogEntry*> hnswIndexes;
    for (auto& indexEntry : catalog->getIndexEntries(transaction::Transaction::Get(*context))) {
        // Cancellation check during collection
        if (database->vectorIndexLoadCancelled.load(std::memory_order_acquire)) {
            fprintf(stderr, "[KUZU DEBUG] initHNSWEntries: Cancelled during collection\n");
            fflush(stderr);
            return;
        }

        if (indexEntry->getIndexType() == HNSWIndexCatalogEntry::TYPE_NAME &&
            !indexEntry->isLoaded()) {
            hnswIndexes.push_back(indexEntry);
        }
    }

    if (hnswIndexes.empty()) {
        return;
    }

    // Parallel loading with thread pool
    size_t numThreads = std::min(
        static_cast<size_t>(context->getDatabase()->getConfig().maxNumThreads),
        hnswIndexes.size()
    );

    fprintf(stderr, "[KUZU DEBUG] Loading %zu HNSW indexes using %zu threads\n",
            hnswIndexes.size(), numThreads);
    fflush(stderr);

    std::atomic<size_t> nextIndexToProcess{0};
    std::vector<std::thread> workers;
    std::mutex errorMutex;
    std::vector<std::string> errors;

    // Create fixed number of worker threads
    for (size_t i = 0; i < numThreads; ++i) {
        workers.emplace_back([&, database]() {
            while (true) {
                // Cancellation check at loop start
                if (database->vectorIndexLoadCancelled.load(std::memory_order_acquire)) {
                    fprintf(stderr, "[KUZU DEBUG] Thread %zu: Cancelled at loop start\n", i);
                    fflush(stderr);
                    break;
                }

                size_t idx = nextIndexToProcess.fetch_add(1);
                if (idx >= hnswIndexes.size()) {
                    break;
                }

                auto* indexEntry = hnswIndexes[idx];
                try {
                    fprintf(stderr, "[KUZU DEBUG] Thread %zu loading index: %s\n",
                            i, indexEntry->getIndexName().c_str());
                    fflush(stderr);

                    // Cancellation check before loading
                    if (database->vectorIndexLoadCancelled.load(std::memory_order_acquire)) {
                        fprintf(stderr, "[KUZU DEBUG] Thread %zu: Cancelled before loading %s\n",
                                i, indexEntry->getIndexName().c_str());
                        fflush(stderr);
                        break;
                    }

                    // Deserialize aux info
                    indexEntry->setAuxInfo(
                        HNSWIndexAuxInfo::deserialize(indexEntry->getAuxBufferReader())
                    );

                    // Load index in storage
                    auto& nodeTable = storageManager->getTable(indexEntry->getTableID())
                        ->cast<storage::NodeTable>();
                    auto optionalIndex = nodeTable.getIndexHolder(indexEntry->getIndexName());

                    if (optionalIndex.has_value()) {
                        auto& indexHolder = optionalIndex.value().get();
                        if (!indexHolder.isLoaded()) {
                            // Cancellation check before expensive loading
                            if (database->vectorIndexLoadCancelled.load(std::memory_order_acquire)) {
                                fprintf(stderr, "[KUZU DEBUG] Thread %zu: Cancelled during loading %s\n",
                                        i, indexEntry->getIndexName().c_str());
                                fflush(stderr);
                                break;
                            }

                            indexHolder.load(context, storageManager);
                        }
                    }

                    fprintf(stderr, "[KUZU DEBUG] Thread %zu completed index: %s\n",
                            i, indexEntry->getIndexName().c_str());
                    fflush(stderr);

                } catch (const std::exception& e) {
                    std::lock_guard<std::mutex> lock(errorMutex);
                    errors.push_back(indexEntry->getIndexName() + ": " + e.what());
                }
            }
        });
    }

    // Wait for all threads
    for (auto& worker : workers) {
        worker.join();
    }

    // Handle errors only if not cancelled
    if (!database->vectorIndexLoadCancelled.load(std::memory_order_acquire) && !errors.empty()) {
        std::string errorMsg = "HNSW index loading failed:\n";
        for (const auto& error : errors) {
            errorMsg += "  - " + error + "\n";
        }
        throw common::RuntimeException(errorMsg);
    }

    fprintf(stderr, "[KUZU DEBUG] initHNSWEntries completed\n");
    fflush(stderr);
}

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

    // Capture Database* and shared_ptr to lifecycle manager
    auto* database = context->getDatabase();
    auto lifeCycleManager = database->dbLifeCycleManager;

    // Start background loading
    std::thread([database, lifeCycleManager]() {
        fprintf(stderr, "[KUZU DEBUG] Background thread: Started\n");
        fflush(stderr);

        try {
            // CRITICAL SECTION: Check and create ClientContext atomically
            // This prevents TOCTOU race with destructor
            main::ClientContext* bgContextPtr = nullptr;
            {
                std::lock_guard<std::mutex> lock(database->backgroundThreadStartMutex);

                // Check if Database already closed
                if (lifeCycleManager->isDatabaseClosed) {
                    fprintf(stderr, "[KUZU DEBUG] Background thread: Database already closed, exiting\n");
                    fflush(stderr);
                    return;
                }

                fprintf(stderr, "[KUZU DEBUG] Background thread: Creating ClientContext (protected by mutex)\n");
                fflush(stderr);

                // Create ClientContext while holding lock
                bgContextPtr = new main::ClientContext(database);
            }
            // Lock released: Destructor can now proceed if needed

            // Wrap in unique_ptr for automatic cleanup
            std::unique_ptr<main::ClientContext> bgContext(bgContextPtr);

            fprintf(stderr, "[KUZU DEBUG] Background thread: ClientContext created\n");
            fflush(stderr);

            // Early exit if cancelled
            if (database->vectorIndexLoadCancelled.load(std::memory_order_acquire)) {
                fprintf(stderr, "[KUZU DEBUG] Background thread: Cancelled before transaction\n");
                fflush(stderr);
                return;
            }

            fprintf(stderr, "[KUZU DEBUG] Background thread: Beginning READ_ONLY transaction\n");
            fflush(stderr);

            // Begin READ_ONLY transaction
            auto* txn = database->getTransactionManager()->beginTransaction(
                *bgContext,
                transaction::TransactionType::READ_ONLY
            );

            // Early exit if cancelled
            if (database->vectorIndexLoadCancelled.load(std::memory_order_acquire)) {
                fprintf(stderr, "[KUZU DEBUG] Background thread: Cancelled, rolling back transaction\n");
                fflush(stderr);
                database->getTransactionManager()->rollback(*bgContext, txn);
                return;
            }

            fprintf(stderr, "[KUZU DEBUG] Background thread: Loading HNSW indexes...\n");
            fflush(stderr);

            // Execute HNSW loading
            initHNSWEntries(bgContext.get());

            fprintf(stderr, "[KUZU DEBUG] Background thread: Loading completed\n");
            fflush(stderr);

            // Check cancellation before committing
            if (database->vectorIndexLoadCancelled.load(std::memory_order_acquire)) {
                fprintf(stderr, "[KUZU DEBUG] Background thread: Cancelled after loading, rolling back\n");
                fflush(stderr);
                database->getTransactionManager()->rollback(*bgContext, txn);
                return;
            }

            fprintf(stderr, "[KUZU DEBUG] Background thread: Committing transaction\n");
            fflush(stderr);

            // Commit transaction
            database->getTransactionManager()->commit(*bgContext, txn);

            fprintf(stderr, "[KUZU DEBUG] Background thread: Transaction committed\n");
            fflush(stderr);

            // Notify completion (internally checks vectorIndexLoadCancelled)
            fprintf(stderr, "[KUZU DEBUG] Background thread: Notifying completion\n");
            fflush(stderr);
            database->notifyVectorIndexLoadComplete(true);

        } catch (const std::exception& e) {
            fprintf(stderr, "[KUZU ERROR] Background thread: Exception: %s\n", e.what());
            fflush(stderr);

            // Notify error (internally checks vectorIndexLoadCancelled)
            database->notifyVectorIndexLoadComplete(false, e.what());

        } catch (...) {
            fprintf(stderr, "[KUZU ERROR] Background thread: Unknown exception\n");
            fflush(stderr);

            // Notify error (internally checks vectorIndexLoadCancelled)
            database->notifyVectorIndexLoadComplete(false, "Unknown error");
        }

        fprintf(stderr, "[KUZU DEBUG] Background thread: Exiting\n");
        fflush(stderr);

    }).detach();

    fprintf(stderr, "[KUZU DEBUG] VectorExtension::load() completed, background loading started\n");
    fflush(stderr);
}

} // namespace vector_extension
} // namespace kuzu

#if defined(BUILD_DYNAMIC_LOAD)
extern "C" {
// Because we link against the static library on windows, we implicitly inherit KUZU_STATIC_DEFINE,
// which cancels out any exporting, so we can't use KUZU_API.
#if defined(_WIN32)
#define INIT_EXPORT __declspec(dllexport)
#else
#define INIT_EXPORT __attribute__((visibility("default")))
#endif
INIT_EXPORT void init(kuzu::main::ClientContext* context) {
    kuzu::vector_extension::VectorExtension::load(context);
}

INIT_EXPORT const char* name() {
    return kuzu::vector_extension::VectorExtension::EXTENSION_NAME;
}
}
#endif
