/*
 * Copyright 1997-2011, Regents of the University of Minnesota
 *
 * constructor_failure.c
 *
 * Allocation-by-allocation signal recovery checks for private constructors.
 */

#include "metislib.h"


static size_t allocation_count;
static size_t fail_at=SIZE_MAX;
static int fail_nosignal;
static const char *fail_message;
static int count_mallocs, in_gk_malloc;
static void *mallocs[16];
static size_t fail_malloc_at=SIZE_MAX, malloc_count, nmallocs;
static size_t fail_mcore_at=SIZE_MAX, mcore_count;


void *__real_gk_malloc(size_t nbytes, const char *msg);
void *__real_gk_malloc_nosignal(size_t nbytes);
void *__real_gk_mcoreMalloc(gk_mcore_t *mcore, size_t nbytes);
void *__real_malloc(size_t nbytes);
void __real_free(void *ptr);


/*************************************************************************/
/*! Records allocations made directly by the mesh graph constructors. */
/*************************************************************************/
void *__wrap_malloc(size_t nbytes)
{
  void *ptr;

  if (count_mallocs && !in_gk_malloc && ++malloc_count == fail_malloc_at) {
    errno = ENOMEM;
    return NULL;
  }
  ptr = __real_malloc(nbytes);
  if (count_mallocs && !in_gk_malloc && ptr != NULL && nmallocs < 16)
    mallocs[nmallocs++] = ptr;
  return ptr;
}


/*************************************************************************/
/*! Removes direct mesh allocations from the outstanding set. */
/*************************************************************************/
void __wrap_free(void *ptr)
{
  size_t i;

  if (count_mallocs && ptr != NULL) {
    for (i=0; i<nmallocs; i++) {
      if (mallocs[i] == ptr) {
        mallocs[i] = mallocs[--nmallocs];
        break;
      }
    }
  }
  __real_free(ptr);
}


/*************************************************************************/
/*! Injects one reported allocation failure for the GNU linker fixture. */
/*************************************************************************/
void *__wrap_gk_malloc(size_t nbytes, const char *msg)
{
  void *ptr;

  if (!fail_nosignal) {
    allocation_count++;
    if (allocation_count == fail_at ||
        (fail_message != NULL && strcmp(msg, fail_message) == 0)) {
      errno = ENOMEM;
      gk_errexit(SIGMEM, "Injected reported allocation failure");
      errno = ENOMEM;
      return NULL;
    }
  }
  in_gk_malloc++;
  ptr = __real_gk_malloc(nbytes, msg);
  in_gk_malloc--;
  return ptr;
}


/*************************************************************************/
/*! Injects one no-signal allocation failure for the GNU linker fixture. */
/*************************************************************************/
void *__wrap_gk_malloc_nosignal(size_t nbytes)
{
  if (fail_nosignal) {
    allocation_count++;
    if (allocation_count == fail_at) {
      errno = ENOMEM;
      return NULL;
    }
  }
  return __real_gk_malloc_nosignal(nbytes);
}


/*************************************************************************/
/*! Injects one workspace allocation failure for the GNU linker fixture. */
/*************************************************************************/
void *__wrap_gk_mcoreMalloc(gk_mcore_t *mcore, size_t nbytes)
{
  if (fail_mcore_at != SIZE_MAX && ++mcore_count == fail_mcore_at) {
    errno = ENOMEM;
    gk_errexit(SIGMEM, "Injected workspace allocation failure");
    errno = ENOMEM;
    return NULL;
  }
  return __real_gk_mcoreMalloc(mcore, nbytes);
}


/*************************************************************************/
/*! Selects the next allocator call to fail. */
/*************************************************************************/
static void FailAllocation(size_t allocation, int nosignal)
{
  allocation_count = 0;
  fail_at = allocation;
  fail_nosignal = nosignal;
  fail_message = NULL;
}


/*************************************************************************/
/*! Selects the next workspace allocation to fail. */
/*************************************************************************/
static void FailMcoreAllocation(size_t allocation)
{
  fail_mcore_at = allocation;
  mcore_count = 0;
}


/*************************************************************************/
/*! Selects an allocation diagnostic to fail. */
/*************************************************************************/
static void FailMessage(const char *message)
{
  allocation_count = 0;
  fail_at = SIZE_MAX;
  fail_nosignal = 0;
  fail_message = message;
}


/*************************************************************************/
/*! Disables allocation failure without resetting the observed count. */
/*************************************************************************/
static void AllowAllocations(void)
{
  fail_at = SIZE_MAX;
  fail_nosignal = 0;
  fail_message = NULL;
  fail_mcore_at = SIZE_MAX;
}


/*************************************************************************/
/*! Starts recording direct allocations made outside gk_malloc. */
/*************************************************************************/
static void StartMallocCount(void)
{
  fail_malloc_at = SIZE_MAX;
  malloc_count = 0;
  nmallocs = 0;
  in_gk_malloc = 0;
  count_mallocs = 1;
}


/*************************************************************************/
/*! Selects a direct mesh allocation to fail. */
/*************************************************************************/
static void FailMalloc(size_t allocation)
{
  fail_malloc_at = allocation;
}


/*************************************************************************/
/*! Stops direct-allocation recording and releases leaked test storage. */
/*************************************************************************/
static size_t StopMallocCount(void)
{
  size_t count=nmallocs;

  count_mallocs = 0;
  fail_malloc_at = SIZE_MAX;
  while (nmallocs > 0)
    __real_free(mallocs[--nmallocs]);
  in_gk_malloc = 0;
  return count;
}


/*************************************************************************/
/*! Checks every allocation made while constructing a control object. */
/*************************************************************************/
static int CheckSetupCtrlFailures(void)
{
  ctrl_t *ctrl;
  size_t baseline, fail;
  int status;

  if (!gk_malloc_init())
    return 10;
  baseline = gk_GetCurMemoryUsed();
  for (fail=1; fail<=5; fail++) {
    ctrl = (ctrl_t *)(uintptr_t)1;
    FailAllocation(fail, 0);
    status = SetupCtrl(METIS_OP_KMETIS, NULL, 1, 2, NULL, NULL, &ctrl);
    AllowAllocations();
    if (status != METIS_ERROR_MEMORY || ctrl != NULL ||
        gk_GetCurMemoryUsed() != baseline) {
      gk_malloc_cleanup(0);
      return 11+(int)fail;
    }
  }

  ctrl = NULL;
  FailAllocation(6, 0);
  status = SetupCtrl(METIS_OP_KMETIS, NULL, 1, 2, NULL, NULL, &ctrl);
  AllowAllocations();
  if (status != METIS_OK || ctrl == NULL || allocation_count != 5) {
    FreeCtrl(&ctrl);
    gk_malloc_cleanup(0);
    return 17;
  }
  FreeCtrl(&ctrl);
  if (gk_GetCurMemoryUsed() != baseline) {
    gk_malloc_cleanup(0);
    return 18;
  }
  gk_malloc_cleanup(0);
  return 0;
}


