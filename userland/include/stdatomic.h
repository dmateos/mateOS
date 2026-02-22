#ifndef MATEOS_STDATOMIC_H
#define MATEOS_STDATOMIC_H

#define _Atomic(type) type
#define ATOMIC_VAR_INIT(value) (value)

#define memory_order_relaxed 0
#define memory_order_consume 1
#define memory_order_acquire 2
#define memory_order_release 3
#define memory_order_acq_rel 4
#define memory_order_seq_cst 5

typedef struct { volatile int v; } atomic_int;

typedef int memory_order;

static inline void atomic_thread_fence(memory_order order) { (void)order; }

#endif
