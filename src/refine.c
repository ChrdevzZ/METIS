/*
\file
\brief This file contains the driving routines for multilevel refinement

\date   Started 7/24/1997
\author George  
\author Copyright 1997-2009, Regents of the University of Minnesota 
\version\verbatim $Id: refine.c 14362 2013-05-21 21:35:23Z karypis $ \endverbatim
*/

#include "metislib.h"


/*************************************************************************/
/*! This function is the entry point of refinement */
/*************************************************************************/
void Refine2Way(ctrl_t *ctrl, graph_t *orggraph, graph_t *graph, real_t *tpwgts)
{

  IFSET(ctrl->dbglvl, METIS_DBG_TIME, gk_startcputimer(ctrl->UncoarsenTmr));

  /* Compute the parameters of the coarsest graph */
  Compute2WayPartitionParams(ctrl, graph);

  for (;;) {
    ASSERT(CheckBnd(graph));

    IFSET(ctrl->dbglvl, METIS_DBG_TIME, gk_startcputimer(ctrl->RefTmr));

    Balance2Way(ctrl, graph, tpwgts);

    FM_2WayRefine(ctrl, graph, tpwgts, ctrl->niter); 

    IFSET(ctrl->dbglvl, METIS_DBG_TIME, gk_stopcputimer(ctrl->RefTmr));

    if (graph == orggraph)
      break;

    graph = graph->finer;
    graph_ReadFromDisk(ctrl, graph);

    IFSET(ctrl->dbglvl, METIS_DBG_TIME, gk_startcputimer(ctrl->ProjectTmr));
    Project2WayPartition(ctrl, graph);
    IFSET(ctrl->dbglvl, METIS_DBG_TIME, gk_stopcputimer(ctrl->ProjectTmr));
    if (ctrl->status != METIS_OK)
      break;
  }

  IFSET(ctrl->dbglvl, METIS_DBG_TIME, gk_stopcputimer(ctrl->UncoarsenTmr));
}


