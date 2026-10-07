#include "metislib.h"

static int frame_errno, frame_calls;
#if defined(METIS_CLI_FRAME_FILLIN)
static int allocation_failure, allocation_calls, allocation_errno;
static int cleanup_clobbers;


static idx_t *fail_order_allocation(size_t count, char *message)
{
  if (allocation_failure != 0 && ++allocation_calls == allocation_failure) {
    errno = allocation_errno;
    gk_errexit(allocation_errno == ENOMEM || allocation_errno == EOVERFLOW ?
        SIGMEM : SIGERR, "Injected ordering allocation failure");
    errno = allocation_errno;
    return NULL;
  }
  return imalloc(count, message);
}


static void clobber_order_cleanup(void **first, ...)
{
  va_list arguments;
  void **pointer=first;
  int released=0;

  va_start(arguments, first);
  while (pointer != LTERM) {
    if (*pointer != NULL)
      released = 1;
    gk_free(pointer, LTERM);
    pointer = va_arg(arguments, void **);
  }
  va_end(arguments);
  if (allocation_failure != 0 && released) {
    cleanup_clobbers++;
    errno = EACCES;
  }
}
#endif


static int fail_cli_frame(void)
{
#if defined(METIS_CLI_FRAME_FILLIN)
  if (allocation_failure != 0)
    return gk_sigtrap();
#endif
  frame_calls++;
  errno = frame_errno;
  return 0;
}


/* Include the CLI owners so static allocation helpers remain testable without
   a production failpoint or linker-specific interception of their calls. */
#define gk_sigtrap fail_cli_frame
#if defined(METIS_CLI_FRAME_IO)
#include "../apps/io.c"
#elif defined(METIS_CLI_FRAME_FACTOR)
#include "../apps/smbfactor.c"
#else
#undef imalloc
#define imalloc fail_order_allocation
#define gk_free clobber_order_cleanup
#define main metis_cli_cmpfillin_main
#include "../apps/cmpfillin.c"
#undef main
#undef gk_free
#undef imalloc
#endif
#undef gk_sigtrap


#if defined(METIS_CLI_FRAME_FILLIN)
static int check_order_allocations(int error, int position)
{
  idx_t *first=NULL, *second=NULL;
  int depth=gk_cur_jbufs, status, saved_errno;
  size_t baseline=gk_GetCurMemoryUsed();

  allocation_failure = position;
  allocation_errno = error;
  allocation_calls = cleanup_clobbers = 0;
  errno = 0;
  status = AllocateOrderVectors(2, &first, &second);
  saved_errno = errno;
  allocation_failure = 0;
  if (status != 0 || first != NULL || second != NULL || saved_errno != error ||
      allocation_calls != position || cleanup_clobbers != (position == 2) ||
      gk_cur_jbufs != depth || gk_GetCurMemoryUsed() != baseline) {
    fprintf(stderr, "ordering allocation %d: status %d errno %d calls %d cleanup %d\n",
        position, status, saved_errno, allocation_calls, cleanup_clobbers);
    return 19;
  }
  return 0;
}
#endif


static int check_helpers(int error)
{
#if defined(METIS_CLI_FRAME_IO)
  graph_t graph;
  mesh_t mesh;
  idx_t *old_eind;
  real_t *weights;
  size_t baseline;
  int expected_status, status, growth_valid;
#endif
  idx_t *first;
#if !defined(METIS_CLI_FRAME_FACTOR)
  idx_t *second;
  char *name;
#endif
  int expected_errno=error == 0 ? ENOMEM : error;

  frame_errno = error;
  frame_calls = 0;
#if defined(METIS_CLI_FRAME_IO)
  memset(&graph, 0, sizeof(graph));
  memset(&mesh, 0, sizeof(mesh));
  weights = NULL;
  expected_status = expected_errno == ENOMEM || expected_errno == EOVERFLOW ?
      METIS_ERROR_MEMORY : METIS_ERROR;
#endif
  first = NULL;
#if !defined(METIS_CLI_FRAME_FACTOR)
  second = NULL;
  name = NULL;
#endif
#if defined(METIS_CLI_FRAME_FILLIN)
  if (CopyInputFilename((char *)"graph", &name) || name != NULL ||
      errno != expected_errno)
    return 1;
  if (AllocateOrderVectors(2, &first, &second) || first != NULL ||
      second != NULL || errno != expected_errno)
    return 2;
  if (frame_calls != 2)
    return 10;
#elif defined(METIS_CLI_FRAME_IO)
  status = AppendOutputSuffix("graph", ".part", &name);
  if (status != expected_status || name != NULL || errno != expected_errno)
    return 3;
  if (AllocateGraphInput(&graph, 1, 1) || graph.xadj != NULL ||
      graph.adjncy != NULL || graph.vwgt != NULL || graph.adjwgt != NULL ||
      graph.vsize != NULL || errno != expected_errno)
    return 4;
  if (AllocateMeshInput(&mesh, 1, 4) || mesh.eptr != NULL ||
      mesh.eind != NULL || mesh.ewgt != NULL || errno != expected_errno)
    return 5;
  old_eind = imalloc(2, "CLI frame fixture");
  if (old_eind == NULL)
    return 6;
  old_eind[0] = 7;
  old_eind[1] = 9;
  mesh.eind = old_eind;
  baseline = gk_GetCurMemoryUsed();
  growth_valid = !GrowMeshInput(&mesh, 8) && mesh.eind == old_eind &&
      old_eind[0] == 7 && old_eind[1] == 9 && errno == expected_errno &&
      gk_GetCurMemoryUsed() == baseline;
  gk_free((void **)&old_eind, LTERM);
  if (!growth_valid)
    return 6;
  if (AllocateTPwgts(2, &weights) || weights != NULL || errno != expected_errno)
    return 7;
  if (AllocatePOVectors(2, &first, &second) || first != NULL || second != NULL ||
      errno != expected_errno)
    return 8;
  if (frame_calls != 6)
    return 10;
#else
  if (AllocateFactorArray(2, 0, "factor", &first) || first != NULL ||
      errno != expected_errno || frame_calls != 1)
    return 9;
#endif
  return 0;
}


