# Credential Store v1

First native prototype for Bilibili credential storage.

## Security design

- AES-256-GCM encrypts the credential payload.
- PBKDF2-HMAC-SHA256 derives a 256-bit key from a user-supplied vault password (600,000 iterations).
- Each save generates a fresh 128-bit salt and 96-bit nonce.
- The authentication tag detects wrong passwords and modified ciphertext.
- Derived key material is cleansed after each operation.
- Writes use a temporary file, fsync, atomic rename, and mode 0600.
- The encrypted payload should be a small JSON object containing only required cookies. Never log credentials.

This is not hardware-backed security. Since the device runs the app as root, another root process can inspect process memory or replace the native library. This prototype protects a copied encrypted file when the vault password is not available; it does not protect a compromised running device.

## File format

Binary: magic (4 bytes), version (1 byte), salt (16 bytes), nonce (12 bytes), GCM tag (16 bytes), ciphertext.

## Build

The target must provide OpenSSL 1.1-compatible headers and libcrypto. The device inspection found libcrypto.so.1.1 but no OpenSSL headers, so build against a matching ARMv7 hard-float sysroot/toolchain and deploy the shared library with a native bridge.

Example:

    make CC=arm-linux-gnueabihf-gcc

Do not use a random host OpenSSL build. Match the device ABI, libc (glibc vs musl), ARM float ABI, and OpenSSL ABI.

## C API

- cs_store(path, password, data, data_len)
- cs_load(path, password, &data, &data_len)
- cs_delete(path)
- cs_free(data, data_len)

Passwords must be at least 8 bytes. The caller must clear its own password buffer. cs_load returns -3 for a wrong password or modified file.

## Integration status

This is a native library prototype only. The current MiniApp repository has no confirmed JS-to-native invocation contract, so the Vue login flow is intentionally not switched to it yet. Do not fall back to plaintext storage if the native provider is unavailable. Once the device-side bridge is confirmed, wire this API behind a CredentialManager adapter and add the vault unlock UI.


## Native HTTP / libcurl (ARMv7)

```sh
bash native/build-libcurl-armv7.sh
```

The script cross-builds OpenSSL 1.1.1w and libcurl 8.12.1 as static PIC archives, then links them into `libs/libjsapi_hyperhttp.so`. It targets ARMv7 hard-float with glibc 2.23 and statically includes TLS dependencies, so it does not depend on the pen's OpenSSL 1.1 ABI or headers. HTTPS peer and hostname verification are explicitly enabled.

The Falcon module is registered as `hyperHttp` by `custom_init_jsapis()` in `http_plugin.c` (QuickJS C-module ABI shared with `readex-nt`). JS imports it with `import('hyperHttp')`. The adapter in `src/utils/http.js` submits work, then polls every 20 ms from the JS event loop; it never waits synchronously for network I/O. The app build downloads this native library into `libs/` before packaging, so the `.amr` contains `libs/libjsapi_hyperhttp.so`.

The exported C API is in `http_client.h`:

- `hb_http_init()` / `hb_http_shutdown()`: start and drain a bounded worker pool (2 workers).
- `hb_http_submit()`: copy request URL, headers, and body into native-owned memory and enqueue it. All outstanding requests (queued, active, or completed-but-not-polled) are bounded to 8; overload returns an error.
- `hb_http_poll()`: non-blocking completion poll. Only the caller/JS owner thread should convert results into JS values and settle Promises.
- `hb_http_response_free()`: release response memory.

Workers perform all libcurl work. They never receive or access QuickJS runtime/context/value pointers. The response's raw header buffer preserves each header line separately, including repeated `Set-Cookie` lines; it deliberately does not join them with commas (a comma can occur inside cookie attributes such as `Expires`). Redirect response header blocks are retained too, so the JS adapter should select the final HTTP header block while keeping each Set-Cookie line as a separate array item.

**Runtime lifecycle:** `http_plugin.c` exports `custom_init_jsapis()` and registers the `hyperHttp` QuickJS module. `src/utils/http.js` imports it dynamically, submits requests, and polls from the JS event loop; it never waits synchronously for network I/O. The shared-library destructor calls `hb_http_shutdown()` and joins all workers before unload. Workers never touch QuickJS. `src/bilibiliclient/login.js` now handles `set-cookie` as a list and no longer splits cookie strings on commas.
