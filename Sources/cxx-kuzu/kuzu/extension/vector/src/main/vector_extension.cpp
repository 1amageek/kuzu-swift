#include "main/vector_extension.h"

#include "catalog/hnsw_index_catalog_entry.h"
#include "function/hnsw_index_functions.h"
#include "main/client_context.h"
#include "main/database.h"
#include "storage/storage_manager.h"

#include <atomic>
#include <thread>
#include <vector>
#include <mutex>

namespace kuzu {
namespace vector_extension {

static void initHNSWEntries(main::ClientContext* context) {
    auto storageManager = storage::StorageManager::Get(*context);
    auto catalog = catalog::Catalog::Get(*context);

    // Collect HNSW indexes
    std::vector<catalog::IndexCatalogEntry*> hnswIndexes;
    for (auto& indexEntry : catalog->getIndexEntries(transaction::Transaction::Get(*context))) {
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
        workers.emplace_back([&]() {
            while (true) {
                size_t idx = nextIndexToProcess.fetch_add(1);
                if (idx >= hnswIndexes.size()) {
                    break;
                }

                auto* indexEntry = hnswIndexes[idx];
                try {
                    fprintf(stderr, "[KUZU DEBUG] Thread %zu loading index: %s\n",
                            i, indexEntry->getIndexName().c_str());
                    fflush(stderr);

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

    // Handle errors
    if (!errors.empty()) {
        std::string errorMsg = "HNSW index loading failed:\n";
        for (const auto& error : errors) {
            errorMsg += "  - " + error + "\n";
        }
        throw common::RuntimeException(errorMsg);
    }

    fprintf(stderr, "[KUZU DEBUG] All HNSW indexes loaded successfully\n");
    fflush(stderr);
}

void VectorExtension::load(main::ClientContext* context) {
    auto& db = *context->getDatabase();
    extension::ExtensionUtils::addTableFunc<QueryVectorIndexFunction>(db);
    extension::ExtensionUtils::addInternalStandaloneTableFunc<InternalCreateHNSWIndexFunction>(db);
    extension::ExtensionUtils::addInternalStandaloneTableFunc<InternalFinalizeHNSWIndexFunction>(
        db);
    extension::ExtensionUtils::addStandaloneTableFunc<CreateVectorIndexFunction>(db);
    extension::ExtensionUtils::addInternalStandaloneTableFunc<InternalDropHNSWIndexFunction>(db);
    extension::ExtensionUtils::addStandaloneTableFunc<DropVectorIndexFunction>(db);
    extension::ExtensionUtils::registerIndexType(db, OnDiskHNSWIndex::getIndexType());
    initHNSWEntries(context);
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
