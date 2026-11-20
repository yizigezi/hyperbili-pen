#define _POSIX_C_SOURCE 200809L
#include "credential_store.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#define CS_MAGIC "HBCS"
#define CS_VERSION 1
#define CS_SALT_LEN 16
#define CS_NONCE_LEN 12
#define CS_TAG_LEN 16
#define CS_KEY_LEN 32
#define CS_KDF_ITERATIONS 600000
#define CS_HEADER_LEN (4 + 1 + CS_SALT_LEN + CS_NONCE_LEN + CS_TAG_LEN)
#define CS_MAX_DATA (1024 * 1024)

static int derive_key(const char *password, const unsigned char salt[CS_SALT_LEN], unsigned char key[CS_KEY_LEN]) {
    size_t n;
    if (!password) return -1;
    n = strlen(password);
    if (n < 8 || n > 1024) return -1;
    return PKCS5_PBKDF2_HMAC(password, (int)n, salt, CS_SALT_LEN, CS_KDF_ITERATIONS,
                             EVP_sha256(), CS_KEY_LEN, key) == 1 ? 0 : -2;
}
static int write_all(int fd, const unsigned char *p, size_t n) {
    size_t off = 0;
    while (off < n) {
        ssize_t k = write(fd, p + off, n - off);
        if (k < 0) { if (errno == EINTR) continue; return -1; }
        if (k == 0) return -1;
        off += (size_t)k;
    }
    return 0;
}
static int read_all(int fd, unsigned char *p, size_t n) {
    size_t off = 0;
    while (off < n) {
        ssize_t k = read(fd, p + off, n - off);
        if (k < 0) { if (errno == EINTR) continue; return -1; }
        if (k == 0) return -1;
        off += (size_t)k;
    }
    return 0;
}

int cs_store(const char *path, const char *password, const unsigned char *data, size_t data_len) {
    unsigned char salt[CS_SALT_LEN], nonce[CS_NONCE_LEN], key[CS_KEY_LEN], tag[CS_TAG_LEN];
    unsigned char *cipher = NULL, *file_buf = NULL;
    EVP_CIPHER_CTX *ctx = NULL;
    int a = 0, b = 0, rc = -2, fd = -1;
    size_t total_len = 0;
    char *tmp = NULL;
    if (!path || !*path || !password || !data || !data_len || data_len > CS_MAX_DATA) return -1;
    memset(key, 0, sizeof(key));
    if (RAND_bytes(salt, sizeof(salt)) != 1 || RAND_bytes(nonce, sizeof(nonce)) != 1 ||
        derive_key(password, salt, key) != 0) goto done;
    cipher = malloc(data_len + EVP_MAX_BLOCK_LENGTH);
    ctx = EVP_CIPHER_CTX_new();
    if (!cipher || !ctx) goto done;
    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL) != 1 ||
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, CS_NONCE_LEN, NULL) != 1 ||
        EVP_EncryptInit_ex(ctx, NULL, NULL, key, nonce) != 1 ||
        EVP_EncryptUpdate(ctx, cipher, &a, data, (int)data_len) != 1 ||
        EVP_EncryptFinal_ex(ctx, cipher + a, &b) != 1 ||
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, CS_TAG_LEN, tag) != 1) goto done;
    total_len = CS_HEADER_LEN + (size_t)a + (size_t)b;
    file_buf = malloc(total_len);
    if (!file_buf) goto done;
    memcpy(file_buf, CS_MAGIC, 4);
    file_buf[4] = CS_VERSION;
    memcpy(file_buf + 5, salt, CS_SALT_LEN);
    memcpy(file_buf + 5 + CS_SALT_LEN, nonce, CS_NONCE_LEN);
    memcpy(file_buf + 5 + CS_SALT_LEN + CS_NONCE_LEN, tag, CS_TAG_LEN);
    memcpy(file_buf + CS_HEADER_LEN, cipher, (size_t)a + (size_t)b);
    tmp = malloc(strlen(path) + 16);
    if (!tmp) goto done;
    sprintf(tmp, "%s.tmp.XXXXXX", path);
    fd = mkstemp(tmp);
    if (fd < 0 || fchmod(fd, S_IRUSR | S_IWUSR) != 0 ||
        write_all(fd, file_buf, total_len) != 0 || fsync(fd) != 0) goto done;
    if (close(fd) != 0) { fd = -1; goto done; }
    fd = -1;
    if (rename(tmp, path) != 0) goto done;
    rc = 0;
