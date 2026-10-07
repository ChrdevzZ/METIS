/*
 * Copyright 1997-2011, Regents of the University of Minnesota
 *
 * constructor_failure.c
 *
 * Allocation-by-allocation signal recovery checks for private constructors.
 */

#include "../apps/metisbin.h"
#ifdef METIS_TEST_POSIX_QUERY
#include <pthread.h>
#endif


static size_t allocation_count;
static size_t fail_at=SIZE_MAX;
static int fail_nosignal;
static int in_mcore_create;
static size_t mcore_allocator_calls, mcore_allocator_hits;
static size_t mcore_malloc_calls, mcore_calloc_calls;
static const char *fail_message;
static size_t message_hits;
static int count_mallocs, in_gk_malloc;
static void *mallocs[16];
static size_t fail_malloc_at=SIZE_MAX, malloc_count, nmallocs;
static size_t fail_mcore_at=SIZE_MAX, mcore_count;
static int recursive_status=METIS_OK, recursive_calls, in_recursive;
static int fail_recursive_allocation, initialization_failed;
static size_t allocations_after_failure;
static int trap_armed, trap_hits, trap_error, clobber_cleanup, heap_workspace;
static int heap_workspace_invalid, heap_pop_active;
static size_t trap_count, trap_fail_at, cleanup_clobbers;
static size_t heap_workspace_hits, heap_pop_clobbers;
static int query_ncon=1, query_edge_separator, query_minconn;
static int release_failure;
#ifdef METIS_TEST_POSIX_QUERY
static int SameQueryMask(const sigset_t *left, const sigset_t *right);
int __real_pthread_sigmask(int how, const sigset_t *set, sigset_t *oldset);
#endif


void *__real_gk_malloc(size_t nbytes, const char *msg);
void *__real_gk_malloc_nosignal(size_t nbytes);
void *__real_gk_mcoreMalloc(gk_mcore_t *mcore, size_t nbytes);
gk_mcore_t *__real_gk_mcoreCreate(size_t coresize);
void __real_gk_mcorePop(gk_mcore_t *mcore);
void *__real_malloc(size_t nbytes);
void *__real_calloc(size_t count, size_t nbytes);
void __real_free(void *ptr);
int __real_gk_sigtrap(void);
int __real_gk_siguntrap(void);
int __real_METIS_PartGraphRecursive(idx_t *nvtxs, idx_t *ncon, idx_t *xadj,
    idx_t *adjncy, idx_t *vwgt, idx_t *vsize, idx_t *adjwgt, idx_t *nparts,
    real_t *tpwgts, real_t *ubvec, idx_t *options, idx_t *objval, idx_t *part);


/*************************************************************************/
/*! Injects returned errors at the recursive API boundary of initialization. */
/*************************************************************************/
int __wrap_METIS_PartGraphRecursive(idx_t *nvtxs, idx_t *ncon, idx_t *xadj,
    idx_t *adjncy, idx_t *vwgt, idx_t *vsize, idx_t *adjwgt, idx_t *nparts,
    real_t *tpwgts, real_t *ubvec, idx_t *options, idx_t *objval, idx_t *part)
{
  int status;

  recursive_calls++;
  if (recursive_status != METIS_OK) {
    initialization_failed = 1;
    errno = recursive_status == METIS_ERROR_MEMORY ? ENOMEM : EINVAL;
    return recursive_status;
  }
  in_recursive++;
  status = __real_METIS_PartGraphRecursive(nvtxs, ncon, xadj, adjncy,
      vwgt, vsize, adjwgt, nparts, tpwgts, ubvec, options, objval, part);
  in_recursive--;
  if (status != METIS_OK)
    initialization_failed = 1;
  return status;
}


/*************************************************************************/
/*! Fails one real allocator call within the selected mcore constructor. */
/*************************************************************************/
static int FailMcoreAllocator(int zero_fill)
{
  if (!in_mcore_create || !fail_nosignal)
    return 0;
  mcore_allocator_calls++;
  if (zero_fill)
    mcore_calloc_calls++;
  else
    mcore_malloc_calls++;
  if (++allocation_count != fail_at)
    return 0;
  mcore_allocator_hits++;
  in_mcore_create = 0;
  initialization_failed = 1;
  errno = ENOMEM;
  return 1;
}


