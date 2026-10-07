/*
 * Copyright 1997-2011, Regents of the University of Minnesota
 *
 * stat_failure.c
 *
 * Allocation-failure checks for application result reporting.
 */

#include "metisbin.h"

#ifdef _WIN32
#include <io.h>
#define TEST_CLOSE _close
#define TEST_DUP _dup
#define TEST_DUP2 _dup2
#define TEST_FILENO _fileno
#else
#include <unistd.h>
#define TEST_CLOSE close
#define TEST_DUP dup
#define TEST_DUP2 dup2
#define TEST_FILENO fileno
#endif


static const char *failed_message;
static int failed_errno=ENOMEM, failure_seen, clobber_cleanup;
static size_t allocations_after_failure, cleanup_clobbers;


static FILE *OpenCapture(void)
{
#ifdef _WIN32
  return tmpfile();
#else
  char name[] = "stat-failure-XXXXXX";
  FILE *stream;
  int fd = mkstemp(name);

  if (fd < 0)
    return NULL;
  if (unlink(name) != 0) {
    close(fd);
    return NULL;
  }
  stream = fdopen(fd, "w+b");
  if (stream == NULL)
    close(fd);
  return stream;
#endif
}


void *__real_gk_malloc(size_t nbytes, const char *msg);
void __real_free(void *ptr);


/*************************************************************************/
/*! Injects an allocation failure selected by its diagnostic string. */
/*************************************************************************/
void *__wrap_gk_malloc(size_t nbytes, const char *msg)
{
  if (failure_seen)
    allocations_after_failure++;
  if (failed_message != NULL && strcmp(msg, failed_message) == 0) {
    failure_seen = 1;
    errno = failed_errno;
    gk_errexit(failed_errno == EIO ? SIGERR : SIGMEM,
        "Injected result-reporting allocation failure");
    errno = failed_errno;
    return NULL;
  }

  return __real_gk_malloc(nbytes, msg);
}


/*************************************************************************/
/*! Allows successful cleanup to change errno after the selected failure. */
/*************************************************************************/
void __wrap_free(void *ptr)
{
  int should_clobber = failure_seen && clobber_cleanup && ptr != NULL;

  __real_free(ptr);
  if (should_clobber) {
    cleanup_clobbers++;
    errno = EACCES;
  }
}


/*************************************************************************/
/*! Requires a failed report to leave stdout empty and release its work. */
/*************************************************************************/
static int CheckReportingFailureOutput(params_t *params, graph_t *graph,
    idx_t *where, const char *message, int bipartite)
{
  FILE *capture;
  long output_size;
  int saved_stdout, status, stdout_fd, saved_errno, expected;
  idx_t *old_vwgt=graph->vwgt, *old_adjwgt=graph->adjwgt;

  /* Create the POSIX capture exclusively in the binary working directory. */
  capture = OpenCapture();
  if (capture == NULL)
    return 1;
  if (fflush(stdout) != 0) {
    fclose(capture);
    return 2;
  }

  stdout_fd = TEST_FILENO(stdout);
  saved_stdout = TEST_DUP(stdout_fd);
  if (saved_stdout < 0 || TEST_DUP2(TEST_FILENO(capture), stdout_fd) < 0) {
    if (saved_stdout >= 0)
      TEST_CLOSE(saved_stdout);
    fclose(capture);
    return 3;
  }

  failed_message = message;
  failure_seen = 0;
  allocations_after_failure = cleanup_clobbers = 0;
  errno = 0;
  status = METIS_OK;
  if (bipartite)
    ComputePartitionInfoBipartite(graph, params->nparts, where);
  else
    status = ComputePartitionInfo(params, graph, where);
  saved_errno = errno;
  failed_message = NULL;
  failure_seen = 0;
  if (fflush(stdout) != 0 || fseek(capture, 0, SEEK_END) != 0)
    output_size = -1;
  else
    output_size = ftell(capture);

  if (TEST_DUP2(saved_stdout, stdout_fd) < 0) {
    TEST_CLOSE(saved_stdout);
    fclose(capture);
    return 4;
  }
  TEST_CLOSE(saved_stdout);
  fclose(capture);

  expected = failed_errno == EIO ? METIS_ERROR : METIS_ERROR_MEMORY;
  if ((!bipartite && status != expected) || saved_errno != failed_errno) {
    fprintf(stderr, "report %s: status %d errno %d, expected %d/%d\n",
        message, status, saved_errno, expected, failed_errno);
    return 5;
  }
  if (output_size != 0)
    return 6;
  if (gk_GetCurMemoryUsed() != 0)
    return 7;
  if (allocations_after_failure != 0) {
    fprintf(stderr, "report %s: %zu allocations after failure\n",
        message, allocations_after_failure);
    return 8;
  }
  if (graph->vwgt != old_vwgt || graph->adjwgt != old_adjwgt)
    return 9;
  if (!bipartite && clobber_cleanup &&
      strcmp(message, "ComputeVolume: marker") != 0 &&
      strcmp(message, "ComputePartitionInfo: kpwgts") != 0 &&
      cleanup_clobbers == 0)
    return 10;
  return 0;
}


