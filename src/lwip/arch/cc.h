#ifndef LWIP_ARCH_CC_H
#define LWIP_ARCH_CC_H

/* We have stdint.h and stddef.h from freestanding compiler */
/* lwIP arch.h will typedef u8_t, u16_t etc. from stdint.h automatically */

/* We don't have inttypes.h (PRIu32 etc.) - use simple format strings */
#define LWIP_NO_INTTYPES_H 1
#define X8_F  "x"
#define U16_F "u"
#define S16_F "d"
#define X16_F "x"
#define U32_F "u"
#define S32_F "d"
#define X32_F "x"
#define SZT_F "u"

/* We don't have ctype.h */
#define LWIP_NO_CTYPE_H 1

/* We don't have limits.h - provide needed defines */
#define LWIP_NO_LIMITS_H 1
#define INT_MAX 0x7FFFFFFF

/* No unistd.h */
#define LWIP_NO_UNISTD_H 1

/* Provide errno ourselves */
#define LWIP_PROVIDE_ERRNO 1

/* Diagnostics - use our kernel printf */
void printf(const char *format, ...);
#define LWIP_PLATFORM_DIAG(x) do { printf x; } while(0)
#define LWIP_PLATFORM_ASSERT(x) do { printf("lwIP ASSERT: %s\n", x); while(1){} } while(0)

#endif
