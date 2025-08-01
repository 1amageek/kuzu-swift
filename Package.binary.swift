// swift-tools-version: 5.9
// Package.swift for binary distribution of kuzu-swift

import PackageDescription

let package = Package(
    name: "kuzu-swift",
    platforms: [
        .macOS(.v11),
        .iOS(.v14),
    ],
    products: [
        .library(
            name: "Kuzu",
            targets: ["Kuzu"]),
    ],
    dependencies: [],
    targets: [
        .binaryTarget(
            name: "Kuzu",
            // For local development/testing:
            // path: "./build/Kuzu.xcframework"
            
            // For remote distribution:
            url: "https://github.com/kuzudb/kuzu-swift/releases/download/VERSION/Kuzu.xcframework.zip",
            checksum: "CHECKSUM_PLACEHOLDER"
        )
    ]
)

// Usage Instructions:
// 1. Replace VERSION with the actual release version (e.g., v0.11.1)
// 2. Replace CHECKSUM_PLACEHOLDER with the actual checksum from build-xcframework.sh output
// 3. Upload the Kuzu.xcframework.zip to the GitHub release
// 4. Users can then add this package using the binary distribution URL