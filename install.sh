#!/bin/bash
# 构建生产包并安装到词典笔（需已连接 adb）
set -e

# Native HTTP JSAPI is required for Set-Cookie access.
if [ ! -f libs/libjsapi_hyperhttp.so ]; then
  bash native/build-libcurl-armv7.sh
fi

npm run build:prod
amr_file=$(ls *.amr | head -n1)
base_name="${amr_file%%.*}"

adb push "$amr_file" /userdisk/Favorite/
# Falcon may keep an existing native libs/ extraction for the same appid.
# Uninstall the app manually before reinstalling when updating libjsapi_hyperhttp.so.
adb shell "miniapp_cli install /userdisk/Favorite/$amr_file"
adb shell "miniapp_cli start $base_name"
rm -f *.amr