/*************************************************************************/
/*! Checks staged graph construction at each of its allocation sites. */
/*************************************************************************/
static int CheckSetupGraphFailures(void)
{
  idx_t xadj[] = {0, 1, 2}, adjncy[] = {1, 0};
  ctrl_t *ctrl=NULL;
  graph_t *graph;
  size_t baseline, fail, nallocs;
  int status;

  if (!gk_malloc_init() ||
      SetupCtrl(METIS_OP_PMETIS, NULL, 1, 2, NULL, NULL, &ctrl) !=
          METIS_OK) {
    gk_malloc_cleanup(0);
    return 20;
  }
  graph = (graph_t *)(uintptr_t)1;
  status = SetupGraph(NULL, 2, 1, xadj, adjncy, NULL, NULL, NULL,
      &graph);
  if (status != METIS_ERROR_INPUT || graph != NULL) {
    FreeCtrl(&ctrl);
    gk_malloc_cleanup(0);
    return 21;
  }
  baseline = gk_GetCurMemoryUsed();

  graph = NULL;
  FailAllocation(SIZE_MAX, 0);
  status = SetupGraph(ctrl, 2, 1, xadj, adjncy, NULL, NULL, NULL,
      &graph);
  nallocs = allocation_count;
  AllowAllocations();
  if (status != METIS_OK || graph == NULL || nallocs == 0) {
    FreeGraph(&graph);
    FreeCtrl(&ctrl);
    gk_malloc_cleanup(0);
    return 22;
  }
  FreeGraph(&graph);
  if (gk_GetCurMemoryUsed() != baseline) {
    FreeCtrl(&ctrl);
    gk_malloc_cleanup(0);
    return 23;
  }

  for (fail=1; fail<=nallocs; fail++) {
    graph = (graph_t *)(uintptr_t)1;
    FailAllocation(fail, 0);
    status = SetupGraph(ctrl, 2, 1, xadj, adjncy, NULL, NULL, NULL,
        &graph);
    AllowAllocations();
    if (status != METIS_ERROR_MEMORY || graph != NULL ||
        xadj[0] != 0 || xadj[1] != 1 || xadj[2] != 2 ||
        adjncy[0] != 1 || adjncy[1] != 0 ||
        gk_GetCurMemoryUsed() != baseline) {
      FreeCtrl(&ctrl);
      gk_malloc_cleanup(0);
      return 20+(int)fail;
    }
  }

#if GKLIB_ASSERTIONS_ENABLED
  gk_set_exit_on_error(0);
  graph = (graph_t *)(uintptr_t)1;
  FailMessage("htable");
  status = SetupGraph(ctrl, 2, 1, xadj, adjncy, NULL, NULL, NULL,
      &graph);
  AllowAllocations();
  gk_set_exit_on_error(1);
  if (status != METIS_ERROR_MEMORY || graph != NULL ||
      gk_GetCurMemoryUsed() != baseline) {
    FreeGraph(&graph);
    FreeCtrl(&ctrl);
    gk_malloc_cleanup(0);
    return 27;
  }
#endif

  graph = NULL;
  FailAllocation(nallocs+1, 0);
  status = SetupGraph(ctrl, 2, 1, xadj, adjncy, NULL, NULL, NULL,
      &graph);
  AllowAllocations();
  if (status != METIS_OK || graph == NULL || allocation_count != nallocs) {
    FreeGraph(&graph);
    FreeCtrl(&ctrl);
    gk_malloc_cleanup(0);
    return 29;
  }
  FreeGraph(&graph);
  FreeCtrl(&ctrl);
  if (gk_GetCurMemoryUsed() != 0) {
    gk_malloc_cleanup(0);
    return 28;
  }
  gk_malloc_cleanup(0);
  return 0;
}


/*************************************************************************/
/*! Checks mesh constructor cleanup and one-based input restoration. */
/*************************************************************************/
static int CheckMeshFailures(void)
{
  static const char *dual_messages[] = {
    "CreateGraphDual: nptr", "CreateGraphDual: nind",
    "CreateGraphDual: marker", "CreateGraphDual: nbrs"
  };
  static const char *nodal_messages[] = {
    "CreateGraphNodal: nptr", "CreateGraphNodal: nind",
    "CreateGraphNodal: marker", "CreateGraphNodal: nbrs"
  };
  idx_t ne=2, nn=3, ncommon=1, numflag=1;
  idx_t eptr[3], eind[4];
  idx_t *xadj, *adjncy;
  size_t baseline, i, outstanding;
  int status;

  if (!gk_malloc_init())
    return 60;
  baseline = gk_GetCurMemoryUsed();

  for (i=0; i<4; i++) {
    eptr[0] = 1; eptr[1] = 3; eptr[2] = 5;
    eind[0] = 1; eind[1] = 2; eind[2] = 2; eind[3] = 3;
    xadj = adjncy = (idx_t *)(uintptr_t)1;
    StartMallocCount();
    FailMessage(dual_messages[i]);
    status = METIS_MeshToDual(&ne, &nn, eptr, eind, &ncommon, &numflag,
        &xadj, &adjncy);
    AllowAllocations();
    outstanding = StopMallocCount();
    if (status != METIS_ERROR_MEMORY || xadj != NULL || adjncy != NULL ||
        outstanding != 0 || eptr[0] != 1 || eptr[1] != 3 || eptr[2] != 5 ||
        eind[0] != 1 || eind[1] != 2 || eind[2] != 2 || eind[3] != 3 ||
        gk_GetCurMemoryUsed() != baseline) {
      gk_malloc_cleanup(0);
      return 61+(int)i;
    }
  }

  for (i=0; i<4; i++) {
    eptr[0] = 1; eptr[1] = 3; eptr[2] = 5;
    eind[0] = 1; eind[1] = 2; eind[2] = 2; eind[3] = 3;
    xadj = adjncy = (idx_t *)(uintptr_t)1;
    StartMallocCount();
    FailMessage(nodal_messages[i]);
    status = METIS_MeshToNodal(&ne, &nn, eptr, eind, &numflag,
        &xadj, &adjncy);
    AllowAllocations();
    outstanding = StopMallocCount();
    if (status != METIS_ERROR_MEMORY || xadj != NULL || adjncy != NULL ||
        outstanding != 0 || eptr[0] != 1 || eptr[1] != 3 || eptr[2] != 5 ||
        eind[0] != 1 || eind[1] != 2 || eind[2] != 2 || eind[3] != 3 ||
        gk_GetCurMemoryUsed() != baseline) {
      gk_malloc_cleanup(0);
      return 65+(int)i;
    }
  }

  for (i=1; i<=2; i++) {
    eptr[0] = 1; eptr[1] = 3; eptr[2] = 5;
    eind[0] = 1; eind[1] = 2; eind[2] = 2; eind[3] = 3;
    xadj = adjncy = (idx_t *)(uintptr_t)1;
    StartMallocCount();
    FailMalloc(i);
    errno = 0;
    status = METIS_MeshToDual(&ne, &nn, eptr, eind, &ncommon, &numflag,
        &xadj, &adjncy);
    outstanding = StopMallocCount();
    if (status != METIS_ERROR_MEMORY || errno != ENOMEM ||
        xadj != NULL || adjncy != NULL || outstanding != 0 ||
        eptr[0] != 1 || eptr[1] != 3 || eptr[2] != 5 ||
        eind[0] != 1 || eind[1] != 2 || eind[2] != 2 || eind[3] != 3 ||
        gk_GetCurMemoryUsed() != baseline) {
      gk_malloc_cleanup(0);
      return 69+(int)i;
    }
  }

  for (i=1; i<=2; i++) {
    eptr[0] = 1; eptr[1] = 3; eptr[2] = 5;
    eind[0] = 1; eind[1] = 2; eind[2] = 2; eind[3] = 3;
    xadj = adjncy = (idx_t *)(uintptr_t)1;
    StartMallocCount();
    FailMalloc(i);
    errno = 0;
    status = METIS_MeshToNodal(&ne, &nn, eptr, eind, &numflag,
        &xadj, &adjncy);
    outstanding = StopMallocCount();
    if (status != METIS_ERROR_MEMORY || errno != ENOMEM ||
        xadj != NULL || adjncy != NULL || outstanding != 0 ||
        eptr[0] != 1 || eptr[1] != 3 || eptr[2] != 5 ||
        eind[0] != 1 || eind[1] != 2 || eind[2] != 2 || eind[3] != 3 ||
        gk_GetCurMemoryUsed() != baseline) {
      gk_malloc_cleanup(0);
      return 72+(int)i;
    }
  }

  gk_malloc_cleanup(0);
  return 0;
}


