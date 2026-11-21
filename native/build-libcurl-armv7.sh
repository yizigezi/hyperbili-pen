#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
WORK="$ROOT/.build-libcurl"
PREFIX="$WORK/target"
OPENSSL_VERSION="1.1.1w"
CURL_VERSION="8.12.1"
TOOLCHAIN="arm-linux-gnueabihf-"
CC="\${TOOLCHAIN}gcc"
mkdir -p "$WORK" "$PREFIX"
cd "$WORK"
fetch_source() {
  local name="$1" url="$2"
  if [ ! -d "$name" ]; then
    curl -fL "$url" -o "$name.tar.gz"
    tar xf "$name.tar.gz"
  fi
}
fetch_source "openssl-$OPENSSL_VERSION" "https://www.openssl.org/source/openssl-$OPENSSL_VERSION.tar.gz"
fetch_source "curl-$CURL_VERSION" "https://curl.se/download/curl-$CURL_VERSION.tar.xz"
cd "$WORK/openssl-$OPENSSL_VERSION"
if [ ! -f "$PREFIX/lib/libcrypto.a" ]; then
  CROSS_COMPILE="$TOOLCHAIN" ./Configure linux-armv4 no-shared no-tests no-asm -fPIC --prefix="$PREFIX" --openssldir=/etc/ssl
  make -j2
  make install_sw
fi
cd "$WORK/curl-$CURL_VERSION"
if [ ! -f "$PREFIX/lib/libcurl.a" ]; then
  ./configure \
    --host=arm-linux-gnueabihf \
    --build="$(./config.guess)" \
    CC="$CC" AR="\${TOOLCHAIN}ar" RANLIB="\${TOOLCHAIN}ranlib" \
    CFLAGS="-O2 -fPIC" \
    --prefix="$PREFIX" \
    --with-openssl="$PREFIX" \
    --disable-shared --enable-static \
    --without-zlib \
    --disable-ftp --disable-file --disable-ldap --disable-ldaps \
    --disable-dict --disable-telnet --disable-tftp --disable-pop3 \
    --disable-imap --disable-smtp --disable-gopher --disable-mqtt \
    --disable-smb --disable-rtsp
  make -j2
  make install
fi
cd "$ROOT"
"$CC" -O2 -fPIC -Wall -Wextra -shared \
  -Wl,-soname,libhyperhttp.so \
  -o "$ROOT/libhyperhttp.so" "$ROOT/http_client.c" \
  -I"$PREFIX/include" "$PREFIX/lib/libcurl.a" \
  -Wl,-Bstatic "$PREFIX/lib/libssl.a" "$PREFIX/lib/libcrypto.a" \
  -Wl,-Bdynamic -ldl -lpthread
file "$ROOT/libhyperhttp.so"
