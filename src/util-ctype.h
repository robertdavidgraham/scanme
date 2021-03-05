/*
    Wrappers for CTYPE functions.

 The various <ctype.h> functions like 'isspace()', 'isalpha()' have
 abstract portability problems, like not working as expected on IBM
 mainframes using EBCDIC when parsing network data. Therefore, this
 utility module wraps those functions for such cases.
*/
#ifndef UTIL_CTYPE_H
#define UTIL_CTYPE_H
#include <ctype.h>

#define ISSPACE(c) isspace(c)
#define ISALNuM(c) isalnum(c)
#define ISDIGIT(c) isdigit(c)
#define ISXDIGIT(c) isxdigit(c)

#endif