/*************************************************************************/
/*! Checks mesh partitioning cleanup at each post-partition allocation. */
/*************************************************************************/
static int CheckMeshPartitionFailures(void)
{
  static const char *dual_messages[] = {
    "METIS_PartMeshDual: epart", "METIS_PartMeshDual: nptr",
    "METIS_PartMeshDual: nind",
    "InduceRowPartFromColumnPart: pwgts",
    "InduceRowPartFromColumnPart: nbrdom",
    "InduceRowPartFromColumnPart: nbrwgt",
    "InduceRowPartFromColumnPart: nbrmrk",
    "InduceRowPartFromColumnPart: itpwgts"
  };
  static const char *nodal_messages[] = {
    "METIS_PartMeshNodal: npart",
    "InduceRowPartFromColumnPart: pwgts",
    "InduceRowPartFromColumnPart: nbrdom",
    "InduceRowPartFromColumnPart: nbrwgt",
    "InduceRowPartFromColumnPart: nbrmrk",
    "InduceRowPartFromColumnPart: itpwgts"
  };
  idx_t ne=2, nn=3, ncommon=1, nparts=2, objval;
  idx_t eptr[3], eind[4], epart[2], npart[3];
  idx_t options[METIS_NOPTIONS];
  size_t i;
  int status;

  gk_set_exit_on_error(0);
  for (i=0; i<sizeof(dual_messages)/sizeof(dual_messages[0]); i++) {
    eptr[0] = 1; eptr[1] = 3; eptr[2] = 5;
    eind[0] = 1; eind[1] = 2; eind[2] = 2; eind[3] = 3;
    iset(2, -9, epart);
    iset(3, -9, npart);
    objval = -9;
    METIS_SetDefaultOptions(options);
    options[METIS_OPTION_NUMBERING] = 1;
    FailMessage(dual_messages[i]);
    status = METIS_PartMeshDual(&ne, &nn, eptr, eind, NULL, NULL,
        &ncommon, &nparts, NULL, options, &objval, epart, npart);
    AllowAllocations();
    if (status != METIS_ERROR_MEMORY || objval != -9 ||
        options[METIS_OPTION_NUMBERING] != 1 ||
        eptr[0] != 1 || eptr[1] != 3 || eptr[2] != 5 ||
        eind[0] != 1 || eind[1] != 2 || eind[2] != 2 || eind[3] != 3 ||
        epart[0] != -9 || epart[1] != -9 ||
        npart[0] != -9 || npart[1] != -9 || npart[2] != -9 ||
        gk_GetCurMemoryUsed() != 0)
      return 80+(int)i;
  }

  for (i=0; i<sizeof(nodal_messages)/sizeof(nodal_messages[0]); i++) {
    eptr[0] = 1; eptr[1] = 3; eptr[2] = 5;
    eind[0] = 1; eind[1] = 2; eind[2] = 2; eind[3] = 3;
    iset(2, -9, epart);
    iset(3, -9, npart);
    objval = -9;
    METIS_SetDefaultOptions(options);
    options[METIS_OPTION_NUMBERING] = 1;
    FailMessage(nodal_messages[i]);
    status = METIS_PartMeshNodal(&ne, &nn, eptr, eind, NULL, NULL,
        &nparts, NULL, options, &objval, epart, npart);
    AllowAllocations();
    if (status != METIS_ERROR_MEMORY || objval != -9 ||
        options[METIS_OPTION_NUMBERING] != 1 ||
        eptr[0] != 1 || eptr[1] != 3 || eptr[2] != 5 ||
        eind[0] != 1 || eind[1] != 2 || eind[2] != 2 || eind[3] != 3 ||
        epart[0] != -9 || epart[1] != -9 ||
        npart[0] != -9 || npart[1] != -9 || npart[2] != -9 ||
        gk_GetCurMemoryUsed() != 0)
      return 87+(int)i;
  }
  gk_set_exit_on_error(1);

  return 0;
}


/*************************************************************************/
/*! Checks matching-sort workspace failure before its permutation is used. */
/*************************************************************************/
static int CheckMatchingSortFailures(void)
{
  ctrl_t ctrl;
  graph_t graph;
  idx_t xadj[] = {0, 2, 4, 6, 8};
  idx_t adjncy[] = {1, 3, 0, 2, 1, 3, 0, 2};
  idx_t adjwgt[] = {1, 2, 1, 3, 3, 4, 2, 4};
  idx_t vwgt[] = {1, 1, 1, 1}, cmap[4], maxvwgt[] = {3};
  idx_t result;
  size_t baseline;
  int i;

  if (!gk_malloc_init())
    return 90;
  memset(&ctrl, 0, sizeof(ctrl));
  InitGraph(&graph);
  ctrl.mcore = gk_mcoreCreate(1024);
  ctrl.maxvwgt = maxvwgt;
  graph.nvtxs = 4;
  graph.nedges = 8;
  graph.ncon = 1;
  graph.xadj = xadj;
  graph.adjncy = adjncy;
  graph.adjwgt = adjwgt;
  graph.vwgt = vwgt;
  graph.cmap = cmap;
  if (ctrl.mcore == NULL) {
    gk_malloc_cleanup(0);
    return 91;
  }
  baseline = gk_GetCurMemoryUsed();

  gk_set_exit_on_error(0);
  for (i=0; i<3; i++) {
    ctrl.status = METIS_OK;
    graph.coarser = NULL;
    errno = 0;
    FailMcoreAllocation(5);
    if (i == 0)
      result = Match_RM(&ctrl, &graph);
    else if (i == 1)
      result = Match_SHEM(&ctrl, &graph);
    else
      result = Match_JC(&ctrl, &graph);
    AllowAllocations();
    if (result != -1 || ctrl.status != METIS_ERROR_MEMORY ||
        errno != ENOMEM || mcore_count != 5 || graph.coarser != NULL ||
        ctrl.mcore->cmop != 0 || ctrl.mcore->corecpos != 0 ||
        gk_GetCurMemoryUsed() != baseline) {
      gk_mcoreDestroy(&ctrl.mcore, 0);
      gk_malloc_cleanup(0);
      return 92+i;
    }
  }
  gk_set_exit_on_error(1);
  gk_mcoreDestroy(&ctrl.mcore, 0);
  if (gk_GetCurMemoryUsed() != 0) {
    gk_malloc_cleanup(0);
    return 95;
  }
  gk_malloc_cleanup(0);
  return 0;
}


