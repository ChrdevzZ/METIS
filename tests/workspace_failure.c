/*
 * Copyright 1997-2011, Regents of the University of Minnesota
 *
 * workspace_failure.c
 *
 * Regression checks for transactional workspace growth failures.
 */

#include "metislib.h"


/*************************************************************************/
/*! Verifies that a pool request beyond its configured ceiling fails
    without advancing the cursor or changing the allocation. */
/*************************************************************************/
static int CheckPoolCeiling(void)
{
  cnbr_t cnbrs[1];
  vnbr_t vnbrs[1];
  ctrl_t ctrl;

  memset(&ctrl, 0, sizeof(ctrl));
  ctrl.nparts = 2;
  ctrl.nbrpoolsize_max = 1;
  ctrl.nbrpoolsize = 1;
  ctrl.cnbrpool = cnbrs;
  if (cnbrpoolGetNext(&ctrl, 2) != -1 || ctrl.cnbrpool != cnbrs ||
      ctrl.nbrpoolsize != 1 || ctrl.nbrpoolcpos != 0)
    return 10;

  ctrl.vnbrpool = vnbrs;
  if (vnbrpoolGetNext(&ctrl, 2) != -1 || ctrl.vnbrpool != vnbrs ||
      ctrl.nbrpoolsize != 1 || ctrl.nbrpoolcpos != 0)
    return 11;

  return 0;
}


/*************************************************************************/
/*! Verifies that cursor arithmetic overflow is rejected transactionally. */
/*************************************************************************/
static int CheckPoolPositionOverflow(void)
{
  ctrl_t ctrl;

  memset(&ctrl, 0, sizeof(ctrl));
  ctrl.nparts = 1;
  ctrl.nbrpoolsize_max = SIZE_MAX;
  ctrl.nbrpoolsize = SIZE_MAX;
  ctrl.nbrpoolcpos = SIZE_MAX;
  if (cnbrpoolGetNext(&ctrl, 1) != -1 ||
      ctrl.nbrpoolsize != SIZE_MAX || ctrl.nbrpoolcpos != SIZE_MAX)
    return 20;

  return 0;
}


/*************************************************************************/
/*! Forces a real pool realloc failure and checks every pool state field. */
/*************************************************************************/
static int CheckPoolReallocFailure(void)
{
  cnbr_t *pool, *old_pool;
  ctrl_t ctrl;
  size_t memory_before;
  int status=0;

  memset(&ctrl, 0, sizeof(ctrl));
  if (!gk_malloc_init())
    return 22;
  pool = (cnbr_t *)gk_malloc(sizeof(cnbr_t),
      "CheckPoolReallocFailure: pool");
  if (pool == NULL || !gk_malloc_init())
    return 23;

  old_pool = pool;
  ctrl.nparts = 2;
  ctrl.nbrpoolsize_max = 4;
  ctrl.nbrpoolsize = 1;
  ctrl.nbrpoolcpos = 1;
  ctrl.cnbrpool = pool;
  memory_before = gk_GetCurMemoryUsed();
  if (cnbrpoolGetNext(&ctrl, 1) != -1 || ctrl.cnbrpool != old_pool ||
      ctrl.nbrpoolsize != 1 || ctrl.nbrpoolcpos != 1 ||
      ctrl.nbrpoolreallocs != 0 ||
      gk_GetCurMemoryUsed() != memory_before)
    status = 24;

  gk_malloc_cleanup(0);
  gk_free((void **)&pool, LTERM);
  gk_malloc_cleanup(0);
  return status;
}


/*************************************************************************/
/*! Verifies that a volume update reserves its pool entries before it
    changes the refinement state. */
