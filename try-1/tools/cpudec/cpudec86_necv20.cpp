
#ifdef HAVE_CONFIG_H
# include "config.h" // must be first
#endif

#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>

#if HAVE_ENDIAN_H
# include <endian.h>
#else
# include "dosboxxr/lib/util/endian.h"
#endif

#include "cpudec86common.h"

#include "dosboxxr/lib/cpu/core/all_symbols.h"
#include "dosboxxr/lib/cpu/ipdec_pre_core.h"

void IPDec_necv20(x86_offset_t ip) {
    struct opcc_decom_state opstate;
    char *ipw,*ipwf;

    /* one instruction only */
    ipwf = IPDecStr+sizeof(IPDecStr);
    ipw = IPDecStr;
    IPDecStr[0] = 0;
    IPDecIP = ip;

    /* decode */
_x86decode_begin_code16_addr16:
_x86decode_after_prefix_code16_addr16:
#include "dosboxxr/lib/cpu/core/necv20.cxx.axx.decom.h"
    goto _x86done;
_x86decode_illegal_opcode:
_x86done:
    { } /* shut up GCC */
}
//// END CORE