/*************************************************************************/
/*! Checks minimum degree workspace bounds and public failure recovery. */
/*************************************************************************/
static int CheckMMDOrderFailures(void)
{
  ctrl_t ctrl;
  graph_t graph;
  idx_t xadj[] = {13}, adjncy[] = {17}, order[] = {19};
  idx_t nvtxs=2, fxadj[] = {1, 2, 3}, fadjncy[] = {2, 1};
  idx_t perm[] = {23, 29}, iperm[] = {31, 37};
  idx_t options[METIS_NOPTIONS];
  int status;

  memset(&ctrl, 0, sizeof(ctrl));
  InitGraph(&graph);
  ctrl.status = METIS_OK;
  graph.nvtxs = IDX_MAX-4;
  graph.xadj = xadj;
  graph.adjncy = adjncy;
  errno = 0;
  MMDOrder(&ctrl, &graph, order, IDX_MAX);
  if (ctrl.status != METIS_ERROR_MEMORY || errno != EOVERFLOW ||
      xadj[0] != 13 || adjncy[0] != 17 || order[0] != 19)
    return 140;

  if (METIS_SetDefaultOptions(options) != METIS_OK)
    return 141;
  options[METIS_OPTION_NUMBERING] = 1;
  FailMcoreAllocation(1);
  status = METIS_NodeND(&nvtxs, fxadj, fadjncy, NULL, options, perm, iperm);
  AllowAllocations();
  if (status != METIS_ERROR_MEMORY || mcore_count != 1 ||
      fxadj[0] != 1 || fxadj[1] != 2 || fxadj[2] != 3 ||
      fadjncy[0] != 2 || fadjncy[1] != 1 ||
      perm[0] != 23 || perm[1] != 29 ||
      iperm[0] != 31 || iperm[1] != 37 || gk_GetCurMemoryUsed() != 0)
    return 142;

  return 0;
}


/*************************************************************************/
/*! Checks that coarsening failure reaches the public return status. */
/*************************************************************************/
static int CheckCoarsenFailure(void)
{
  idx_t nvtxs=4, ncon=1, nparts=2, objval=-1;
  idx_t xadj[] = {1, 3, 5, 7, 9};
  idx_t adjncy[] = {2, 4, 1, 3, 2, 4, 1, 3};
  idx_t adjwgt[] = {1, 2, 1, 3, 3, 4, 2, 4};
  idx_t part[] = {-1, -1, -1, -1};
  idx_t options[METIS_NOPTIONS];
  int i, status;

  METIS_SetDefaultOptions(options);
  options[METIS_OPTION_NUMBERING] = 1;
  FailMessage("CoarsenGraph: graph->cmap");
  status = METIS_PartGraphKway(&nvtxs, &ncon, xadj, adjncy, NULL, NULL,
      NULL, &nparts, NULL, NULL, options, &objval, part);
  AllowAllocations();
  if (status != METIS_ERROR_MEMORY ||
      xadj[0] != 1 || xadj[1] != 3 || xadj[2] != 5 ||
      xadj[3] != 7 || xadj[4] != 9 ||
      adjncy[0] != 2 || adjncy[1] != 4 || adjncy[2] != 1 ||
      adjncy[3] != 3 || adjncy[4] != 2 || adjncy[5] != 4 ||
      adjncy[6] != 1 || adjncy[7] != 3)
    return 70;

  gk_set_exit_on_error(0);
  for (i=0; i<2; i++) {
    METIS_SetDefaultOptions(options);
    options[METIS_OPTION_NUMBERING] = 1;
    options[METIS_OPTION_CTYPE] = i == 0 ? METIS_CTYPE_RM : METIS_CTYPE_SHEM;
    FailMcoreAllocation(5);
    status = METIS_PartGraphKway(&nvtxs, &ncon, xadj, adjncy, NULL, NULL,
        i == 0 ? NULL : adjwgt, &nparts, NULL, NULL, options, &objval, part);
    AllowAllocations();
    if (status != METIS_ERROR_MEMORY || mcore_count != 5 ||
        xadj[0] != 1 || xadj[1] != 3 || xadj[2] != 5 ||
        xadj[3] != 7 || xadj[4] != 9 ||
        adjncy[0] != 2 || adjncy[1] != 4 || adjncy[2] != 1 ||
        adjncy[3] != 3 || adjncy[4] != 2 || adjncy[5] != 4 ||
        adjncy[6] != 1 || adjncy[7] != 3) {
      gk_set_exit_on_error(1);
      return 71+i;
    }
  }
  gk_set_exit_on_error(1);

  return 0;
}


/*************************************************************************/
/*! Checks coarse graph size boundaries before allocation or linking. */
/*************************************************************************/
static int CheckCoarseGraphSizeFailures(void)
{
  graph_t graph;
  graph_t *cgraph;
  size_t i;
  struct {
    idx_t cnvtxs, nedges, ncon;
  } cases[] = {
    {-1, 0, 1},
    {1, -1, 1},
    {1, 0, 0}
  };

  memset(&graph, 0, sizeof(graph));
  graph.coarser = &graph;
  FailAllocation(SIZE_MAX, 0);
  errno = 0;
  cgraph = SetupCoarseGraph(NULL, 1, 0);
  if (cgraph != NULL || errno != EINVAL || allocation_count != 0)
    return 71;

  for (i=0; i<sizeof(cases)/sizeof(cases[0]); i++) {
    graph.nedges = cases[i].nedges;
    graph.ncon = cases[i].ncon;
    errno = 0;
    cgraph = SetupCoarseGraph(&graph, cases[i].cnvtxs, 0);
    if (cgraph != NULL || errno != EINVAL ||
        allocation_count != 0 || graph.coarser != &graph)
      return 72+(int)i;
  }

  graph.nedges = 0;
  graph.ncon = IDX_MAX;
  errno = 0;
  cgraph = SetupCoarseGraph(&graph, IDX_MAX, 0);
  if (cgraph != NULL || errno != EOVERFLOW ||
      allocation_count != 0 || graph.coarser != &graph)
    return 75;

  if ((uintmax_t)(SIZE_MAX/sizeof(idx_t)) < (uintmax_t)IDX_MAX) {
    graph.nedges = (idx_t)(SIZE_MAX/sizeof(idx_t));
    graph.ncon = 1;
    errno = 0;
    cgraph = SetupCoarseGraph(&graph, 1, 0);
    if (cgraph != NULL || errno != EOVERFLOW ||
        allocation_count != 0 || graph.coarser != &graph)
      return 76;

    graph.nedges = 0;
    errno = 0;
    cgraph = SetupCoarseGraph(&graph,
        (idx_t)(SIZE_MAX/sizeof(idx_t)), 0);
    if (cgraph != NULL || errno != EOVERFLOW ||
        allocation_count != 0 || graph.coarser != &graph)
      return 77;
  }

  if ((uintmax_t)IDX_MAX > (uintmax_t)SIZE_MAX) {
    graph.nedges = 0;
    graph.ncon = 1;
    errno = 0;
    cgraph = SetupCoarseGraph(&graph, (idx_t)SIZE_MAX, 0);
    if (cgraph != NULL || errno != EOVERFLOW ||
        allocation_count != 0 || graph.coarser != &graph)
      return 78;
  }
  AllowAllocations();
  return 0;
}


