#!/bin/bash

# build-xcframework-simple.sh
# Simplified build script with warning suppression

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

# Clean previous builds
echo -e "${YELLOW}Cleaning previous builds...${NC}"
rm -rf "${OUTPUT_DIR}"
mkdir -p "${ARCHIVES_DIR}"

# Build settings to suppress warnings
BUILD_SETTINGS=(
    "SKIP_INSTALL=NO"
    "BUILD_LIBRARY_FOR_DISTRIBUTION=YES"
    "ONLY_ACTIVE_ARCH=NO"
    "GCC_WARN_64_TO_32_BIT_CONVERSION=NO"
    "CLANG_WARN_IMPLICIT_SIGN_CONVERSION=NO"
    "GCC_WARN_ABOUT_RETURN_TYPE=YES_ERROR"
    "CLANG_WARN_STRICT_PROTOTYPES=NO"
    "OTHER_CPLUSPLUSFLAGS=-Wno-shorten-64-to-32 -Wno-sign-conversion"
)

# Function to build archive
build_archive() {
    local platform=$1
    local destination=$2
    local archive_name=$3
    
    echo -e "${GREEN}Building for ${platform}...${NC}"
    echo -e "${YELLOW}This may take several minutes due to C++ compilation...${NC}"
    
    xcodebuild archive \
        -scheme "${SCHEME_NAME}" \
        -destination "${destination}" \
        -archivePath "${ARCHIVES_DIR}/${archive_name}" \
        -derivedDataPath "${OUTPUT_DIR}/DerivedData" \
        "${BUILD_SETTINGS[@]}" \
        -quiet || {
            echo -e "${RED}Build failed for ${platform}${NC}"
            echo -e "${YELLOW}Try running with -verbose flag for detailed output${NC}"
            exit 1
        }
    
    echo -e "${GREEN}✅ ${platform} build complete${NC}"
}

# Parse command line arguments
VERBOSE=false
if [[ "$1" == "-verbose" ]]; then
    VERBOSE=true
fi

# Try building for iOS only first (faster test)
if [[ "$1" == "-ios-only" ]]; then
    echo -e "${YELLOW}Building iOS only (test mode)...${NC}"
    build_archive "iOS" \
        "generic/platform=iOS" \
        "${FRAMEWORK_NAME}-iOS.xcarchive"
    
    echo -e "${GREEN}✅ iOS test build successful!${NC}"
    echo -e "${YELLOW}Run without -ios-only flag to build full XCFramework${NC}"
    exit 0
fi

# Build for all platforms
build_archive "iOS" \
    "generic/platform=iOS" \
    "${FRAMEWORK_NAME}-iOS.xcarchive"

build_archive "iOS Simulator" \
    "generic/platform=iOS Simulator" \
    "${FRAMEWORK_NAME}-iOS-Simulator.xcarchive"

build_archive "macOS" \
    "generic/platform=macOS" \
    "${FRAMEWORK_NAME}-macOS.xcarchive"

# Create XCFramework
echo -e "${GREEN}Creating XCFramework...${NC}"

xcodebuild -create-xcframework \
    -archive "${ARCHIVES_DIR}/${FRAMEWORK_NAME}-iOS.xcarchive" -framework "${FRAMEWORK_NAME}.framework" \
    -archive "${ARCHIVES_DIR}/${FRAMEWORK_NAME}-iOS-Simulator.xcarchive" -framework "${FRAMEWORK_NAME}.framework" \
    -archive "${ARCHIVES_DIR}/${FRAMEWORK_NAME}-macOS.xcarchive" -framework "${FRAMEWORK_NAME}.framework" \
    -output "${XCFRAMEWORK_PATH}" \
    -quiet || exit 1

# Verify XCFramework
if [ -d "${XCFRAMEWORK_PATH}" ]; then
    echo -e "${GREEN}✅ XCFramework created successfully at: ${XCFRAMEWORK_PATH}${NC}"
    
    # Calculate size
    FRAMEWORK_SIZE=$(du -sh "${XCFRAMEWORK_PATH}" | cut -f1)
    echo -e "${YELLOW}Framework size: ${FRAMEWORK_SIZE}${NC}"
    
    # Create zip for distribution
    echo -e "${GREEN}Creating zip archive...${NC}"
    cd "${OUTPUT_DIR}"
    zip -rq "${FRAMEWORK_NAME}.xcframework.zip" "${FRAMEWORK_NAME}.xcframework"
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
echo -e "${YELLOW}Usage options:${NC}"
echo -e "  ${GREEN}-ios-only${NC}     Build iOS only (faster, for testing)"
echo -e "  ${GREEN}-verbose${NC}      Show detailed build output"