/*************************************************************************/
/*! This function allocates memory for 2-way edge refinement */
/*************************************************************************/
int Allocate2WayPartitionMemory(ctrl_t *ctrl, graph_t *graph)
{
  volatile int sigrval=0;
  idx_t *cleanup_pwgts, *cleanup_where, *cleanup_bndptr, *cleanup_bndind;
  idx_t *cleanup_id, *cleanup_ed;
  idx_t * volatile pwgts=NULL, * volatile where=NULL;
  idx_t * volatile bndptr=NULL, * volatile bndind=NULL;
  idx_t * volatile id=NULL, * volatile ed=NULL;
  idx_t nvtxs, ncon;

  if (ctrl == NULL || graph == NULL) {
    errno = EINVAL;
    if (ctrl != NULL)
      ctrl->status = METIS_ERROR_INPUT;
    return METIS_ERROR_INPUT;
  }

  nvtxs = graph->nvtxs;
  ncon  = graph->ncon;

  if (ncon <= 0 || nvtxs < 0) {
    errno = EINVAL;
    ctrl->status = METIS_ERROR_INPUT;
    return METIS_ERROR_INPUT;
  }
  if (ncon > IDX_MAX/2 ||
      (uintmax_t)2*ncon > (uintmax_t)SIZE_MAX/sizeof(idx_t) ||
      (uintmax_t)nvtxs > (uintmax_t)SIZE_MAX/sizeof(idx_t)) {
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

  pwgts  = imalloc(2*ncon, "Allocate2WayPartitionMemory: pwgts");
  where  = imalloc(nvtxs, "Allocate2WayPartitionMemory: where");
  bndptr = imalloc(nvtxs, "Allocate2WayPartitionMemory: bndptr");
  bndind = imalloc(nvtxs, "Allocate2WayPartitionMemory: bndind");
  id     = imalloc(nvtxs, "Allocate2WayPartitionMemory: id");
  ed     = imalloc(nvtxs, "Allocate2WayPartitionMemory: ed");
  if (pwgts == NULL || where == NULL || bndptr == NULL || bndind == NULL ||
      id == NULL || ed == NULL)
    goto ALLOCATION_ERROR;

  gk_siguntrap();
  gk_free((void **)&graph->pwgts, &graph->where, &graph->bndptr,
      &graph->bndind, &graph->id, &graph->ed, LTERM);
  graph->pwgts  = (idx_t *)pwgts;
  graph->where  = (idx_t *)where;
  graph->bndptr = (idx_t *)bndptr;
  graph->bndind = (idx_t *)bndind;
  graph->id     = (idx_t *)id;
  graph->ed     = (idx_t *)ed;
  return METIS_OK;

ALLOCATION_ERROR:
  cleanup_pwgts = (idx_t *)pwgts;
  cleanup_where = (idx_t *)where;
  cleanup_bndptr = (idx_t *)bndptr;
  cleanup_bndind = (idx_t *)bndind;
  cleanup_id = (idx_t *)id;
  cleanup_ed = (idx_t *)ed;
  gk_free((void **)&cleanup_pwgts, &cleanup_where, &cleanup_bndptr,
      &cleanup_bndind, &cleanup_id, &cleanup_ed, LTERM);
  gk_siguntrap();

MEMORY_ERROR:
  ctrl->status = METIS_ERROR_MEMORY;
  if (errno == 0)
    errno = ENOMEM;
  return METIS_ERROR_MEMORY;
}


/*************************************************************************/
/*! This function computes the initial id/ed */
/*************************************************************************/
void Compute2WayPartitionParams(ctrl_t *ctrl, graph_t *graph)
{
  idx_t i, j, nvtxs, ncon, nbnd, mincut, istart, iend, tid, ted, me;
  idx_t *xadj, *vwgt, *adjncy, *adjwgt, *pwgts;
  idx_t *where, *bndptr, *bndind, *id, *ed;

  nvtxs  = graph->nvtxs;
  ncon   = graph->ncon;
  xadj   = graph->xadj;
  vwgt   = graph->vwgt;
  adjncy = graph->adjncy;
  adjwgt = graph->adjwgt;

  where  = graph->where;
  id     = graph->id;
  ed     = graph->ed;

  pwgts  = iset(2*ncon, 0, graph->pwgts);
  bndptr = iset(nvtxs, -1, graph->bndptr);
  bndind = graph->bndind;

  /* Compute pwgts */
  if (ncon == 1) {
    for (i=0; i<nvtxs; i++) {
      ASSERT(where[i] >= 0 && where[i] <= 1);
      pwgts[where[i]] += vwgt[i];
    }
    ASSERT(pwgts[0]+pwgts[1] == graph->tvwgt[0]);
  }
  else {
    for (i=0; i<nvtxs; i++) {
      me = where[i];
      for (j=0; j<ncon; j++)
        pwgts[me*ncon+j] += vwgt[i*ncon+j];
    }
  }


  /* Compute the required info for refinement  */
  for (nbnd=0, mincut=0, i=0; i<nvtxs; i++) {
    istart = xadj[i];
    iend   = xadj[i+1];

    me = where[i];
    tid = ted = 0;

    for (j=istart; j<iend; j++) {
      if (me == where[adjncy[j]])
        tid += adjwgt[j];
      else
        ted += adjwgt[j];
    }
    id[i] = tid;
    ed[i] = ted;
  
    if (ted > 0 || istart == iend) {
      BNDInsert(nbnd, bndind, bndptr, i);
      mincut += ted;
    }
  }

  graph->mincut = mincut/2;
  graph->nbnd   = nbnd;

}


/*************************************************************************/
/*! Projects a partition and computes the refinement params. */
/*************************************************************************/
void Project2WayPartition(ctrl_t *ctrl, graph_t *graph)
{
  idx_t i, j, istart, iend, nvtxs, nbnd, me, tid, ted;
  idx_t *xadj, *adjncy, *adjwgt;
  idx_t *cmap, *where, *bndptr, *bndind;
  idx_t *cwhere, *cbndptr;
  idx_t *id, *ed;
  graph_t *cgraph;
  int dropedges;

  if (Allocate2WayPartitionMemory(ctrl, graph) != METIS_OK)
    return;

  dropedges = ctrl->dropedges;

  cgraph  = graph->coarser;
  cwhere  = cgraph->where;
  cbndptr = cgraph->bndptr;

  nvtxs   = graph->nvtxs;
  cmap    = graph->cmap;
  xadj    = graph->xadj;
  adjncy  = graph->adjncy;
  adjwgt  = graph->adjwgt;

  where  = graph->where;
  id     = graph->id;
  ed     = graph->ed;

  bndptr = iset(nvtxs, -1, graph->bndptr);
  bndind = graph->bndind;

  /* Project the partition and record which of these nodes came from the
     coarser boundary */
  for (i=0; i<nvtxs; i++) {
    j = cmap[i];
    where[i] = cwhere[j];
    cmap[i]  = (dropedges ? 0 : cbndptr[j]);
  }

  /* Compute the refinement information of the nodes */
  for (nbnd=0, i=0; i<nvtxs; i++) {
    istart = xadj[i];
    iend   = xadj[i+1];
  
    tid = ted = 0;
    if (cmap[i] == -1) { /* Interior node. Note that cmap[i] = cbndptr[cmap[i]] */
      for (j=istart; j<iend; j++)
        tid += adjwgt[j];
    }
    else { /* Potentially an interface node */
      me = where[i];
      for (j=istart; j<iend; j++) {
        if (me == where[adjncy[j]])
          tid += adjwgt[j];
        else
          ted += adjwgt[j];
      }
    }
    id[i] = tid;
    ed[i] = ted;

    if (ted > 0 || istart == iend) 
      BNDInsert(nbnd, bndind, bndptr, i);
  }
  graph->mincut = (dropedges ? ComputeCut(graph, where) : cgraph->mincut);
  graph->nbnd   = nbnd;

  /* copy pwgts */
  icopy(2*graph->ncon, cgraph->pwgts, graph->pwgts);

  FreeGraph(&graph->coarser);
  graph->coarser = NULL;
}