/*************************************************************************/
static int CheckVolUpdatePoolFailure(void)
{
  ctrl_t ctrl;
  graph_t graph;
  idx_t xadj[] = {0, 1, 2};
  idx_t adjncy[] = {1, 0};
  idx_t vsize[] = {1, 1};
  idx_t where[] = {0, 1};
  idx_t vmarker[] = {0, 0};
  idx_t pmarker[] = {-1, -1};
  idx_t modind[] = {-1, -1};
  vkrinfo_t rinfo[2], oldrinfo;
  vnbr_t pool[1];

  memset(&ctrl, 0, sizeof(ctrl));
  memset(&graph, 0, sizeof(graph));
  memset(rinfo, 0, sizeof(rinfo));
  ctrl.nparts = 2;
  ctrl.status = METIS_OK;
  ctrl.nbrpoolsize_max = 1;
  ctrl.nbrpoolsize = 1;
  ctrl.nbrpoolcpos = 1;
  ctrl.vnbrpool = pool;
  graph.nvtxs = 2;
  graph.xadj = xadj;
  graph.adjncy = adjncy;
  graph.vsize = vsize;
  graph.where = where;
  graph.vkrinfo = rinfo;
  rinfo[0].ned = 1;
  rinfo[0].inbr = 0;
  rinfo[0].nnbrs = 1;
  rinfo[1].ned = 1;
  rinfo[1].inbr = -1;
  pool[0].pid = 1;
  pool[0].ned = 1;
  pool[0].gv = 0;
  oldrinfo = rinfo[0];

  KWayVolUpdate(&ctrl, &graph, 0, 0, 1, NULL, NULL, NULL, NULL, NULL,
      BNDTYPE_REFINE, vmarker, pmarker, modind);
  if (ctrl.status != METIS_ERROR_MEMORY || ctrl.nbrpoolcpos != 1 ||
      rinfo[1].inbr != -1 || memcmp(&rinfo[0], &oldrinfo, sizeof(oldrinfo)) ||
      vmarker[0] != 0 || vmarker[1] != 0 ||
      pmarker[0] != -1 || pmarker[1] != -1)
    return 25;

  return 0;
}


/*************************************************************************/
/*! Verifies that FixGraph rejects malformed offsets before traversing them. */
/*************************************************************************/
static int CheckFixGraphInput(void)
{
  graph_t graph, *fixed;
  idx_t xadj[] = {0, 2, 1};
  idx_t large_xadj[] = {0, IDX_MAX};
  idx_t adjncy[] = {1, 0};
  idx_t adjwgt[] = {1, 1};

  errno = 0;
  if (FixGraph(NULL) != NULL || errno != EINVAL)
    return 26;

  memset(&graph, 0, sizeof(graph));
  graph.nvtxs = 2;
  graph.ncon = 1;
  graph.xadj = xadj;
  graph.adjncy = adjncy;
  graph.adjwgt = adjwgt;
  errno = 0;
  fixed = FixGraph(&graph);
  if (fixed != NULL || errno != EINVAL)
    return 27;

  if ((uintmax_t)IDX_MAX > (uintmax_t)SIZE_MAX/sizeof(uvw_t)) {
    memset(&graph, 0, sizeof(graph));
    graph.nvtxs = 1;
    graph.ncon = 1;
    graph.xadj = large_xadj;
    graph.adjncy = adjncy;
    graph.adjwgt = adjwgt;
    errno = 0;
    fixed = FixGraph(&graph);
    if (fixed != NULL || errno != EOVERFLOW)
      return 28;
  }

  return 0;
}


/*************************************************************************/
/*! Verifies that typed workspace helpers reject negative element counts
    before they can reach a null or otherwise invalid memory core. */
/*************************************************************************/
static int CheckTypedWorkspaceSize(void)
{
  ctrl_t ctrl;

  memset(&ctrl, 0, sizeof(ctrl));
  if (iwspacemalloc(&ctrl, -1) != NULL ||
      rwspacemalloc(&ctrl, -1) != NULL ||
      ikvwspacemalloc(&ctrl, -1) != NULL)
    return 25;

  return 0;
}


/*************************************************************************/
/*! Places graph arrays outside the active tracker frame so realloc must
    fail, then verifies that the original allocations remain owned. */
/*************************************************************************/
static int CheckCoarseGraphReallocFailure(void)
{
  graph_t cgraph, graph;
  idx_t *adjncy, *adjwgt;
  size_t memory_before;
  int status=0;

  memset(&cgraph, 0, sizeof(cgraph));
  memset(&graph, 0, sizeof(graph));
  if (!gk_malloc_init())
    return 30;
  adjncy = imalloc(10001, "CheckCoarseGraphReallocFailure: adjncy");
  adjwgt = imalloc(1, "CheckCoarseGraphReallocFailure: adjwgt");
  if (adjncy == NULL || adjwgt == NULL || !gk_malloc_init())
    return 31;

  graph.nedges = 20000;
  cgraph.nedges = 10001;
  cgraph.adjncy = adjncy;
  cgraph.adjwgt = adjwgt;
  adjncy[0] = 17;
  adjncy[10000] = 23;
  memory_before = gk_GetCurMemoryUsed();
  ReAdjustMemory(NULL, &graph, &cgraph);
  if (cgraph.adjncy != adjncy || cgraph.adjwgt != adjwgt ||
      adjncy[0] != 17 || adjncy[10000] != 23 ||
      gk_GetCurMemoryUsed() != memory_before)
    status = 32;

  gk_malloc_cleanup(0);
  gk_free((void **)&adjncy, &adjwgt, LTERM);
  gk_malloc_cleanup(0);
  return status;
}