/*************************************************************************/
/*! Verifies that failed workspace replacement retains the old mcore. */
/*************************************************************************/
static int CheckWorkspaceFailures(void)
{
  ctrl_t ctrl;
  graph_t graph;
  gk_mcore_t *old_mcore;
  size_t baseline, fail;
  int status;

  memset(&ctrl, 0, sizeof(ctrl));
  memset(&graph, 0, sizeof(graph));
  ctrl.ncon = 1;
  ctrl.nparts = 2;
  graph.nvtxs = 2;
  if (!gk_malloc_init())
    return 30;
  old_mcore = ctrl.mcore = gk_mcoreCreate(64);
  if (old_mcore == NULL) {
    gk_malloc_cleanup(0);
    return 31;
  }
  baseline = gk_GetCurMemoryUsed();

  for (fail=1; fail<=3; fail++) {
    FailAllocation(fail, 1);
    status = AllocateWorkSpace(&ctrl, &graph);
    AllowAllocations();
    if (status != METIS_ERROR_MEMORY || ctrl.mcore != old_mcore ||
        gk_GetCurMemoryUsed() != baseline) {
      FreeWorkSpace(&ctrl);
      gk_malloc_cleanup(0);
      return 31+(int)fail;
    }
  }

  FailAllocation(4, 1);
  status = AllocateWorkSpace(&ctrl, &graph);
  AllowAllocations();
  if (status != METIS_OK || ctrl.mcore == NULL || ctrl.mcore == old_mcore ||
      allocation_count != 3) {
    FreeWorkSpace(&ctrl);
    gk_malloc_cleanup(0);
    return 35;
  }
  FreeWorkSpace(&ctrl);
  if (gk_GetCurMemoryUsed() != 0) {
    gk_malloc_cleanup(0);
    return 36;
  }
  gk_malloc_cleanup(0);
  return 0;
}


/*************************************************************************/
/*! Verifies that refinement allocation commits only after full success. */
/*************************************************************************/
static int CheckRefinementFailures(void)
{
  ctrl_t ctrl;
  cnbr_t *old_pool;
  double *old_sqrt;
  size_t baseline, fail;
  int status;

  memset(&ctrl, 0, sizeof(ctrl));
  ctrl.nparts = 2;
  ctrl.objtype = METIS_OBJTYPE_CUT;
  ctrl.nbrpoolsize_max = 7;
  ctrl.nbrpoolsize = 1;
  ctrl.nbrpoolcpos = 1;
  ctrl.nbrpoolreallocs = 3;
  if (!gk_malloc_init())
    return 40;
  old_pool = ctrl.cnbrpool = (cnbr_t *)gk_malloc(sizeof(cnbr_t),
      "CheckRefinementFailures: old pool");
  old_sqrt = ctrl.cnbrsqrt = (double *)gk_malloc(sizeof(double),
      "CheckRefinementFailures: old sqrt");
  if (old_pool == NULL || old_sqrt == NULL) {
    gk_malloc_cleanup(0);
    return 41;
  }
  baseline = gk_GetCurMemoryUsed();

  for (fail=1; fail<=2; fail++) {
    FailAllocation(fail, 0);
    status = AllocateRefinementWorkSpace(&ctrl, 4, 2);
    AllowAllocations();
    if (status != METIS_ERROR_MEMORY || ctrl.cnbrpool != old_pool ||
        ctrl.cnbrsqrt != old_sqrt || ctrl.nbrpoolsize_max != 7 ||
        ctrl.nbrpoolsize != 1 || ctrl.nbrpoolcpos != 1 ||
        ctrl.nbrpoolreallocs != 3 ||
        gk_GetCurMemoryUsed() != baseline) {
      FreeWorkSpace(&ctrl);
      gk_malloc_cleanup(0);
      return 41+(int)fail;
    }
  }

  FailAllocation(3, 0);
  status = AllocateRefinementWorkSpace(&ctrl, 4, 2);
  AllowAllocations();
  if (status != METIS_OK || ctrl.cnbrpool == NULL ||
      ctrl.cnbrpool == old_pool || ctrl.cnbrsqrt == NULL ||
      ctrl.cnbrsqrt == old_sqrt || ctrl.nbrpoolsize_max != 4 ||
      ctrl.nbrpoolsize != 2 || ctrl.nbrpoolcpos != 0 ||
      ctrl.nbrpoolreallocs != 0 || allocation_count != 2) {
    FreeWorkSpace(&ctrl);
    gk_malloc_cleanup(0);
    return 44;
  }
  FreeWorkSpace(&ctrl);
  if (gk_GetCurMemoryUsed() != 0) {
    gk_malloc_cleanup(0);
    return 45;
  }
  gk_malloc_cleanup(0);
  return 0;
}


