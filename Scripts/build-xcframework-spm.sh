#!/bin/bash

# build-xcframework-spm.sh
# Alternative build script using Swift Package Manager directly

set -e

# Configuration
FRAMEWORK_NAME="Kuzu"
OUTPUT_DIR="./build"
XCFRAMEWORK_PATH="${OUTPUT_DIR}/${FRAMEWORK_NAME}.xcframework"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${YELLOW}Building XCFramework using Swift Package Manager...${NC}"

# Clean previous builds
echo -e "${YELLOW}Cleaning previous builds...${NC}"
rm -rf "${OUTPUT_DIR}"
mkdir -p "${OUTPUT_DIR}"

# Build for each platform using SPM
echo -e "${GREEN}Building for iOS...${NC}"
swift build -c release \
    --sdk `xcrun --show-sdk-path --sdk iphoneos` \
    --triple arm64-apple-ios14.0 \
    -Xswiftc -enable-library-evolution

echo -e "${GREEN}Building for iOS Simulator (arm64)...${NC}"
swift build -c release \
    --sdk `xcrun --show-sdk-path --sdk iphonesimulator` \
    --triple arm64-apple-ios14.0-simulator \
    -Xswiftc -enable-library-evolution

echo -e "${GREEN}Building for iOS Simulator (x86_64)...${NC}"
swift build -c release \
    --sdk `xcrun --show-sdk-path --sdk iphonesimulator` \
    --triple x86_64-apple-ios14.0-simulator \
    -Xswiftc -enable-library-evolution

echo -e "${GREEN}Building for macOS (arm64)...${NC}"
swift build -c release \
    --triple arm64-apple-macosx11.0 \
    -Xswiftc -enable-library-evolution

echo -e "${GREEN}Building for macOS (x86_64)...${NC}"
swift build -c release \
    --triple x86_64-apple-macosx11.0 \
    -Xswiftc -enable-library-evolution

# Create module map
echo -e "${GREEN}Creating module map...${NC}"
mkdir -p "${OUTPUT_DIR}/module"
cat > "${OUTPUT_DIR}/module/module.modulemap" << EOF
framework module Kuzu {
  umbrella header "Kuzu-Swift.h"
  export *
  module * { export * }
}
EOF

# Note about manual XCFramework creation
echo -e "${YELLOW}Note: Manual XCFramework creation from SPM builds requires additional steps.${NC}"
echo -e "${YELLOW}Consider using one of these alternatives:${NC}"
echo -e "${YELLOW}1. Use swift-create-xcframework: https://github.com/unsignedapps/swift-create-xcframework${NC}"
echo -e "${YELLOW}2. Create an Xcode project and build from there${NC}"
echo -e "${YELLOW}3. Use the pre-built binaries from releases${NC}"

# Alternative: Install and use swift-create-xcframework
echo -e "${GREEN}Checking for swift-create-xcframework...${NC}"
if command -v swift-create-xcframework &> /dev/null; then
    echo -e "${GREEN}swift-create-xcframework found. Creating XCFramework...${NC}"
    
    swift-create-xcframework \
        --output "${XCFRAMEWORK_PATH}" \
        --platforms ios ios-simulator macos \
        -- -c release \
        -Xswiftc -enable-library-evolution
    
    if [ -d "${XCFRAMEWORK_PATH}" ]; then
        echo -e "${GREEN}✅ XCFramework created successfully!${NC}"
        
        # Create zip
        cd "${OUTPUT_DIR}"
        zip -r "${FRAMEWORK_NAME}.xcframework.zip" "${FRAMEWORK_NAME}.xcframework"
        cd - > /dev/null
        
        # Calculate checksum
        CHECKSUM=$(swift package compute-checksum "${OUTPUT_DIR}/${FRAMEWORK_NAME}.xcframework.zip")
        echo -e "${YELLOW}Checksum: ${CHECKSUM}${NC}"
    fi
else
    echo -e "${YELLOW}swift-create-xcframework not found.${NC}"
    echo -e "${YELLOW}Install with:${NC}"
    echo -e "${GREEN}brew install swift-create-xcframework${NC}"
    echo -e "${YELLOW}or${NC}"
    echo -e "${GREEN}mint install unsignedapps/swift-create-xcframework${NC}"
fi

echo -e "${GREEN}✅ Build script complete!${NC}"