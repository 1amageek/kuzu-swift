#!/usr/bin/env bash
# build-xcframework.sh
# Swift Package から XCFramework を生成するユーティリティ
#
#   ./build-xcframework.sh <ProductName> [<OutputDir>]
#
# 戦略：
# 1. Swift Package をラップする Framework プロジェクトを作成
# 2. 各プラットフォーム用にアーカイブをビルド
# 3. アーカイブから XCFramework を作成
#
# ----------------------------------------------
set -euo pipefail

# 0) 引数とディレクトリ設定
PRODUCT="${1:?Error: product name required}"           # 例: Kuzu
OUT_DIR="${2:-build}"
WRAPPER_PROJECT="KuzuWrapper"
WRAPPER_FRAMEWORK="KuzuFramework"
XCFRAMEWORK="${OUT_DIR}/Kuzu.xcframework"

# 1) 前回生成物を掃除
echo "==> Cleaning previous builds"
rm -rf "${OUT_DIR}"
rm -rf "${WRAPPER_PROJECT}.xcodeproj"
mkdir -p "${OUT_DIR}"

# 2) Wrapper Xcode プロジェクトを作成
echo "==> Creating wrapper Xcode project"
mkdir -p "${WRAPPER_PROJECT}.xcodeproj/project.xcworkspace/xcshareddata"

# Create workspace settings to disable automatic scheme creation
cat > "${WRAPPER_PROJECT}.xcodeproj/project.xcworkspace/xcshareddata/WorkspaceSettings.xcsettings" << 'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>IDEWorkspaceSharedSettings_AutocreateContextsIfNeeded</key>
    <false/>
</dict>
</plist>
EOF

