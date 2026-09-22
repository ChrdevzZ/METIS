/*
 * Copyright 1997, Regents of the University of Minnesota
 *
 * bucketsort.c
 *
 * This file contains code that implement a variety of counting sorting
 * algorithms
 *
 * Started 7/25/97
 * George
 *
 */

#include "metislib.h"



/*************************************************************************
* This function uses simple counting sort to return a permutation array
* corresponding to the sorted order. The keys are arsumed to start from
* 0 and they are positive.  This sorting is used during matching.
**************************************************************************/
void BucketSortKeysInc(ctrl_t *ctrl, idx_t n, idx_t max, idx_t *keys, 
         idx_t *tperm, idx_t *perm)
{
  idx_t i, ii;
  idx_t *counts;

  if (!WCOREPUSH)
    return;

  counts = iwspacemalloc(ctrl, max+2);
  if (counts == NULL) {
    ctrl->status = METIS_ERROR_MEMORY;
    WCOREPOP;
    return;
  }
  iset(max+2, 0, counts);

  for (i=0; i<n; i++)
    counts[keys[i]]++;
  MAKECSR(i, max+1, counts);

  for (ii=0; ii<n; ii++) {
    i = tperm[ii];
    perm[counts[keys[i]]++] = i;
  }

  WCOREPOP;
}

