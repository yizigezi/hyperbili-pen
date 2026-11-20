CC ?= arm-linux-gnueabihf-gcc
CFLAGS ?= -O2 -fPIC -Wall -Wextra
LDFLAGS ?=
OPENSSL_LIBS ?= -lcrypto
all: libcredential_store.so
libcredential_store.so: credential_store.c credential_store.h
	$(CC) $(CFLAGS) -shared -Wl,-soname,libcredential_store.so -o $@ credential_store.c $(LDFLAGS) $(OPENSSL_LIBS)
clean:
	rm -f libcredential_store.so
.PHONY: all clean
