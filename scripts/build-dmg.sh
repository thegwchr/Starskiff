#!/bin/bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
CONFIGURATION="${CONFIGURATION:-Release}"
DERIVED_DATA_PATH="${DERIVED_DATA_PATH:-$ROOT_DIR/build}"
PRODUCTS_DIR="$DERIVED_DATA_PATH/Build/Products/$CONFIGURATION"
APP_PATH="$PRODUCTS_DIR/Starskiff.app"
ARTIFACTS_DIR="${ARTIFACTS_DIR:-$ROOT_DIR/Artifacts}"
STAGING_DIR="$ROOT_DIR/.dmg-staging"
DMG_NAME="${DMG_NAME:-Starskiff.dmg}"
DMG_PATH="$ARTIFACTS_DIR/$DMG_NAME"

if [ ! -d "$APP_PATH" ]; then
  echo "Missing app bundle: $APP_PATH" >&2
  echo "Build Starskiff first, or set DERIVED_DATA_PATH/CONFIGURATION." >&2
  exit 1
fi

rm -rf "$STAGING_DIR"
mkdir -p "$STAGING_DIR" "$ARTIFACTS_DIR"
cp -R "$APP_PATH" "$STAGING_DIR/"
ln -s /Applications "$STAGING_DIR/Applications"

rm -f "$DMG_PATH"
hdiutil create \
  -volname "Starskiff" \
  -srcfolder "$STAGING_DIR" \
  -ov \
  -format UDZO \
  "$DMG_PATH"

rm -rf "$STAGING_DIR"
echo "$DMG_PATH"