/*************************************************************************/
/*! Checks atomic construction of the two-way graph refinement state. */
/*************************************************************************/
static int Check2WayPartitionFailures(void)
{
  ctrl_t ctrl;
  graph_t graph;
  idx_t *old_pwgts, *old_where, *old_bndptr, *old_bndind, *old_id, *old_ed;
  size_t baseline, fail;
  int mode, status;

  if (!gk_malloc_init())
    return 100;
  for (mode=0; mode<2; mode++) {
    memset(&ctrl, 0, sizeof(ctrl));
    InitGraph(&graph);
    ctrl.status = METIS_OK;
    graph.nvtxs = 4;
    graph.ncon = 2;
    old_pwgts = graph.pwgts = imalloc(4, "Check2WayPartitionFailures: pwgts");
    old_where = graph.where = imalloc(4, "Check2WayPartitionFailures: where");
    old_bndptr = graph.bndptr = imalloc(4,
        "Check2WayPartitionFailures: bndptr");
    old_bndind = graph.bndind = imalloc(4,
        "Check2WayPartitionFailures: bndind");
    old_id = graph.id = imalloc(4, "Check2WayPartitionFailures: id");
    old_ed = graph.ed = imalloc(4, "Check2WayPartitionFailures: ed");
    if (old_pwgts == NULL || old_where == NULL || old_bndptr == NULL ||
        old_bndind == NULL || old_id == NULL || old_ed == NULL) {
      FreeRData(&graph);
      gk_malloc_cleanup(0);
      return 101;
    }
    baseline = gk_GetCurMemoryUsed();
    gk_set_exit_on_error(mode);
    graph.ncon = -1;
    ctrl.status = METIS_OK;
    FailAllocation(SIZE_MAX, 0);
    status = Allocate2WayPartitionMemory(&ctrl, &graph);
    AllowAllocations();
    if (status != METIS_ERROR_INPUT || ctrl.status != METIS_ERROR_INPUT ||
        errno != EINVAL || allocation_count != 0 ||
        graph.pwgts != old_pwgts || gk_GetCurMemoryUsed() != baseline) {
      FreeRData(&graph);
      gk_malloc_cleanup(0);
      return 102+mode*10;
    }
    graph.ncon = IDX_MAX;
    ctrl.status = METIS_OK;
    FailAllocation(SIZE_MAX, 0);
    status = Allocate2WayPartitionMemory(&ctrl, &graph);
    AllowAllocations();
    if (status != METIS_ERROR_MEMORY || ctrl.status != METIS_ERROR_MEMORY ||
        errno != EOVERFLOW || allocation_count != 0 ||
        graph.pwgts != old_pwgts || gk_GetCurMemoryUsed() != baseline) {
      FreeRData(&graph);
      gk_malloc_cleanup(0);
      return 103+mode*10;
    }
    graph.ncon = 2;
    for (fail=1; fail<=6; fail++) {
      ctrl.status = METIS_OK;
      FailAllocation(fail, 0);
      status = Allocate2WayPartitionMemory(&ctrl, &graph);
      AllowAllocations();
      if (status != METIS_ERROR_MEMORY ||
          ctrl.status != METIS_ERROR_MEMORY || graph.pwgts != old_pwgts ||
          graph.where != old_where || graph.bndptr != old_bndptr ||
          graph.bndind != old_bndind || graph.id != old_id ||
          graph.ed != old_ed || gk_GetCurMemoryUsed() != baseline) {
        FreeRData(&graph);
        gk_malloc_cleanup(0);
        return 101+mode*10+(int)fail;
      }
    }
    ctrl.status = METIS_OK;
    FailAllocation(7, 0);
    status = Allocate2WayPartitionMemory(&ctrl, &graph);
    AllowAllocations();
    if (status != METIS_OK || allocation_count != 6 ||
        graph.pwgts == old_pwgts || graph.where == old_where ||
        graph.bndptr == old_bndptr || graph.bndind == old_bndind ||
        graph.id == old_id || graph.ed == old_ed) {
      FreeRData(&graph);
      gk_malloc_cleanup(0);
      return 108+mode*10;
    }
    FreeRData(&graph);
    if (gk_GetCurMemoryUsed() != 0) {
      gk_malloc_cleanup(0);
      return 109+mode*10;
    }
  }
  gk_set_exit_on_error(1);
  gk_malloc_cleanup(0);
  return 0;
}


/*************************************************************************/
/*! Checks atomic construction of the two-way node refinement state. */
/*************************************************************************/
static int Check2WayNodePartitionFailures(void)
{
  ctrl_t ctrl;
  graph_t graph;
  idx_t *old_pwgts, *old_where, *old_bndptr, *old_bndind;
  nrinfo_t *old_nrinfo;
  size_t baseline, fail;
  int mode, status;

  if (!gk_malloc_init())
    return 130;
  for (mode=0; mode<2; mode++) {
    memset(&ctrl, 0, sizeof(ctrl));
    InitGraph(&graph);
    ctrl.status = METIS_OK;
    graph.nvtxs = 4;
    old_pwgts = graph.pwgts = imalloc(3,
        "Check2WayNodePartitionFailures: pwgts");
    old_where = graph.where = imalloc(4,
        "Check2WayNodePartitionFailures: where");
    old_bndptr = graph.bndptr = imalloc(4,
        "Check2WayNodePartitionFailures: bndptr");
    old_bndind = graph.bndind = imalloc(4,
        "Check2WayNodePartitionFailures: bndind");
    old_nrinfo = graph.nrinfo = (nrinfo_t *)gk_malloc(4*sizeof(nrinfo_t),
        "Check2WayNodePartitionFailures: nrinfo");
    if (old_pwgts == NULL || old_where == NULL || old_bndptr == NULL ||
        old_bndind == NULL || old_nrinfo == NULL) {
      FreeRData(&graph);
      gk_malloc_cleanup(0);
      return 131;
    }
    baseline = gk_GetCurMemoryUsed();
    gk_set_exit_on_error(mode);
    graph.nvtxs = -1;
    ctrl.status = METIS_OK;
    FailAllocation(SIZE_MAX, 0);
    status = Allocate2WayNodePartitionMemory(&ctrl, &graph);
    AllowAllocations();
    if (status != METIS_ERROR_INPUT || ctrl.status != METIS_ERROR_INPUT ||
        errno != EINVAL || allocation_count != 0 ||
        graph.pwgts != old_pwgts || gk_GetCurMemoryUsed() != baseline) {
      FreeRData(&graph);
      gk_malloc_cleanup(0);
      return 132+mode*10;
    }
    graph.nvtxs = 4;
    for (fail=1; fail<=5; fail++) {
      ctrl.status = METIS_OK;
      FailAllocation(fail, 0);
      status = Allocate2WayNodePartitionMemory(&ctrl, &graph);
      AllowAllocations();
      if (status != METIS_ERROR_MEMORY ||
          ctrl.status != METIS_ERROR_MEMORY || graph.pwgts != old_pwgts ||
          graph.where != old_where || graph.bndptr != old_bndptr ||
          graph.bndind != old_bndind || graph.nrinfo != old_nrinfo ||
          gk_GetCurMemoryUsed() != baseline) {
        FreeRData(&graph);
        gk_malloc_cleanup(0);
        return 131+mode*10+(int)fail;
      }
    }
    ctrl.status = METIS_OK;
    FailAllocation(6, 0);
    status = Allocate2WayNodePartitionMemory(&ctrl, &graph);
    AllowAllocations();
    if (status != METIS_OK || allocation_count != 5 ||
        graph.pwgts == old_pwgts || graph.where == old_where ||
        graph.bndptr == old_bndptr || graph.bndind == old_bndind ||
        graph.nrinfo == old_nrinfo) {
      FreeRData(&graph);
      gk_malloc_cleanup(0);
      return 137+mode*10;
    }
    FreeRData(&graph);
    if (gk_GetCurMemoryUsed() != 0) {
      gk_malloc_cleanup(0);
      return 138+mode*10;
    }
  }
  gk_set_exit_on_error(1);
  gk_malloc_cleanup(0);
  return 0;
}