# Create the project file - use printf to avoid variable expansion issues
PROJECT_FILE="${WRAPPER_PROJECT}.xcodeproj/project.pbxproj"
cat > "$PROJECT_FILE" << ENDPROJ
// !\$*UTF8*\$!
{
	archiveVersion = 1;
	classes = {
	};
	objectVersion = 56;
	objects = {

/* Begin PBXBuildFile section */
		1234567890ABCDEF /* ${PRODUCT} in Frameworks */ = {isa = PBXBuildFile; productRef = 1234567890ABCDE0 /* ${PRODUCT} */; };
		1234567890SWIFT1 /* ${WRAPPER_FRAMEWORK}.swift in Sources */ = {isa = PBXBuildFile; fileRef = 1234567890SWIFT0 /* ${WRAPPER_FRAMEWORK}.swift */; };
/* End PBXBuildFile section */

/* Begin PBXFileReference section */
		1234567890ABCDE1 /* ${WRAPPER_FRAMEWORK}.framework */ = {isa = PBXFileReference; explicitFileType = wrapper.framework; includeInIndex = 0; path = ${WRAPPER_FRAMEWORK}.framework; sourceTree = BUILT_PRODUCTS_DIR; };
		1234567890SWIFT0 /* ${WRAPPER_FRAMEWORK}.swift */ = {isa = PBXFileReference; lastKnownFileType = sourcecode.swift; path = ${WRAPPER_FRAMEWORK}.swift; sourceTree = "<group>"; };
/* End PBXFileReference section */

/* Begin PBXFrameworksBuildPhase section */
		1234567890ABCDE2 /* Frameworks */ = {
			isa = PBXFrameworksBuildPhase;
			buildActionMask = 2147483647;
			files = (
				1234567890ABCDEF /* ${PRODUCT} in Frameworks */,
			);
			runOnlyForDeploymentPostprocessing = 0;
		};
/* End PBXFrameworksBuildPhase section */

/* Begin PBXGroup section */
		1234567890ABCDE3 = {
			isa = PBXGroup;
			children = (
				1234567890SWIFT0 /* ${WRAPPER_FRAMEWORK}.swift */,
				1234567890ABCDE4 /* Products */,
			);
			sourceTree = "<group>";
		};
		1234567890ABCDE4 /* Products */ = {
			isa = PBXGroup;
			children = (
				1234567890ABCDE1 /* ${WRAPPER_FRAMEWORK}.framework */,
			);
			name = Products;
			sourceTree = "<group>";
		};
/* End PBXGroup section */

/* Begin PBXHeadersBuildPhase section */
		1234567890HEADER /* Headers */ = {
			isa = PBXHeadersBuildPhase;
			buildActionMask = 2147483647;
			files = (
			);
			runOnlyForDeploymentPostprocessing = 0;
		};
/* End PBXHeadersBuildPhase section */

/* Begin PBXNativeTarget section */
		1234567890ABCDE5 /* ${WRAPPER_FRAMEWORK} */ = {
			isa = PBXNativeTarget;
			buildConfigurationList = 1234567890ABCDE6 /* Build configuration list for PBXNativeTarget "${WRAPPER_FRAMEWORK}" */;
			buildPhases = (
				1234567890HEADER /* Headers */,
				1234567890SOURCE /* Sources */,
				1234567890ABCDE2 /* Frameworks */,
				1234567890RESO /* Resources */,
			);
			buildRules = (
			);
			dependencies = (
			);
			name = ${WRAPPER_FRAMEWORK};
			packageProductDependencies = (
				1234567890ABCDE0 /* ${PRODUCT} */,
			);
			productName = ${WRAPPER_FRAMEWORK};
			productReference = 1234567890ABCDE1 /* ${WRAPPER_FRAMEWORK}.framework */;
			productType = "com.apple.product-type.framework";
		};
/* End PBXNativeTarget section */

/* Begin PBXProject section */
		1234567890ABCDE7 /* Project object */ = {
			isa = PBXProject;
			attributes = {
				BuildIndependentTargetsInParallel = 1;
				LastUpgradeCheck = 1540;
				TargetAttributes = {
					1234567890ABCDE5 = {
						CreatedOnToolsVersion = 15.4;
					};
				};
			};
			buildConfigurationList = 1234567890ABCDE8 /* Build configuration list for PBXProject "${WRAPPER_PROJECT}" */;
			compatibilityVersion = "Xcode 14.0";
			developmentRegion = en;
			hasScannedForEncodings = 0;
			knownRegions = (
				en,
				Base,
			);
			mainGroup = 1234567890ABCDE3;
			packageReferences = (
				1234567890ABCDE9 /* XCLocalSwiftPackageReference "." */,
			);
			productRefGroup = 1234567890ABCDE4 /* Products */;
			projectDirPath = "";
			projectRoot = "";
			targets = (
				1234567890ABCDE5 /* ${WRAPPER_FRAMEWORK} */,
			);
		};
/* End PBXProject section */

/* Begin PBXResourcesBuildPhase section */
		1234567890RESO /* Resources */ = {
			isa = PBXResourcesBuildPhase;
			buildActionMask = 2147483647;
			files = (
			);
			runOnlyForDeploymentPostprocessing = 0;
		};
/* End PBXResourcesBuildPhase section */

/* Begin PBXSourcesBuildPhase section */
		1234567890SOURCE /* Sources */ = {
			isa = PBXSourcesBuildPhase;
			buildActionMask = 2147483647;
			files = (
				1234567890SWIFT1 /* ${WRAPPER_FRAMEWORK}.swift in Sources */,
			);
			runOnlyForDeploymentPostprocessing = 0;
		};
/* End PBXSourcesBuildPhase section */

/* Begin XCBuildConfiguration section */
		1234567890ABCDEA /* Debug */ = {
			isa = XCBuildConfiguration;
			buildSettings = {
				ALWAYS_SEARCH_USER_PATHS = NO;
				BUILD_LIBRARY_FOR_DISTRIBUTION = YES;
				CLANG_ENABLE_MODULES = YES;
				CODE_SIGN_STYLE = Automatic;
				CURRENT_PROJECT_VERSION = 1;
				DEFINES_MODULE = YES;
				DYLIB_COMPATIBILITY_VERSION = 1;
				DYLIB_CURRENT_VERSION = 1;
				DYLIB_INSTALL_NAME_BASE = "@rpath";
				ENABLE_MODULE_VERIFIER = YES;
				GENERATE_INFOPLIST_FILE = YES;
				INFOPLIST_KEY_NSHumanReadableCopyright = "";
				INSTALL_PATH = "\$(LOCAL_LIBRARY_DIR)/Frameworks";
				IPHONEOS_DEPLOYMENT_TARGET = 14.0;
				LD_RUNPATH_SEARCH_PATHS = (
					"\$(inherited)",
					"@executable_path/Frameworks",
					"@loader_path/Frameworks",
				);
				MACOSX_DEPLOYMENT_TARGET = 11.0;
				MARKETING_VERSION = 1.0;
				MODULE_VERIFIER_SUPPORTED_LANGUAGES = "objective-c objective-c++";
				MODULE_VERIFIER_SUPPORTED_LANGUAGE_STANDARDS = "gnu11 gnu++20";
				PRODUCT_BUNDLE_IDENTIFIER = "com.kuzu.${PRODUCT}";
				PRODUCT_MODULE_NAME = Kuzu;
				PRODUCT_NAME = "\$(TARGET_NAME:c99extidentifier)";
				SKIP_INSTALL = NO;
				SUPPORTED_PLATFORMS = "iphoneos iphonesimulator macosx";
				SUPPORTS_MACCATALYST = NO;
				SWIFT_EMIT_LOC_STRINGS = YES;
				SWIFT_OPTIMIZATION_LEVEL = "-Onone";
				SWIFT_VERSION = 5.0;
				TARGETED_DEVICE_FAMILY = "1,2";
			};
			name = Debug;
		};
		1234567890ABCDEB /* Release */ = {
			isa = XCBuildConfiguration;
			buildSettings = {
				ALWAYS_SEARCH_USER_PATHS = NO;
				BUILD_LIBRARY_FOR_DISTRIBUTION = YES;
				CLANG_ENABLE_MODULES = YES;
				CODE_SIGN_STYLE = Automatic;
				CURRENT_PROJECT_VERSION = 1;
				DEFINES_MODULE = YES;
				DYLIB_COMPATIBILITY_VERSION = 1;
				DYLIB_CURRENT_VERSION = 1;
				DYLIB_INSTALL_NAME_BASE = "@rpath";
				ENABLE_MODULE_VERIFIER = YES;
				GENERATE_INFOPLIST_FILE = YES;
				INFOPLIST_KEY_NSHumanReadableCopyright = "";
				INSTALL_PATH = "\$(LOCAL_LIBRARY_DIR)/Frameworks";
				IPHONEOS_DEPLOYMENT_TARGET = 14.0;
				LD_RUNPATH_SEARCH_PATHS = (
					"\$(inherited)",
					"@executable_path/Frameworks",
					"@loader_path/Frameworks",
				);
				MACOSX_DEPLOYMENT_TARGET = 11.0;
				MARKETING_VERSION = 1.0;
				MODULE_VERIFIER_SUPPORTED_LANGUAGES = "objective-c objective-c++";
				MODULE_VERIFIER_SUPPORTED_LANGUAGE_STANDARDS = "gnu11 gnu++20";
				PRODUCT_BUNDLE_IDENTIFIER = "com.kuzu.${PRODUCT}";
				PRODUCT_MODULE_NAME = Kuzu;
				PRODUCT_NAME = "\$(TARGET_NAME:c99extidentifier)";
				SKIP_INSTALL = NO;
				SUPPORTED_PLATFORMS = "iphoneos iphonesimulator macosx";
				SUPPORTS_MACCATALYST = NO;
				SWIFT_COMPILATION_MODE = wholemodule;
				SWIFT_EMIT_LOC_STRINGS = YES;
				SWIFT_OPTIMIZATION_LEVEL = "-O";
				SWIFT_VERSION = 5.0;
				TARGETED_DEVICE_FAMILY = "1,2";
			};
			name = Release;
		};
		1234567890ABCDEC /* Debug */ = {
			isa = XCBuildConfiguration;
			buildSettings = {
				ALWAYS_SEARCH_USER_PATHS = NO;
				CLANG_ANALYZER_NONNULL = YES;
				CLANG_ANALYZER_NUMBER_OBJECT_CONVERSION = YES_AGGRESSIVE;
				CLANG_CXX_LANGUAGE_STANDARD = "gnu++20";
				CLANG_ENABLE_MODULES = YES;
				CLANG_ENABLE_OBJC_ARC = YES;
				CLANG_ENABLE_OBJC_WEAK = YES;
				CLANG_WARN_BLOCK_CAPTURE_AUTORELEASING = YES;
				CLANG_WARN_BOOL_CONVERSION = YES;
				CLANG_WARN_COMMA = YES;
				CLANG_WARN_CONSTANT_CONVERSION = YES;
				CLANG_WARN_DEPRECATED_OBJC_IMPLEMENTATIONS = YES;
				CLANG_WARN_DIRECT_OBJC_ISA_USAGE = YES_ERROR;
				CLANG_WARN_DOCUMENTATION_COMMENTS = YES;
				CLANG_WARN_EMPTY_BODY = YES;
				CLANG_WARN_ENUM_CONVERSION = YES;
				CLANG_WARN_INFINITE_RECURSION = YES;
				CLANG_WARN_INT_CONVERSION = YES;
				CLANG_WARN_NON_LITERAL_NULL_CONVERSION = YES;
				CLANG_WARN_OBJC_IMPLICIT_RETAIN_SELF = YES;
				CLANG_WARN_OBJC_LITERAL_CONVERSION = YES;
				CLANG_WARN_OBJC_ROOT_CLASS = YES_ERROR;
				CLANG_WARN_QUOTED_INCLUDE_IN_FRAMEWORK_HEADER = YES;
				CLANG_WARN_RANGE_LOOP_ANALYSIS = YES;
				CLANG_WARN_STRICT_PROTOTYPES = YES;
				CLANG_WARN_SUSPICIOUS_MOVE = YES;
				CLANG_WARN_UNGUARDED_AVAILABILITY = YES_AGGRESSIVE;
				CLANG_WARN_UNREACHABLE_CODE = YES;
				CLANG_WARN__DUPLICATE_METHOD_MATCH = YES;
				COPY_PHASE_STRIP = NO;
				DEBUG_INFORMATION_FORMAT = dwarf;
				ENABLE_STRICT_OBJC_MSGSEND = YES;
				ENABLE_TESTABILITY = YES;
				ENABLE_USER_SCRIPT_SANDBOXING = YES;
				GCC_C_LANGUAGE_STANDARD = gnu17;
				GCC_DYNAMIC_NO_PIC = NO;
				GCC_NO_COMMON_BLOCKS = YES;
				GCC_OPTIMIZATION_LEVEL = 0;
				GCC_PREPROCESSOR_DEFINITIONS = (
					"DEBUG=1",
					"\$(inherited)",
				);
				GCC_WARN_64_TO_32_BIT_CONVERSION = YES;
				GCC_WARN_ABOUT_RETURN_TYPE = YES_ERROR;
				GCC_WARN_UNDECLARED_SELECTOR = YES;
				GCC_WARN_UNINITIALIZED_AUTOS = YES_AGGRESSIVE;
				GCC_WARN_UNUSED_FUNCTION = YES;
				GCC_WARN_UNUSED_VARIABLE = YES;
				IPHONEOS_DEPLOYMENT_TARGET = 14.0;
				LOCALIZATION_PREFERS_STRING_CATALOGS = YES;
				MACOSX_DEPLOYMENT_TARGET = 11.0;
				MTL_ENABLE_DEBUG_INFO = INCLUDE_SOURCE;
				MTL_FAST_MATH = YES;
				ONLY_ACTIVE_ARCH = YES;
				SUPPORTED_PLATFORMS = "iphoneos iphonesimulator macosx";
				SUPPORTS_MACCATALYST = NO;
				SWIFT_ACTIVE_COMPILATION_CONDITIONS = "DEBUG \$(inherited)";
				SWIFT_OPTIMIZATION_LEVEL = "-Onone";
			};
			name = Debug;
		};
		1234567890ABCDED /* Release */ = {
			isa = XCBuildConfiguration;
			buildSettings = {
				ALWAYS_SEARCH_USER_PATHS = NO;
				CLANG_ANALYZER_NONNULL = YES;
				CLANG_ANALYZER_NUMBER_OBJECT_CONVERSION = YES_AGGRESSIVE;
				CLANG_CXX_LANGUAGE_STANDARD = "gnu++20";
				CLANG_ENABLE_MODULES = YES;
				CLANG_ENABLE_OBJC_ARC = YES;
				CLANG_ENABLE_OBJC_WEAK = YES;
				CLANG_WARN_BLOCK_CAPTURE_AUTORELEASING = YES;
				CLANG_WARN_BOOL_CONVERSION = YES;
				CLANG_WARN_COMMA = YES;
				CLANG_WARN_CONSTANT_CONVERSION = YES;
				CLANG_WARN_DEPRECATED_OBJC_IMPLEMENTATIONS = YES;
				CLANG_WARN_DIRECT_OBJC_ISA_USAGE = YES_ERROR;
				CLANG_WARN_DOCUMENTATION_COMMENTS = YES;
				CLANG_WARN_EMPTY_BODY = YES;
				CLANG_WARN_ENUM_CONVERSION = YES;
				CLANG_WARN_INFINITE_RECURSION = YES;
				CLANG_WARN_INT_CONVERSION = YES;
				CLANG_WARN_NON_LITERAL_NULL_CONVERSION = YES;
				CLANG_WARN_OBJC_IMPLICIT_RETAIN_SELF = YES;
				CLANG_WARN_OBJC_LITERAL_CONVERSION = YES;
				CLANG_WARN_OBJC_ROOT_CLASS = YES_ERROR;
				CLANG_WARN_QUOTED_INCLUDE_IN_FRAMEWORK_HEADER = YES;
				CLANG_WARN_RANGE_LOOP_ANALYSIS = YES;
				CLANG_WARN_STRICT_PROTOTYPES = YES;
				CLANG_WARN_SUSPICIOUS_MOVE = YES;
				CLANG_WARN_UNGUARDED_AVAILABILITY = YES_AGGRESSIVE;
				CLANG_WARN_UNREACHABLE_CODE = YES;
				CLANG_WARN__DUPLICATE_METHOD_MATCH = YES;
				COPY_PHASE_STRIP = NO;
				DEBUG_INFORMATION_FORMAT = "dwarf-with-dsym";
				ENABLE_NS_ASSERTIONS = NO;
				ENABLE_STRICT_OBJC_MSGSEND = YES;
				ENABLE_USER_SCRIPT_SANDBOXING = YES;
				GCC_C_LANGUAGE_STANDARD = gnu17;
				GCC_NO_COMMON_BLOCKS = YES;
				GCC_WARN_64_TO_32_BIT_CONVERSION = YES;
				GCC_WARN_ABOUT_RETURN_TYPE = YES_ERROR;
				GCC_WARN_UNDECLARED_SELECTOR = YES;
				GCC_WARN_UNINITIALIZED_AUTOS = YES_AGGRESSIVE;
				GCC_WARN_UNUSED_FUNCTION = YES;
				GCC_WARN_UNUSED_VARIABLE = YES;
				IPHONEOS_DEPLOYMENT_TARGET = 14.0;
				LOCALIZATION_PREFERS_STRING_CATALOGS = YES;
				MACOSX_DEPLOYMENT_TARGET = 11.0;
				MTL_ENABLE_DEBUG_INFO = NO;
				MTL_FAST_MATH = YES;
				SUPPORTED_PLATFORMS = "iphoneos iphonesimulator macosx";
				SUPPORTS_MACCATALYST = NO;
				SWIFT_COMPILATION_MODE = wholemodule;
				VALIDATE_PRODUCT = YES;
			};
			name = Release;
		};
		1234567890ABCDE6 /* Build configuration list for PBXNativeTarget "${WRAPPER_FRAMEWORK}" */ = {
			isa = XCConfigurationList;
			buildConfigurations = (
				1234567890ABCDEA /* Debug */,
				1234567890ABCDEB /* Release */,
			);
			defaultConfigurationIsVisible = 0;
			defaultConfigurationName = Release;
		};
		1234567890ABCDE8 /* Build configuration list for PBXProject "${WRAPPER_PROJECT}" */ = {
			isa = XCConfigurationList;
			buildConfigurations = (
				1234567890ABCDEC /* Debug */,
				1234567890ABCDED /* Release */,
			);
			defaultConfigurationIsVisible = 0;
			defaultConfigurationName = Release;
		};
/* End XCConfigurationList section */

/* Begin XCLocalSwiftPackageReference section */
		1234567890ABCDE9 /* XCLocalSwiftPackageReference "." */ = {
			isa = XCLocalSwiftPackageReference;
			relativePath = .;
		};
/* End XCLocalSwiftPackageReference section */

/* Begin XCSwiftPackageProductDependency section */
		1234567890ABCDE0 /* ${PRODUCT} */ = {
			isa = XCSwiftPackageProductDependency;
			productName = ${PRODUCT};
		};
/* End XCSwiftPackageProductDependency section */
	};
	rootObject = 1234567890ABCDE7 /* Project object */;
}
ENDPROJ

