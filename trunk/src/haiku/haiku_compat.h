/*
 * Force-included into every C++ file of the Haiku build (-include): the few POSIX calls Guitarix makes that Haiku does
 * not have. Haiku sets thread priorities with its own calls, and the audio threads do that themselves.
 */
#ifndef HAIKU_COMPAT_H
#define HAIKU_COMPAT_H

#include <pthread.h>
#include <sched.h>

#ifndef PTHREAD_EXPLICIT_SCHED
#	define PTHREAD_EXPLICIT_SCHED 1
#endif
#ifndef PTHREAD_INHERIT_SCHED
#	define PTHREAD_INHERIT_SCHED 0
#endif

static inline int pthread_attr_setschedpolicy(pthread_attr_t*, int) { return 0; }
static inline int pthread_attr_setinheritsched(pthread_attr_t*, int) { return 0; }

#ifndef MCL_CURRENT
#	define MCL_CURRENT 1
#	define MCL_FUTURE 2
#endif
static inline int mlockall(int) { return 0; }

#endif
