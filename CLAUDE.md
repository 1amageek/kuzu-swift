# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

kuzu-swift is the official Swift language binding for [Kuzu](https://github.com/kuzudb/kuzu), an embeddable property graph database built for query speed and scalability. This package provides a Swift wrapper around Kuzu's C API, exposing graph database functionality to Swift applications on macOS, iOS, and Linux.

## Build and Test Commands

### Building
```bash
swift build
```

### Running Tests
```bash
swift test
```

### Running Single Test
```bash
swift test --filter <TestClassName>.<testMethodName>
```
Example: `swift test --filter DatabaseTests.testDatabaseInitialization`

### Platform-Specific Notes
- macOS: Requires macOS v11 or later
- iOS: Requires iOS v14 or later, tests run on iOS Simulator
- Linux: See Swift.org for supported distributions
- Windows: Not supported

## Architecture

### C API Interop Layer

The Swift package wraps Kuzu's C API via the `cxx-kuzu` target, which includes:
- Complete Kuzu C++ sources compiled as part of the Swift package
- Extensions: algo, fts, json, vector (with HNSW index support)
- Third-party libraries: ANTLR4, re2, snappy, zstd, parquet, etc.

**Import Pattern**: All Swift files use `@_implementationOnly import cxx_kuzu` to hide C API details from public interface.

### Core Swift Classes

**Database** (`Sources/Kuzu/Database.swift`):
- Entry point for database operations
- Manages database lifecycle and system configuration
- **Recent Feature**: Background HNSW vector index loading with callback API
- Thread-safe with `@unchecked Sendable` conformance
- Properties:
  - `vectorIndexesStatus`: Returns loading status (loading/ready/failed)
  - `version`: Static property for Kuzu library version
  - `storageVersion`: Static property for storage format version
- Methods:
  - `onVectorIndexesLoaded(_:)`: Register callback for index load completion

**Connection** (`Sources/Kuzu/Connection.swift`):
- Executes queries against a database instance
- Supports both direct queries and prepared statements
- Thread control: `setMaxNumThreadForExec(_:)`, `getMaxNumThreadForExec()`
- Query timeout: `setQueryTimeout(_:)`, `interrupt()`
- Each connection maintains a reference to its database

**QueryResult** (`Sources/Kuzu/QueryResult.swift`):
- Iterator over query results
- Provides `hasNext()` and `getNext()` methods
- Returns `FlatTuple` instances

**PreparedStatement** (`Sources/Kuzu/PreparedStatement.swift`):
- Parameterized queries for performance and safety
- Created via `Connection.prepare(_:)`
- Executed via `Connection.execute(_:_:)` with parameter bindings

**Type Conversions** (`Sources/Kuzu/Util.swift`, `Sources/Kuzu/Types.swift`):
- Bidirectional conversion between Swift and Kuzu types
- Key functions: `swiftValueToKuzuValue(_:)`, `kuzuValueToSwift(_:)`
- Supports: primitives, collections (Array/List, Dictionary/Struct, Map), dates, nodes, relationships
- Special wrapper types for unsigned integers (e.g., `KuzuUInt64Wrapper`) due to Swift/ObjC bridging limitations

### Vector Index Support

**HNSW Index Background Loading**:
- Vector indexes load asynchronously in background thread to avoid blocking database initialization
- Design files in root: `DESIGN_FINAL_BACKGROUND_LOADING_V3.md` (current), V2, V1, and analysis docs
- Critical thread safety considerations:
  - `vectorIndexLoadCancelled` (atomic): Main cancellation signal
  - `vectorIndexesLoaded` (atomic): Completion flag
  - `vectorIndexesLoadSuccess` (atomic): Success/failure flag
  - `isDatabaseClosed` (plain bool): Only accessed inside mutex-protected critical section
  - `backgroundThreadStartMutex`: Protects ClientContext creation and database closed check
  - `vectorIndexCallbackMutex`: Protects callback registration/invocation

**Thread Safety Pattern** (Critical for any modifications):
1. Check `isDatabaseClosed` ONLY inside `backgroundThreadStartMutex` critical section
2. Use `vectorIndexLoadCancelled` (atomic) everywhere else for cancellation checks
3. Never read `isDatabaseClosed` outside mutex (would be data race)
4. Destructor sets both flags atomically but only `vectorIndexLoadCancelled` is safe to check from background thread

## Recent Bug Fixes

**HNSW Index Threading Issues** (commits 88cc03b, 326660e, 40a9c6a, fd4fdcc, 0a5771d):
- Fixed data races in `RelTable::detachDeleteForCSRRels` during parallel operations
- Upgraded from `std::mutex` to `std::shared_mutex` for concurrent reads
- Added comprehensive thread safety tests in `ThreadSafetyTests.swift`
- These tests simulate production workloads with HNSW vector indexing

**Background Loading Race Conditions**:
- V1: Initial design with TOCTOU race
- V2: Fixed TOCTOU but had `isDatabaseClosed` data race
- V3: Complete fix using proper memory ordering and mutex protection

## Testing Approach

### Thread Safety Tests
Location: `Tests/kuzu-swiftTests/ThreadSafetyTests.swift`

**Purpose**: Reproduce production crashes from HNSW index operations under multi-threaded load
- `testConcurrentHNSWIndexInsertions`: Basic parallel insertions
- `testDetachDeleteWithInternalThreads`: Relationship deletion under load
- `testLargeScaleHNSWIndexing`: 1000+ item stress test
- `testSingleLargeTransactionStress`: Maximum parallelism in single transaction
- `testMixedOperationsStress`: Insert/delete/query combinations

**Why These Matter**: Kuzu uses internal thread pools (TaskScheduler) that can cause race conditions if table operations aren't properly synchronized. Always run with multiple threads (`maxNumThreads: 8`) to catch issues.

### Test Data Management
- Tests create temporary databases in `NSTemporaryDirectory()`
- Use `deleteTestDatabaseDirectory()` helper for cleanup
- Tests are self-contained and can run in any order

## Common Development Patterns

### Creating a Database and Running Queries
```swift
let db = try Database("/path/to/db")
let conn = try Connection(db)
let result = try conn.query("MATCH (n:Node) RETURN n;")
while result.hasNext() {
    let tuple = try result.getNext()
    // Process tuple
}
```

### Using Prepared Statements
```swift
let stmt = try conn.prepare("CREATE (p:Person {name: $name, age: $age})")
let result = try conn.execute(stmt, ["name": "Alice", "age": 30])
```

### Vector Index Callback
```swift
db.onVectorIndexesLoaded { result in
    switch result {
    case .success:
        print("Indexes ready")
    case .failure(let error):
        print("Index loading failed: \(error)")
    }
}
```

### System Configuration
```swift
let config = SystemConfig(
    bufferPoolSize: 256 * 1024 * 1024,
    maxNumThreads: 8,
    enableCompression: true,
    readOnly: false
)
let db = try Database("/path/to/db", config)
```

## Error Handling

All operations throw `KuzuError`:
- `databaseInitializationFailed`
- `connectionInitializationFailed`
- `queryExecutionFailed`
- `prepareStatmentFailed` (note typo in existing code)
- `valueConversionFailed`
- `getValueFailed`
- `vectorIndexLoadFailed`

## Package Structure

```
kuzu-swift/
├── Sources/
│   └── Kuzu/           # Swift wrapper layer
│       ├── Database.swift
│       ├── Connection.swift
│       ├── QueryResult.swift
│       ├── PreparedStatement.swift
│       ├── FlatTuple.swift
│       ├── SystemConfig.swift
│       ├── Types.swift
│       └── Util.swift
├── Tests/
│   └── kuzu-swiftTests/
│       ├── DatabaseTests.swift
│       ├── ConnectionTests.swift
│       ├── ThreadSafetyTests.swift  # Critical for HNSW/parallel ops
│       └── ...
├── Package.swift       # SPM manifest with C++ source includes
└── CONTRIBUTING.md
```

## Contributing Guidelines

From CONTRIBUTING.md:
- Discuss changes with core team on GitHub or Discord first
- Do not commit directly to master branch
- All PRs require tests
- Avoid large PRs (hard to review)
- Merge frequently with master branch
- Agreement to CLA (CLA.md) required

## CI/CD

Workflows (`.github/workflows/`):
- `swift.yml`: Build and test on macOS, Ubuntu, iOS Simulator
- `generate-docs.yml`: Auto-generate API docs
- `update-kuzu.yml`: Sync with upstream Kuzu releases

## Key Constraints

1. **C++ Interop**: Changes to Kuzu C API require coordinated updates in Swift wrapper
2. **Thread Safety**: All public classes marked `@unchecked Sendable` - extreme care needed
3. **Memory Management**: Manual C memory management via `defer` blocks with `kuzu_*_destroy` calls
4. **Linux Compatibility**: NSNumber type detection doesn't work on Linux; use wrapper types
5. **Windows Not Supported**: No plans to support Windows platform

## Design Documents in Root

If working on vector index loading or background thread coordination, review:
- `DESIGN_FINAL_BACKGROUND_LOADING_V3.md`: Current production design
- `DESIGN_VECTOR_INDEX_CALLBACK.md`: Callback API design
- `ANALYSIS_BACKGROUND_LOADING_CONSTRAINTS.md`: Constraints and trade-offs

## Debugging Tips

1. **ThreadSanitizer**: Use `-fsanitize=thread` when building Kuzu to catch data races
2. **AddressSanitizer**: Use `-fsanitize=address` for memory issues
3. **Debug Logging**: Recent changes include `fprintf(stderr, ...)` debug logging in C++ layer
4. **Test Isolation**: Run single tests to isolate issues: `swift test --filter ClassName.testMethod`
5. **Background Thread Issues**: Check `vectorIndexLoadCancelled` usage and mutex protection

## Swift/C++ Boundary Notes

- All C API calls return `kuzu_state` (enum) - check for `KuzuSuccess` before proceeding
- C strings from Kuzu must be freed with `kuzu_destroy_string()`
- C values must be freed with `kuzu_value_destroy()`
- Use `defer` blocks religiously for cleanup to avoid leaks
- Pass pointers to structs with `&` (inout) for C API calls

## Performance Considerations

- Database initialization was ~10s before background loading, now ~100ms (100x improvement)
- Vector index loading happens asynchronously; check status before queries if needed
- Connection thread count affects query parallelism: tune with `setMaxNumThreadForExec`
- Buffer pool size in `SystemConfig` impacts large dataset performance
