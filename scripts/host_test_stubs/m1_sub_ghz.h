/* Host-test stub of m1_sub_ghz.h — just enough to compile
 * Sub_Ghz/m1_sub_ghz_validate.c without dragging in STM32 / FatFs
 * headers. The validate engine never references any symbol from this
 * file beyond what minimum the include chain demands. */
#ifndef _M1_SUB_GHZ_HOST_STUB
#define _M1_SUB_GHZ_HOST_STUB

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    SUB_GHZ_BAND_300 = 0,
    SUB_GHZ_BAND_EOL,
} S_M1_SubGHz_Band;

#endif
