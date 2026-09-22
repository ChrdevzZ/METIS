/*
 * metisbin.h
 *
 * This file contains the various header inclusions
 *
 * Started 8/9/02
 * George
 */

#include "metislib.h"
#include <stddef.h>
#include <stdlib.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <ctype.h>
#include <math.h>
#include <time.h>
#include <string.h>
#include <limits.h>
#include <signal.h>
#include <setjmp.h>
#include <assert.h>


#if defined(ENABLE_OPENMP)
  #include <omp.h>
#endif


#include <metis.h>
#include "defs.h"
#include "struct.h"
#include "proto.h"


/*************************************************************************/
/*! Parses a decimal value through a type at least as wide as idx_t.

    The conversion checks errno and the configured idx_t bounds before
    narrowing. The caller can inspect \c endptr to enforce its own token or
    complete-string grammar.
*/
/*************************************************************************/
static inline int ParseIndex(const char *text, char **endptr, idx_t *result)
{
  char *end;
  long long parsed;

  if (text == NULL || result == NULL)
    return 0;

  errno = 0;
  parsed = strtoll(text, &end, 10);
  if (endptr != NULL)
    *endptr = end;
  if (end == text || errno == ERANGE ||
      parsed < (long long)IDX_MIN || parsed > (long long)IDX_MAX)
    return 0;

  *result = (idx_t)parsed;
  return 1;
}


/*************************************************************************/
/*! Parses one complete command-line integer or terminates with an error.

    \param name identifies the option in the diagnostic.
    \param text is the complete decimal argument.
    \returns the validated idx_t value.
*/
/*************************************************************************/
static inline idx_t ParseInteger(const char *name, const char *text)
{
  char *endptr;
  idx_t result=0;

  if (!ParseIndex(text, &endptr, &result) || *endptr != '\0')
    errexit("Invalid integer for %s: %s\n", name, text);

  return result;
}


#if defined(COMPILER_GCC)
extern char* strdup (const char *);
#endif

#define SVNINFO "unknown"

#if defined(COMPILER_MSC)
#if defined(rint)
  #undef rint
#endif
#define rint(x) ((idx_t)((x)+0.5))  /* MSC does not have rint() function */

#endif
