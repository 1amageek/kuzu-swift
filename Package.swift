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
    targets: [
        .binaryTarget(
            name: "Kuzu",
            url: "https://github.com/kuzudb/kuzu-swift/releases/download/v0.11.1/Kuzu.xcframework.zip",
            checksum: "b13968dea0cc5c97e6e7ab7d500a4a8ddc7ddb420b36e25f28eb2bf0de49c1f9"
        )
    ]
)