/*************************************************************************/
/*! Checks minimum-cover cleanup at every temporary allocation. */
/*************************************************************************/
static int CheckMinCoverFailures(void)
{
  idx_t xadj[] = {0, 1, 2, 3, 4};
  idx_t adjncy[] = {2, 3, 0, 1};
  idx_t sxadj[] = {0, 1, 2}, sadjncy[] = {1, 0};
  idx_t bndind[] = {0, 1}, bndptr[] = {0, 1}, where[] = {0, 1};
  idx_t cover[4], csize;
  ctrl_t ctrl;
  graph_t graph;
  size_t baseline, fail;
  int mode, status;

  if (!gk_malloc_init())
    return 160;
  baseline = gk_GetCurMemoryUsed();
  for (mode=0; mode<2; mode++) {
    gk_set_exit_on_error(mode);
    for (fail=1; fail<=6; fail++) {
      iset(4, -9, cover);
      csize = -9;
      FailAllocation(fail, 0);
      status = MinCover(xadj, adjncy, 2, 4, cover, &csize);
      AllowAllocations();
      if (status != METIS_ERROR_MEMORY || csize != -9 ||
          cover[0] != -9 || cover[1] != -9 ||
          cover[2] != -9 || cover[3] != -9 ||
          gk_GetCurMemoryUsed() != baseline) {
        gk_malloc_cleanup(0);
        return 161+mode*10+(int)fail;
      }
    }

    iset(4, -9, cover);
    csize = -9;
    FailAllocation(7, 0);
    status = MinCover(xadj, adjncy, 2, 4, cover, &csize);
    AllowAllocations();
    if (status != METIS_OK || allocation_count != 6 || csize != 2 ||
        gk_GetCurMemoryUsed() != baseline) {
      gk_malloc_cleanup(0);
      return 168+mode*10;
    }

    memset(&ctrl, 0, sizeof(ctrl));
    InitGraph(&graph);
    ctrl.mcore = gk_mcoreCreate(1024);
    if (ctrl.mcore == NULL) {
      gk_malloc_cleanup(0);
      return 169+mode*10;
    }
    ctrl.status = METIS_OK;
    graph.nvtxs = 2;
    graph.xadj = sxadj;
    graph.adjncy = sadjncy;
    graph.nbnd = 2;
    graph.bndind = bndind;
    graph.bndptr = bndptr;
    graph.where = where;
    where[0] = 0;
    where[1] = 1;
    FailMessage("MinCover: mate");
    ConstructMinCoverSeparator(&ctrl, &graph);
    AllowAllocations();
    if (ctrl.status != METIS_ERROR_MEMORY || where[0] != 0 || where[1] != 1 ||
        ctrl.mcore->cmop != 0 || ctrl.mcore->corecpos != 0) {
      gk_mcoreDestroy(&ctrl.mcore, 0);
      gk_malloc_cleanup(0);
      return 170+mode*10;
    }
    gk_mcoreDestroy(&ctrl.mcore, 0);
    if (gk_GetCurMemoryUsed() != baseline) {
      gk_malloc_cleanup(0);
      return 171+mode*10;
    }
  }
  gk_set_exit_on_error(1);
  gk_malloc_cleanup(0);
  return 0;
}


/*************************************************************************/
/*! Checks initial node-bisection state construction at each allocation. */
/*************************************************************************/
static int CheckNodeBisectionFailures(void)
{
  static const char *messages[] = {
    "Allocate2WayNodeBisectionMemory: pwgts",
    "Allocate2WayNodeBisectionMemory: where",
    "Allocate2WayNodeBisectionMemory: bndptr",
    "Allocate2WayNodeBisectionMemory: bndind",
    "Allocate2WayNodeBisectionMemory: id",
    "Allocate2WayNodeBisectionMemory: ed",
    "Allocate2WayNodeBisectionMemory: nrinfo"
  };
  ctrl_t ctrl;
  graph_t graph;
  idx_t xadj[] = {0, 1, 2}, adjncy[] = {1, 0};
  idx_t vwgt[] = {1, 1}, adjwgt[] = {1, 1}, tvwgt[] = {2};
  real_t ubfactors[] = {1.03}, ntpwgts[] = {0.5, 0.5};
  idx_t *old_pwgts, *old_where, *old_bndptr, *old_bndind, *old_id, *old_ed;
  nrinfo_t *old_nrinfo;
  size_t baseline, i;
  int mode;

  if (!gk_malloc_init())
    return 160;
  for (mode=0; mode<2; mode++) {
    memset(&ctrl, 0, sizeof(ctrl));
    InitGraph(&graph);
    ctrl.status = METIS_OK;
    ctrl.ubfactors = ubfactors;
    ctrl.mcore = gk_mcoreCreate(4096);
    graph.nvtxs = 2;
    graph.ncon = 1;
    graph.xadj = xadj;
    graph.adjncy = adjncy;
    graph.vwgt = vwgt;
    graph.adjwgt = adjwgt;
    graph.tvwgt = tvwgt;
    old_pwgts = graph.pwgts = imalloc(3,
        "CheckNodeBisectionFailures: pwgts");
    old_where = graph.where = imalloc(2,
        "CheckNodeBisectionFailures: where");
    old_bndptr = graph.bndptr = imalloc(2,
        "CheckNodeBisectionFailures: bndptr");
    old_bndind = graph.bndind = imalloc(2,
        "CheckNodeBisectionFailures: bndind");
    old_id = graph.id = imalloc(2, "CheckNodeBisectionFailures: id");
    old_ed = graph.ed = imalloc(2, "CheckNodeBisectionFailures: ed");
    old_nrinfo = graph.nrinfo = (nrinfo_t *)gk_malloc(2*sizeof(nrinfo_t),
        "CheckNodeBisectionFailures: nrinfo");
    if (ctrl.mcore == NULL || old_pwgts == NULL || old_where == NULL ||
        old_bndptr == NULL || old_bndind == NULL || old_id == NULL ||
        old_ed == NULL || old_nrinfo == NULL) {
      FreeRData(&graph);
      FreeWorkSpace(&ctrl);
      gk_malloc_cleanup(0);
      return 161;
    }
    baseline = gk_GetCurMemoryUsed();
    gk_set_exit_on_error(mode);
    for (i=0; i<sizeof(messages)/sizeof(messages[0]); i++) {
      ctrl.status = METIS_OK;
      FailMessage(messages[i]);
      GrowBisectionNode(&ctrl, &graph, ntpwgts, 1);
      AllowAllocations();
      if (ctrl.status != METIS_ERROR_MEMORY || graph.pwgts != old_pwgts ||
          graph.where != old_where || graph.bndptr != old_bndptr ||
          graph.bndind != old_bndind || graph.id != old_id ||
          graph.ed != old_ed || graph.nrinfo != old_nrinfo ||
          gk_GetCurMemoryUsed() != baseline) {
        FreeRData(&graph);
        FreeWorkSpace(&ctrl);
        gk_malloc_cleanup(0);
        return 161+mode*10+(int)i;
      }
    }
    ctrl.status = METIS_OK;
    FailMessage(messages[0]);
    GrowBisectionNode2(&ctrl, &graph, ntpwgts, 1);
    AllowAllocations();
    if (ctrl.status != METIS_ERROR_MEMORY || graph.pwgts != old_pwgts ||
        graph.where != old_where || graph.bndptr != old_bndptr ||
        graph.bndind != old_bndind || graph.id != old_id ||
        graph.ed != old_ed || graph.nrinfo != old_nrinfo ||
        gk_GetCurMemoryUsed() != baseline) {
      FreeRData(&graph);
      FreeWorkSpace(&ctrl);
      gk_malloc_cleanup(0);
      return 168+mode*10;
    }
    FreeRData(&graph);
    FreeWorkSpace(&ctrl);
    if (gk_GetCurMemoryUsed() != 0) {
      gk_malloc_cleanup(0);
      return 169+mode*10;
    }
  }
  gk_set_exit_on_error(1);
  gk_malloc_cleanup(0);
  return 0;
}


