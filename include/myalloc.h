#ifndef MYALLOC_H
#define MYALLOC_H

#include <stddef.h>   /* for size_t */

void *my_malloc(size_t size);
void  my_free(void *ptr);
void *my_calloc(size_t count, size_t size);
void *my_realloc(void *ptr, size_t size);
void  my_alloc_stats(void);

#endif