# 3) Create Swift source file that re-exports the package
echo "==> Creating Swift source file"
cat > "${WRAPPER_FRAMEWORK}.swift" << ENDSWIFT
// Re-export the ${PRODUCT} module
@_exported import ${PRODUCT}
ENDSWIFT

# 4) Create scheme for the framework
echo "==> Creating build scheme"
mkdir -p "${WRAPPER_PROJECT}.xcodeproj/xcshareddata/xcschemes"
cat > "${WRAPPER_PROJECT}.xcodeproj/xcshareddata/xcschemes/${WRAPPER_FRAMEWORK}.xcscheme" << ENDSCHEME
<?xml version="1.0" encoding="UTF-8"?>
<Scheme
   LastUpgradeVersion = "1540"
   version = "1.7">
   <BuildAction
      parallelizeBuildables = "YES"
      buildImplicitDependencies = "YES">
      <BuildActionEntries>
         <BuildActionEntry
            buildForTesting = "YES"
            buildForRunning = "YES"
            buildForProfiling = "YES"
            buildForArchiving = "YES"
            buildForAnalyzing = "YES">
            <BuildableReference
               BuildableIdentifier = "primary"
               BlueprintIdentifier = "1234567890ABCDE5"
               BuildableName = "${WRAPPER_FRAMEWORK}.framework"
               BlueprintName = "${WRAPPER_FRAMEWORK}"
               ReferencedContainer = "container:${WRAPPER_PROJECT}.xcodeproj">
            </BuildableReference>
         </BuildActionEntry>
      </BuildActionEntries>
   </BuildAction>
   <TestAction
      buildConfiguration = "Debug"
      selectedDebuggerIdentifier = "Xcode.DebuggerFoundation.Debugger.LLDB"
      selectedLauncherIdentifier = "Xcode.DebuggerFoundation.Launcher.LLDB"
      shouldUseLaunchSchemeArgsEnv = "YES">
   </TestAction>
   <LaunchAction
      buildConfiguration = "Debug"
      selectedDebuggerIdentifier = "Xcode.DebuggerFoundation.Debugger.LLDB"
      selectedLauncherIdentifier = "Xcode.DebuggerFoundation.Launcher.LLDB"
      launchStyle = "0"
      useCustomWorkingDirectory = "NO"
      ignoresPersistentStateOnLaunch = "NO"
      debugDocumentVersioning = "YES"
      debugServiceExtension = "internal"
      allowLocationSimulation = "YES">
   </LaunchAction>
   <ProfileAction
      buildConfiguration = "Release"
      shouldUseLaunchSchemeArgsEnv = "YES"
      savedToolIdentifier = ""
      useCustomWorkingDirectory = "NO"
      debugDocumentVersioning = "YES">
   </ProfileAction>
   <AnalyzeAction
      buildConfiguration = "Debug">
   </AnalyzeAction>
   <ArchiveAction
      buildConfiguration = "Release"
      revealArchiveInOrganizer = "YES">
   </ArchiveAction>