static int check_callers(int error)
{
#if defined(METIS_CLI_FRAME_FILLIN)
  char *arguments[]={(char *)"cmpfillin", (char *)"graph", (char *)"permutation"};
  int expected_errno=error == 0 ? ENOMEM : error;

  frame_errno = error;
  frame_calls = 0;
  return metis_cli_cmpfillin_main(3, arguments) == EXIT_FAILURE &&
      errno == expected_errno && frame_calls == 1 ? 0 : 10;
#else
#if defined(METIS_CLI_FRAME_IO)
  const char *graph_name="metis-cli-frame.graph";
  const char *mesh_name="metis-cli-frame.mesh";
  FILE *stream;
  params_t params;
  graph_t graph, *read_graph=NULL;
  mesh_t *read_mesh=NULL;
#else
  graph_t graph;
#endif
  idx_t xadj[3]={0, 1, 2}, adjncy[2]={1, 0};
#if defined(METIS_CLI_FRAME_IO)
  idx_t output[2]={-1, -1};
#else
  idx_t perm[2]={0, 1}, iperm[2]={0, 1};
  uint64_t fill=UINT64_MAX, operations=UINT64_MAX;
#endif
  int expected_errno=error == 0 ? ENOMEM : error;
  int expected_status=expected_errno == ENOMEM || expected_errno == EOVERFLOW ?
      METIS_ERROR_MEMORY : METIS_ERROR;
  int result=0;

  frame_errno = error;
#if defined(METIS_CLI_FRAME_IO)
  memset(&params, 0, sizeof(params));
#endif
  memset(&graph, 0, sizeof(graph));
  graph.nvtxs = 2;
  graph.nedges = 2;
  graph.ncon = 1;
  graph.xadj = xadj;
  graph.adjncy = adjncy;
  frame_calls = 0;
#if defined(METIS_CLI_FRAME_IO)
  stream = fopen(graph_name, "w");
  if (stream == NULL)
    return 10;
  fputs("2 1\n2\n1\n", stream);
  if (fclose(stream) != 0)
    return 10;
  stream = fopen(mesh_name, "w");
  if (stream == NULL)
    return 11;
  fputs("1\n1 2\n", stream);
  if (fclose(stream) != 0)
    return 11;

  params.filename = (char *)graph_name;
  if (ReadGraph(&params, &read_graph) != expected_status || read_graph != NULL ||
      errno != expected_errno)
    result = 12;
  params.filename = (char *)mesh_name;
  if (ReadMesh(&params, &read_mesh) != expected_status || read_mesh != NULL ||
      errno != expected_errno)
    result = 13;
  params.nparts = 2;
  if (ReadTPwgts(&params, 1) != expected_status || params.tpwgts != NULL ||
      errno != expected_errno)
    result = 14;
  if (ReadPOVector(&graph, (char *)graph_name, output) != expected_status ||
      output[0] != -1 || output[1] != -1 || errno != expected_errno)
    result = 15;
#else
  if (ComputeFillIn(&graph, perm, iperm, &fill, &operations) != expected_status ||
      errno != expected_errno || fill != UINT64_MAX || operations != UINT64_MAX ||
      xadj[0] != 0 || xadj[1] != 1 || xadj[2] != 2 ||
      adjncy[0] != 1 || adjncy[1] != 0 || perm[0] != 0 || perm[1] != 1 ||
      iperm[0] != 0 || iperm[1] != 1)
    result = 16;
#endif
#if defined(METIS_CLI_FRAME_IO)
  if (frame_calls != 4 || remove(graph_name) != 0 || remove(mesh_name) != 0)
    result = 17;
#else
  if (frame_calls != 1)
    result = 17;
#endif
  return result;
#endif
}


int main(void)
{
  const int errors[]={EIO, EINVAL, ENOMEM, EOVERFLOW, 0};
  int mode, depth=gk_cur_jbufs, result;
  size_t i, baseline;

  if (!gk_malloc_init())
    return 1;
  baseline = gk_GetCurMemoryUsed();
  for (mode=0; mode<=1; mode++) {
    gk_set_exit_on_error(mode);
    for (i=0; i<sizeof(errors)/sizeof(errors[0]); i++) {
      result = check_helpers(errors[i]);
      if (result == 0)
        result = check_callers(errors[i]);
#if defined(METIS_CLI_FRAME_FILLIN)
      if (result == 0 && errors[i] != 0)
        result = check_order_allocations(errors[i], 1);
      if (result == 0 && errors[i] != 0)
        result = check_order_allocations(errors[i], 2);
#endif
      if (result != 0 || gk_cur_jbufs != depth ||
          gk_GetCurMemoryUsed() != baseline) {
        fprintf(stderr, "CLI frame mode %d errno %d failed at %d\n",
            mode, errors[i], result);
        return result != 0 ? result : 18;
      }
    }
  }
  gk_malloc_cleanup(0);
  return 0;
}
