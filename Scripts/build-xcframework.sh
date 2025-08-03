#!/usr/bin/env bash
# build-xcframework.sh
# Swift Package から XCFramework を生成するユーティリティ
#
#   ./build-xcframework.sh <ProductName> [<OutputDir>]
#
# 戦略：
# 1. xcodebuild を使用して Swift Package を直接ビルド
# 2. 各プラットフォーム用にフレームワークをビルド
# 3. ビルドされたフレームワークから XCFramework を作成
#
# ----------------------------------------------
set -euo pipefail

# 0) 引数とディレクトリ設定
PRODUCT="${1:?Error: product name required}"           # 例: Kuzu
OUT_DIR="${2:-build}"
XCFRAMEWORK="${OUT_DIR}/${PRODUCT}.xcframework"

# 1) 前回生成物を掃除
echo "==> Cleaning previous builds"
rm -rf "${OUT_DIR}"
mkdir -p "${OUT_DIR}"

# 2) 各プラットフォーム用のビルド
echo "==> Building for iOS"
xcodebuild build \
  -scheme "${PRODUCT}" \
  -destination "generic/platform=iOS" \
  -derivedDataPath "${OUT_DIR}/DerivedData" \
  -configuration Release \
  BUILD_LIBRARY_FOR_DISTRIBUTION=YES \
  SKIP_INSTALL=NO \
  OTHER_SWIFT_FLAGS="-no-verify-emitted-module-interface"

echo "==> Building for iOS Simulator"
xcodebuild build \
  -scheme "${PRODUCT}" \
  -destination "generic/platform=iOS Simulator" \
  -derivedDataPath "${OUT_DIR}/DerivedData" \
  -configuration Release \
  BUILD_LIBRARY_FOR_DISTRIBUTION=YES \
  SKIP_INSTALL=NO \
  OTHER_SWIFT_FLAGS="-no-verify-emitted-module-interface"

echo "==> Building for macOS"
xcodebuild build \
  -scheme "${PRODUCT}" \
  -destination "generic/platform=macOS" \
  -derivedDataPath "${OUT_DIR}/DerivedData" \
  -configuration Release \
  BUILD_LIBRARY_FOR_DISTRIBUTION=YES \
  SKIP_INSTALL=NO \
  OTHER_SWIFT_FLAGS="-no-verify-emitted-module-interface"

# 3) ビルド成果物のパスを検索
echo "==> Locating build products"
DERIVED_DATA="${OUT_DIR}/DerivedData"

# フレームワークのパスを探す
find_framework() {
  local platform="$1"
  local pattern="$2"
  find "${DERIVED_DATA}/Build/Products" -name "${PRODUCT}.framework" -type d | grep -E "${pattern}" | head -1
}

IOS_FRAMEWORK=$(find_framework "iOS" "Release-iphoneos")
IOS_SIM_FRAMEWORK=$(find_framework "iOS Simulator" "Release-iphonesimulator")
MACOS_FRAMEWORK=$(find_framework "macOS" "Release/")

# パスが見つかったか確認
if [[ -z "$IOS_FRAMEWORK" ]] || [[ -z "$IOS_SIM_FRAMEWORK" ]] || [[ -z "$MACOS_FRAMEWORK" ]]; then
  echo "Error: Could not find all required frameworks"
  echo "iOS: $IOS_FRAMEWORK"
  echo "iOS Simulator: $IOS_SIM_FRAMEWORK"
  echo "macOS: $MACOS_FRAMEWORK"
  exit 1
fi

# 4) XCFramework を作成
echo "==> Creating XCFramework"
echo "iOS Framework: $IOS_FRAMEWORK"
echo "iOS Simulator Framework: $IOS_SIM_FRAMEWORK"
echo "macOS Framework: $MACOS_FRAMEWORK"

xcodebuild -create-xcframework \
  -framework "${IOS_FRAMEWORK}" \
  -framework "${IOS_SIM_FRAMEWORK}" \
  -framework "${MACOS_FRAMEWORK}" \
  -output "${XCFRAMEWORK}"

# 5) XCFramework の検証
if [[ ! -d "${XCFRAMEWORK}" ]]; then
  echo "Error: Failed to create XCFramework"
  exit 1
fi

echo "✅ XCFramework created at: $XCFRAMEWORK"

# 6) ZIP作成とチェックサム計算
cd "$OUT_DIR"
ZIPNAME="${PRODUCT}.xcframework.zip"
echo "==> Creating ZIP archive"
zip -q -r "$ZIPNAME" "${PRODUCT}.xcframework"

echo "==> Computing checksum"
CHECKSUM=$(swift package compute-checksum "$ZIPNAME")
cd - >/dev/null

# 7) 結果を表示
cat <<EOF

────────────────────────────────────────
✅ Build completed successfully!

📦 XCFramework: ${XCFRAMEWORK}
📦 ZIP file: ${OUT_DIR}/${ZIPNAME}
🔑 Checksum: ${CHECKSUM}

To use in Package.swift:

.binaryTarget(
    name: "${PRODUCT}",
    url: "https://github.com/1amageek/kuzu-swift/releases/download/VERSION/${ZIPNAME}",
    checksum: "${CHECKSUM}"
)

To sign for distribution:
codesign --timestamp -s "Your Identity" "${XCFRAMEWORK}"
────────────────────────────────────────
EOF