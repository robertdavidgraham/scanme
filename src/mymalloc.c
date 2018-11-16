#include "mymalloc.h"
#include <stdint.h>
#include <limits.h>
#include <string.h>

#define MAXNUM ((size_t)1 << (sizeof(size_t)*4)) 

void *
MALLOC(size_t size)
{
    void *result;

    result = malloc(size);
    if (result == NULL)
        abort();

    return result;
}

void *
CALLOC(size_t count, size_t size)
{
    void *result;

    /* Verify there is no integer overflow. Libraries should do this, but
     * the principle of this program is to be extra paranoid */
    if (count >= MAXNUM || size >= MAXNUM) {
        if (size != 0 && count >= SIZE_MAX/size)
            abort();
    }

    result = calloc(count, size);
    if (result == NULL)
        abort();

    return result;
}


void *
REALLOC(void *p, size_t size)
{
    void *result;

    result = realloc(p, size);
    if (result == NULL)
        abort();

    return result;
}

void *
REALLOCARRAY(void *p, size_t count, size_t size)
{
    void *result;

    /* Verify no integer overflow when these numbers are multiplied */
    if (count >= MAXNUM || size >= MAXNUM) {
        if (size != 0 && count >= SIZE_MAX/size)
            abort();
    }

    result = realloc(p, count * size);
    if (result == NULL)
        abort();

    return result;
}

char *STRDUP(const char *s)
{
    char *result;

#if defined(WIN32)
    result = _strdup(s);
#else
    result = strdup(s);
#endif

    if (result == NULL)
        abort();

    return result;
}
