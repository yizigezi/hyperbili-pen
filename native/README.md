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
