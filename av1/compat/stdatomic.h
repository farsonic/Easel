/* stdatomic.h - Easel builds dav1d with a single thread, so its atomics are plain memory accesses */
#ifndef EASEL_STDATOMIC_H
#define EASEL_STDATOMIC_H

typedef int atomic_int;
typedef unsigned int atomic_uint;

#define memory_order_relaxed 0
#define memory_order_acquire 0

static int easel_atomic_add(void *p, int v) { int o = *(int *)p; *(int *)p = o + v; return o; }
static int easel_atomic_or(void *p, int v) { int o = *(int *)p; *(int *)p = o | v; return o; }
static unsigned easel_atomic_xchg(void *p, unsigned v) { unsigned o = *(unsigned *)p; *(unsigned *)p = v; return o; }
static int easel_atomic_cas(void *p, void *expected, unsigned v)
{
  if (*(unsigned *)p == *(unsigned *)expected) { *(unsigned *)p = v; return 1; }
  *(unsigned *)expected = *(unsigned *)p;
  return 0;
}

#define atomic_init(p, v) (*(p) = (v))
#define atomic_store(p, v) (*(p) = (v))
#define atomic_load(p) (*(p))
#define atomic_load_explicit(p, mo) (*(p))
#define atomic_fetch_add(p, v) easel_atomic_add((p), (int)(v))
#define atomic_fetch_add_explicit(p, v, mo) easel_atomic_add((p), (int)(v))
#define atomic_fetch_sub(p, v) easel_atomic_add((p), -(int)(v))
#define atomic_fetch_or(p, v) easel_atomic_or((p), (int)(v))
#define atomic_exchange(p, v) easel_atomic_xchg((p), (unsigned)(v))
#define atomic_compare_exchange_strong(p, e, v) easel_atomic_cas((p), (e), (unsigned)(v))

#endif
