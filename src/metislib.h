/*
 * Copyright 1997, Regents of the University of Minnesota
 *
 * metis.h
 *
 * This file includes all necessary header files
 *
 * Started 8/27/94
 * George
 *
 * $Id: metislib.h 10655 2011-08-02 17:38:11Z benjamin $
 */

#ifndef _LIBMETIS_METISLIB_H_
#define _LIBMETIS_METISLIB_H_

/* Templates instantiated in METIS follow METIS' assertion policy. */
#ifdef METIS_ASSERTIONS_ENABLED
#undef GKLIB_ASSERTIONS_ENABLED
#define GKLIB_ASSERTIONS_ENABLED METIS_ASSERTIONS_ENABLED
#endif
#ifdef METIS_ASSERTIONS_EXPENSIVE_ENABLED
#undef GKLIB_ASSERTIONS_EXPENSIVE_ENABLED
#define GKLIB_ASSERTIONS_EXPENSIVE_ENABLED METIS_ASSERTIONS_EXPENSIVE_ENABLED
#endif
#include <GKlib.h>

#if defined(ENABLE_OPENMP)
  #include <omp.h>
#endif


#include <metis.h>
#include "rename.h"
#include "gklib_defs.h"

#include "defs.h"
#include "struct.h"
#include "macros.h"
#include "proto.h"


/* ISO C permits setjmp only in a restricted set of controlling expressions.
   Keep gk_sigcatch() as the complete switch expression and translate the two
   signals installed by gk_sigtrap() without assigning the setjmp result. */
#define METIS_SIGCATCH(r_sigrval) \
  do { \
    switch (gk_sigcatch()) { \
      case 0: \
        break; \
      case SIGMEM: \
        (r_sigrval) = SIGMEM; \
        break; \
      default: \
        (r_sigrval) = SIGERR; \
        break; \
    } \
  } while (0)


/*************************************************************************/
/*! Attempts an integer-array allocation under a nested signal trap. */
/*************************************************************************/
static inline idx_t *iMallocNoSignal(size_t count, const char *message,
    int *r_sigrval)
{
  volatile int sigrval=0;
  idx_t * volatile result=NULL;

  *r_sigrval = 0;
  if (!gk_sigtrap()) {
    errno = ENOMEM;
    *r_sigrval = SIGMEM;
    return NULL;
  }
  METIS_SIGCATCH(sigrval);
  if (sigrval == 0)
    result = imalloc(count, message);
  gk_siguntrap();

  *r_sigrval = sigrval;
  return (idx_t *)result;
}


/*************************************************************************/
/*! Attempts a real-array allocation under a nested signal trap. */
/*************************************************************************/
static inline real_t *rMallocNoSignal(size_t count, const char *message,
    int *r_sigrval)
{
  volatile int sigrval=0;
  real_t * volatile result=NULL;

  *r_sigrval = 0;
  if (!gk_sigtrap()) {
    errno = ENOMEM;
    *r_sigrval = SIGMEM;
    return NULL;
  }
  METIS_SIGCATCH(sigrval);
  if (sigrval == 0)
    result = rmalloc(count, message);
  gk_siguntrap();

  *r_sigrval = sigrval;
  return (real_t *)result;
}


/*************************************************************************/
/*! Attempts an integer-array reallocation under a nested signal trap.

    This helper lets callers release staged companion allocations before
    rethrowing a memory signal, while preserving GKlib's returning-error mode.
*/
/*************************************************************************/
static inline idx_t *iReallocNoSignal(idx_t *oldptr, size_t count,
    const char *message, int *r_sigrval)
{
  volatile int sigrval=0;
  idx_t * volatile result=NULL;

  *r_sigrval = 0;
  if (!gk_sigtrap()) {
    errno = ENOMEM;
    *r_sigrval = SIGMEM;
    return NULL;
  }
  METIS_SIGCATCH(sigrval);
  if (sigrval == 0)
    result = irealloc(oldptr, count, message);
  gk_siguntrap();

  *r_sigrval = sigrval;
  return (idx_t *)result;
}


/*************************************************************************/
/*! Converts a nonnegative real threshold to idx_t, saturating at IDX_MAX. */
/*************************************************************************/
static inline idx_t rToIdx(double value)
{
  if (!isfinite(value) || value >= (double)IDX_MAX)
    return IDX_MAX;
  return (idx_t)value;
}


#if defined(COMPILER_MSC)
#if defined(rint)
  #undef rint
#endif
#define rint(x) ((idx_t)((x)+0.5))  /* MSC does not have rint() function */
#endif

#endif
