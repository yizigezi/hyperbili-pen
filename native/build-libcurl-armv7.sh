#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORK="$ROOT/native/.build-libcurl"
PREFIX="$WORK/target"
OPENSSL_VERSION="3.5.4"
CURL_VERSION="8.12.1"
TARGET="arm-linux-gnueabihf.2.23"
TOOLCHAIN="arm-linux-gnueabihf-"

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
  -shared -Wl,-soname,libjsapi_hyperhttp.so -Wl,--exclude-libs,ALL \
  -I"$ROOT/native/quickjs" -I"$ROOT/native" \
  "$ROOT/native/http_plugin.c" "$ROOT/native/http_client.c" \
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
  if [ "$(printf '%s\\n%s\\n' "2.23" "$GLIBC_VER" | sort -V | tail -1)" != "2.23" ]; then
    echo "glibc symbol version too new: $GLIBC_MAX" >&2
    exit 1
  fi
fi
cp "$SO" "$ROOT/libs/libjsapi_hyperhttp.so"
file "$SO"
echo "Built and staged: libs/libjsapi_hyperhttp.so"
