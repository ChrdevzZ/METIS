/*!
\file  stat.c
\brief Functions for printing various statistics for the computed partitionings
       and orderings.

\date   Started 7/25/1997
\author George  
\author Copyright 1997-2009, Regents of the University of Minnesota 
\version\verbatim $Id: stat.c 17513 2014-08-05 16:20:50Z dominique $ \endverbatim
*/



#include "metisbin.h"


/****************************************************************************/
/*! This function computes various information associated with a partition */
/****************************************************************************/
int ComputePartitionInfo(params_t *params, graph_t *graph, idx_t *where)
{
  idx_t i, ii, j, k, nvtxs, ncon, nparts, tvwgt, volume;
  idx_t *xadj, *adjncy, *vwgt, *adjwgt, *kpwgts=NULL;
  real_t *tpwgts, unbalance;
  idx_t pid, ndom, maxndom, minndom, tndom;
  idx_t *pptr=NULL, *pind=NULL, *pdom=NULL;
  idx_t gncmps=0, ncmps, nover=0, *cptr=NULL, *cind=NULL, *cpwgts=NULL;
  size_t nwgt;
  int sigrval;

  if (params == NULL || graph == NULL || where == NULL ||
      graph->nvtxs <= 0 || graph->ncon <= 0 || params->nparts <= 0 ||
      params->nparts > graph->nvtxs) {
    errno = EINVAL;
    return METIS_ERROR_INPUT;
  }
  if ((uintmax_t)graph->ncon > (uintmax_t)SIZE_MAX/(size_t)params->nparts ||
      (uintmax_t)graph->nvtxs >= (uintmax_t)IDX_MAX ||
      (uintmax_t)params->nparts >= (uintmax_t)IDX_MAX ||
      (uintmax_t)graph->nvtxs+1 >
          (uintmax_t)SIZE_MAX/sizeof(idx_t) ||
      (uintmax_t)params->nparts+1 >
          (uintmax_t)SIZE_MAX/sizeof(idx_t)) {
    errno = EOVERFLOW;
    return METIS_ERROR_MEMORY;
  }
  if (graph->xadj == NULL || graph->vwgt == NULL ||
      params->tpwgts == NULL) {
    errno = EINVAL;
    return METIS_ERROR_INPUT;
  }
  if (graph->xadj[graph->nvtxs] > 0 &&
      (graph->adjncy == NULL || graph->adjwgt == NULL)) {
    errno = EINVAL;
    return METIS_ERROR_INPUT;
  }

  nvtxs  = graph->nvtxs;
  ncon   = graph->ncon;
  xadj   = graph->xadj;
  adjncy = graph->adjncy;
  vwgt   = graph->vwgt;
  adjwgt = graph->adjwgt;

  nparts = params->nparts;
  tpwgts = params->tpwgts;
  nwgt = (size_t)ncon*(size_t)nparts;
  for (i=0; i<nvtxs; i++) {
    if (where[i] < 0 || where[i] >= nparts) {
      errno = EINVAL;
      return METIS_ERROR_INPUT;
    }
  }

  /* Compute objective-related information */
  volume = ComputeVolume(graph, where);
  if (volume < 0)
    goto MEMORY_ERROR;


  /* Compute constraint-related information */
  kpwgts = iMallocNoSignal(nwgt,
      "ComputePartitionInfo: kpwgts", &sigrval);
  if (kpwgts == NULL)
    goto MEMORY_ERROR;
  iset(nwgt, 0, kpwgts);

  for (i=0; i<nvtxs; i++) {
    for (j=0; j<ncon; j++) 
      kpwgts[where[i]*ncon+j] += vwgt[i*ncon+j];
  }

  /* Compute subdomain adjacency information */
  pptr = iMallocNoSignal((size_t)nparts+1,
      "ComputePartitionInfo: pptr", &sigrval);
  pind = iMallocNoSignal((size_t)nvtxs,
      "ComputePartitionInfo: pind", &sigrval);
  pdom = iMallocNoSignal((size_t)nparts,
      "ComputePartitionInfo: pdom", &sigrval);
  if (pptr == NULL || pind == NULL || pdom == NULL)
    goto MEMORY_ERROR;

  iarray2csr(nvtxs, nparts, where, pptr, pind);

  maxndom = nparts+1;
  minndom = 0;
  for (tndom=0, pid=0; pid<nparts; pid++) {
    iset(nparts, 0, pdom);
    for (ii=pptr[pid]; ii<pptr[pid+1]; ii++) {
      i = pind[ii];
      for (j=xadj[i]; j<xadj[i+1]; j++)
        pdom[where[adjncy[j]]] += adjwgt[j];
    }
    pdom[pid] = 0;
    for (ndom=0, i=0; i<nparts; i++)
      ndom += (pdom[i] > 0 ? 1 : 0);
    tndom += ndom;
    if (pid == 0 || maxndom < ndom)
      maxndom = ndom;
    if (pid == 0 || minndom > ndom)
      minndom = ndom;
  }

  /* Compute subdomain adjacency information */
  cptr = iMallocNoSignal((size_t)nvtxs+1,
      "ComputePartitionInfo: cptr", &sigrval);
  cind = iMallocNoSignal((size_t)nvtxs,
      "ComputePartitionInfo: cind", &sigrval);
  cpwgts = iMallocNoSignal((size_t)nparts,
      "ComputePartitionInfo: cpwgts", &sigrval);
  if (cptr == NULL || cind == NULL || cpwgts == NULL)
    goto MEMORY_ERROR;
  iset(nparts, 0, cpwgts);

  ncmps = FindPartitionInducedComponents(graph, where, cptr, cind);
  if (ncmps < 0)
    goto MEMORY_ERROR;
  if (ncmps != nparts) {
    gncmps = FindPartitionInducedComponents(graph, NULL, NULL, NULL);
    if (gncmps < 0)
      goto MEMORY_ERROR;
    if (gncmps == 1) {
      for (nover=0, i=0; i<ncmps; i++) {
        cpwgts[where[cind[cptr[i]]]]++;
        if (cpwgts[where[cind[cptr[i]]]] == 2)
          nover++;
      }
    }
  }

  printf(" - Edgecut: %"PRIDX", communication volume: %"PRIDX".\n\n",
      ComputeCut(graph, where), volume);


  /* Report on balance */
  printf(" - Balance:\n");
  for (j=0; j<ncon; j++) {
    tvwgt = isum(nparts, kpwgts+j, ncon);
    for (k=0, unbalance=1.0*kpwgts[k*ncon+j]/(tpwgts[k*ncon+j]*tvwgt), i=1; i<nparts; i++) {
      if (unbalance < 1.0*kpwgts[i*ncon+j]/(tpwgts[i*ncon+j]*tvwgt)) {
        unbalance = 1.0*kpwgts[i*ncon+j]/(tpwgts[i*ncon+j]*tvwgt);
        k = i;
      }
    }
    printf("     constraint #%"PRIDX":  %5.3"PRREAL" out of %5.3"PRREAL"\n", 
        j, unbalance,
         1.0*nparts*vwgt[ncon*iargmax_strd(nvtxs, vwgt+j, ncon)+j]/
            (1.0*isum(nparts, kpwgts+j, ncon)));
  }
  printf("\n");

  if (ncon == 1) {
    tvwgt = isum(nparts, kpwgts, 1);
    for (k=0, unbalance=kpwgts[k]/(tpwgts[k]*tvwgt), i=1; i<nparts; i++) {
      if (unbalance < kpwgts[i]/(tpwgts[i]*tvwgt)) {
        unbalance = kpwgts[i]/(tpwgts[i]*tvwgt);
        k = i;
      }
    }

    printf(" - Most overweight partition:\n"
           "     pid: %"PRIDX", actual: %"PRIDX", desired: %"PRIDX", ratio: %.2"PRREAL".\n\n",
        k, kpwgts[k], (idx_t)(tvwgt*tpwgts[k]), unbalance);
  }

  printf(" - Subdomain connectivity: max: %"PRIDX", min: %"PRIDX", avg: %.2"PRREAL"\n\n",
      maxndom, minndom, 1.0*tndom/nparts);

  if (ncmps == nparts)
    printf(" - Each partition is contiguous.\n");
  else if (gncmps == 1) {
    printf(" - There are %"PRIDX" non-contiguous partitions.\n"
           "   Total components after removing the cut edges: %"PRIDX",\n"
           "   max components: %"PRIDX" for pid: %"PRIDX".\n",
        nover, ncmps, imax(nparts, cpwgts,1), (idx_t)iargmax(nparts, cpwgts,1));
  }
  else {
    printf(" - The original graph had %"PRIDX" connected components and the resulting\n"
           "   partitioning after removing the cut edges has %"PRIDX" components.",
       gncmps, ncmps);
  }

  gk_free((void **)&kpwgts, &pptr, &pind, &pdom,
      &cptr, &cind, &cpwgts, LTERM);

  return METIS_OK;

MEMORY_ERROR:
  gk_free((void **)&kpwgts, &pptr, &pind, &pdom,
      &cptr, &cind, &cpwgts, LTERM);
  if (errno == 0)
    errno = ENOMEM;
  return METIS_ERROR_MEMORY;
}


