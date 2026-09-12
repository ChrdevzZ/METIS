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


#if defined(COMPILER_MSC)
#if defined(rint)
  #undef rint
#endif
#define rint(x) ((idx_t)((x)+0.5))  /* MSC does not have rint() function */
#endif

#endif
