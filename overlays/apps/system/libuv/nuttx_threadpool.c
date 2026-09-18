/****************************************************************************
 * apps/system/libuv/nuttx_threadpool.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * NuttX-specific wrapper for libuv's thread pool implementation.
 ****************************************************************************/

#include <stdlib.h>

#ifdef __NuttX__
/*
 * Some NuttX framework workers, including ZBlue's sysworkq, do not own a
 * process environment.  libuv may initialize its per-task thread pool from
 * one of those workers and must not call getenv() there.  Returning NULL
 * keeps the deterministic Kconfig defaults for thread count and priority.
 */
static char *uv_nuttx_threadpool_getenv(const char *name)
{
  (void)name;
  return NULL;
}

#  define getenv(name) uv_nuttx_threadpool_getenv(name)
#endif

#include "libuv/src/threadpool.c"