/*************************************************************************/
/*! Records mesh storage and the real malloc boundary of mcore creation. */
/*************************************************************************/
void *__wrap_malloc(size_t nbytes)
{
  void *ptr;

  if (FailMcoreAllocator(0))
    return NULL;
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
/*! Covers the malloc/zero-fill to calloc transformation under IPO. */
/*************************************************************************/
void *__wrap_calloc(size_t count, size_t nbytes)
{
  if (FailMcoreAllocator(1))
    return NULL;
  return __real_calloc(count, nbytes);
}


/*************************************************************************/
/*! Removes direct mesh allocations from the outstanding set. */
/*************************************************************************/
void __wrap_free(void *ptr)
{
  size_t i;
  int should_clobber = clobber_cleanup && initialization_failed && ptr != NULL;

  if (count_mallocs && ptr != NULL) {
    for (i=0; i<nmallocs; i++) {
      if (mallocs[i] == ptr) {
        mallocs[i] = mallocs[--nmallocs];
        break;
      }
    }
  }
  __real_free(ptr);
  if (should_clobber) {
    cleanup_clobbers++;
    if (heap_workspace && heap_pop_active)
      heap_pop_clobbers++;
    errno = EACCES;
  }
}


/*************************************************************************/
/*! Controls a frame-setup return at the real production call boundary. */
/*************************************************************************/
int __wrap_gk_sigtrap(void)
{
  if (trap_armed && ++trap_count == trap_fail_at) {
    trap_armed = 0;
    trap_hits++;
    initialization_failed = 1;
    errno = trap_error;
    return 0;
  }
  return __real_gk_sigtrap();
}


/*************************************************************************/
/*! Leaves the outer API frame active when its release fails. */
/*************************************************************************/
int __wrap_gk_siguntrap(void)
{
  if (release_failure && gk_cur_jbufs == 0) {
    errno = EIO;
    return 0;
  }
  return __real_gk_siguntrap();
}


/*************************************************************************/
/*! Injects one reported allocation failure for the wrapping linker fixture. */
/*************************************************************************/
void *__wrap_gk_malloc(size_t nbytes, const char *msg)
{
  void *ptr;

  if (initialization_failed)
    allocations_after_failure++;
  if (!fail_nosignal) {
    allocation_count++;
    if (allocation_count == fail_at ||
        (fail_message != NULL && strcmp(msg, fail_message) == 0) ||
        (fail_recursive_allocation && in_recursive &&
         strcmp(msg, "SetupCtrl: ctrl") == 0)) {
      if (fail_message != NULL && strcmp(msg, fail_message) == 0)
        message_hits++;
      if (clobber_cleanup || strcmp(msg, "InitKWayPartitioning: ubvec") == 0)
        initialization_failed = 1;
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
/*! Injects one no-signal allocation failure for the wrapping linker fixture. */
/*************************************************************************/
void *__wrap_gk_malloc_nosignal(size_t nbytes)
{
  if (initialization_failed)
    allocations_after_failure++;
  if (fail_nosignal && !in_mcore_create) {
    allocation_count++;
    if (allocation_count == fail_at) {
      errno = ENOMEM;
      return NULL;
    }
  }
  return __real_gk_malloc_nosignal(nbytes);
}


/*************************************************************************/
/*! Injects one workspace allocation failure for the wrapping linker fixture. */
/*************************************************************************/
void *__wrap_gk_mcoreMalloc(gk_mcore_t *mcore, size_t nbytes)
{
  if (initialization_failed)
    allocations_after_failure++;
  if (fail_mcore_at != SIZE_MAX && ++mcore_count == fail_mcore_at) {
    if (clobber_cleanup)
      initialization_failed = 1;
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
  mcore_allocator_calls = 0;
  mcore_allocator_hits = 0;
  mcore_malloc_calls = mcore_calloc_calls = 0;
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
  message_hits = 0;
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
  in_mcore_create = 0;
  in_gk_malloc = 0;
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
  static ctrl_t ctrl;
  static graph_t graph;
  gk_mcore_t *old_mcore;
  uintptr_t old_address;
  unsigned char *marker;
  size_t baseline, fail, total_hits=0;
  int mode, status, depth, saved_errno;
#ifdef METIS_TEST_POSIX_QUERY
  sigset_t before, after;
#endif

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
  old_address = (uintptr_t)old_mcore;
  marker = (unsigned char *)gk_malloc(32, "workspace outer marker");
  if (marker == NULL || !gk_sigtrap())
    return 31;
  memset(marker, 0x5a, 32);
  baseline = gk_GetCurMemoryUsed();
  depth = gk_cur_jbufs;
#ifdef METIS_TEST_POSIX_QUERY
  if (__real_pthread_sigmask(SIG_SETMASK, NULL, &before))
    return 31;
#endif

  for (mode=0; mode<2; mode++) {
    gk_set_exit_on_error(mode);
    for (fail=1; fail<=3; fail++) {
      ctrl.status = METIS_OK;
      FailAllocation(fail, 1);
      clobber_cleanup = 1;
      initialization_failed = 0;
      status = METIS_OK;
      switch (gk_sigcatch()) {
        case 0:
          status = AllocateWorkSpace(&ctrl, &graph);
          break;
        default:
          AllowAllocations();
          clobber_cleanup = initialization_failed = 0;
          return 34;
      }
      saved_errno = errno;
      AllowAllocations();
      clobber_cleanup = initialization_failed = 0;
      if (status != METIS_ERROR_MEMORY || ctrl.mcore != old_mcore ||
          saved_errno != ENOMEM || mcore_allocator_hits != 1 ||
          mcore_allocator_calls != fail || gk_cur_jbufs != depth ||
          marker[0] != 0x5a || marker[31] != 0x5a ||
          gk_GetCurMemoryUsed() != baseline)
        return 31+(int)fail;
#ifdef METIS_TEST_POSIX_QUERY
      if (__real_pthread_sigmask(SIG_SETMASK, NULL, &after) ||
          !SameQueryMask(&before, &after))
        return 34;
#endif
      total_hits += mcore_allocator_hits;
      fprintf(stderr, "workspace mode %d fail %zu: hit=%zu malloc=%zu calloc=%zu\n",
          mode, fail, mcore_allocator_hits, mcore_malloc_calls, mcore_calloc_calls);
    }
  }

  ctrl.status = METIS_OK;
  gk_set_exit_on_error(1);
  FailAllocation(4, 1);
  switch (gk_sigcatch()) {
    case 0:
      status = AllocateWorkSpace(&ctrl, &graph);
      break;
    default:
      AllowAllocations();
      return 35;
  }
  AllowAllocations();
  if (status != METIS_OK || ctrl.mcore == NULL ||
      (uintptr_t)ctrl.mcore == old_address ||
      allocation_count != 3 || mcore_allocator_calls != 3 ||
      mcore_allocator_hits != 0) {
    FreeWorkSpace(&ctrl);
    gk_malloc_cleanup(0);
    return 35;
  }
  FreeWorkSpace(&ctrl);
  if (!gk_siguntrap() || gk_cur_jbufs != depth-1)
    return 36;
  gk_free((void **)&marker, LTERM);
  if (gk_GetCurMemoryUsed() != 0) {
    gk_malloc_cleanup(0);
    return 36;
  }
  gk_malloc_cleanup(0);
  fprintf(stderr, "workspace real allocator: failure hits=%zu, healthy calls=%zu, modes=2\n",
      total_hits, mcore_allocator_calls);
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
  uintptr_t old_addresses[2];
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
  old_addresses[0] = (uintptr_t)old_pool;
  old_addresses[1] = (uintptr_t)old_sqrt;
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
      (uintptr_t)ctrl.cnbrpool == old_addresses[0] || ctrl.cnbrsqrt == NULL ||
      (uintptr_t)ctrl.cnbrsqrt == old_addresses[1] || ctrl.nbrpoolsize_max != 4 ||
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
  uintptr_t old_addresses[6];
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
    old_addresses[0] = (uintptr_t)old_pwgts;
    old_addresses[1] = (uintptr_t)old_where;
    old_addresses[2] = (uintptr_t)old_bndptr;
    old_addresses[3] = (uintptr_t)old_bndind;
    old_addresses[4] = (uintptr_t)old_id;
    old_addresses[5] = (uintptr_t)old_ed;
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
        (uintptr_t)graph.pwgts == old_addresses[0] || (uintptr_t)graph.where == old_addresses[1] ||
        (uintptr_t)graph.bndptr == old_addresses[2] || (uintptr_t)graph.bndind == old_addresses[3] ||
        (uintptr_t)graph.id == old_addresses[4] || (uintptr_t)graph.ed == old_addresses[5]) {
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
  uintptr_t old_addresses[5];
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
    old_addresses[0] = (uintptr_t)old_pwgts;
    old_addresses[1] = (uintptr_t)old_where;
    old_addresses[2] = (uintptr_t)old_bndptr;
    old_addresses[3] = (uintptr_t)old_bndind;
    old_addresses[4] = (uintptr_t)old_nrinfo;
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
        (uintptr_t)graph.pwgts == old_addresses[0] || (uintptr_t)graph.where == old_addresses[1] ||
        (uintptr_t)graph.bndptr == old_addresses[2] || (uintptr_t)graph.bndind == old_addresses[3] ||
        (uintptr_t)graph.nrinfo == old_addresses[4]) {
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
  uintptr_t old_addresses[6];
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
      old_addresses[0] = (uintptr_t)old_pwgts;
      old_addresses[1] = (uintptr_t)old_where;
      old_addresses[2] = (uintptr_t)old_bndptr;
      old_addresses[3] = (uintptr_t)old_bndind;
      old_addresses[4] = (uintptr_t)old_ckrinfo;
      old_addresses[5] = (uintptr_t)old_vkrinfo;
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
          (uintptr_t)graph.pwgts == old_addresses[0] || (uintptr_t)graph.where == old_addresses[1] ||
          (uintptr_t)graph.bndptr == old_addresses[2] || (uintptr_t)graph.bndind == old_addresses[3] ||
          (uintptr_t)graph.ckrinfo == old_addresses[4] ||
          (objtype == METIS_OBJTYPE_CUT && graph.vkrinfo != NULL) ||
          (objtype == METIS_OBJTYPE_VOL &&
           ((void *)graph.ckrinfo != (void *)graph.vkrinfo ||
            (uintptr_t)graph.vkrinfo == old_addresses[5]))) {
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


/*************************************************************************/
/*! Checks initialization failure through the linked public k-way entry point.
    Returned recursive errors are injected at an API boundary; the final case
    also executes the real recursive API with a failing control allocation. */
/*************************************************************************/
static int CheckKWayInitializationFailure(int failure, int mode, int objtype)
{
  int errors[] = {METIS_ERROR_MEMORY, METIS_ERROR_MEMORY,
                 METIS_ERROR_INPUT, METIS_ERROR};
  idx_t nvtxs=4, ncon=2, nparts=2, objval=-9;
  idx_t xadj[] = {1, 3, 5, 7, 9};
  idx_t adjncy[] = {2, 4, 1, 3, 2, 4, 1, 3};
  idx_t old_xadj[5], old_adjncy[8], vwgt[] = {1, 1, 1, 1, 1, 1, 1, 1};
  idx_t part[4], options[METIS_NOPTIONS];
  int status, expected, old_depth;

  memcpy(old_xadj, xadj, sizeof(xadj));
  memcpy(old_adjncy, adjncy, sizeof(adjncy));
  METIS_SetDefaultOptions(options);
  options[METIS_OPTION_NUMBERING] = 1;
  options[METIS_OPTION_NCUTS] = 2;
  options[METIS_OPTION_OBJTYPE] = objtype;
  iset(4, -9, part);
  old_depth = gk_cur_jbufs;
  recursive_calls = in_recursive = initialization_failed = 0;
  allocations_after_failure = 0;
  fail_recursive_allocation = failure == 4;
  recursive_status = failure > 0 && failure < 4 ? errors[failure] : METIS_OK;
  expected = failure < 4 ? errors[failure] : METIS_ERROR_MEMORY;
  if (failure == 0)
    FailMessage("InitKWayPartitioning: ubvec");
  gk_set_exit_on_error(mode);
  status = METIS_PartGraphKway(&nvtxs, &ncon, xadj, adjncy, vwgt, NULL,
      NULL, &nparts, NULL, NULL, options, &objval, part);
  AllowAllocations();
  fail_recursive_allocation = 0;
  recursive_status = METIS_OK;
  initialization_failed = 0;
  in_recursive = 0;
  gk_set_exit_on_error(1);
  if (status != expected || allocations_after_failure != 0 ||
      recursive_calls != (failure == 0 ? 0 : 1) ||
      memcmp(xadj, old_xadj, sizeof(xadj)) != 0 ||
      memcmp(adjncy, old_adjncy, sizeof(adjncy)) != 0 ||
      options[METIS_OPTION_NUMBERING] != 1 || gk_cur_jbufs != old_depth ||
      gk_GetCurMemoryUsed() != 0) {
    fprintf(stderr, "k-way failure %d mode %d: status %d, recursive calls %d, later allocations %zu\n",
        failure, mode, status, recursive_calls, allocations_after_failure);
    return 1;
  }
  status = METIS_PartGraphKway(&nvtxs, &ncon, xadj, adjncy, vwgt, NULL,
      NULL, &nparts, NULL, NULL, options, &objval, part);
  if (status != METIS_OK || objval < 0 ||
      memcmp(xadj, old_xadj, sizeof(xadj)) != 0 ||
      memcmp(adjncy, old_adjncy, sizeof(adjncy)) != 0 ||
      gk_cur_jbufs != old_depth || gk_GetCurMemoryUsed() != 0)
    return 2;
  return 0;
}


static int CheckKWayInitializationFailures(void)
{
  ctrl_t ctrl;
  graph_t graph;
  int failure, mode, objtype;

  for (mode=0; mode<2; mode++) {
    for (objtype=METIS_OBJTYPE_CUT; objtype<=METIS_OBJTYPE_VOL; objtype++) {
      for (failure=0; failure<5; failure++) {
        if (CheckKWayInitializationFailure(failure, mode, objtype) != 0)
          return 1;
      }
    }
  }
  memset(&ctrl, 0, sizeof(ctrl));
  InitGraph(&graph);
  ctrl.status = METIS_ERROR_INPUT;
  FailAllocation(SIZE_MAX, 0);
  InitKWayPartitioning(&ctrl, &graph);
  AllowAllocations();
  return ctrl.status == METIS_ERROR_INPUT && allocation_count == 0 ? 0 : 2;
}


#ifdef METIS_TEST_POSIX_QUERY
static int query_armed, query_hits, query_depth, query_error;
static int query_caller_depth, query_max_depth;
static size_t query_count, query_fail_at;

int __real_pthread_sigmask(int, const sigset_t *, sigset_t *);


/* Inject the producer's query return status without setting consumer errno. */
int __wrap_pthread_sigmask(int how, const sigset_t *set, sigset_t *oldset)
{
  if (query_armed && set == NULL && oldset != NULL &&
      gk_cur_jbufs-query_caller_depth > query_max_depth)
    query_max_depth = gk_cur_jbufs-query_caller_depth;
  if (query_armed && set == NULL && oldset != NULL &&
      gk_cur_jbufs == query_depth && ++query_count == query_fail_at) {
    query_armed = 0;
    query_hits++;
    initialization_failed = 1;
    return query_error;
  }
  return __real_pthread_sigmask(how, set, oldset);
}


static int SameQueryMask(const sigset_t *left, const sigset_t *right)
{
  int signum;

  for (signum=1; signum<=SIGRTMAX; signum++) {
    if (sigismember(left, signum) != sigismember(right, signum))
      return 0;
  }
  return 1;
}
#endif


/*************************************************************************/
/*! Uses real heap scratch releases in the large-graph cleanup checks. */
/*************************************************************************/
gk_mcore_t *__wrap_gk_mcoreCreate(size_t coresize)
{
  gk_mcore_t *mcore;

  in_mcore_create = fail_nosignal;
  mcore = __real_gk_mcoreCreate(heap_workspace ? 0 : coresize);
  in_mcore_create = 0;
  if (heap_workspace) {
    heap_workspace_hits++;
    if (mcore != NULL && mcore->coresize != 0)
      heap_workspace_invalid = 1;
  }
  return mcore;
}


void __wrap_gk_mcorePop(gk_mcore_t *mcore)
{
  heap_pop_active++;
  __real_gk_mcorePop(mcore);
  heap_pop_active--;
}


/*************************************************************************/
/*! Exercises the multi-separator and large-graph node bisection paths. */
/*************************************************************************/
static int RunLargeNodeAPI(void)
{
  static idx_t xadj[5001], adjncy[10000], perm[5000], iperm[5000];
  idx_t nvtxs=5000, options[METIS_NOPTIONS], i;
  int status, saved_errno;

  for (i=0; i<nvtxs; i++) {
    xadj[i] = 2*i;
    adjncy[2*i] = (i+nvtxs-1)%nvtxs;
    adjncy[2*i+1] = (i+1)%nvtxs;
  }
  xadj[nvtxs] = 2*nvtxs;
  METIS_SetDefaultOptions(options);
  options[METIS_OPTION_COMPRESS] = 0;
  options[METIS_OPTION_NSEPS] = 2;
  /* Force heap-backed scratch so workspace pops exercise real releases. */
  heap_workspace_hits = 0;
  heap_workspace_invalid = 0;
  heap_workspace = 1;
  status = METIS_NodeND(&nvtxs, xadj, adjncy, NULL, options, perm, iperm);
  saved_errno = errno;
  heap_workspace = 0;
  if (heap_workspace_invalid || (status == METIS_OK && heap_workspace_hits == 0))
    status = METIS_ERROR_INPUT;
  for (i=0; i<nvtxs; i++) {
    if (xadj[i] != 2*i || adjncy[2*i] != (i+nvtxs-1)%nvtxs ||
        adjncy[2*i+1] != (i+1)%nvtxs ||
        (status == METIS_OK && (perm[i] < 0 || perm[i] >= nvtxs ||
         iperm[perm[i]] != i)))
      status = METIS_ERROR_INPUT;
  }
  if (xadj[nvtxs] != 2*nvtxs)
    status = METIS_ERROR_INPUT;
  errno = saved_errno;
  return status;
}


static int RunQueryAPI(int api, int objtype, int debug)
{
  idx_t nvtxs=4, ncon=query_ncon, nparts=2, options[METIS_NOPTIONS], obj=-9;
  idx_t weights[]={1, 2, 2, 1, 1, 2, 2, 1};
  idx_t xadj[]={1, 3, 5, 7, 9}, adjncy[]={2, 4, 1, 3, 2, 4, 1, 3};
  idx_t part[8], iperm[4], ne=2, nn=3, numflag=1, common=1;
  idx_t vxadj[]={1, 3, 5, 7, 9, 11, 13, 15, 17};
  idx_t vadjncy[]={2, 8, 1, 3, 2, 4, 3, 5, 4, 6, 5, 7, 6, 8, 7, 1};
  idx_t eptr[]={1, 3, 5}, eind[]={1, 2, 2, 3}, epart[2], npart[3];
  idx_t pxadj[]={0, 2, 4, 6, 8}, padjncy[]={1, 3, 0, 2, 1, 3, 0, 2};
  idx_t hmarker[]={-1, -1, -1, -1}, refined[]={0, 2, 1, 2}, sizes[3];
  idx_t *mxadj=NULL, *madjncy=NULL;
  int status;

  if (api == 10)
    return RunLargeNodeAPI();
  METIS_SetDefaultOptions(options);
  options[METIS_OPTION_NUMBERING] = 1;
  options[METIS_OPTION_CONTIG] = 1;
  options[METIS_OPTION_MINCONN] = query_minconn;
  if (query_edge_separator)
    options[METIS_OPTION_IPTYPE] = METIS_IPTYPE_EDGE;
  if (api != 2 && api < 7)
    options[METIS_OPTION_OBJTYPE] = objtype;
  if (api >= 7) {
    options[METIS_OPTION_NUMBERING] = 0;
    options[METIS_OPTION_OBJTYPE] = METIS_OBJTYPE_NODE;
  }
  options[METIS_OPTION_DBGLVL] = debug;
  /* Use a ring that retains both contiguous parts in the volume objective. */
  if (api == 0 && objtype == METIS_OBJTYPE_VOL)
    nvtxs = 8;
  if (api == 0)
    status = METIS_PartGraphKway(&nvtxs, &ncon,
        nvtxs == 8 ? vxadj : xadj, nvtxs == 8 ? vadjncy : adjncy,
        ncon == 2 ? weights : NULL,
        NULL, NULL, &nparts, NULL, NULL, options, &obj, part);
  else if (api == 1)
    status = METIS_PartGraphRecursive(&nvtxs, &ncon, xadj, adjncy,
        ncon == 2 ? weights : NULL,
        NULL, NULL, &nparts, NULL, NULL, options, &obj, part);
  else if (api == 2)
    status = METIS_NodeND(&nvtxs, xadj, adjncy, NULL, options, part, iperm);
  else if (api == 3)
    status = METIS_MeshToNodal(&ne, &nn, eptr, eind, &numflag,
        &mxadj, &madjncy);
  else if (api == 4)
    status = METIS_MeshToDual(&ne, &nn, eptr, eind, &common, &numflag,
        &mxadj, &madjncy);
  else if (api == 5)
    status = METIS_PartMeshNodal(&ne, &nn, eptr, eind, NULL, NULL,
        &nparts, NULL, options, &obj, epart, npart);
  else if (api == 6)
    status = METIS_PartMeshDual(&ne, &nn, eptr, eind, NULL, NULL,
        &common, &nparts, NULL, options, &obj, epart, npart);
  else if (api == 7)
    status = METIS_NodeNDP(nvtxs, pxadj, padjncy, NULL, 2, options,
        part, iperm, sizes);
  else if (api == 8)
    status = METIS_ComputeVertexSeparator(&nvtxs, pxadj, padjncy,
        NULL, options, &obj, part);
  else
    status = METIS_NodeRefine(nvtxs, pxadj, NULL, padjncy,
        refined, hmarker, (real_t)1.05);

  /* Preserve errno before checking input restoration or releasing outputs. */
  {
    int saved_errno = errno;
    idx_t old_xadj[]={1, 3, 5, 7, 9}, old_adjncy[]={2, 4, 1, 3, 2, 4, 1, 3};
    idx_t old_eptr[]={1, 3, 5}, old_eind[]={1, 2, 2, 3};
    idx_t old_vxadj[]={1, 3, 5, 7, 9, 11, 13, 15, 17};
    idx_t old_vadjncy[]={2, 8, 1, 3, 2, 4, 3, 5, 4, 6, 5, 7, 6, 8, 7, 1};
    idx_t old_pxadj[]={0, 2, 4, 6, 8}, old_padjncy[]={1, 3, 0, 2, 1, 3, 0, 2};
    idx_t i, j, separator=0;
    if (memcmp(xadj, old_xadj, sizeof(xadj)) ||
        memcmp(adjncy, old_adjncy, sizeof(adjncy)) ||
        memcmp(vxadj, old_vxadj, sizeof(vxadj)) ||
        memcmp(vadjncy, old_vadjncy, sizeof(vadjncy)) ||
        memcmp(eptr, old_eptr, sizeof(eptr)) ||
        memcmp(eind, old_eind, sizeof(eind)) ||
        memcmp(pxadj, old_pxadj, sizeof(pxadj)) ||
        memcmp(padjncy, old_padjncy, sizeof(padjncy)) ||
        options[METIS_OPTION_NUMBERING] != (api < 7 ? 1 : 0) || numflag != 1 ||
        ((api == 3 || api == 4) && status != METIS_OK &&
         (mxadj != NULL || madjncy != NULL)))
      status = METIS_ERROR_INPUT;
    if (status == METIS_OK && api == 7) {
      for (i=0; i<nvtxs; i++) {
        if (part[i] < 0 || part[i] >= nvtxs || iperm[part[i]] != i)
          status = METIS_ERROR_INPUT;
      }
    }
    if (status == METIS_OK && api >= 8) {
      idx_t *where = api == 8 ? part : refined;
      for (i=0; i<nvtxs; i++) {
        if (where[i] < 0 || where[i] > 2)
          status = METIS_ERROR_INPUT;
        separator += where[i] == 2;
        for (j=pxadj[i]; j<pxadj[i+1]; j++) {
          if (where[i] != 2 && where[padjncy[j]] != 2 &&
              where[i] != where[padjncy[j]])
            status = METIS_ERROR_INPUT;
        }
      }
      if (api == 8 && separator != obj)
        status = METIS_ERROR_INPUT;
    }
    METIS_Free(mxadj);
    METIS_Free(madjncy);
    errno = saved_errno;
  }
  return status;
}


/*************************************************************************/
/*! Checks first-error preservation even when successful free changes errno. */
/*************************************************************************/
static int CheckFrameCleanup(int api, int objtype, int debug, size_t nth,
    int mode, int error, size_t *observed, size_t *clobbers)
{
  unsigned char *marker;
  size_t baseline;
  volatile int status=METIS_ERROR;
  int depth, saved_errno, expected, result=0;

  AllowAllocations();
  initialization_failed = 0;
  if (!gk_malloc_init())
    return 1;
  marker = (unsigned char *)gk_malloc(32, "frame cleanup marker");
  if (marker == NULL || !gk_sigtrap())
    return 2;
  memset(marker, 0x5a, 32);
  baseline = gk_GetCurMemoryUsed();
  depth = gk_cur_jbufs;
  trap_count = trap_hits = cleanup_clobbers = 0;
  heap_pop_clobbers = 0;
  trap_fail_at = nth;
  trap_error = error;
  trap_armed = 1;
  clobber_cleanup = nth != SIZE_MAX;
  allocations_after_failure = 0;
  errno = 0;
  gk_set_exit_on_error(mode);
  switch (gk_sigcatch()) {
    case 0:
      status = RunQueryAPI(api, objtype, debug);
      break;
    default:
      /* The public entry must catch its own errors inside this live frame. */
      result = 3;
      break;
  }
  saved_errno = errno;
  trap_armed = clobber_cleanup = initialization_failed = heap_workspace = 0;
  heap_pop_active = 0;
  *observed = trap_count;
  *clobbers = cleanup_clobbers;
  expected = nth == SIZE_MAX ? METIS_OK :
      (error == ENOMEM || error == EOVERFLOW ? METIS_ERROR_MEMORY : METIS_ERROR);
  if (result == 0 && (status != expected ||
      trap_hits != (nth == SIZE_MAX ? 0 : 1) ||
      (nth != SIZE_MAX && saved_errno != error) ||
      allocations_after_failure != 0 || gk_cur_jbufs != depth ||
      gk_GetCurMemoryUsed() != baseline || marker[0] != 0x5a ||
      marker[31] != 0x5a))
    result = 4;
  if (result == 0 && RunQueryAPI(api, objtype, debug) != METIS_OK)
    result = 5;
  if (gk_cur_jbufs != depth || gk_GetCurMemoryUsed() != baseline ||
      !gk_siguntrap())
    result = 6;
  gk_free((void **)&marker, LTERM);
  gk_malloc_cleanup(0);
  gk_set_exit_on_error(1);
  if (result != 0)
    fprintf(stderr, "frame cleanup API %d nth %zu mode %d obj %d debug %d: status %d errno %d expected %d/%d hits %d frees %zu result %d\n",
        api, nth, mode, objtype, debug, status, saved_errno,
        expected, error, trap_hits, cleanup_clobbers, result);
  return result;
}


static int CheckFrameCleanupFailures(void)
{
  int errors[] = {EIO, ENOMEM, EOVERFLOW};
  int api, mode, variant, objtype, debug;
  size_t error, total, nth, observed, clobbers, cases=0, frees=0;

  for (api=0; api<10; api++) {
    for (variant=0; variant<(api == 0 ? 6 : (api == 1 || api == 2 ? 2 : 1)); variant++) {
      query_ncon = (api == 0 && variant == 3) ||
          (api == 1 && variant == 1) ? 2 : 1;
      query_edge_separator = api == 2 && variant == 1;
      query_minconn = api == 0 && variant == 5;
      objtype = api == 0 && (variant == 1 || variant == 5) ?
          METIS_OBJTYPE_VOL : METIS_OBJTYPE_CUT;
      debug = api == 0 && variant == 2 ? METIS_DBG_REFINE :
          (api == 0 && variant == 4 ? METIS_DBG_TIME : 0);
      if (CheckFrameCleanup(api, objtype, debug, SIZE_MAX, 0, EIO,
          &total, &clobbers) != 0 || total == 0)
        return 1;
      for (nth=1; nth<=total; nth++) {
        for (mode=0; mode<2; mode++) {
          for (error=0; error<sizeof(errors)/sizeof(errors[0]); error++) {
            if (CheckFrameCleanup(api, objtype, debug, nth, mode,
                errors[error], &observed, &clobbers) != 0)
              return 2;
            frees += clobbers;
            cases++;
          }
        }
      }
    }
  }
  printf("Controlled frame cleanup cases: %zu, errno-clobbering frees: %zu\n",
      cases, frees);
  return cases != 0 && frees != 0 ? 0 : 3;
}


static int CheckLargeFrameCleanupFailures(void)
{
  int errors[] = {EIO, ENOMEM, EOVERFLOW};
  int mode;
  size_t error, total, positions[8], i, observed, clobbers, frees=0, pops=0;

  if (CheckFrameCleanup(10, METIS_OBJTYPE_NODE, 0, SIZE_MAX, 0, EIO,
      &total, &clobbers) != 0 || total < 12)
    return 1;
  /* Bound the run time while visiting several nested construction depths. */
  positions[0] = 8;
  positions[1] = 9;
  positions[2] = 10;
  positions[3] = 11;
  positions[4] = total/3;
  positions[5] = total/2;
  positions[6] = 2*total/3;
  positions[7] = total-1;
  for (i=0; i<sizeof(positions)/sizeof(positions[0]); i++) {
    for (mode=0; mode<2; mode++) {
      for (error=0; error<sizeof(errors)/sizeof(errors[0]); error++) {
        if (CheckFrameCleanup(10, METIS_OBJTYPE_NODE, 0, positions[i],
            mode, errors[error], &observed, &clobbers) != 0)
          return 2;
        frees += clobbers;
        pops += heap_pop_clobbers;
      }
    }
  }
  printf("Large-graph frame cleanup cases: 48, errno-clobbering frees: %zu, heap pop frees: %zu\n",
      frees, pops);
  return frees != 0 && pops != 0 ? 0 : 3;
}


#ifdef METIS_TEST_POSIX_QUERY
static int CheckQueryAPI(int api, int relative_depth, size_t nth,
    int mode, int error, int objtype, int debug, size_t *observed)
{
  unsigned char *marker;
  size_t baseline;
  int depth, status, saved_errno, expected, result=0;
  sigset_t before, after;

  AllowAllocations();
  initialization_failed = 0;
  if (!gk_malloc_init())
    return 1;
  marker = (unsigned char *)gk_malloc(32, "query API marker");
  if (marker == NULL || !gk_sigtrap())
    return 2;
  memset(marker, 0x5a, 32);
  baseline = gk_GetCurMemoryUsed();
  depth = gk_cur_jbufs;
  if (__real_pthread_sigmask(SIG_SETMASK, NULL, &before))
    return 3;
  query_count = query_hits = 0;
  query_caller_depth = depth;
  query_max_depth = 0;
  query_depth = depth+relative_depth;
  query_fail_at = nth;
  query_error = error;
  query_armed = 1;
  allocations_after_failure = 0;
  errno = 0;
  gk_set_exit_on_error(mode);
  switch (gk_sigcatch()) {
    case 0:
      status = RunQueryAPI(api, objtype, debug);
      break;
    default:
      /* A public API must recover its own error, leaving this caller alive. */
      return 4;
  }
  saved_errno = errno;
  query_armed = 0;
  initialization_failed = 0;
  *observed = query_count;
  expected = nth == SIZE_MAX ? METIS_OK :
      (error == ENOMEM || error == EOVERFLOW ? METIS_ERROR_MEMORY : METIS_ERROR);
  if (__real_pthread_sigmask(SIG_SETMASK, NULL, &after) ||
      status != expected || query_hits != (nth == SIZE_MAX ? 0 : 1) ||
      (nth != SIZE_MAX && saved_errno != error) ||
      allocations_after_failure != 0 ||
      gk_cur_jbufs != depth || !SameQueryMask(&before, &after) ||
      gk_GetCurMemoryUsed() != baseline || marker[0] != 0x5a ||
      marker[31] != 0x5a)
    result = 5;
  if (result == 0 && RunQueryAPI(api, objtype, debug) != METIS_OK)
    result = 6;
  if (gk_cur_jbufs != depth || gk_GetCurMemoryUsed() != baseline ||
      !gk_siguntrap())
    result = 7;
  gk_free((void **)&marker, LTERM);
  gk_malloc_cleanup(0);
  gk_set_exit_on_error(1);
  if (result != 0)
    fprintf(stderr, "query API %d depth %d nth %zu mode %d obj %d debug %d: status %d errno %d hits %d result %d\n",
        api, relative_depth, nth, mode, objtype, debug, status,
        saved_errno, query_hits, result);
  return result;
}


static int CheckQueryFailures(void)
{
  int errors[] = {EIO, ENOMEM, EOVERFLOW};
  int api, depth, max_depth, mode, variant, objtype, debug;
  size_t total, nth, error, observed, cases=0;

  /* Calibrate all observed depths, then fail queries on the same input. */
  for (api=0; api<10; api++) {
    for (variant=0; variant<(api == 0 ? 6 : (api == 1 || api == 2 ? 2 : 1)); variant++) {
      query_ncon = (api == 0 && variant == 3) ||
          (api == 1 && variant == 1) ? 2 : 1;
      query_edge_separator = api == 2 && variant == 1;
      query_minconn = api == 0 && variant == 5;
      objtype = api == 0 && (variant == 1 || variant == 5) ?
          METIS_OBJTYPE_VOL : METIS_OBJTYPE_CUT;
      debug = api == 0 && variant == 2 ? METIS_DBG_REFINE :
          (api == 0 && variant == 4 ? METIS_DBG_TIME : 0);
      if (CheckQueryAPI(api, 0, SIZE_MAX, 0, EIO, objtype, debug,
          &total) != 0 || total == 0)
        return 1;
      max_depth = query_max_depth;
      printf("POSIX query API %d variant %d ncon %d obj %d debug %d maximum relative depth: %d\n",
          api, variant, query_ncon, objtype, debug, max_depth);
      for (depth=0; depth<=max_depth; depth++) {
        if (CheckQueryAPI(api, depth, SIZE_MAX, 0, EIO, objtype, debug,
            &total) != 0)
          return 1;
        for (nth=1; nth<=total; nth++) {
          for (mode=0; mode<2; mode++) {
            for (error=0; error<sizeof(errors)/sizeof(errors[0]); error++) {
              if (CheckQueryAPI(api, depth, nth, mode, errors[error],
                  objtype, debug, &observed) != 0)
                return 2;
              cases++;
            }
          }
        }
      }
    }
  }
  printf("POSIX query failure cases: %zu\n", cases);
  return cases != 0 ? 0 : 3;
}


/* Static objects retain their owned pointers across the caller's jump. */
static int CheckQueryResize(int kind, int mode, int error)
{
  static ctrl_t ctrl;
  static graph_t graph, coarse;
  static idx_t *adids[2], *adwgts[2], maxnads[2], nads[2];
  idx_t *old_first, *old_second;
  size_t baseline;
  sigset_t before, after;
  volatile int caught=0;
  int depth, saved_errno, expected, result=0, i;

  AllowAllocations();
  if (!gk_malloc_init())
    return 1;
  memset(&ctrl, 0, sizeof(ctrl));
  memset(&graph, 0, sizeof(graph));
  memset(&coarse, 0, sizeof(coarse));
  ctrl.status = METIS_OK;
  if (kind != 1) {
    graph.nedges = 20000;
    coarse.nedges = 10001;
    coarse.adjncy = ismalloc(20000, 17, "query resize adjacency");
    coarse.adjwgt = ismalloc(20000, 23, "query resize weights");
    if (coarse.adjncy == NULL || coarse.adjwgt == NULL)
      return 2;
    old_first = coarse.adjncy;
    old_second = coarse.adjwgt;
  }
  else {
    for (i=0; i<2; i++) {
      adids[i] = ismalloc(1, i, "query resize subdomain ids");
      adwgts[i] = ismalloc(1, 1, "query resize subdomain weights");
      if (adids[i] == NULL || adwgts[i] == NULL)
        return 3;
      nads[i] = maxnads[i] = 1;
    }
    ctrl.nparts = 2;
    ctrl.adids = adids;
    ctrl.adwgts = adwgts;
    ctrl.maxnads = maxnads;
    ctrl.nads = nads;
    old_first = adids[0];
    old_second = adwgts[0];
  }
  baseline = gk_GetCurMemoryUsed();
  if (!gk_sigtrap() ||
      __real_pthread_sigmask(SIG_SETMASK, NULL, &before))
    return 4;
  depth = gk_cur_jbufs;
  query_depth = depth;
  query_count = query_hits = 0;
  query_fail_at = 1;
  query_error = error;
  query_armed = 1;
  errno = 0;
  gk_set_exit_on_error(mode);
  switch (gk_sigcatch()) {
    case 0:
      if (kind != 1)
        ReAdjustMemory(kind == 2 ? NULL : &ctrl, &graph, &coarse);
      else
        UpdateEdgeSubDomainGraph(&ctrl, 0, 1, 1, NULL);
      break;
    case SIGMEM:
      caught = SIGMEM;
      break;
    default:
      caught = SIGERR;
      break;
  }
  saved_errno = errno;
  query_armed = 0;
  initialization_failed = 0;
  expected = error == ENOMEM || error == EOVERFLOW ? SIGMEM : SIGERR;
  if (__real_pthread_sigmask(SIG_SETMASK, NULL, &after) ||
      query_hits != 1 || saved_errno != error || gk_cur_jbufs != depth ||
      !SameQueryMask(&before, &after) || gk_GetCurMemoryUsed() != baseline ||
      caught != (mode ? expected : 0) ||
      (!mode && kind != 2 && ctrl.status != metis_rcode(expected)))
    result = 5;
  if (kind != 1) {
    if (coarse.adjncy != old_first || coarse.adjwgt != old_second ||
        coarse.adjncy[0] != 17 || coarse.adjncy[19999] != 17 ||
        coarse.adjwgt[0] != 23)
      result = 6;
  }
  else if (adids[0] != old_first || adwgts[0] != old_second ||
      nads[0] != 1 || maxnads[0] != 1 ||
      adids[0][0] != 0 || adwgts[0][0] != 1)
    result = 7;

  if (result == 0) {
    ctrl.status = METIS_OK;
    if (kind != 1) {
      ReAdjustMemory(kind == 2 ? NULL : &ctrl, &graph, &coarse);
      if (ctrl.status != METIS_OK || coarse.adjncy[0] != 17 ||
          coarse.adjncy[10000] != 17 || coarse.adjwgt[10000] != 23)
        result = 8;
    }
    else {
      UpdateEdgeSubDomainGraph(&ctrl, 0, 1, 1, NULL);
      if (ctrl.status != METIS_OK || nads[0] != 2 || nads[1] != 2)
        result = 9;
    }
  }
  if (!gk_siguntrap() || gk_cur_jbufs != depth-1)
    result = 10;
  gk_malloc_cleanup(0);
  gk_set_exit_on_error(1);
  if (result != 0)
    fprintf(stderr, "query resize kind %d mode %d errno %d expected %d hits %d result %d\n",
        kind, mode, saved_errno, error, query_hits, result);
  return result;
}


static int CheckQueryResizeFailures(void)
{
  int errors[] = {EIO, ENOMEM, EOVERFLOW};
  int kind, mode;
  size_t error;

  for (kind=0; kind<3; kind++) {
    for (mode=0; mode<2; mode++) {
      for (error=0; error<sizeof(errors)/sizeof(errors[0]); error++) {
        if (CheckQueryResize(kind, mode, errors[error]) != 0)
          return 1;
      }
    }
  }
  return 0;
}
#endif


/*************************************************************************/
/*! Preserves a returned scratch error through real queue and heap releases. */
/*************************************************************************/
static int CheckScratchCleanupFailures(void)
{
  static ctrl_t ctrl;
  static graph_t graph;
  static idx_t xadj[]={0, 2, 4, 6, 8}, adjncy[]={1, 3, 0, 2, 1, 3, 0, 2};
  static idx_t old_xadj[5], old_adjncy[8], pwgts[]={4, 0, 0}, order[4];
  static idx_t hmarker[]={-1, -1, -1, -1};
  static real_t targets[]={0.5, 0.5}, factors[]={1.03, 1.03};
  static idx_t tvwgt[]={4, 4}, vwgt[]={1, 1, 1, 1, 1, 1, 1, 1};
  unsigned char *marker;
  size_t baseline;
  int kind, depth, saved_errno, result=0;
  volatile int caught=0;

  if (!gk_malloc_init())
    return 1;
  marker = (unsigned char *)gk_malloc(32, "scratch cleanup marker");
  if (marker == NULL || !gk_sigtrap())
    return 2;
  memset(marker, 0x5a, 32);
  baseline = gk_GetCurMemoryUsed();
  depth = gk_cur_jbufs;
  memcpy(old_xadj, xadj, sizeof(xadj));
  memcpy(old_adjncy, adjncy, sizeof(adjncy));
  gk_set_exit_on_error(0);
  for (kind=0; kind<9; kind++) {
    memset(&ctrl, 0, sizeof(ctrl));
    InitGraph(&graph);
    ctrl.status = METIS_OK;
    ctrl.mcore = gk_mcoreCreate(0);
    if (ctrl.mcore == NULL)
      return 3;
    graph.nvtxs = 4;
    graph.ncon = kind == 8 ? 2 : 1;
    graph.vwgt = vwgt;
    graph.tvwgt = tvwgt;
    ctrl.ubfactors = factors;
    graph.xadj = xadj;
    graph.adjncy = adjncy;
    graph.pwgts = pwgts;
    graph.mincut = 0;
    clobber_cleanup = 1;
    cleanup_clobbers = 0;
    initialization_failed = 0;
    allocations_after_failure = 0;
    caught = 0;
    FailMcoreAllocation(2);
    errno = 0;
    switch (gk_sigcatch()) {
      case 0:
        if (kind == 0)
          MMDOrder(&ctrl, &graph, order, 4);
        else if (kind == 1)
          FM_2WayNodeRefine1SidedP(&ctrl, &graph, hmarker, 1.03, 1);
        else if (kind == 2)
          Bnd2WayBalance(&ctrl, &graph, targets);
        else if (kind == 3)
          FM_2WayNodeRefine2Sided(&ctrl, &graph, 1);
        else if (kind == 4)
          FM_2WayNodeRefine1Sided(&ctrl, &graph, 1);
        else if (kind == 5)
          FM_2WayNodeBalance(&ctrl, &graph);
        else if (kind == 6)
          FM_2WayNodeRefine2SidedP(&ctrl, &graph, hmarker, 1.03, 1);
        else if (kind == 7)
          General2WayBalance(&ctrl, &graph, targets);
        else
          McGeneral2WayBalance(&ctrl, &graph, targets);
        break;
      default:
        caught = 1;
        break;
    }
    saved_errno = errno;
    AllowAllocations();
    clobber_cleanup = initialization_failed = 0;
    if (caught || saved_errno != ENOMEM || cleanup_clobbers == 0 ||
        allocations_after_failure != 0 || mcore_count != 2 ||
        ctrl.status != METIS_ERROR_MEMORY || gk_cur_jbufs != depth ||
        ctrl.mcore->cmop != 0 || ctrl.mcore->cur_hallocs != 0 ||
        memcmp(xadj, old_xadj, sizeof(xadj)) != 0 ||
        memcmp(adjncy, old_adjncy, sizeof(adjncy)) != 0) {
      fprintf(stderr, "scratch cleanup kind %d: errno %d, frees %zu, status %d, allocations %zu, after-failure %zu\n",
          kind, saved_errno, cleanup_clobbers, ctrl.status, mcore_count,
          allocations_after_failure);
      result = 1;
    }
    fprintf(stderr, "scratch case %d: errno %d, frees %zu, status %d, allocations %zu, after-failure %zu\n",
        kind, saved_errno, cleanup_clobbers, ctrl.status, mcore_count,
        allocations_after_failure);
    gk_mcoreDestroy(&ctrl.mcore, 0);
    if (gk_GetCurMemoryUsed() != baseline || marker[0] != 0x5a ||
        marker[31] != 0x5a || RunQueryAPI(kind == 1 ? 9 : 2,
          METIS_OBJTYPE_CUT, 0) != METIS_OK)
      result = 1;
  }
  if (!gk_siguntrap() || gk_cur_jbufs != depth-1)
    result = 1;
  gk_free((void **)&marker, LTERM);
  gk_malloc_cleanup(0);
  gk_set_exit_on_error(1);
  return result;
}


/*************************************************************************/
/*! Exercises scratch cleanup using real graph and partition constructors. */
/*************************************************************************/
static int CheckPreparedScratchFailures(void)
{
  static const int kinds[]={0, 1, 1, 2, 3, 4, 5, 6, 6, 6,
      7, 7, 7, 7, 8, 8, 8, 8, 8, 9, 9, 9, 9, 10, 10, 10, 10, 10};
  static const size_t positions[]={2, 2, 8, 2, 2, 2, 2, 2, 5, 0,
      2, 4, 6, 9, 2, 4, 6, 9, 12, 2, 5, 7, 10, 2, 5, 7, 10, 13};
  static ctrl_t *ctrl;
  static graph_t *graph;
  static idx_t xadj[65], adjncy[128], old_xadj[65], old_adjncy[128];
  static real_t targets[]={0.5, 0.5, 0.5, 0.5};
  idx_t options[METIS_NOPTIONS];
  unsigned char *marker;
  size_t baseline, test;
  int i, kind, ncon, depth, saved_errno, status, result=0;
  volatile int caught=0;
#ifdef METIS_TEST_POSIX_QUERY
  sigset_t before, after;

  if (pthread_sigmask(SIG_SETMASK, NULL, &before) != 0)
    return 1;
#endif

  AllowAllocations();
  if (!gk_malloc_init())
    return 2;
  marker = (unsigned char *)gk_malloc(32, "prepared scratch marker");
  if (marker == NULL || !gk_sigtrap())
    return 3;
  memset(marker, 0x5a, 32);
  baseline = gk_GetCurMemoryUsed();
  depth = gk_cur_jbufs;
  for (i=0; i<64; i++) {
    xadj[i] = 2*i;
    adjncy[2*i] = (i+63)%64;
    adjncy[2*i+1] = (i+1)%64;
  }
  xadj[64] = 128;
  memcpy(old_xadj, xadj, sizeof(xadj));
  memcpy(old_adjncy, adjncy, sizeof(adjncy));
  gk_set_exit_on_error(0);
  for (test=0; test<sizeof(kinds)/sizeof(kinds[0]); test++) {
    kind = kinds[test];
    ncon = kind == 1 || kind == 4 || kind >= 9 ? 2 : 1;
    METIS_SetDefaultOptions(options);
    options[METIS_OPTION_SEED] = 12345;
    if (kind >= 7) {
      options[METIS_OPTION_OBJTYPE] = kind == 8 || kind == 10 ?
          METIS_OBJTYPE_VOL : METIS_OBJTYPE_CUT;
      options[METIS_OPTION_CONTIG] = 1;
    }
    ctrl = NULL;
    graph = NULL;
    status = SetupCtrl(kind >= 7 ? METIS_OP_KMETIS :
        kind == 5 || kind == 6 ? METIS_OP_OMETIS : METIS_OP_PMETIS,
        options, ncon, kind == 5 || kind == 6 ? 3 : 2, NULL, NULL, &ctrl);
    if (status != METIS_OK || SetupGraph(ctrl, 64, ncon, xadj, adjncy,
          NULL, NULL, NULL, &graph) != METIS_OK)
      return 4;
    ctrl->mcore = gk_mcoreCreate(0);
    if (ctrl->mcore == NULL || ctrl->mcore->coresize != 0)
      return 5;
    if (kind >= 7) {
      SetupKWayBalMultipliers(ctrl, graph);
      if (AllocateRefinementWorkSpace(ctrl, 128, 128) != METIS_OK ||
          AllocateKWayPartitionMemory(ctrl, graph) != METIS_OK)
        return 6;
      for (i=0; i<64; i++)
        graph->where[i] = i < 32 ? 0 : 1;
      if (ComputeKWayPartitionParams(ctrl, graph) != METIS_OK)
        return 7;
      ComputeKWayBoundary(ctrl, graph, BNDTYPE_REFINE);
    }
    else {
      Setup2WayBalMultipliers(ctrl, graph, targets);
      if (kind <= 1 || kind == 6) {
        if (Allocate2WayPartitionMemory(ctrl, graph) != METIS_OK)
          return 8;
        for (i=0; i<64; i++)
          graph->where[i] = i < 32 ? 0 : 1;
        Compute2WayPartitionParams(ctrl, graph);
      }
    }
    if (ctrl->status != METIS_OK || ctrl->mcore->cmop != 0 ||
        ctrl->mcore->cur_hallocs != 0)
      return 9;
    clobber_cleanup = 1;
    cleanup_clobbers = allocations_after_failure = 0;
    initialization_failed = 0;
    caught = 0;
    FailMcoreAllocation(positions[test] == 0 ? SIZE_MAX : positions[test]);
    if (positions[test] == 0)
      FailMessage("MinCover: flag");
    errno = 0;
    switch (gk_sigcatch()) {
      case 0:
        if (kind == 0)
          FM_2WayCutRefine(ctrl, graph, targets, 1);
        else if (kind == 1)
          FM_Mc2WayCutRefine(ctrl, graph, targets, 1);
        else if (kind == 2)
          RandomBisection(ctrl, graph, targets, 1);
        else if (kind == 3)
          GrowBisection(ctrl, graph, targets, 1);
        else if (kind == 4)
          McRandomBisection(ctrl, graph, targets, 1);
        else if (kind == 5)
          GrowBisectionNode(ctrl, graph, targets, 1);
        else if (kind == 6)
          ConstructMinCoverSeparator(ctrl, graph);
        else if (kind == 7)
          Greedy_KWayCutOptimize(ctrl, graph, 1, 0.0, OMODE_REFINE);
        else if (kind == 8)
          Greedy_KWayVolOptimize(ctrl, graph, 1, 0.0, OMODE_REFINE);
        else if (kind == 9)
          Greedy_McKWayCutOptimize(ctrl, graph, 1, 0.0, OMODE_REFINE);
        else
          Greedy_McKWayVolOptimize(ctrl, graph, 1, 0.0, OMODE_REFINE);
        break;
      default:
        caught = 1;
        break;
    }
    saved_errno = errno;
    AllowAllocations();
    clobber_cleanup = initialization_failed = 0;
    if (caught || saved_errno != ENOMEM || cleanup_clobbers == 0 ||
        allocations_after_failure != 0 ||
        mcore_count != (positions[test] == 0 ? 0 : positions[test]) ||
        (positions[test] == 0 && message_hits != 1) ||
        ctrl->status != METIS_ERROR_MEMORY || gk_cur_jbufs != depth ||
        ctrl->mcore->cmop != 0 || ctrl->mcore->cur_hallocs != 0 ||
        memcmp(xadj, old_xadj, sizeof(xadj)) != 0 ||
        memcmp(adjncy, old_adjncy, sizeof(adjncy)) != 0)
      result = 1;
#ifdef METIS_TEST_POSIX_QUERY
    if (pthread_sigmask(SIG_SETMASK, NULL, &after) != 0 ||
        !SameQueryMask(&before, &after))
      result = 1;
#endif
    fprintf(stderr, "prepared scratch %zu kind %d position %zu: errno %d, frees %zu, status %d, allocations %zu, after-failure %zu, message-hits %zu\n",
        test, kind, positions[test], saved_errno, cleanup_clobbers,
        ctrl->status, mcore_count, allocations_after_failure,
        positions[test] == 0 ? message_hits : 0);
    FreeGraph(&graph);
    FreeCtrl(&ctrl);
    if (gk_GetCurMemoryUsed() != baseline || marker[0] != 0x5a ||
        marker[31] != 0x5a || RunQueryAPI(2, METIS_OBJTYPE_CUT, 0) != METIS_OK)
      result = 1;
  }
  if (!gk_siguntrap() || gk_cur_jbufs != depth-1)
    result = 1;
  gk_free((void **)&marker, LTERM);
  gk_malloc_cleanup(0);
  gk_set_exit_on_error(1);
  return result;
}


/*************************************************************************/
/*! Checks frame failures across real CLI and library constructor boundaries. */
/*************************************************************************/
static int CheckCLIFrameFailures(void)
{
  const int errors[]={EIO, EINVAL, ENOMEM, EOVERFLOW};
  const char *filenames[]={"metis-cli-linked.graph", "metis-cli-linked.mesh"};
  const char *contents[]={"2 1\n2\n1\n", "1\n1 2\n"};
  params_t params;
  graph_t *graph;
  mesh_t *mesh;
  FILE *stream;
  size_t error, baseline;
  int mode, kind, position, status, saved_errno, expected;
  int depth=gk_cur_jbufs, result=0;

  for (kind=0; kind<2; kind++) {
    stream = fopen(filenames[kind], "w");
    if (stream == NULL)
      return 1;
    fputs(contents[kind], stream);
    if (fclose(stream) != 0)
      return 1;
  }
  AllowAllocations();
  if (!gk_malloc_init())
    return 2;
  baseline = gk_GetCurMemoryUsed();
  memset(&params, 0, sizeof(params));
  for (mode=0; mode<=1; mode++) {
    gk_set_exit_on_error(mode);
    for (error=0; error<sizeof(errors)/sizeof(errors[0]); error++) {
      expected = errors[error] == ENOMEM || errors[error] == EOVERFLOW ?
          METIS_ERROR_MEMORY : METIS_ERROR;
      for (kind=0; kind<2; kind++) {
        params.filename = (char *)filenames[kind];
        for (position=1; position<=2; position++) {
          graph = NULL;
          mesh = NULL;
          trap_error = errors[error];
          trap_hits = 0;
          trap_count = 0;
          trap_fail_at = position;
          trap_armed = 1;
          errno = 0;
          if (kind == 0)
            status = ReadGraph(&params, &graph);
          else
            status = ReadMesh(&params, &mesh);
          saved_errno = errno;
          trap_armed = 0;
          initialization_failed = 0;
          if (status != expected || saved_errno != errors[error] ||
              trap_hits != 1 || trap_count != (size_t)position ||
              graph != NULL || mesh != NULL || gk_cur_jbufs != depth ||
              gk_GetCurMemoryUsed() != baseline) {
            fprintf(stderr, "linked CLI mode %d kind %d frame %d: status %d errno %d hits %d\n",
                mode, kind, position, status, saved_errno, trap_hits);
            result = 3;
          }
        }
      }
    }
  }
  for (kind=0; kind<2; kind++) {
    if (remove(filenames[kind]) != 0)
      result = 4;
  }
  gk_malloc_cleanup(0);
  gk_set_exit_on_error(1);
  return result;
}


/*************************************************************************/
/*! Makes termination callbacks distinguishable from a strict failure exit. */
/*************************************************************************/
static void ReleaseExitCallback(void)
{
  fputs("ATEXIT\n", stdout);
}


static void ReleaseAbortHandler(int signum)
{
  (void)signum;
  _Exit(EXIT_SUCCESS);
}


static int CheckReleaseFailure(int mode)
{
  idx_t nvtxs=2, ncon=1, nparts=1, objval=-1;
  idx_t xadj[3]={0, 1, 2}, adjncy[2]={1, 0}, part[2]={-1, -1};
  idx_t options[METIS_NOPTIONS];

  if (atexit(ReleaseExitCallback) != 0 ||
      signal(SIGABRT, ReleaseAbortHandler) == SIG_ERR)
    return EXIT_SUCCESS;
  AllowAllocations();
  gk_set_exit_on_error(mode);
  METIS_SetDefaultOptions(options);
  release_failure = 1;
  METIS_PartGraphKway(&nvtxs, &ncon, xadj, adjncy, NULL, NULL, NULL,
      &nparts, NULL, NULL, options, &objval, part);
  fputs("AFTER\n", stdout);
  return EXIT_FAILURE;
}


int main(int argc, char *argv[])
{
  int status;

  if (argc == 2 && strcmp(argv[1], "exit-status") == 0)
    return EXIT_FAILURE;
  if (argc == 2 && strcmp(argv[1], "cli-frame-failure") == 0)
    return CheckCLIFrameFailures();
  if (argc == 3 && strcmp(argv[1], "release-failure") == 0)
    return CheckReleaseFailure(atoi(argv[2]));
  if (argc == 2 && strcmp(argv[1], "workspace") == 0)
    return CheckWorkspaceFailures();
  if (argc == 2 && strcmp(argv[1], "prepared-scratch") == 0)
    return CheckPreparedScratchFailures();
  if (argc == 2 && strcmp(argv[1], "scratch-cleanup") == 0)
    return CheckScratchCleanupFailures();
  if (argc == 2 && strcmp(argv[1], "frame-cleanup") == 0) {
    status = CheckFrameCleanupFailures();
    return status == 0 ? CheckLargeFrameCleanupFailures() : status;
  }
#ifdef METIS_TEST_POSIX_QUERY
  if (argc == 2 && strcmp(argv[1], "posix-query") == 0) {
    status = CheckQueryFailures();
    return status == 0 ? CheckQueryResizeFailures() : status;
  }
#endif
  if (argc == 3)
    return CheckKWayInitializationFailure(atoi(argv[1]), atoi(argv[2]),
        METIS_OBJTYPE_CUT);
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
  if (status == 0)
    status = CheckKWayInitializationFailures();
  if (status == 0)
    status = CheckScratchCleanupFailures();
  if (status == 0)
    status = CheckPreparedScratchFailures();
  AllowAllocations();
  return status;
}
