/*
 * Copyright 1997-2011, Regents of the University of Minnesota
 *
 * coarsen_numeric.c
 *
 * Regression checks for coarsening and partitioning numeric boundaries.
 */

#include "metislib.h"


/*************************************************************************/
/*! Verifies that compression hashes use defined modular arithmetic. */
/*************************************************************************/
static int CheckCompressionKeyOverflow(void)
{
#if IDXTYPEWIDTH == 32
  ctrl_t ctrl;
  graph_t *graph=NULL;
  idx_t i, nvtxs=65537, nedges=2*(nvtxs-1);
  idx_t *xadj, *adjncy, *cptr, *cind;
  int status=0;

  xadj = (idx_t *)malloc((size_t)(nvtxs+1)*sizeof(idx_t));
  adjncy = (idx_t *)malloc((size_t)nedges*sizeof(idx_t));
  cptr = (idx_t *)malloc((size_t)(nvtxs+1)*sizeof(idx_t));
  cind = (idx_t *)malloc((size_t)nvtxs*sizeof(idx_t));
  if (xadj == NULL || adjncy == NULL || cptr == NULL || cind == NULL) {
    status = 10;
    goto DONE;
  }

  xadj[0] = 0;
  xadj[1] = nvtxs-1;
  for (i=1; i<nvtxs; i++) {
    adjncy[i-1] = i;
    adjncy[nvtxs-2+i] = 0;
    xadj[i+1] = nvtxs-1+i;
  }

  memset(&ctrl, 0, sizeof(ctrl));
  ctrl.status = METIS_OK;
  graph = CompressGraph(&ctrl, nvtxs, xadj, adjncy, NULL, cptr, cind);
  if (graph != NULL || ctrl.status != METIS_OK)
    status = 11;

DONE:
  FreeGraph(&graph);
  free(cind);
  free(cptr);
  free(adjncy);
  free(xadj);
  return status;
#else
  return 0;
#endif
}


/*************************************************************************/
/*! Verifies that drop-edge keys retain the full edge-weight range. */
/*************************************************************************/
static int CheckDropEdgeKeyOverflow(void)
{
  ctrl_t ctrl;
  graph_t graph;
  graph_t *cgraph;
  idx_t xadj[] = {0, 1, 2}, adjncy[] = {1, 0};
  idx_t vwgt[] = {1, 1}, adjwgt[] = {IDX_MAX/3, IDX_MAX/3};
  idx_t cmap[] = {0, 1}, match[] = {0, 1};
  int status, result=0;

  memset(&ctrl, 0, sizeof(ctrl));
  InitGraph(&graph);
  ctrl.status = METIS_OK;
  ctrl.objtype = METIS_OBJTYPE_CUT;
  ctrl.dropedges = 1;
  ctrl.mcore = gk_mcoreCreate(65536);
  if (ctrl.mcore == NULL)
    return 20;

  graph.nvtxs = 2;
  graph.nedges = 2;
  graph.ncon = 1;
  graph.xadj = xadj;
  graph.adjncy = adjncy;
  graph.vwgt = vwgt;
  graph.adjwgt = adjwgt;
  graph.cmap = cmap;

  status = CreateCoarseGraph(&ctrl, &graph, 2, match);
  cgraph = graph.coarser;
  if (status != METIS_OK || ctrl.status != METIS_OK || cgraph == NULL ||
      cgraph->nvtxs != 2 || cgraph->nedges != 2 ||
      cgraph->xadj[0] != 0 || cgraph->xadj[1] != 1 ||
      cgraph->xadj[2] != 2 || cgraph->adjncy[0] != 1 ||
      cgraph->adjncy[1] != 0 || cgraph->adjwgt[0] != IDX_MAX/3 ||
      cgraph->adjwgt[1] != IDX_MAX/3 || cgraph->droppedewgt != 0)
    result = 21;

  FreeGraph(&graph.coarser);
  gk_mcoreDestroy(&ctrl.mcore, 0);
  return result;
}


