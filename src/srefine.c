/*
 * Copyright 1997, Regents of the University of Minnesota
 *
 * srefine.c
 *
 * This file contains code for the separator refinement algorithms
 *
 * Started 8/1/97
 * George
 *
 * $Id: srefine.c 14362 2013-05-21 21:35:23Z karypis $
 *
 */

#include "metislib.h"


/*************************************************************************/
/*! This function is the entry point of the separator refinement. 
    It does not perform any refinement on graph, but it starts by first
    projecting it to the next level finer graph and proceeds from there. */
/*************************************************************************/
void Refine2WayNode(ctrl_t *ctrl, graph_t *orggraph, graph_t *graph)
{

  IFSET(ctrl->dbglvl, METIS_DBG_TIME, gk_startcputimer(ctrl->UncoarsenTmr));

  if (graph == orggraph) {
    Compute2WayNodePartitionParams(ctrl, graph);
  }
  else {
    do {
      graph = graph->finer;

      graph_ReadFromDisk(ctrl, graph);

      IFSET(ctrl->dbglvl, METIS_DBG_TIME, gk_startcputimer(ctrl->ProjectTmr));
      Project2WayNodePartition(ctrl, graph);
      IFSET(ctrl->dbglvl, METIS_DBG_TIME, gk_stopcputimer(ctrl->ProjectTmr));
      if (ctrl->status != METIS_OK)
        break;

      IFSET(ctrl->dbglvl, METIS_DBG_TIME, gk_startcputimer(ctrl->RefTmr));
      FM_2WayNodeBalance(ctrl, graph); 

      ASSERT(CheckNodePartitionParams(graph));

      switch (ctrl->rtype) {
        case METIS_RTYPE_SEP2SIDED:
          FM_2WayNodeRefine2Sided(ctrl, graph, ctrl->niter); 
          break;
        case METIS_RTYPE_SEP1SIDED:
          FM_2WayNodeRefine1Sided(ctrl, graph, ctrl->niter); 
          break;
        default:
          gk_errexit(SIGERR, "Unknown rtype of %d\n", ctrl->rtype);
      }
      IFSET(ctrl->dbglvl, METIS_DBG_TIME, gk_stopcputimer(ctrl->RefTmr));

    } while (graph != orggraph);
  }

  IFSET(ctrl->dbglvl, METIS_DBG_TIME, gk_stopcputimer(ctrl->UncoarsenTmr));
}


/*************************************************************************/
/*! This function allocates memory for 2-way node-based refinement */
/**************************************************************************/
int Allocate2WayNodePartitionMemory(ctrl_t *ctrl, graph_t *graph)
{
  volatile int sigrval=0;
  idx_t *cleanup_pwgts, *cleanup_where, *cleanup_bndptr, *cleanup_bndind;
  idx_t * volatile pwgts=NULL, * volatile where=NULL;
  idx_t * volatile bndptr=NULL, * volatile bndind=NULL;
  nrinfo_t *cleanup_nrinfo;
  nrinfo_t * volatile nrinfo=NULL;
  idx_t nvtxs;

  if (ctrl == NULL || graph == NULL) {
    errno = EINVAL;
    if (ctrl != NULL)
      ctrl->status = METIS_ERROR_INPUT;
    return METIS_ERROR_INPUT;
  }

  nvtxs = graph->nvtxs;

  if (nvtxs < 0) {
    errno = EINVAL;
    ctrl->status = METIS_ERROR_INPUT;
    return METIS_ERROR_INPUT;
  }
  if ((uintmax_t)nvtxs > (uintmax_t)SIZE_MAX/sizeof(idx_t) ||
      (uintmax_t)nvtxs > (uintmax_t)SIZE_MAX/sizeof(nrinfo_t)) {
    errno = EOVERFLOW;
    goto MEMORY_ERROR;
  }

  if (!gk_sigtrap()) {
    errno = ENOMEM;
    goto MEMORY_ERROR;
  }
  METIS_SIGCATCH(sigrval);
  if (sigrval != 0)
    goto ALLOCATION_ERROR;

  pwgts  = imalloc(3, "Allocate2WayNodePartitionMemory: pwgts");
  where  = imalloc(nvtxs, "Allocate2WayNodePartitionMemory: where");
  bndptr = imalloc(nvtxs, "Allocate2WayNodePartitionMemory: bndptr");
  bndind = imalloc(nvtxs, "Allocate2WayNodePartitionMemory: bndind");
  nrinfo = (nrinfo_t *)gk_malloc((size_t)nvtxs*sizeof(nrinfo_t),
      "Allocate2WayNodePartitionMemory: nrinfo");
  if (pwgts == NULL || where == NULL || bndptr == NULL || bndind == NULL ||
      nrinfo == NULL)
    goto ALLOCATION_ERROR;

  gk_siguntrap();
  gk_free((void **)&graph->pwgts, &graph->where, &graph->bndptr,
      &graph->bndind, &graph->nrinfo, LTERM);
  graph->pwgts  = (idx_t *)pwgts;
  graph->where  = (idx_t *)where;
  graph->bndptr = (idx_t *)bndptr;
  graph->bndind = (idx_t *)bndind;
  graph->nrinfo = (nrinfo_t *)nrinfo;
  return METIS_OK;

ALLOCATION_ERROR:
  cleanup_pwgts = (idx_t *)pwgts;
  cleanup_where = (idx_t *)where;
  cleanup_bndptr = (idx_t *)bndptr;
  cleanup_bndind = (idx_t *)bndind;
  cleanup_nrinfo = (nrinfo_t *)nrinfo;
  gk_free((void **)&cleanup_pwgts, &cleanup_where, &cleanup_bndptr,
      &cleanup_bndind, &cleanup_nrinfo, LTERM);
  gk_siguntrap();

MEMORY_ERROR:
  ctrl->status = METIS_ERROR_MEMORY;
  if (errno == 0)
    errno = ENOMEM;
  return METIS_ERROR_MEMORY;
}


