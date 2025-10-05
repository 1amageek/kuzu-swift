//
//  kuzu-swift
//  https://github.com/kuzudb/kuzu-swift
//
//  Copyright © 2023 - 2025 Kùzu Inc.
//  This code is licensed under MIT license (see LICENSE for details)

import Foundation
@_implementationOnly import cxx_kuzu

/// Represents the loading status of vector indexes in the database.
public enum VectorIndexesStatus {
    /// Vector indexes are currently loading in the background.
    case loading
    /// Vector indexes have been successfully loaded and are ready for use.
    case ready
    /// Vector index loading failed with an error.
    case failed(Error)
}

/// A class representing a Kuzu database instance.
public final class Database: @unchecked Sendable {
    internal var cDatabase: kuzu_database
    private var loadCallback: ((Result<Void, Error>) -> Void)?

    /// Initializes a new Kuzu database instance.
    /// - Parameters:
    ///   - databasePath: The path to the database. Defaults to ":memory:" for in-memory database.
    ///   - systemConfig: Optional configuration for the database system. If nil, default configuration will be used.
    /// - Throws: `KuzuError.databaseInitializationFailed` if the database initialization fails.
    public init(
        _ databasePath: String = ":memory:",
        _ systemConfig: SystemConfig? = nil
    ) throws {
        cDatabase = kuzu_database()
        let cSystemConfg =
            systemConfig?.cSystemConfig ?? kuzu_default_system_config()
        let state = kuzu_database_init(
            databasePath,
            cSystemConfg,
            &self.cDatabase
        )
        if state == KuzuSuccess {
            return
        } else {
            throw KuzuError.databaseInitializationFailed(
                "Database initialization failed with error code: \(state)"
            )
        }
    }

    /// The version of the Kuzu library as a string.
    ///
    /// This property returns the version of the underlying Kuzu library.
    /// Useful for debugging and ensuring compatibility.
    public static var version: String {
        let resultCString = kuzu_get_version()
        defer { kuzu_destroy_string(resultCString) }
        return String(cString: resultCString!)
    }

    /// The storage version of the Kuzu library as an unsigned 64-bit integer.
    ///
    /// This property returns the storage format version used by the Kuzu library.
    /// It can be used to check compatibility of database files.
    public static var storageVersion: UInt64 {
        let storageVersion = kuzu_get_storage_version()
        return storageVersion
    }

    /// The current status of vector indexes loading.
    ///
    /// This property indicates whether vector indexes are still loading,
    /// ready for use, or have failed to load.
    ///
    /// - Returns: The current `VectorIndexesStatus`.
    public var vectorIndexesStatus: VectorIndexesStatus {
        let isLoaded = kuzu_database_is_vector_indexes_loaded(&cDatabase)
        let isReady = kuzu_database_is_vector_indexes_ready(&cDatabase)

        if isReady {
            return .ready
        } else if isLoaded {
            // Loaded but not ready means it failed
            return .failed(KuzuError.vectorIndexLoadFailed("Vector index loading failed"))
        } else {
            return .loading
        }
    }

    /// Registers a callback to be invoked when vector indexes finish loading.
    ///
    /// The callback is invoked on a background thread when loading completes.
    /// If indexes are already loaded when this method is called, the callback
    /// will be invoked immediately on the calling thread.
    ///
    /// - Parameter completion: A closure that receives a `Result<Void, Error>`.
    ///   - `.success` if all indexes loaded successfully
    ///   - `.failure` if loading failed
    ///
    /// - Note: Only one callback can be registered at a time. Calling this method
    ///         again will replace the previous callback.
    public func onVectorIndexesLoaded(_ completion: @escaping (Result<Void, Error>) -> Void) {
        self.loadCallback = completion

        // Create a context to pass to C
        let context = Unmanaged.passRetained(self).toOpaque()

        kuzu_database_set_vector_index_load_callback(&cDatabase, { userData, success, errorMessage in
            guard let userData = userData else { return }

            // Retrieve the Database instance
            let database = Unmanaged<Database>.fromOpaque(userData).takeRetainedValue()

            // Call the Swift callback
            if let callback = database.loadCallback {
                if success {
                    callback(.success(()))
                } else {
                    let errorMsg = errorMessage.map { String(cString: $0) } ?? "Unknown error"
                    callback(.failure(KuzuError.vectorIndexLoadFailed(errorMsg)))
                }
            }
        }, context)
    }

    deinit {
        // Unregister callback before destroying
        kuzu_database_set_vector_index_load_callback(&cDatabase, nil, nil)
        kuzu_database_destroy(&self.cDatabase)
    }
}