/*************************************************************************/
/*! Forces sparse subdomain arrays to reject cross-frame reallocations and
    checks that the paired pointers and capacity remain unchanged. */
/*************************************************************************/
static int CheckSubdomainReallocFailure(void)
{
  ctrl_t ctrl;
  idx_t *adids[2], *adwgts[2], maxnads[2], nads[2];
  idx_t *old_adids, *old_adwgts;
  size_t memory_before;
  int status=0;

  memset(&ctrl, 0, sizeof(ctrl));
  if (!gk_malloc_init())
    return 40;
  adids[0] = imalloc(1, "CheckSubdomainReallocFailure: adids[0]");
  adids[1] = imalloc(1, "CheckSubdomainReallocFailure: adids[1]");
  adwgts[0] = imalloc(1, "CheckSubdomainReallocFailure: adwgts[0]");
  adwgts[1] = imalloc(1, "CheckSubdomainReallocFailure: adwgts[1]");
  if (adids[0] == NULL || adids[1] == NULL || adwgts[0] == NULL ||
      adwgts[1] == NULL || !gk_malloc_init())
    return 41;

  adids[0][0] = 0;
  adids[1][0] = 1;
  adwgts[0][0] = adwgts[1][0] = 1;
  maxnads[0] = maxnads[1] = 1;
  nads[0] = nads[1] = 1;
  old_adids = adids[0];
  old_adwgts = adwgts[0];
  ctrl.nparts = 2;
  ctrl.status = METIS_OK;
  ctrl.adids = adids;
  ctrl.adwgts = adwgts;
  ctrl.maxnads = maxnads;
  ctrl.nads = nads;

  memory_before = gk_GetCurMemoryUsed();
  UpdateEdgeSubDomainGraph(&ctrl, 0, 1, 1, NULL);
  if (adids[0] != old_adids || adwgts[0] != old_adwgts ||
      maxnads[0] != 1 || nads[0] != 1 ||
      gk_GetCurMemoryUsed() != memory_before ||
      ctrl.status != METIS_ERROR_MEMORY)
    status = 42;

  gk_malloc_cleanup(0);
  gk_free((void **)&adids[0], &adids[1], &adwgts[0], &adwgts[1], LTERM);
  gk_malloc_cleanup(0);
  return status;
}


/*************************************************************************/
/*! Repeats paired growth in signal mode and verifies pre-signal cleanup. */
/*************************************************************************/
static int CheckSubdomainSignalFailure(void)
{
  ctrl_t ctrl;
  idx_t *adids[2], *adwgts[2], maxnads[2], nads[2];
  idx_t *old_adids, *old_adwgts;
  size_t memory_before;
  volatile int sigrval=0;
  int status=0;

  memset(&ctrl, 0, sizeof(ctrl));
  if (!gk_malloc_init())
    return 50;
  adids[0] = imalloc(1, "CheckSubdomainSignalFailure: adids[0]");
  adids[1] = imalloc(1, "CheckSubdomainSignalFailure: adids[1]");
  adwgts[0] = imalloc(1, "CheckSubdomainSignalFailure: adwgts[0]");
  adwgts[1] = imalloc(1, "CheckSubdomainSignalFailure: adwgts[1]");
  if (adids[0] == NULL || adids[1] == NULL || adwgts[0] == NULL ||
      adwgts[1] == NULL || !gk_malloc_init() || !gk_sigtrap())
    return 51;

  adids[0][0] = 0;
  adids[1][0] = 1;
  adwgts[0][0] = adwgts[1][0] = 1;
  maxnads[0] = maxnads[1] = 1;
  nads[0] = nads[1] = 1;
  old_adids = adids[0];
  old_adwgts = adwgts[0];
  ctrl.nparts = 2;
  ctrl.adids = adids;
  ctrl.adwgts = adwgts;
  ctrl.maxnads = maxnads;
  ctrl.nads = nads;
  memory_before = gk_GetCurMemoryUsed();

  METIS_SIGCATCH(sigrval);
  if (sigrval == 0) {
    UpdateEdgeSubDomainGraph(&ctrl, 0, 1, 1, NULL);
    status = 52;
  }
  else if (sigrval != SIGMEM) {
    status = 53;
  }

  if (!gk_siguntrap() || adids[0] != old_adids ||
      adwgts[0] != old_adwgts || maxnads[0] != 1 || nads[0] != 1 ||
      gk_GetCurMemoryUsed() != memory_before)
    status = 54;
  gk_malloc_cleanup(0);
  gk_free((void **)&adids[0], &adids[1], &adwgts[0], &adwgts[1], LTERM);
  gk_malloc_cleanup(0);
  return status;
}


