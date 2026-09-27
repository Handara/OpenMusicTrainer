// pl_mpeg's code: the header holds it all, and this define turns on the implementation in exactly one file.
// It expects the standard headers for size_t and FILE to be included first.
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PL_MPEG_IMPLEMENTATION
#include "pl_mpeg.h"