/*************************************************************************/
/*! Verifies the existing drop-edge ordering for ordinary edge weights. */
/*************************************************************************/
static int CheckDropEdgeKeyOrdering(void)
{
  ctrl_t ctrl;
  graph_t graph;
  graph_t *cgraph;
  idx_t xadj[] = {0, 3, 6, 9, 12, 12, 12, 12, 12};
  idx_t adjncy[] = {1, 2, 3, 0, 2, 3, 0, 1, 3, 0, 1, 2};
  idx_t adjwgt[] = {1, 2, 3, 1, 4, 5, 2, 4, 6, 3, 5, 6};
  idx_t vwgt[] = {1, 1, 1, 1, 1, 1, 1, 1};
  idx_t cmap[] = {0, 1, 2, 3, 0, 1, 2, 3};
  idx_t match[] = {4, 5, 6, 7, 0, 1, 2, 3};
  idx_t expected_xadj[] = {0, 2, 4, 7, 10};
  idx_t expected_adjncy[] = {3, 2, 3, 2, 3, 0, 1, 2, 0, 1};
  idx_t expected_adjwgt[] = {3, 2, 5, 4, 6, 2, 4, 6, 3, 5};
  int status, result=0;

  memset(&ctrl, 0, sizeof(ctrl));
  InitGraph(&graph);
  ctrl.status = METIS_OK;
  ctrl.objtype = METIS_OBJTYPE_CUT;
  ctrl.dropedges = 1;
  ctrl.mcore = gk_mcoreCreate(65536);
  if (ctrl.mcore == NULL)
    return 30;

  graph.nvtxs = 8;
  graph.nedges = 12;
  graph.ncon = 1;
  graph.xadj = xadj;
  graph.adjncy = adjncy;
  graph.vwgt = vwgt;
  graph.adjwgt = adjwgt;
  graph.cmap = cmap;

  InitRandom(1);
  status = CreateCoarseGraph(&ctrl, &graph, 4, match);
  cgraph = graph.coarser;
  if (status != METIS_OK || ctrl.status != METIS_OK || cgraph == NULL ||
      cgraph->nvtxs != 4 || cgraph->nedges != 10)
    result = 31;
  if (result == 0 &&
      (memcmp(cgraph->xadj, expected_xadj, sizeof(expected_xadj)) != 0 ||
       memcmp(cgraph->adjncy, expected_adjncy,
           sizeof(expected_adjncy)) != 0 ||
       memcmp(cgraph->adjwgt, expected_adjwgt,
           sizeof(expected_adjwgt)) != 0 || cgraph->droppedewgt != 2))
    result = 32;

  FreeGraph(&graph.coarser);
  gk_mcoreDestroy(&ctrl.mcore, 0);
  return result;
}


/*************************************************************************/
/*! Verifies matching comparisons at the vertex-weight limit. */
/*************************************************************************/
static int CheckMatchingWeightOverflow(void)
{
  ctrl_t ctrl;
  graph_t graph;
  idx_t xadj[] = {0, 1, 2}, adjncy[] = {1, 0};
  idx_t adjwgt[] = {1, 1}, vwgt[] = {IDX_MAX/2+1, 2};
  idx_t cmap[2], maxvwgt[] = {IDX_MAX/2+2};
  int i, result;

  for (i=0; i<2; i++) {
    memset(&ctrl, 0, sizeof(ctrl));
    InitGraph(&graph);
    ctrl.status = METIS_OK;
    ctrl.objtype = METIS_OBJTYPE_CUT;
    ctrl.no2hop = 1;
    ctrl.maxvwgt = maxvwgt;
    ctrl.mcore = gk_mcoreCreate(65536);
    if (ctrl.mcore == NULL)
      return 40;

    graph.nvtxs = 2;
    graph.nedges = 2;
    graph.ncon = 1;
    graph.xadj = xadj;
    graph.adjncy = adjncy;
    graph.vwgt = vwgt;
    graph.adjwgt = adjwgt;
    graph.cmap = cmap;

    InitRandom(1);
    result = i == 0 ? Match_RM(&ctrl, &graph) : Match_SHEM(&ctrl, &graph);
    if (result != 2 || ctrl.status != METIS_OK || graph.coarser == NULL ||
        graph.coarser->nvtxs != 2 ||
        graph.coarser->vwgt[0] != IDX_MAX/2+1 ||
        graph.coarser->vwgt[1] != 2) {
      FreeGraph(&graph.coarser);
      gk_mcoreDestroy(&ctrl.mcore, 0);
      return 41+i;
    }

    FreeGraph(&graph.coarser);
    gk_mcoreDestroy(&ctrl.mcore, 0);
  }

  return 0;
}