done:
    if (fd >= 0) close(fd);
    if (tmp && rc != 0) unlink(tmp);
    if (ctx) EVP_CIPHER_CTX_free(ctx);
    if (cipher) { OPENSSL_cleanse(cipher, data_len + EVP_MAX_BLOCK_LENGTH); free(cipher); }
    if (file_buf) { OPENSSL_cleanse(file_buf, total_len); free(file_buf); }
    OPENSSL_cleanse(key, sizeof(key));
    OPENSSL_cleanse(tag, sizeof(tag));
    free(tmp);
    return rc;
}

int cs_load(const char *path, const char *password, unsigned char **out_data, size_t *out_len) {
    int fd = -1, rc = -2, a = 0, b = 0;
    struct stat st;
    memset(&st, 0, sizeof(st));
    unsigned char *buf = NULL, *plain = NULL, key[CS_KEY_LEN];
    const unsigned char *salt, *nonce, *tag, *cipher;
    size_t cipher_len = 0;
    EVP_CIPHER_CTX *ctx = NULL;
    if (!path || !password || !out_data || !out_len) return -1;
    *out_data = NULL; *out_len = 0; memset(key, 0, sizeof(key));
    fd = open(path, O_RDONLY
#ifdef O_NOFOLLOW
              | O_NOFOLLOW
#endif
    );
    if (fd < 0 || fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) ||
        st.st_size < (off_t)CS_HEADER_LEN ||
        st.st_size > (off_t)(CS_HEADER_LEN + CS_MAX_DATA + EVP_MAX_BLOCK_LENGTH)) goto done;
    buf = malloc((size_t)st.st_size);
    if (!buf || read_all(fd, buf, (size_t)st.st_size) != 0) goto done;
    if (memcmp(buf, CS_MAGIC, 4) != 0 || buf[4] != CS_VERSION) goto done;
    salt = buf + 5; nonce = salt + CS_SALT_LEN; tag = nonce + CS_NONCE_LEN;
    cipher = buf + CS_HEADER_LEN; cipher_len = (size_t)st.st_size - CS_HEADER_LEN;
    if (derive_key(password, salt, key) != 0) goto done;
    plain = malloc(cipher_len + 1);
    ctx = EVP_CIPHER_CTX_new();
    if (!plain || !ctx) goto done;
    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL) != 1 ||
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, CS_NONCE_LEN, NULL) != 1 ||
        EVP_DecryptInit_ex(ctx, NULL, NULL, key, nonce) != 1 ||
        EVP_DecryptUpdate(ctx, plain, &a, cipher, (int)cipher_len) != 1 ||
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, CS_TAG_LEN, (void *)tag) != 1 ||
        EVP_DecryptFinal_ex(ctx, plain + a, &b) != 1) { rc = -3; goto done; }
    *out_len = (size_t)a + (size_t)b;
    plain[*out_len] = 0; *out_data = plain; plain = NULL; rc = 0;
done:
    if (fd >= 0) close(fd);
    if (ctx) EVP_CIPHER_CTX_free(ctx);
    if (buf) { OPENSSL_cleanse(buf, (size_t)(st.st_size > 0 ? st.st_size : 0)); free(buf); }
    if (plain) { OPENSSL_cleanse(plain, cipher_len + 1); free(plain); }
    OPENSSL_cleanse(key, sizeof(key));
    return rc;
}
int cs_delete(const char *path) {
    if (!path || !*path) return -1;
    if (unlink(path) == 0 || errno == ENOENT) return 0;
    return -2;
}
void cs_free(void *ptr, size_t len) { if (ptr) { OPENSSL_cleanse(ptr, len); free(ptr); } }
