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


void *__real_gk_malloc(size_t nbytes, const char *msg);


/*************************************************************************/
/*! Injects an allocation failure selected by its diagnostic string. */
/*************************************************************************/
void *__wrap_gk_malloc(size_t nbytes, const char *msg)
{
  if (failed_message != NULL && strcmp(msg, failed_message) == 0) {
    errno = ENOMEM;
    gk_errexit(SIGMEM, "Injected result-reporting allocation failure");
    errno = ENOMEM;
    return NULL;
  }

  return __real_gk_malloc(nbytes, msg);
}


/*************************************************************************/
/*! Requires a failed report to leave stdout empty and release its work. */
/*************************************************************************/
static int CheckReportingFailureOutput(params_t *params, graph_t *graph,
    idx_t *where, const char *message)
{
  FILE *capture;
  long output_size;
  int saved_stdout, status, stdout_fd;

  capture = tmpfile();
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
  status = ComputePartitionInfo(params, graph, where);
  failed_message = NULL;
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

  if (status != METIS_ERROR_MEMORY)
    return 5;
  if (output_size != 0)
    return 6;
  if (gk_GetCurMemoryUsed() != 0)
    return 7;
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
    status = CheckReportingFailureOutput(&params, &graph, where, messages[i]);
    if (status != 0)
      return 10+10*(int)i+status;
  }

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
  int status;

  if (!gk_malloc_init())
    return 1;
  status = CheckReportingFailures(0);
  if (status == 0)
    status = CheckReportingFailures(1);
  if (status == 0)
    status = CheckConnectivityFailures(0);
  if (status == 0)
    status = CheckConnectivityFailures(1);
  failed_message = NULL;
  gk_set_exit_on_error(1);
  gk_malloc_cleanup(0);

  return status;
}