/*************************************************************************/
/*! Verifies that multi-section seeds are sampled without replacement. */
/*************************************************************************/
static int CheckMultisectionSeeds(void)
{
  ctrl_t ctrl;
  graph_t graph;
  idx_t xadj[] = {0, 1, 4, 5, 6};
  idx_t adjncy[] = {1, 0, 2, 3, 1, 1};
  idx_t vwgt[] = {1, 1, 1, 1}, where[4], counts[4];
  idx_t i, nparts;

  memset(&ctrl, 0, sizeof(ctrl));
  InitGraph(&graph);
  ctrl.status = METIS_OK;
  ctrl.mcore = gk_mcoreCreate(65536);
  if (ctrl.mcore == NULL)
    return 50;

  graph.nvtxs = 4;
  graph.nedges = 6;
  graph.ncon = 1;
  graph.xadj = xadj;
  graph.adjncy = adjncy;
  graph.vwgt = vwgt;

  InitRandom(1);
  nparts = GrowMultisection(&ctrl, &graph, 4, where);
  iset(4, 0, counts);
  for (i=0; i<4; i++) {
    if (where[i] < 0 || where[i] >= 4)
      break;
    counts[where[i]]++;
  }
  if (nparts != 4 || ctrl.status != METIS_OK || i != 4 ||
      counts[0] != 1 || counts[1] != 1 ||
      counts[2] != 1 || counts[3] != 1) {
    gk_mcoreDestroy(&ctrl.mcore, 0);
    return 51;
  }

  gk_mcoreDestroy(&ctrl.mcore, 0);
  return 0;
}


/*************************************************************************/
/*! Verifies exact vector comparisons outside the idx_t arithmetic range. */
/*************************************************************************/
static int CheckVectorComparisons(void)
{
  idx_t x, y, z;

  x = IDX_MAX/2+1;
  y = IDX_MAX/2+1;
  z = IDX_MAX;
  if (ivecaxpylez(1, 2, &x, &y, &z))
    return 60;
  if (!ivecaxpygez(1, 2, &x, &y, &z))
    return 61;

  x = IDX_MAX;
  y = -IDX_MAX-1;
  z = 0;
  if (!ivecaxpylez(1, -1, &x, &y, &z))
    return 62;
  if (ivecaxpygez(1, -1, &x, &y, &z))
    return 63;

  x = IDX_MAX;
  y = IDX_MAX;
  z = IDX_MAX;
  if (ivecaxpylez(1, IDX_MAX, &x, &y, &z))
    return 64;
  if (!ivecaxpygez(1, IDX_MAX, &x, &y, &z))
    return 65;

  return 0;
}


/*************************************************************************/
/*! Verifies multi-constraint initial partition counts at IDX_MAX. */
/*************************************************************************/
static int CheckInitialPartitionCountOverflow(void)
{
  ctrl_t *ctrl;
  graph_t *graph;
  idx_t xadj[] = {0, 0, 0}, vwgt[] = {1, 1, 1, 1};
  idx_t i, method, options[METIS_NOPTIONS];
  int result, status;

  for (method=0; method<2; method++) {
    ctrl = NULL;
    graph = NULL;
    result = 0;
    if (!gk_malloc_init())
      return 70;
    METIS_SetDefaultOptions(options);
    status = SetupCtrl(METIS_OP_PMETIS, options, 2, 2, NULL, NULL, &ctrl);
    if (status != METIS_OK) {
      result = 71;
      goto DONE;
    }
    status = SetupGraph(ctrl, 2, 2, xadj, NULL, vwgt, NULL, NULL, &graph);
    if (status != METIS_OK) {
      result = 72;
      goto DONE;
    }
    Setup2WayBalMultipliers(ctrl, graph, ctrl->tpwgts);
    if (AllocateWorkSpace(ctrl, graph) != METIS_OK) {
      result = 73;
      goto DONE;
    }

    if (method == 0)
      McRandomBisection(ctrl, graph, ctrl->tpwgts, IDX_MAX);
    else
      McGrowBisection(ctrl, graph, ctrl->tpwgts, IDX_MAX);
    if (ctrl->status != METIS_OK || graph->where == NULL) {
      result = 74+method;
      goto DONE;
    }
    for (i=0; i<2; i++) {
      if (graph->where[i] < 0 || graph->where[i] > 1) {
        result = 76+method;
        break;
      }
    }

DONE:
    FreeGraph(&graph);
    FreeCtrl(&ctrl);
    gk_malloc_cleanup(0);
    if (result != 0)
      return result;
  }

  return 0;
}


int main(void)
{
  int status;

  status = CheckCompressionKeyOverflow();
  if (status != 0)
    return status;
  status = CheckDropEdgeKeyOverflow();
  if (status != 0)
    return status;
  status = CheckDropEdgeKeyOrdering();
  if (status != 0)
    return status;
  status = CheckMatchingWeightOverflow();
  if (status != 0)
    return status;
  status = CheckMultisectionSeeds();
  if (status != 0)
    return status;
  status = CheckVectorComparisons();
  if (status != 0)
    return status;
  status = CheckInitialPartitionCountOverflow();
  if (status != 0)
    return status;

  return 0;
}