</Scheme>
ENDSCHEME

# 4) Build archives for each platform
echo "==> Building for iOS"
xcodebuild archive \
  -project "${WRAPPER_PROJECT}.xcodeproj" \
  -scheme "${WRAPPER_FRAMEWORK}" \
  -configuration Release \
  -destination "generic/platform=iOS" \
  -archivePath "${OUT_DIR}/ios.xcarchive" \
  SKIP_INSTALL=NO \
  BUILD_LIBRARY_FOR_DISTRIBUTION=YES

echo "==> Building for iOS Simulator"
xcodebuild archive \
  -project "${WRAPPER_PROJECT}.xcodeproj" \
  -scheme "${WRAPPER_FRAMEWORK}" \
  -configuration Release \
  -destination "generic/platform=iOS Simulator" \
  -archivePath "${OUT_DIR}/ios-simulator.xcarchive" \
  SKIP_INSTALL=NO \
  BUILD_LIBRARY_FOR_DISTRIBUTION=YES

echo "==> Building for macOS"
xcodebuild archive \
  -project "${WRAPPER_PROJECT}.xcodeproj" \
  -scheme "${WRAPPER_FRAMEWORK}" \
  -configuration Release \
  -destination "generic/platform=macOS" \
  -archivePath "${OUT_DIR}/macos.xcarchive" \
  SKIP_INSTALL=NO \
  BUILD_LIBRARY_FOR_DISTRIBUTION=YES

