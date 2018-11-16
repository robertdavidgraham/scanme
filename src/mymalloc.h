#ifndef MYMALLOC_H
#define MYMALLOC_H
#include <stdio.h>
#include <stdlib.h>
#include <stdlib.h>

void *MALLOC(size_t size);
void *CALLOC(size_t count, size_t size);
void *REALLOC(void *p, size_t size);
void *REALLOCARRAY(void *p, size_t count, size_t size);
char *STRDUP(const char *s);

#define FREE(x) free(x)



#endif
