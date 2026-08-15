#include "../../src/windows/wincrt.h"

void init_crt(void) {}

size_t strlen_wide(const char_t *str) {
    size_t result = 0;
    while (*str++)
        result++;
    return result;
}

void *malloc(size_t size) {
    return HeapAlloc(GetProcessHeap(), HEAP_GENERATE_EXCEPTIONS, size);
}

void *calloc(size_t num, size_t size) {
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, num * size);
}

void free(void *mem) {
    if (mem)
        HeapFree(GetProcessHeap(), 0, mem);
}

char_t *strncpy_wide(char_t *dst, const char_t *src, size_t len) {
    char_t *result = dst;
    while (len--)
        *dst++ = *src++;
    return result;
}

char_t *strcpy_wide(char_t *dst, const char_t *src) {
    char_t *result = dst;
    while ((*dst++ = *src++) != TEXT('\0')) {
    }
    return result;
}

char_t *strcat_wide(char_t *dst, const char_t *src) {
    char_t *result = dst;
    while (*dst)
        dst++;
    strcpy_wide(dst, src);
    return result;
}

void *dlsym(void *handle, const char *name) {
    (void)handle;
    (void)name;
    return NULL;
}

void *dlopen(const char_t *filename, int flag) {
    (void)filename;
    (void)flag;
    return NULL;
}

int setenv(const char_t *name, const char_t *value, int overwrite) {
    (void)name;
    (void)value;
    (void)overwrite;
    return 0;
}

char_t *getenv_wide(const char_t *name) {
    (void)name;
    return NULL;
}

void shutenv(char_t *value) { free(value); }
