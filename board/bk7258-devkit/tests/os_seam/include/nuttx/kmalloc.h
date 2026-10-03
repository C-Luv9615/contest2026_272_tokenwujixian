#ifndef HOSTSTUB_KMALLOC_H
#define HOSTSTUB_KMALLOC_H
#include <stddef.h>
void *kmm_zalloc(size_t size);
void *kmm_malloc(size_t size);
void kmm_free(void *ptr);
#endif
