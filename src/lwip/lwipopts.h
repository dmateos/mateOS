#ifndef LWIPOPTS_H
#define LWIPOPTS_H

/* --- NO_SYS mode (bare-metal, no threads) --- */
#define NO_SYS                  1
#define LWIP_NETCONN            0
#define LWIP_SOCKET             0

/* --- Memory --- */
#define MEM_ALIGNMENT           4
#define MEM_SIZE                (32 * 1024)
#define MEMP_NUM_PBUF           16
#define MEMP_NUM_TCP_PCB        4
#define MEMP_NUM_TCP_PCB_LISTEN 2
#define MEMP_NUM_UDP_PCB        4
#define PBUF_POOL_SIZE          16
#define PBUF_POOL_BUFSIZE       1600

/* --- Protocols --- */
#define LWIP_ARP                1
#define LWIP_ICMP               1
#define LWIP_UDP                1
#define LWIP_TCP                1
#define LWIP_DHCP               1
#define LWIP_ACD                1
#define LWIP_DNS                0
#define LWIP_RAW                1
#define LWIP_AUTOIP             0
#define LWIP_IGMP               0

/* --- TCP tuning --- */
#define TCP_MSS                 1460
#define TCP_WND                 (4 * TCP_MSS)
#define TCP_SND_BUF             (4 * TCP_MSS)

/* Simplify TX: single contiguous pbuf per outgoing frame */
#define LWIP_NETIF_TX_SINGLE_PBUF 1

/* PRNG for TCP ISN */
extern unsigned int get_tick_count(void);
#define LWIP_RAND()             ((u32_t)(get_tick_count() * 214013u + 2531011u))

/* --- Checksums (software) --- */
#define CHECKSUM_GEN_IP         1
#define CHECKSUM_GEN_UDP        1
#define CHECKSUM_GEN_TCP        1
#define CHECKSUM_CHECK_IP       1
#define CHECKSUM_CHECK_UDP      1
#define CHECKSUM_CHECK_TCP      1

/* --- Interrupt protection --- */
#define SYS_LIGHTWEIGHT_PROT    1
typedef int sys_prot_t;

#include "arch/i686/cpu.h"

static inline sys_prot_t sys_arch_protect(void) {
  return (sys_prot_t)cpu_irq_save();
}
static inline void sys_arch_unprotect(sys_prot_t pval) {
  cpu_irq_restore((uint32_t)pval);
}

#define SYS_ARCH_DECL_PROTECT(lev)  sys_prot_t lev
#define SYS_ARCH_PROTECT(lev)       lev = sys_arch_protect()
#define SYS_ARCH_UNPROTECT(lev)     sys_arch_unprotect(lev)

/* --- Stats (off) --- */
#define LWIP_STATS              0

/* --- Debug (off by default) --- */
#define LWIP_DEBUG              0

#endif
