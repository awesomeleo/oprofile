/**
 * @file opd_cookie.h
 * sys_lookup_dcookie support
 *
 * @remark Copyright 2002 OProfile authors
 * @remark Read the file COPYING
 *
 * @author John Levon
 */

#ifndef OPD_COOKIE_H
#define OPD_COOKIE_H

#include <sys/syscall.h>
#include <unistd.h>
#include "op_types.h"

#if defined(__i386__)
#define opd_nr_lookup_dcookie 253
#elif defined(__x86_64__)
#define opd_nr_lookup_dcookie 212
#elif defined(__powerpc__)
#define opd_nr_lookup_dcookie 235
#elif defined(__alpha__)
#define opd_nr_lookup_dcookie 406
#elif defined(__hppa__)
#define opd_nr_lookup_dcookie 223
#elif defined(__ia64__)
#define opd_nr_lookup_dcookie 1237
#elif defined(__sparc__)
/* untested */
#define opd_nr_lookup_dcookie 208
#else
#error Please define lookup_dcookie for your architecture
#endif

#if (defined(__powerpc__) && !defined(__powerpc64__)) || defined(__hppa__)
static inline int lookup_dcookie(cookie_t cookie, char * buf, size_t size)
{
	return syscall(opd_nr_lookup_dcookie, (unsigned long)(cookie >> 32),
		       (unsigned long)(cookie & 0xffffffff), buf, size);
}
#else
static inline int lookup_dcookie(cookie_t cookie, char * buf, size_t size)
{
	return syscall(opd_nr_lookup_dcookie, cookie, buf, size);
}
#endif

#endif /* OPD_COOKIE_H */