/*************************************************************************/
/*! Checks each result-reporting allocation in the requested error mode. */
/*************************************************************************/
static int CheckReportingFailures(int exit_on_error)
{
  static const char *messages[] = {
    "ComputeVolume: marker",
    "ComputePartitionInfo: kpwgts",
    "ComputePartitionInfo: pptr",
    "ComputePartitionInfo: pind",
    "ComputePartitionInfo: pdom",
    "ComputePartitionInfo: cptr",
    "ComputePartitionInfo: cind",
    "ComputePartitionInfo: cpwgts",
    "FindPartitionInducedComponents: cptr",
    "FindPartitionInducedComponents: cind",
    "FindPartitionInducedComponents: where",
    "FindPartitionInducedComponents: perm",
    "FindPartitionInducedComponents: todo",
    "FindPartitionInducedComponents: touched"
  };
  graph_t graph;
  params_t params;
  idx_t xadj[] = {0, 1, 2};
  idx_t adjncy[] = {1, 0};
  idx_t vwgt[] = {1, 1};
  idx_t vsize[] = {1, 1};
  idx_t adjwgt[] = {1, 1};
  idx_t where[] = {0, 0};
  real_t tpwgts[] = {0.5, 0.5};
  size_t i;
  int status;

  memset(&graph, 0, sizeof(graph));
  memset(&params, 0, sizeof(params));
  graph.nvtxs = 2;
  graph.ncon = 1;
  graph.xadj = xadj;
  graph.adjncy = adjncy;
  graph.vwgt = vwgt;
  graph.vsize = vsize;
  graph.adjwgt = adjwgt;
  params.nparts = 2;
  params.tpwgts = tpwgts;

  gk_set_exit_on_error(exit_on_error);
  for (i=0; i<sizeof(messages)/sizeof(messages[0]); i++) {
    status = CheckReportingFailureOutput(&params, &graph, where, messages[i], 0);
    if (status != 0)
      return 10+10*(int)i+status;
  }
  status = CheckReportingFailureOutput(&params, &graph, where, messages[0], 1);
  if (status != 0)
    return 160+status;
  graph.vwgt = graph.adjwgt = NULL;
  status = CheckReportingFailureOutput(&params, &graph, where, messages[0], 1);
  if (status != 0)
    return 170+status;

  return 0;
}


/*************************************************************************/
/*! Checks helper failure sentinels and empty induced vertex sets. */
/*************************************************************************/
static int CheckConnectivityFailures(int exit_on_error)
{
  static const char *messages[] = {
    "IsConnected: touched",
    "IsConnected: queue",
    "IsConnected: cptr"
  };
  graph_t graph;
  ctrl_t ctrl;
  idx_t xadj[] = {0, 1, 2};
  idx_t adjncy[] = {1, 0};
  idx_t vwgt[] = {1, 1};
  idx_t where[] = {2, 2};
  idx_t cptr[3], cind[2];
  size_t i;

  memset(&graph, 0, sizeof(graph));
  memset(&ctrl, 0, sizeof(ctrl));
  graph.nvtxs = 2;
  graph.ncon = 1;
  graph.xadj = xadj;
  graph.adjncy = adjncy;
  graph.vwgt = vwgt;
  graph.where = where;

  gk_set_exit_on_error(exit_on_error);
  failed_message = "FindPartitionInducedComponents: perm";
  if (IsConnected(&graph, 0) != -1)
    return 30;
  failed_message = "ComputeMaxCut: cuts";
  if (ComputeMaxCut(&graph, 3, where) != -1)
    return 31;
  failed_message = NULL;

  if (IsConnectedSubdomain(&ctrl, &graph, 0, 0) != 0)
    return 32;
  if (FindSepInducedComponents(&ctrl, &graph, cptr, cind) != 0 ||
      cptr[0] != 0)
    return 33;

  where[0] = 0;
  for (i=0; i<sizeof(messages)/sizeof(messages[0]); i++) {
    ctrl.status = METIS_OK;
    failed_message = messages[i];
    if (IsConnectedSubdomain(&ctrl, &graph, 0, 0) != -1 ||
        ctrl.status != METIS_ERROR_MEMORY)
      return 35+(int)i;
    failed_message = NULL;
    if (gk_GetCurMemoryUsed() != 0)
      return 40+(int)i;
  }
  where[1] = 0;
  ctrl.status = METIS_OK;
  failed_message = "IsConnected: queue";
  if (FindSepInducedComponents(&ctrl, &graph, cptr, cind) != -1 ||
      ctrl.status != METIS_ERROR_MEMORY)
    return 44;
  failed_message = NULL;
  if (gk_GetCurMemoryUsed() != 0)
    return 34;

  return 0;
}


int main(void)
{
  int errors[] = {ENOMEM, EOVERFLOW, EIO};
  int status=0, mode, cleanup;
  size_t error;

  if (!gk_malloc_init())
    return 1;
  for (cleanup=0; cleanup<2 && status == 0; cleanup++) {
    clobber_cleanup = cleanup;
    for (mode=0; mode<2 && status == 0; mode++) {
      for (error=0; error<sizeof(errors)/sizeof(errors[0]) && status == 0; error++) {
        failed_errno = errors[error];
        status = CheckReportingFailures(mode);
      }
    }
  }
  failed_errno = ENOMEM;
  clobber_cleanup = 0;
  if (status == 0)
    status = CheckConnectivityFailures(0);
  if (status == 0)
    status = CheckConnectivityFailures(1);
  failed_message = NULL;
  gk_set_exit_on_error(1);
  gk_malloc_cleanup(0);

  return status;
}
