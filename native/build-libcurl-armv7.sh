#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORK="$ROOT/native/.build-libcurl"
PREFIX="$WORK/target"
OPENSSL_VERSION="3.5.4"
CURL_VERSION="8.12.1"
TARGET="arm-linux-gnueabihf.2.23"
TOOLCHAIN="arm-linux-gnueabihf-"

CA_BUNDLE_COMMIT="024ad1476500f7593a6c08061b1273fd44f35158"
CA_BUNDLE_URL="https://raw.githubusercontent.com/composer/ca-bundle/$CA_BUNDLE_COMMIT/res/cacert.pem"
CA_BUNDLE_GIT_BLOB="c5818b24d021b6244cdc0ee865f997acd627f4fc"
CA_FILE="$WORK/cacert.pem"
CA_SOURCE="$WORK/ca_bundle.c"
CA_HEADER="$WORK/ca_bundle.h"

if command -v zig >/dev/null 2>&1; then
  ZIG_CMD=(zig)
elif python3 -c "import ziglang" >/dev/null 2>&1; then
  ZIG_CMD=(python3 -m ziglang)
else
  echo "zig is required (install with: python3 -m pip install ziglang)" >&2
  exit 1
fi
mkdir -p "$WORK" "$PREFIX" "$ROOT/native/dist" "$ROOT/libs"
cd "$WORK"

fetch_source() {
  local name="$1" url="$2"
  if [ ! -d "$name" ]; then
    curl -fL "$url" -o "$name.archive"
    case "$url" in
      *.tar.xz) tar xf "$name.archive" ;;
      *) tar xf "$name.archive" ;;
    esac
  fi
}
fetch_source "openssl-$OPENSSL_VERSION" "https://www.openssl.org/source/openssl-$OPENSSL_VERSION.tar.gz"
fetch_source "curl-$CURL_VERSION" "https://curl.se/download/curl-$CURL_VERSION.tar.xz"

# Fetch a pinned Mozilla-derived CA bundle. The checksum makes this reproducible:
# updating the upstream file requires an intentional checksum review.
curl -fL "$CA_BUNDLE_URL" -o "$CA_FILE"
CA_BUNDLE_ACTUAL="$(git hash-object "$CA_FILE")"
if [ "$CA_BUNDLE_ACTUAL" != "$CA_BUNDLE_GIT_BLOB" ]; then
  echo "CA bundle Git blob hash mismatch: expected $CA_BUNDLE_GIT_BLOB, got $CA_BUNDLE_ACTUAL" >&2
  exit 1
fi
python3 - "$CA_FILE" "$CA_SOURCE" "$CA_HEADER" <<'PY'
import pathlib
import sys

source_path, c_path, h_path = map(pathlib.Path, sys.argv[1:])
data = source_path.read_bytes()
if b"-----BEGIN CERTIFICATE-----" not in data:
    raise SystemExit("CA bundle does not look like a PEM certificate bundle")
nl = chr(10)
c_source = '#include "ca_bundle.h"' + nl + 'const unsigned char hb_ca_bundle[] = {' + nl
c_source += ''.join('  ' + ', '.join('0x%02x' % b for b in data[i:i+16]) + ',' + nl
                    for i in range(0, len(data), 16))
c_source += '};' + nl + 'const size_t hb_ca_bundle_len = sizeof(hb_ca_bundle);' + nl
c_path.write_text(c_source, encoding="ascii")
h_path.write_text(
    '#ifndef HB_CA_BUNDLE_H' + nl + '#define HB_CA_BUNDLE_H' + nl
    + '#include <stddef.h>' + nl
    + 'extern const unsigned char hb_ca_bundle[];' + nl
    + 'extern const size_t hb_ca_bundle_len;' + nl
    + '#endif' + nl,
    encoding="ascii"
)
print("Verified and prepared embedded CA bundle:", len(data), "bytes")
PY
CC="${ZIG_CMD[*]} cc -target $TARGET"
AR="${TOOLCHAIN}ar"
RANLIB="${TOOLCHAIN}ranlib"

cd "$WORK/openssl-$OPENSSL_VERSION"
if [ ! -f "$PREFIX/lib/libcrypto.a" ]; then
  CC="$CC" AR="$AR" RANLIB="$RANLIB" ./Configure linux-armv4 no-shared no-tests no-asm -fPIC --prefix="$PREFIX" --openssldir=/etc/ssl
  make -j2
  make install_sw
fi

cd "$WORK/curl-$CURL_VERSION"
if [ ! -f "$PREFIX/lib/libcurl.a" ]; then
  CC="$CC" AR="$AR" RANLIB="$RANLIB" CFLAGS="-O2 -fPIC" \
    ./configure --host=arm-linux-gnueabihf --build="$(./config.guess)" \
      --prefix="$PREFIX" --with-openssl="$PREFIX" \
      --disable-shared --enable-static --without-zlib --without-libpsl \
      --disable-ftp --disable-file --disable-ldap --disable-ldaps \
      --disable-dict --disable-telnet --disable-tftp --disable-pop3 \
      --disable-imap --disable-smtp --disable-gopher --disable-mqtt \
      --disable-smb --disable-rtsp
  make -j2
  make install
fi

cd "$ROOT"
"${ZIG_CMD[@]}" cc -target "$TARGET" -O2 -fPIC -fvisibility=hidden -Wall -Wextra \
  -shared -Wl,-soname,libjsapi_hyperhttp.so \
  -I"$ROOT/native/quickjs" -I"$ROOT/native" -I"$WORK" -I"$PREFIX/include" \
  "$ROOT/native/http_plugin.c" "$ROOT/native/http_client.c" "$CA_SOURCE" \
  "$ROOT/native/credential_plugin.c" "$ROOT/native/credential_store.c" \
  "$PREFIX/lib/libcurl.a" -Wl,-Bstatic "$PREFIX/lib/libssl.a" "$PREFIX/lib/libcrypto.a" \
  -Wl,-Bdynamic -ldl -lpthread -lm \
  -o "$ROOT/native/dist/libjsapi_hyperhttp.so"

SO="$ROOT/native/dist/libjsapi_hyperhttp.so"
readelf -h "$SO" | grep -q 'Machine:.*ARM'
readelf -A "$SO" | grep -q 'Tag_ABI_VFP_args: VFP registers'
nm -D "$SO" | grep -q ' T custom_init_jsapis'
nm -D "$SO" | grep -q ' U registerCModuleLoader'
GLIBC_MAX="$(readelf -V "$SO" | grep -o 'GLIBC_[0-9.]*' | sort -Vu | tail -1 || true)"
if [ -n "$GLIBC_MAX" ]; then
  GLIBC_VER="${GLIBC_MAX#GLIBC_}"
  if [ "$(printf '%s\n%s\n' "2.23" "$GLIBC_VER" | sort -V | tail -1)" != "2.23" ]; then
    echo "glibc symbol version too new: $GLIBC_MAX" >&2
    exit 1
  fi
fi
cp "$SO" "$ROOT/libs/libjsapi_hyperhttp.so"
file "$SO"
echo "Built and staged: libs/libjsapi_hyperhttp.so"