/*************************************************************************/
/*! This function computes the edegrees[] to the left & right sides */
/*************************************************************************/
void Compute2WayNodePartitionParams(ctrl_t *ctrl, graph_t *graph)
{
  idx_t i, j, nvtxs, nbnd;
  idx_t *xadj, *adjncy, *vwgt;
  idx_t *where, *pwgts, *bndind, *bndptr, *edegrees;
  nrinfo_t *rinfo;
  idx_t me, other;

  nvtxs  = graph->nvtxs;
  xadj   = graph->xadj;
  vwgt   = graph->vwgt;
  adjncy = graph->adjncy;

  where  = graph->where;
  rinfo  = graph->nrinfo;
  pwgts  = iset(3, 0, graph->pwgts);
  bndind = graph->bndind;
  bndptr = iset(nvtxs, -1, graph->bndptr);


  /*------------------------------------------------------------
  / Compute now the separator external degrees
  /------------------------------------------------------------*/
  nbnd = 0;
  for (i=0; i<nvtxs; i++) {
    me = where[i];
    pwgts[me] += vwgt[i];

    ASSERT(me >=0 && me <= 2);

    if (me == 2) { /* If it is on the separator do some computations */
      BNDInsert(nbnd, bndind, bndptr, i);

      edegrees = rinfo[i].edegrees;
      edegrees[0] = edegrees[1] = 0;

      for (j=xadj[i]; j<xadj[i+1]; j++) {
        other = where[adjncy[j]];
        if (other != 2)
          edegrees[other] += vwgt[adjncy[j]];
      }
    }
  }

  ASSERT(CheckNodeBnd(graph, nbnd));

  graph->mincut = pwgts[2];
  graph->nbnd   = nbnd;
}


/*************************************************************************/
/*! This function projects the node-based bisection */
/*************************************************************************/
void Project2WayNodePartition(ctrl_t *ctrl, graph_t *graph)
{
  idx_t i, j, nvtxs;
  idx_t *cmap, *where, *cwhere;
  graph_t *cgraph;

  cgraph = graph->coarser;
  cwhere = cgraph->where;

  nvtxs = graph->nvtxs;
  cmap  = graph->cmap;

  if (Allocate2WayNodePartitionMemory(ctrl, graph) != METIS_OK)
    return;
  where = graph->where;
  
  /* Project the partition */
  for (i=0; i<nvtxs; i++) {
    where[i] = cwhere[cmap[i]];
    ASSERTP(where[i] >= 0 && where[i] <= 2, ("%"PRIDX" %"PRIDX" %"PRIDX" %"PRIDX"\n", 
          i, cmap[i], where[i], cwhere[cmap[i]]));
  }

  FreeGraph(&graph->coarser);
  graph->coarser = NULL;

  Compute2WayNodePartitionParams(ctrl, graph);
}
