/* Shadows glibc's <malloc.h>: the seam reads NuttX's mallinfo shape. */
#ifndef HOSTSTUB_MALLOC_H
#define HOSTSTUB_MALLOC_H
#include <stddef.h>
struct mallinfo
{
  int arena;
  int ordblks;
  int mxordblk;
  int uordblks;
  int fordblks;
};
struct mallinfo mallinfo(void);
void *malloc(size_t size);
void *calloc(size_t n, size_t size);
void free(void *ptr);
#endif