# 5) Create XCFramework
echo "==> Creating XCFramework"
xcodebuild -create-xcframework \
  -framework "${OUT_DIR}/ios.xcarchive/Products/Library/Frameworks/${WRAPPER_FRAMEWORK}.framework" \
  -framework "${OUT_DIR}/ios-simulator.xcarchive/Products/Library/Frameworks/${WRAPPER_FRAMEWORK}.framework" \
  -framework "${OUT_DIR}/macos.xcarchive/Products/Library/Frameworks/${WRAPPER_FRAMEWORK}.framework" \
  -output "${XCFRAMEWORK}"

# 6) Clean up
echo "==> Cleaning up"
rm -rf "${WRAPPER_PROJECT}.xcodeproj"
rm -f "${WRAPPER_FRAMEWORK}.swift"
rm -rf "${OUT_DIR}"/*.xcarchive

# 7) Verify XCFramework
if [[ ! -d "${XCFRAMEWORK}" ]]; then
  echo "Error: Failed to create XCFramework"
  exit 1
fi

echo "✅ XCFramework created at: $XCFRAMEWORK"

# 8) Create zip and calculate checksum
cd "$OUT_DIR"
ZIPNAME="Kuzu.xcframework.zip"
echo "==> Creating ZIP archive"
zip -q -r "$ZIPNAME" "Kuzu.xcframework"

echo "==> Computing checksum"
CHECKSUM=$(swift package compute-checksum "$ZIPNAME")
cd - >/dev/null

# 9) Display results
cat <<EOF

────────────────────────────────────────
✅ Build completed successfully!

📦 XCFramework: ${XCFRAMEWORK}
📦 ZIP file: ${OUT_DIR}/${ZIPNAME}
🔑 Checksum: ${CHECKSUM}

To use in Package.swift:

.binaryTarget(
    name: "Kuzu",
    url: "https://github.com/1amageek/kuzu-swift/releases/download/VERSION/${ZIPNAME}",
    checksum: "${CHECKSUM}"
)

To sign for distribution:
codesign --timestamp -s "Your Identity" "${XCFRAMEWORK}"
────────────────────────────────────────
EOF