#include "storage/checkpointer.h"

#include "catalog/catalog.h"
#include "common/file_system/file_system.h"
#include "common/file_system/virtual_file_system.h"
#include "common/serializer/buffered_file.h"
#include "common/serializer/deserializer.h"
#include "common/serializer/in_mem_file_writer.h"
#include "extension/extension_manager.h"
#include "main/client_context.h"
#include "main/db_config.h"
#include "storage/buffer_manager/buffer_manager.h"
#include "storage/database_header.h"
#include "storage/shadow_utils.h"
#include "storage/storage_manager.h"
#include "storage/wal/local_wal.h"

namespace kuzu {
namespace storage {

Checkpointer::Checkpointer(main::ClientContext& clientContext)
    : clientContext{clientContext},
      isInMemory{main::DBConfig::isDBPathInMemory(clientContext.getDatabasePath())} {}

Checkpointer::~Checkpointer() = default;

PageRange Checkpointer::serializeCatalog(const catalog::Catalog& catalog,
    StorageManager& storageManager) {
    auto catalogWriter =
        std::make_shared<common::InMemFileWriter>(*MemoryManager::Get(clientContext));
    common::Serializer catalogSerializer(catalogWriter);
    catalog.serialize(catalogSerializer);
    auto pageAllocator = storageManager.getDataFH()->getPageManager();
    return catalogWriter->flush(*pageAllocator, storageManager.getShadowFile());
}

PageRange Checkpointer::serializeMetadata(const catalog::Catalog& catalog,
    StorageManager& storageManager) {
    auto metadataWriter =
        std::make_shared<common::InMemFileWriter>(*MemoryManager::Get(clientContext));
    common::Serializer metadataSerializer(metadataWriter);
    storageManager.serialize(catalog, metadataSerializer);

    // We need to preallocate the pages for the page manager before we actually serialize it,
    // this is because the page manager needs to track the pages used for itself.
    // The number of pages needed for the page manager should only decrease after making an
    // additional allocation, so we just calculate the number of pages needed to serialize the
    // current state of the page manager.
    // Thus, it is possible that we allocate an extra page that we won't end up writing to when we
    // flush the metadata writer. This may cause a discrepancy between the number of tracked pages
    // and the number of physical pages in the file but shouldn't cause any actual incorrect
    // behavior in the database.
    auto& pageManager = *storageManager.getDataFH()->getPageManager();
    const auto pagesForPageManager = pageManager.estimatePagesNeededForSerialize();
    auto pageAllocator = storageManager.getDataFH()->getPageManager();
    const auto allocatedPages = pageAllocator->allocatePageRange(
        metadataWriter->getNumPagesToFlush() + pagesForPageManager);
    pageManager.serialize(metadataSerializer);

    metadataWriter->flush(allocatedPages, pageAllocator->getDataFH(),
        storageManager.getShadowFile());
    return allocatedPages;
}

void Checkpointer::writeCheckpoint() {
    if (isInMemory) {
        return;
    }

    auto databaseHeader =
        *StorageManager::Get(clientContext)->getOrInitDatabaseHeader(clientContext);
    // Checkpoint storage. Note that we first checkpoint storage before serializing the catalog, as
    // checkpointing storage may overwrite columnIDs in the catalog.
    bool hasStorageChanges = checkpointStorage();

    const auto storageManager = StorageManager::Get(clientContext);
    const auto catalog = catalog::Catalog::Get(clientContext);
    auto* dataFH = storageManager->getDataFH();

    // Check if there are any changes that require checkpointing
    bool hasCatalogChanges = databaseHeader.catalogPageRange.startPageIdx == common::INVALID_PAGE_IDX ||
                             catalog->changedSinceLastCheckpoint();
    bool hasMetadataChanges = databaseHeader.metadataPageRange.startPageIdx == common::INVALID_PAGE_IDX ||
                              hasStorageChanges || catalog->changedSinceLastCheckpoint() ||
                              dataFH->getPageManager()->changedSinceLastCheckpoint();

    // If there are no changes at all, skip the checkpoint entirely
    if (!hasStorageChanges && !hasCatalogChanges && !hasMetadataChanges) {
        fprintf(stderr, "[KUZU DEBUG] Checkpointer: No changes detected, skipping checkpoint\n");
        fflush(stderr);
        return;
    }

    serializeCatalogAndMetadata(databaseHeader, hasStorageChanges);
    writeDatabaseHeader(databaseHeader);
    logCheckpointAndApplyShadowPages();

    // This function will evict all pages that were freed during this checkpoint
    // It must be called before we remove all evicted candidates from the BM
    // Or else the evicted pages may end up appearing multiple times in the eviction queue
    storageManager->finalizeCheckpoint();
    // When a page is freed by the FSM, it evicts it from the BM. However, if the page is freed,
    // then reused over and over, it can be appended to the eviction queue multiple times. To
    // prevent multiple entries of the same page from existing in the eviction queue, at the end of
    // each checkpoint we remove any already-evicted pages.
    auto bufferManager = MemoryManager::Get(clientContext)->getBufferManager();
    bufferManager->removeEvictedCandidates();

    catalog::Catalog::Get(clientContext)->resetVersion();
    dataFH->getPageManager()->resetVersion();
    storageManager->getWAL().reset();
    storageManager->getShadowFile().reset();
}

bool Checkpointer::checkpointStorage() {
    const auto storageManager = StorageManager::Get(clientContext);
    auto pageAllocator = storageManager->getDataFH()->getPageManager();
    return storageManager->checkpoint(&clientContext, *pageAllocator);
}

void Checkpointer::serializeCatalogAndMetadata(DatabaseHeader& databaseHeader,
    bool hasStorageChanges) {
    const auto storageManager = StorageManager::Get(clientContext);
    const auto catalog = catalog::Catalog::Get(clientContext);
    auto* dataFH = storageManager->getDataFH();

    // Serialize the catalog if there are changes
    if (databaseHeader.catalogPageRange.startPageIdx == common::INVALID_PAGE_IDX ||
        catalog->changedSinceLastCheckpoint()) {
        databaseHeader.updateCatalogPageRange(*dataFH->getPageManager(),
            serializeCatalog(*catalog, *storageManager));
    }
    // Serialize the storage metadata if there are changes
    bool metadataInvalid = databaseHeader.metadataPageRange.startPageIdx == common::INVALID_PAGE_IDX;
    bool catalogChanged = catalog->changedSinceLastCheckpoint();
    bool pageManagerChanged = dataFH->getPageManager()->changedSinceLastCheckpoint();

    fprintf(stderr, "[KUZU DEBUG] Checkpointer: metadata serialization check - metadataInvalid=%d, hasStorageChanges=%d, catalogChanged=%d, pageManagerChanged=%d\n",
        metadataInvalid, hasStorageChanges, catalogChanged, pageManagerChanged);
    fflush(stderr);

    if (metadataInvalid || hasStorageChanges || catalogChanged || pageManagerChanged) {
        fprintf(stderr, "[KUZU DEBUG] Checkpointer: Re-serializing metadata\n");
        fflush(stderr);
        // We must free the existing metadata page range before serializing
        // So that the freed pages are serialized by the FSM
        databaseHeader.freeMetadataPageRange(*dataFH->getPageManager());
        databaseHeader.metadataPageRange = serializeMetadata(*catalog, *storageManager);
        fprintf(stderr, "[KUZU DEBUG] Checkpointer: Metadata re-serialized to page %u\n",
            databaseHeader.metadataPageRange.startPageIdx);
        fflush(stderr);
    } else {
        fprintf(stderr, "[KUZU DEBUG] Checkpointer: Skipping metadata serialization\n");
        fflush(stderr);
    }
}

void Checkpointer::writeDatabaseHeader(const DatabaseHeader& header) {
    auto headerWriter =
        std::make_shared<common::InMemFileWriter>(*MemoryManager::Get(clientContext));
    common::Serializer headerSerializer(headerWriter);
    header.serialize(headerSerializer);
    auto headerPage = headerWriter->getPage(0);

    const auto storageManager = StorageManager::Get(clientContext);
    auto dataFH = storageManager->getDataFH();
    auto& shadowFile = storageManager->getShadowFile();
    auto shadowHeader = ShadowUtils::createShadowVersionIfNecessaryAndPinPage(
        common::StorageConstants::DB_HEADER_PAGE_IDX, true /* skipReadingOriginalPage */, *dataFH,
        shadowFile);
    memcpy(shadowHeader.frame, headerPage.data(), common::KUZU_PAGE_SIZE);
    shadowFile.getShadowingFH().unpinPage(shadowHeader.shadowPage);

    // Update the in-memory database header with the new version
    StorageManager::Get(clientContext)->setDatabaseHeader(std::make_unique<DatabaseHeader>(header));
}

void Checkpointer::logCheckpointAndApplyShadowPages() {
    const auto storageManager = StorageManager::Get(clientContext);
    auto& shadowFile = storageManager->getShadowFile();
    // Flush the shadow file.
    shadowFile.flushAll(clientContext);
    auto wal = WAL::Get(clientContext);
    // Log the checkpoint to the WAL and flush WAL. This indicates that all shadow pages and
    // files (snapshots of catalog and metadata) have been written to disk. The part that is not
    // done is to replace them with the original pages or catalog and metadata files. If the
    // system crashes before this point, the WAL can still be used to recover the system to a
    // state where the checkpoint can be redone.
    wal->logAndFlushCheckpoint(&clientContext);
    shadowFile.applyShadowPages(clientContext);
    // Clear the wal and also shadowing files.
    auto bufferManager = MemoryManager::Get(clientContext)->getBufferManager();
    wal->clear();
    shadowFile.clear(*bufferManager);
}

void Checkpointer::rollback() {
    if (isInMemory) {
        return;
    }
    const auto storageManager = StorageManager::Get(clientContext);
    auto catalog = catalog::Catalog::Get(clientContext);
    // Any pages freed during the checkpoint are no longer freed
    storageManager->rollbackCheckpoint(*catalog);
}

bool Checkpointer::canAutoCheckpoint(const main::ClientContext& clientContext,
    const transaction::Transaction& transaction) {
    if (clientContext.isInMemory()) {
        return false;
    }
    if (!clientContext.getDBConfig()->autoCheckpoint) {
        return false;
    }
    if (transaction.isRecovery()) {
        // Recovery transactions are not allowed to trigger auto checkpoint.
        return false;
    }
    auto wal = WAL::Get(clientContext);
    const auto expectedSize = transaction.getLocalWAL().getSize() + wal->getFileSize();
    return expectedSize > clientContext.getDBConfig()->checkpointThreshold;
}

void Checkpointer::readCheckpoint() {
    fprintf(stderr, "[KUZU DEBUG] Checkpointer::readCheckpoint() START\n");
    fflush(stderr);

    auto storageManager = StorageManager::Get(clientContext);
    fprintf(stderr, "[KUZU DEBUG] Checkpointer: calling initDataFileHandle()\n");
    fflush(stderr);
    storageManager->initDataFileHandle(common::VirtualFileSystem::GetUnsafe(clientContext),
        &clientContext);

    if (!isInMemory && storageManager->getDataFH()->getNumPages() > 0) {
        fprintf(stderr, "[KUZU DEBUG] Checkpointer: numPages=%llu, calling readCheckpoint() overload\n",
            storageManager->getDataFH()->getNumPages());
        fflush(stderr);
        readCheckpoint(&clientContext, catalog::Catalog::Get(clientContext), storageManager);
        fprintf(stderr, "[KUZU DEBUG] Checkpointer: readCheckpoint() overload complete\n");
        fflush(stderr);
    } else {
        fprintf(stderr, "[KUZU DEBUG] Checkpointer: skipping readCheckpoint (isInMemory=%d, numPages=%llu)\n",
            isInMemory, storageManager->getDataFH()->getNumPages());
        fflush(stderr);
    }
    fprintf(stderr, "[KUZU DEBUG] Checkpointer: auto-loading linked extensions\n");
    fflush(stderr);
    extension::ExtensionManager::Get(clientContext)->autoLoadLinkedExtensions(&clientContext);
    fprintf(stderr, "[KUZU DEBUG] Checkpointer::readCheckpoint() COMPLETE\n");
    fflush(stderr);
}

void Checkpointer::readCheckpoint(main::ClientContext* context, catalog::Catalog* catalog,
    StorageManager* storageManager) {
    fprintf(stderr, "[KUZU DEBUG] Checkpointer::readCheckpoint(overload) START\n");
    fflush(stderr);

    auto fileInfo = storageManager->getDataFH()->getFileInfo();
    fprintf(stderr, "[KUZU DEBUG] Checkpointer: creating BufferedFileReader\n");
    fflush(stderr);
    auto reader = std::make_unique<common::BufferedFileReader>(*fileInfo);
    common::Deserializer deSer(std::move(reader));

    fprintf(stderr, "[KUZU DEBUG] Checkpointer: deserializing DatabaseHeader\n");
    fflush(stderr);
    auto currentHeader = std::make_unique<DatabaseHeader>(DatabaseHeader::deserialize(deSer));
    fprintf(stderr, "[KUZU DEBUG] Checkpointer: DatabaseHeader deserialized - catalogPageRange.startPageIdx=%u\n",
        currentHeader->catalogPageRange.startPageIdx);
    fflush(stderr);

    // If the catalog page range is invalid, it means there is no catalog to read; thus, the
    // database is empty.
    if (currentHeader->catalogPageRange.startPageIdx != common::INVALID_PAGE_IDX) {
        fprintf(stderr, "[KUZU DEBUG] Checkpointer: reading catalog at page %u\n",
            currentHeader->catalogPageRange.startPageIdx);
        fflush(stderr);
        deSer.getReader()->cast<common::BufferedFileReader>()->resetReadOffset(
            currentHeader->catalogPageRange.startPageIdx * common::KUZU_PAGE_SIZE);
        catalog->deserialize(deSer);
        fprintf(stderr, "[KUZU DEBUG] Checkpointer: catalog deserialized\n");
        fflush(stderr);

        fprintf(stderr, "[KUZU DEBUG] Checkpointer: reading storage manager metadata at page %u\n",
            currentHeader->metadataPageRange.startPageIdx);
        fflush(stderr);
        deSer.getReader()->cast<common::BufferedFileReader>()->resetReadOffset(
            currentHeader->metadataPageRange.startPageIdx * common::KUZU_PAGE_SIZE);
        storageManager->deserialize(context, catalog, deSer);
        fprintf(stderr, "[KUZU DEBUG] Checkpointer: storage manager deserialized\n");
        fflush(stderr);

        fprintf(stderr, "[KUZU DEBUG] Checkpointer: deserializing page manager\n");
        fflush(stderr);
        storageManager->getDataFH()->getPageManager()->deserialize(deSer);
        fprintf(stderr, "[KUZU DEBUG] Checkpointer: page manager deserialized\n");
        fflush(stderr);
    } else {
        fprintf(stderr, "[KUZU DEBUG] Checkpointer: catalog page range is invalid, database is empty\n");
        fflush(stderr);
    }
    fprintf(stderr, "[KUZU DEBUG] Checkpointer: setting database header\n");
    fflush(stderr);
    storageManager->setDatabaseHeader(std::move(currentHeader));
    fprintf(stderr, "[KUZU DEBUG] Checkpointer::readCheckpoint(overload) COMPLETE\n");
    fflush(stderr);
}

} // namespace storage
} // namespace kuzu
