#!/bin/bash

# build-xcframework.sh
# Build XCFramework for kuzu-swift

set -e

# Configuration
SCHEME_NAME="kuzu-swift"
FRAMEWORK_NAME="Kuzu"
OUTPUT_DIR="./build"
ARCHIVES_DIR="${OUTPUT_DIR}/archives"
XCFRAMEWORK_PATH="${OUTPUT_DIR}/${FRAMEWORK_NAME}.xcframework"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Check for xcpretty
HAS_XCPRETTY=$(which xcpretty >/dev/null 2>&1 && echo "yes" || echo "no")
if [ "$HAS_XCPRETTY" = "no" ]; then
    echo -e "${YELLOW}Warning: xcpretty not found. Output will be verbose.${NC}"
    echo -e "${YELLOW}Install with: gem install xcpretty${NC}"
fi

# Clean previous builds
echo -e "${YELLOW}Cleaning previous builds...${NC}"
rm -rf "${OUTPUT_DIR}"
mkdir -p "${ARCHIVES_DIR}"

# Function to build archive
build_archive() {
    local platform=$1
    local destination=$2
    local archive_name=$3
    
    echo -e "${GREEN}Building for ${platform}...${NC}"
    
    if [ "$HAS_XCPRETTY" = "yes" ]; then
        xcodebuild archive \
            -scheme "${SCHEME_NAME}" \
            -destination "${destination}" \
            -archivePath "${ARCHIVES_DIR}/${archive_name}" \
            -derivedDataPath "${OUTPUT_DIR}/DerivedData" \
            SKIP_INSTALL=NO \
            BUILD_LIBRARY_FOR_DISTRIBUTION=YES \
            ONLY_ACTIVE_ARCH=NO \
            | xcpretty || exit 1
    else
        xcodebuild archive \
            -scheme "${SCHEME_NAME}" \
            -destination "${destination}" \
            -archivePath "${ARCHIVES_DIR}/${archive_name}" \
            -derivedDataPath "${OUTPUT_DIR}/DerivedData" \
            SKIP_INSTALL=NO \
            BUILD_LIBRARY_FOR_DISTRIBUTION=YES \
            ONLY_ACTIVE_ARCH=NO || exit 1
    fi
}

# Build for iOS Device
build_archive "iOS" \
    "generic/platform=iOS" \
    "${FRAMEWORK_NAME}-iOS.xcarchive"

# Build for iOS Simulator
build_archive "iOS Simulator" \
    "generic/platform=iOS Simulator" \
    "${FRAMEWORK_NAME}-iOS-Simulator.xcarchive"

# Build for macOS
build_archive "macOS" \
    "generic/platform=macOS" \
    "${FRAMEWORK_NAME}-macOS.xcarchive"

# Create XCFramework
echo -e "${GREEN}Creating XCFramework...${NC}"

if [ "$HAS_XCPRETTY" = "yes" ]; then
    xcodebuild -create-xcframework \
        -archive "${ARCHIVES_DIR}/${FRAMEWORK_NAME}-iOS.xcarchive" -framework "${FRAMEWORK_NAME}.framework" \
        -archive "${ARCHIVES_DIR}/${FRAMEWORK_NAME}-iOS-Simulator.xcarchive" -framework "${FRAMEWORK_NAME}.framework" \
        -archive "${ARCHIVES_DIR}/${FRAMEWORK_NAME}-macOS.xcarchive" -framework "${FRAMEWORK_NAME}.framework" \
        -output "${XCFRAMEWORK_PATH}" \
        | xcpretty || exit 1
else
    xcodebuild -create-xcframework \
        -archive "${ARCHIVES_DIR}/${FRAMEWORK_NAME}-iOS.xcarchive" -framework "${FRAMEWORK_NAME}.framework" \
        -archive "${ARCHIVES_DIR}/${FRAMEWORK_NAME}-iOS-Simulator.xcarchive" -framework "${FRAMEWORK_NAME}.framework" \
        -archive "${ARCHIVES_DIR}/${FRAMEWORK_NAME}-macOS.xcarchive" -framework "${FRAMEWORK_NAME}.framework" \
        -output "${XCFRAMEWORK_PATH}" || exit 1
fi

# Verify XCFramework
if [ -d "${XCFRAMEWORK_PATH}" ]; then
    echo -e "${GREEN}✅ XCFramework created successfully at: ${XCFRAMEWORK_PATH}${NC}"
    
    # Display framework info
    echo -e "${YELLOW}Framework Info:${NC}"
    ls -la "${XCFRAMEWORK_PATH}"
    
    # Calculate size
    FRAMEWORK_SIZE=$(du -sh "${XCFRAMEWORK_PATH}" | cut -f1)
    echo -e "${YELLOW}Framework size: ${FRAMEWORK_SIZE}${NC}"
    
    # Create zip for distribution
    echo -e "${GREEN}Creating zip archive...${NC}"
    cd "${OUTPUT_DIR}"
    zip -r "${FRAMEWORK_NAME}.xcframework.zip" "${FRAMEWORK_NAME}.xcframework"
    cd - > /dev/null
    
    ZIP_SIZE=$(du -sh "${OUTPUT_DIR}/${FRAMEWORK_NAME}.xcframework.zip" | cut -f1)
    echo -e "${GREEN}✅ Zip created: ${OUTPUT_DIR}/${FRAMEWORK_NAME}.xcframework.zip (${ZIP_SIZE})${NC}"
    
    # Calculate checksum
    CHECKSUM=$(swift package compute-checksum "${OUTPUT_DIR}/${FRAMEWORK_NAME}.xcframework.zip")
    echo -e "${YELLOW}Checksum: ${CHECKSUM}${NC}"
    echo -e "${YELLOW}Use this checksum in your Package.swift binary target${NC}"
    
else
    echo -e "${RED}❌ Failed to create XCFramework${NC}"
    exit 1
fi

# Clean up archives
echo -e "${YELLOW}Cleaning up archives...${NC}"
rm -rf "${ARCHIVES_DIR}"

echo -e "${GREEN}✅ Build complete!${NC}"