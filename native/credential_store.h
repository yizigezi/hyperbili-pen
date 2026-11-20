#ifndef HYPERBILI_CREDENTIAL_STORE_H
#define HYPERBILI_CREDENTIAL_STORE_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
int cs_store(const char *path, const char *password, const unsigned char *data, size_t data_len);
int cs_load(const char *path, const char *password, unsigned char **out_data, size_t *out_len);
int cs_delete(const char *path);
void cs_free(void *ptr, size_t len);
#ifdef __cplusplus
}
#endif
#endif
