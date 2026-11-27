#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORK="$ROOT/native/.build-credential"
PREFIX="$WORK/target"
OPENSSL_VERSION="3.5.4"
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

mkdir -p "$WORK" "$PREFIX" "$ROOT/libs"
cd "$WORK"

if [ ! -d "openssl-$OPENSSL_VERSION" ]; then
  curl -fL "https://www.openssl.org/source/openssl-$OPENSSL_VERSION.tar.gz" -o "openssl-$OPENSSL_VERSION.tar.gz"
  tar xf "openssl-$OPENSSL_VERSION.tar.gz"
fi

CC="${ZIG_CMD[*]} cc -target $TARGET"
AR="${TOOLCHAIN}ar"
RANLIB="${TOOLCHAIN}ranlib"
cd "$WORK/openssl-$OPENSSL_VERSION"
if [ ! -f "$PREFIX/lib/libcrypto.a" ]; then
  CC="$CC" AR="$AR" RANLIB="$RANLIB" ./Configure linux-armv4 no-shared no-tests no-asm -fPIC --prefix="$PREFIX" --openssldir=/etc/ssl
  make -j2
  make install_sw
fi

cd "$ROOT"
"${ZIG_CMD[@]}" cc -target "$TARGET" -O2 -fPIC -fvisibility=hidden -Wall -Wextra \
  -shared -Wl,-soname,libjsapi_hypercredential.so \
  -I"$ROOT/native/quickjs" -I"$ROOT/native" -I"$PREFIX/include" \
  "$ROOT/native/credential_plugin.c" "$ROOT/native/credential_store.c" \
  -Wl,-Bstatic "$PREFIX/lib/libcrypto.a" -Wl,-Bdynamic -ldl -lpthread -lm \
  -o "$ROOT/libs/libjsapi_hypercredential.so"

SO="$ROOT/libs/libjsapi_hypercredential.so"
readelf -h "$SO" | grep -q 'Machine:.*ARM'
readelf -A "$SO" | grep -q 'Tag_ABI_VFP_args: VFP registers'
GLIBC_MAX="$(readelf -V "$SO" | grep -o 'GLIBC_[0-9.]*' | sort -Vu | tail -1 || true)"
if [ -n "$GLIBC_MAX" ]; then
  GLIBC_VER="${GLIBC_MAX#GLIBC_}"
  if [ "$(printf '%s\n%s\n' "2.23" "$GLIBC_VER" | sort -V | tail -1)" != "2.23" ]; then
    echo "glibc symbol version too new: $GLIBC_MAX" >&2
    exit 1
  fi
fi
echo "Built credential JSAPI: libs/libjsapi_hypercredential.so"