/*************************************************************************/
/*! Verifies that a failed workspace marker stops its caller and that a
    pop without a marker leaves the allocation records unchanged. */
/*************************************************************************/
static int CheckWorkspaceMarkFailure(void)
{
  ctrl_t ctrl;
  idx_t keys[] = {0};
  idx_t perm[] = {17};
  idx_t tperm[] = {23};
  size_t cmop, nmops;
  int status=0;

  memset(&ctrl, 0, sizeof(ctrl));
  ctrl.status = METIS_OK;
  ctrl.mcore = gk_mcoreCreate(64);
  if (ctrl.mcore == NULL)
    return 60;

  ctrl.mcore->mops[0].type = GK_MOPT_CORE;
  ctrl.mcore->mops[0].nbytes = 0;
  ctrl.mcore->mops[0].ptr = NULL;
  ctrl.mcore->cmop = 1;
  cmop = ctrl.mcore->cmop;
  wspacepop(&ctrl);
  if (ctrl.status != METIS_ERROR_MEMORY || ctrl.mcore->cmop != cmop) {
    status = 62;
    goto DONE;
  }

  ctrl.status = METIS_OK;
  nmops = ctrl.mcore->nmops;
  ctrl.mcore->nmops = SIZE_MAX/2+1;
  ctrl.mcore->cmop = ctrl.mcore->nmops;
  BucketSortKeysInc(&ctrl, 1, 0, keys, tperm, perm);
  if (ctrl.status != METIS_ERROR_MEMORY ||
      ctrl.mcore->cmop != ctrl.mcore->nmops ||
      perm[0] != 17 || tperm[0] != 23)
    status = 63;
  ctrl.mcore->nmops = nmops;
  ctrl.mcore->cmop = cmop;

DONE:
  ctrl.mcore->cmop = 0;
  gk_mcoreDestroy(&ctrl.mcore, 0);
  return status;
}


/*************************************************************************/
/*! Forces workspace payload allocation failures after a valid marker and
    verifies that callers leave their output and frame state unchanged. */
/*************************************************************************/
static int CheckWorkspacePayloadFailure(void)
{
  ctrl_t ctrl;
  graph_t graph;
  idx_t adjncy[] = {1, 0};
  idx_t bfsperm[] = {1, 0};
  idx_t keys[] = {0};
  idx_t perm[] = {17};
  idx_t tperm[] = {0};
  idx_t xadj[] = {0, 1, 2};
  size_t num_hallocs;
  int status=0;

  memset(&ctrl, 0, sizeof(ctrl));
  memset(&graph, 0, sizeof(graph));
  ctrl.status = METIS_OK;
  ctrl.mcore = gk_mcoreCreate(0);
  if (ctrl.mcore == NULL)
    return 70;

  num_hallocs = ctrl.mcore->num_hallocs;
  ctrl.mcore->num_hallocs = SIZE_MAX;
  BucketSortKeysInc(&ctrl, 1, 0, keys, tperm, perm);
  if (ctrl.status != METIS_ERROR_MEMORY || ctrl.mcore->cmop != 0 ||
      perm[0] != 17) {
    status = 71;
    goto DONE;
  }

  ctrl.status = METIS_OK;
  graph.nvtxs = 2;
  graph.xadj = xadj;
  graph.adjncy = adjncy;
  ComputeBFSOrdering(&ctrl, &graph, bfsperm);
  if (ctrl.status != METIS_ERROR_MEMORY || ctrl.mcore->cmop != 0 ||
      bfsperm[0] != 1 || bfsperm[1] != 0)
    status = 72;

DONE:
  ctrl.mcore->num_hallocs = num_hallocs;
  gk_mcoreDestroy(&ctrl.mcore, 0);
  return status;
}


int main(void)
{
  int status;

  gk_set_exit_on_error(0);
  status = CheckPoolCeiling();
  if (status == 0)
    status = CheckPoolPositionOverflow();
  if (status == 0)
    status = CheckPoolReallocFailure();
  if (status == 0)
    status = CheckVolUpdatePoolFailure();
  if (status == 0)
    status = CheckFixGraphInput();
  if (status == 0)
    status = CheckTypedWorkspaceSize();
  if (status == 0)
    status = CheckCoarseGraphReallocFailure();
  if (status == 0)
    status = CheckSubdomainReallocFailure();
  if (status == 0)
    status = CheckWorkspaceMarkFailure();
  if (status == 0)
    status = CheckWorkspacePayloadFailure();
  if (status == 0) {
    gk_set_exit_on_error(1);
    status = CheckSubdomainSignalFailure();
  }
  gk_set_exit_on_error(1);

  return status;
}
