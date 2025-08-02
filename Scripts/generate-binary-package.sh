#!/bin/bash

# generate-binary-package.sh
# Generates a Package.swift for binary distribution

set -e

# Configuration
FRAMEWORK_NAME="Kuzu"
VERSION="${1:-VERSION}"
CHECKSUM="${2:-CHECKSUM_PLACEHOLDER}"

# Colors for output
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${YELLOW}Generating binary distribution Package.swift...${NC}"
echo -e "${YELLOW}Version: ${VERSION}${NC}"
echo -e "${YELLOW}Checksum: ${CHECKSUM}${NC}"

cat > Package.swift << EOF
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
            url: "https://github.com/kuzudb/kuzu-swift/releases/download/${VERSION}/Kuzu.xcframework.zip",
            checksum: "${CHECKSUM}"
        )
    ]
)
EOF

echo -e "${GREEN}✅ Binary distribution Package.swift generated successfully!${NC}"