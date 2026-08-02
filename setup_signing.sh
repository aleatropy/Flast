#!/usr/bin/env bash
# setup_signing.sh
#
# Run this from the FlacPlayer project root every time you generate or
# regenerate flacplayer-key.jks. It makes sure:
#   1. The keystore file lives at keystore/flacplayer-key.jks (the path
#      build.gradle.kts expects).
#   2. keystore/keystore.properties exists with the passwords/alias you
#      just used, so Gradle actually attaches a signingConfig to the
#      release build type -- without that file, `assembleRelease` silently
#      produces an UNSIGNED apk and `installRelease` won't even exist as
#      a task.
#
# Usage:
#   ./setup_signing.sh                     # generate a brand new key
#   ./setup_signing.sh /path/to/existing.jks   # use an existing key
#
# Either way you'll be prompted for the store password, key password,
# and alias (same prompts keytool normally gives you).

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
KEYSTORE_DIR="$PROJECT_ROOT/keystore"
KEYSTORE_FILE="$KEYSTORE_DIR/flacplayer-key.jks"
PROPS_FILE="$KEYSTORE_DIR/keystore.properties"

mkdir -p "$KEYSTORE_DIR"

if [ $# -ge 1 ]; then
    SRC_JKS="$1"
    if [ ! -f "$SRC_JKS" ]; then
        echo "error: $SRC_JKS not found" >&2
        exit 1
    fi
    echo "Using existing keystore: $SRC_JKS"
    cp "$SRC_JKS" "$KEYSTORE_FILE"

    read -r -p "Key alias: " KEY_ALIAS
    read -r -s -p "Store password: " STORE_PASSWORD; echo
    read -r -s -p "Key password (blank = same as store password): " KEY_PASSWORD; echo
    KEY_PASSWORD="${KEY_PASSWORD:-$STORE_PASSWORD}"
else
    echo "No existing .jks given -- generating a brand new key at $KEYSTORE_FILE"
    if [ -f "$KEYSTORE_FILE" ]; then
        read -r -p "$KEYSTORE_FILE already exists, overwrite it? [y/N] " CONFIRM
        [[ "$CONFIRM" =~ ^[Yy]$ ]] || { echo "Aborted."; exit 1; }
        rm -f "$KEYSTORE_FILE"
    fi

    read -r -p "Key alias [mi_alias]: " KEY_ALIAS
    KEY_ALIAS="${KEY_ALIAS:-mi_alias}"
    read -r -s -p "Store password (min 6 chars): " STORE_PASSWORD; echo
    read -r -s -p "Key password (blank = same as store password): " KEY_PASSWORD; echo
    KEY_PASSWORD="${KEY_PASSWORD:-$STORE_PASSWORD}"

    keytool -genkey -v \
        -keystore "$KEYSTORE_FILE" \
        -keyalg RSA -keysize 2048 -validity 10000 \
        -alias "$KEY_ALIAS" \
        -storepass "$STORE_PASSWORD" \
        -keypass "$KEY_PASSWORD" \
        -dname "CN=Aleatropy, OU=Unknown, O=Unknown, L=Unknown, ST=Unknown, C=Unknown"
fi

cat > "$PROPS_FILE" <<EOF
storePassword=$STORE_PASSWORD
keyPassword=$KEY_PASSWORD
keyAlias=$KEY_ALIAS
storeFile=flacplayer-key.jks
EOF

chmod 600 "$PROPS_FILE"

echo
echo "Done."
echo "  Keystore:   $KEYSTORE_FILE"
echo "  Properties: $PROPS_FILE (chmod 600, gitignored -- never commit this)"
echo
echo "Next steps:"
echo "  ./gradlew clean assembleRelease"
echo "  adb install -r app/build/outputs/apk/release/app-release.apk"
