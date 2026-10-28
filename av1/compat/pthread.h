/* pthread.h - stand-ins for Easel's single-threaded dav1d: locks do nothing, threads are never made */
#ifndef EASEL_PTHREAD_H
#define EASEL_PTHREAD_H
#include <stddef.h>

#define pthread_t easel_pthread_t
#define pthread_attr_t easel_pthread_attr_t
#define pthread_mutex_t easel_pthread_mutex_t
#define pthread_cond_t easel_pthread_cond_t
#define pthread_once_t easel_pthread_once_t
typedef int easel_pthread_t, easel_pthread_attr_t, easel_pthread_mutex_t, easel_pthread_cond_t, easel_pthread_once_t;
#undef PTHREAD_ONCE_INIT
#undef PTHREAD_MUTEX_INITIALIZER
#define PTHREAD_ONCE_INIT 0
#define PTHREAD_MUTEX_INITIALIZER 0

#define pthread_attr_init(a) (*(a) = 0)
#define pthread_attr_destroy(a) ((void)(a))
#define pthread_attr_setstacksize(a, s) ((void)(a), (void)(s), 0)
#define pthread_create(t, a, f, arg) (-1)
#define pthread_join(t, r) 0
#define pthread_mutex_init(m, a) (*(m) = 0)
#define pthread_mutex_destroy(m) ((void)(m))
#define pthread_mutex_lock(m) ((void)(m))
#define pthread_mutex_unlock(m) ((void)(m))
#define pthread_cond_init(c, a) (*(c) = 0)
#define pthread_cond_destroy(c) ((void)(c))
#define pthread_cond_wait(c, m) ((void)(c), (void)(m))
#define pthread_cond_signal(c) ((void)(c))
#define pthread_cond_broadcast(c) ((void)(c))
static void easel_once(pthread_once_t *o, void (*f)(void)) { if (!*o) { *o = 1; f(); } }
#define pthread_once(o, f) easel_once((o), (f))
#endif
