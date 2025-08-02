#!/usr/bin/env bash
# build-spm-xcframework.sh
# Swift Package から XCFramework を生成するユーティリティ
#
#   ./build-spm-xcframework.sh <ProductName> [<OutputDir>]
#
# ----------------------------------------------
set -euo pipefail

# 0) 引数とディレクトリ設定
PRODUCT="${1:?Error: product name required}"           # 例: Kuzu
OUT_DIR="${2:-build}"
DERIVED="${OUT_DIR}/DerivedData"
XCFRAMEWORK="${OUT_DIR}/${PRODUCT}.xcframework"

# 1) 前回生成物を掃除
rm -rf "${OUT_DIR}"
mkdir -p "${OUT_DIR}"

# 2) Swift build で各プラットフォーム用にビルド
echo "==> Building for iOS (arm64)"
swift build -c release \
  --sdk $(xcrun --sdk iphoneos --show-sdk-path) \
  --triple arm64-apple-ios14.0 \
  --scratch-path "${DERIVED}/ios"

echo "==> Building for iOS Simulator (arm64)"
swift build -c release \
  --sdk $(xcrun --sdk iphonesimulator --show-sdk-path) \
  --triple arm64-apple-ios14.0-simulator \
  --scratch-path "${DERIVED}/ios-sim-arm64"

echo "==> Building for iOS Simulator (x86_64)"
swift build -c release \
  --sdk $(xcrun --sdk iphonesimulator --show-sdk-path) \
  --triple x86_64-apple-ios14.0-simulator \
  --scratch-path "${DERIVED}/ios-sim-x86_64"

echo "==> Building for macOS (arm64)"
swift build -c release \
  --triple arm64-apple-macos11.0 \
  --scratch-path "${DERIVED}/macos-arm64"

echo "==> Building for macOS (x86_64)"
swift build -c release \
  --triple x86_64-apple-macos11.0 \
  --scratch-path "${DERIVED}/macos-x86_64"

# 3) 出力ライブラリのパスを取得
IOS_LIB="${DERIVED}/ios/release/lib${PRODUCT}.a"
IOS_SIM_ARM64_LIB="${DERIVED}/ios-sim-arm64/release/lib${PRODUCT}.a"
IOS_SIM_X86_64_LIB="${DERIVED}/ios-sim-x86_64/release/lib${PRODUCT}.a"
MAC_ARM64_LIB="${DERIVED}/macos-arm64/release/lib${PRODUCT}.a"
MAC_X86_64_LIB="${DERIVED}/macos-x86_64/release/lib${PRODUCT}.a"

# 4) ユニバーサルバイナリを作成
echo "==> Creating universal binaries"
mkdir -p "${OUT_DIR}/ios-sim-universal" "${OUT_DIR}/macos-universal"

# iOS Simulator universal binary
lipo -create \
  "${IOS_SIM_ARM64_LIB}" \
  "${IOS_SIM_X86_64_LIB}" \
  -output "${OUT_DIR}/ios-sim-universal/lib${PRODUCT}.a"

# macOS universal binary
lipo -create \
  "${MAC_ARM64_LIB}" \
  "${MAC_X86_64_LIB}" \
  -output "${OUT_DIR}/macos-universal/lib${PRODUCT}.a"

# 5) モジュールとヘッダーをコピー
echo "==> Copying modules and headers"
for platform in ios ios-sim-arm64 macos-arm64; do
  MOD_DIR="${DERIVED}/${platform}/release/${PRODUCT}.swiftmodule"
  if [[ -d "$MOD_DIR" ]]; then
    cp -R "$MOD_DIR" "${OUT_DIR}/"
    break
  fi
done

# Find headers
HEADER_DIR=""
for platform in ios ios-sim-arm64 macos-arm64; do
  POTENTIAL_HEADER="${DERIVED}/${platform}/release/${PRODUCT}.framework/Headers"
  if [[ -d "$POTENTIAL_HEADER" ]]; then
    HEADER_DIR="$POTENTIAL_HEADER"
    break
  fi
  # Also check for module map
  POTENTIAL_MODULE="${DERIVED}/${platform}/release/include/${PRODUCT}"
  if [[ -d "$POTENTIAL_MODULE" ]]; then
    HEADER_DIR="$POTENTIAL_MODULE"
    break
  fi
done

# 6) XCFramework を作成
echo "==> Creating XCFramework"
if [[ -n "$HEADER_DIR" ]]; then
  xcodebuild -create-xcframework \
    -library "${IOS_LIB}" \
    -headers "$HEADER_DIR" \
    -library "${OUT_DIR}/ios-sim-universal/lib${PRODUCT}.a" \
    -headers "$HEADER_DIR" \
    -library "${OUT_DIR}/macos-universal/lib${PRODUCT}.a" \
    -headers "$HEADER_DIR" \
    -output "$XCFRAMEWORK"
else
  xcodebuild -create-xcframework \
    -library "${IOS_LIB}" \
    -library "${OUT_DIR}/ios-sim-universal/lib${PRODUCT}.a" \
    -library "${OUT_DIR}/macos-universal/lib${PRODUCT}.a" \
    -output "$XCFRAMEWORK"
fi

echo "✅ XCFramework created at: $XCFRAMEWORK"

# 7) Zip + checksum（SwiftPM での binaryTarget 用）
cd "$OUT_DIR"
ZIPNAME="${PRODUCT}.xcframework.zip"
echo "==> Zipping"
zip -q -r "$ZIPNAME" "${PRODUCT}.xcframework"
echo "==> Computing checksum"
CHECKSUM=$(swift package compute-checksum "$ZIPNAME")
cd - >/dev/null

cat <<EOF

────────────────────────────────────────
📦  ${ZIPNAME} is ready.
🔑  SwiftPM checksum:  ${CHECKSUM}

# Package.swift (binaryTarget の例)
.binaryTarget(
  name: "${PRODUCT}",
  url: "https://…/${ZIPNAME}",
  checksum: "${CHECKSUM}"
)
────────────────────────────────────────────
EOF