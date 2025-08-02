# kuzu-swift

Official Swift language binding for [Kuzu](https://github.com/kuzudb/kuzu). Kuzu an embeddable property graph database management system built for query speed and scalability. For more information, please visit the [Kuzu GitHub repository](https://github.com/kuzudb/kuzu) or the [Kuzu website](https://kuzudb.com).

## Get started

To add kuzu-swift to your Swift project, you can use the Swift Package Manager.

### Option 1: Binary Distribution (Recommended for faster builds)

Using the pre-built XCFramework significantly reduces build times by avoiding compilation of the C++ code.

```swift
// In your Package.swift
dependencies: [
    .package(url: "https://github.com/kuzudb/kuzu-swift", exact: "0.11.1"),  // Use specific version tag
],
targets: [
    .target(
        name: "YourTarget",
        dependencies: [
            .product(name: "Kuzu", package: "kuzu-swift"),
        ]
    )
]
```

Replace `"0.11.1"` with the desired version. Check the [releases page](https://github.com/kuzudb/kuzu-swift/releases) for available versions.

### Option 2: Source Distribution (For development/customization)

If you need to build from source or use the latest development version:

```swift
// In your Package.swift
dependencies: [
    .package(url: "https://github.com/kuzudb/kuzu-swift", branch: "main"),  // Latest development
],
targets: [
    .target(
        name: "YourTarget",
        dependencies: [
            .product(name: "Kuzu", package: "kuzu-swift"),
        ]
    )
]
```

### Developer Option: Testing Binary Distribution Locally

For development and testing purposes, you can use environment variables to switch between source and binary builds:

```bash
# Use binary distribution
export KUZU_USE_BINARY=1
export KUZU_BINARY_URL="https://github.com/kuzudb/kuzu-swift/releases/download/v0.11.1/Kuzu.xcframework.zip"
export KUZU_BINARY_CHECKSUM="your-checksum-here"
swift build

# Use source distribution (default)
unset KUZU_USE_BINARY
swift build
```

### Using Xcode

You can add the package through Xcode:
1. Open your Xcode project.
2. Go to `File` > `Add Packages Dependencies...`.
3. Enter the URL of the kuzu-swift repository: `https://github.com/kuzudb/kuzu-swift`.
4. Select the version you want to use (e.g., `main` branch or a specific tag).

## Docs

The API documentation for kuzu-swift is [available here](https://api-docs.kuzudb.com/swift/documentation/kuzu/).

## Examples

A simple CLI example is provided in the [Example](Example) directory.

A demo iOS application is [provided here](https://github.com/kuzudb/kuzu-swift-demo).

## System requirements

kuzu-swift requires Swift 5.9 or later. It supports the following platforms:
- macOS v11 or later
- iOS v14 or later
- Linux platforms (see the [official documentation](https://www.swift.org/platform-support/) for the supported distros)

Windows platform is not supported and there is no future plan to support it. 

The CI pipeline tests the package on macOS v14 and Ubuntu 24.04.

## Build

```bash
swift build
```

## Tests

To run the tests, you can use the following command:

```bash
swift test
```

## Contributing
We welcome contributions to kuzu-swift. By contributing to kuzu-swift, you agree that your contributions will be licensed under the [MIT License](LICENSE). Please read the [contributing guide](CONTRIBUTING.md) for more information.