/*************************************************************************/
/*! Checks k-way refinement state for both cut and volume objectives. */
/*************************************************************************/
static int CheckKWayPartitionFailures(void)
{
  ctrl_t ctrl;
  graph_t graph;
  idx_t *old_pwgts, *old_where, *old_bndptr, *old_bndind;
  ckrinfo_t *old_ckrinfo;
  vkrinfo_t *old_vkrinfo;
  size_t baseline, fail;
  int mode, objtype, status;

  if (!gk_malloc_init())
    return 200;
  for (mode=0; mode<2; mode++) {
    for (objtype=METIS_OBJTYPE_CUT; objtype<=METIS_OBJTYPE_VOL; objtype++) {
      memset(&ctrl, 0, sizeof(ctrl));
      InitGraph(&graph);
      ctrl.status = METIS_OK;
      ctrl.nparts = 2;
      ctrl.objtype = objtype;
      graph.nvtxs = 4;
      graph.ncon = 2;
      old_pwgts = graph.pwgts = imalloc(4,
          "CheckKWayPartitionFailures: pwgts");
      old_where = graph.where = imalloc(4,
          "CheckKWayPartitionFailures: where");
      old_bndptr = graph.bndptr = imalloc(4,
          "CheckKWayPartitionFailures: bndptr");
      old_bndind = graph.bndind = imalloc(4,
          "CheckKWayPartitionFailures: bndind");
      old_ckrinfo = graph.ckrinfo = NULL;
      old_vkrinfo = graph.vkrinfo = NULL;
      if (objtype == METIS_OBJTYPE_CUT)
        old_ckrinfo = graph.ckrinfo = (ckrinfo_t *)gk_malloc(
            4*sizeof(ckrinfo_t), "CheckKWayPartitionFailures: ckrinfo");
      else {
        old_vkrinfo = graph.vkrinfo = (vkrinfo_t *)gk_malloc(
            4*sizeof(vkrinfo_t), "CheckKWayPartitionFailures: vkrinfo");
        old_ckrinfo = graph.ckrinfo = (ckrinfo_t *)old_vkrinfo;
      }
      if (old_pwgts == NULL || old_where == NULL || old_bndptr == NULL ||
          old_bndind == NULL || old_ckrinfo == NULL) {
        FreeRData(&graph);
        gk_malloc_cleanup(0);
        return 201;
      }
      baseline = gk_GetCurMemoryUsed();
      gk_set_exit_on_error(mode);

      ctrl.status = METIS_OK;
      ctrl.objtype = -1;
      FailAllocation(SIZE_MAX, 0);
      status = AllocateKWayPartitionMemory(&ctrl, &graph);
      AllowAllocations();
      if (status != METIS_ERROR_INPUT || ctrl.status != METIS_ERROR_INPUT ||
          errno != EINVAL || allocation_count != 0 ||
          graph.ckrinfo != old_ckrinfo || graph.vkrinfo != old_vkrinfo ||
          gk_GetCurMemoryUsed() != baseline) {
        FreeRData(&graph);
        gk_malloc_cleanup(0);
        return 212+mode*20+objtype*10;
      }
      ctrl.objtype = objtype;
      ctrl.status = METIS_OK;
      graph.ncon = -1;
      FailAllocation(SIZE_MAX, 0);
      status = AllocateKWayPartitionMemory(&ctrl, &graph);
      AllowAllocations();
      if (status != METIS_ERROR_INPUT || ctrl.status != METIS_ERROR_INPUT ||
          errno != EINVAL || allocation_count != 0 ||
          graph.pwgts != old_pwgts || gk_GetCurMemoryUsed() != baseline) {
        FreeRData(&graph);
        gk_malloc_cleanup(0);
        return 202+mode*20+objtype*10;
      }
      graph.ncon = 2;
      ctrl.nparts = IDX_MAX;
      ctrl.status = METIS_OK;
      FailAllocation(SIZE_MAX, 0);
      status = AllocateKWayPartitionMemory(&ctrl, &graph);
      AllowAllocations();
      if (status != METIS_ERROR_MEMORY || ctrl.status != METIS_ERROR_MEMORY ||
          errno != EOVERFLOW || allocation_count != 0 ||
          graph.pwgts != old_pwgts || gk_GetCurMemoryUsed() != baseline) {
        FreeRData(&graph);
        gk_malloc_cleanup(0);
        return 203+mode*20+objtype*10;
      }
      ctrl.nparts = 2;
      for (fail=1; fail<=5; fail++) {
        ctrl.status = METIS_OK;
        FailAllocation(fail, 0);
        status = AllocateKWayPartitionMemory(&ctrl, &graph);
        AllowAllocations();
        if (status != METIS_ERROR_MEMORY ||
            ctrl.status != METIS_ERROR_MEMORY || errno != ENOMEM ||
            graph.pwgts != old_pwgts || graph.where != old_where ||
            graph.bndptr != old_bndptr || graph.bndind != old_bndind ||
            graph.ckrinfo != old_ckrinfo || graph.vkrinfo != old_vkrinfo ||
            gk_GetCurMemoryUsed() != baseline) {
          FreeRData(&graph);
          gk_malloc_cleanup(0);
          return 204+mode*20+objtype*10+(int)fail;
        }
      }
      ctrl.status = METIS_OK;
      FailAllocation(6, 0);
      status = AllocateKWayPartitionMemory(&ctrl, &graph);
      AllowAllocations();
      if (status != METIS_OK || allocation_count != 5 ||
          graph.pwgts == old_pwgts || graph.where == old_where ||
          graph.bndptr == old_bndptr || graph.bndind == old_bndind ||
          graph.ckrinfo == old_ckrinfo ||
          (objtype == METIS_OBJTYPE_CUT && graph.vkrinfo != NULL) ||
          (objtype == METIS_OBJTYPE_VOL &&
           ((void *)graph.ckrinfo != (void *)graph.vkrinfo ||
            graph.vkrinfo == old_vkrinfo))) {
        FreeRData(&graph);
        gk_malloc_cleanup(0);
        return 210+mode*20+objtype*10;
      }
      FreeRData(&graph);
      if (gk_GetCurMemoryUsed() != 0) {
        gk_malloc_cleanup(0);
        return 211+mode*20+objtype*10;
      }
    }
  }
  gk_set_exit_on_error(1);
  gk_malloc_cleanup(0);
  return 0;
}


int main(void)
{
  int status;

  gk_set_exit_on_error(1);
  AllowAllocations();
  status = CheckSetupCtrlFailures();
  if (status == 0)
    status = CheckSetupGraphFailures();
  if (status == 0)
    status = CheckWorkspaceFailures();
  if (status == 0)
    status = CheckRefinementFailures();
  if (status == 0)
    status = Check2WayPartitionFailures();
  if (status == 0)
    status = Check2WayNodePartitionFailures();
  if (status == 0)
    status = CheckKWayPartitionFailures();
  if (status == 0)
    status = CheckNodeBisectionFailures();
  if (status == 0)
    status = CheckMinCoverFailures();
  if (status == 0)
    status = CheckMeshFailures();
  if (status == 0)
    status = CheckMeshPartitionFailures();
  if (status == 0)
    status = CheckMatchingSortFailures();
  if (status == 0)
    status = CheckMMDOrderFailures();
  if (status == 0)
    status = CheckCoarsenFailure();
  if (status == 0)
    status = CheckCoarseGraphSizeFailures();
  AllowAllocations();
  return status;
}
