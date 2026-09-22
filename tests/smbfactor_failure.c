/*
 * Copyright 1997-2011, Regents of the University of Minnesota
 *
 * smbfactor_failure.c
 *
 * Regression checks for symbolic-factorization failure recovery.
 */

#define METIS_SMBFACTOR_TEST
#include "../apps/smbfactor.c"


int main(void)
{
  graph_t graph;
  graph_t dimension_graph;
  graph_t edgeless_graph;
  graph_t invalid_graph;
  graph_t large_graph;
  idx_t xadj[] = {0, 2, 4, 6, 8};
  idx_t adjncy[] = {1, 3, 0, 2, 1, 3, 0, 2};
  idx_t original_xadj[5], original_adjncy[8];
  idx_t perm[] = {0, 1, 2, 3};
  idx_t iperm[] = {0, 1, 2, 3};
  idx_t original_perm[4], original_iperm[4];
  idx_t edgeless_xadj[] = {0, 0, 0, 0};
  idx_t edgeless_perm[] = {0, 1, 2};
  idx_t edgeless_iperm[] = {0, 1, 2};
  idx_t invalid_xadj[] = {0, IDX_MAX, 0};
  idx_t permutation_xadj[] = {0, 0, 0};
  idx_t invalid_perm[] = {0, IDX_MAX};
  idx_t invalid_iperm[] = {0, 1};
  idx_t mismatch_perm[] = {0, 1};
  idx_t mismatch_iperm[] = {1, 0};
  idx_t valid_perm[] = {0, 1};
  idx_t valid_iperm[] = {0, 1};
  idx_t large_xadj[] = {0, (IDX_MAX-1)/8+1};
  idx_t large_adjncy[] = {0};
  idx_t large_perm[] = {0};
  idx_t large_iperm[] = {0};
  idx_t factor_xadj[] = {1, 2, 3};
  idx_t factor_adjncy[] = {2, 1};
  idx_t factor_perm[] = {1, 2};
  idx_t factor_iperm[] = {1, 2};
  idx_t factor_xlnz[4], factor_xnzsub[4], factor_nzsub[3];
  idx_t factor_maxlnz, factor_maxsub;
  uint64_t maxlnz, opc;
  int allocation;

  memset(&graph, 0, sizeof(graph));
  graph.nvtxs = 4;
  graph.nedges = 8;
  graph.xadj = xadj;
  graph.adjncy = adjncy;
  memcpy(original_xadj, xadj, sizeof(xadj));
  memcpy(original_adjncy, adjncy, sizeof(adjncy));
  memcpy(original_perm, perm, sizeof(perm));
  memcpy(original_iperm, iperm, sizeof(iperm));

  if (!gk_malloc_init())
    return 1;
  gk_set_exit_on_error(1);

  factor_maxlnz = factor_maxsub = -1;
  if (smbfct(0, NULL, NULL, NULL, NULL, NULL, &factor_maxlnz, NULL, NULL,
          &factor_maxsub) != 0 || factor_maxlnz != 0 ||
      factor_maxsub != 0 || gk_GetCurMemoryUsed() != 0) {
    gk_malloc_cleanup(0);
    return 2;
  }

  factor_maxlnz = factor_maxsub = -1;
  errno = 0;
  if (smbfct(IDX_MAX, NULL, NULL, NULL, NULL, NULL, &factor_maxlnz, NULL,
          NULL, &factor_maxsub) != -1 || errno != EOVERFLOW ||
      factor_maxlnz != -1 || factor_maxsub != -1 ||
      gk_GetCurMemoryUsed() != 0) {
    gk_malloc_cleanup(0);
    return 3;
  }

  memset(&edgeless_graph, 0, sizeof(edgeless_graph));
  edgeless_graph.nvtxs = 3;
  edgeless_graph.xadj = edgeless_xadj;
  maxlnz = opc = UINT64_MAX;
  metis_smbfactor_test_fail_allocation(0);
  if (ComputeFillIn(&edgeless_graph, edgeless_perm, edgeless_iperm, &maxlnz,
          &opc) != METIS_OK || maxlnz != 0 || opc != 0 ||
      edgeless_xadj[0] != 0 || edgeless_xadj[1] != 0 ||
      edgeless_xadj[2] != 0 || edgeless_xadj[3] != 0 ||
      edgeless_perm[0] != 0 || edgeless_perm[1] != 1 ||
      edgeless_perm[2] != 2 || edgeless_iperm[0] != 0 ||
      edgeless_iperm[1] != 1 || edgeless_iperm[2] != 2 ||
      gk_GetCurMemoryUsed() != 0) {
    gk_malloc_cleanup(0);
    return 4;
  }

  memset(&large_graph, 0, sizeof(large_graph));
  large_graph.nvtxs = 1;
  large_graph.xadj = large_xadj;
  large_graph.adjncy = large_adjncy;
  maxlnz = opc = UINT64_MAX;
  errno = 0;
  if (ComputeFillIn(&large_graph, large_perm, large_iperm, &maxlnz, &opc) !=
          METIS_ERROR_MEMORY || errno != EOVERFLOW ||
      large_xadj[0] != 0 || large_xadj[1] != (IDX_MAX-1)/8+1 ||
      large_perm[0] != 0 || large_iperm[0] != 0 ||
      maxlnz != UINT64_MAX || opc != UINT64_MAX ||
      gk_GetCurMemoryUsed() != 0) {
    gk_malloc_cleanup(0);
    return 5;
  }

  memset(&dimension_graph, 0, sizeof(dimension_graph));
  dimension_graph.nvtxs = IDX_MAX;
  dimension_graph.xadj = large_xadj;
  maxlnz = opc = UINT64_MAX;
  errno = 0;
  if (ComputeFillIn(&dimension_graph, large_perm, large_iperm, &maxlnz,
          &opc) != METIS_ERROR_MEMORY || errno != EOVERFLOW ||
      large_xadj[0] != 0 || large_xadj[1] != (IDX_MAX-1)/8+1 ||
      large_perm[0] != 0 || large_iperm[0] != 0 ||
      maxlnz != UINT64_MAX || opc != UINT64_MAX ||
      gk_GetCurMemoryUsed() != 0) {
    gk_malloc_cleanup(0);
    return 6;
  }

  memset(&invalid_graph, 0, sizeof(invalid_graph));
  invalid_graph.nvtxs = 2;
  invalid_graph.xadj = invalid_xadj;
  maxlnz = opc = UINT64_MAX;
  if (ComputeFillIn(&invalid_graph, valid_perm, valid_iperm, &maxlnz,
          &opc) != METIS_ERROR_INPUT || invalid_xadj[0] != 0 ||
      invalid_xadj[1] != IDX_MAX || invalid_xadj[2] != 0 ||
      valid_perm[0] != 0 || valid_perm[1] != 1 ||
      valid_iperm[0] != 0 || valid_iperm[1] != 1 ||
      maxlnz != UINT64_MAX || opc != UINT64_MAX ||
      gk_GetCurMemoryUsed() != 0) {
    gk_malloc_cleanup(0);
    return 7;
  }

  invalid_graph.xadj = permutation_xadj;
  maxlnz = opc = UINT64_MAX;
  if (ComputeFillIn(&invalid_graph, invalid_perm, invalid_iperm, &maxlnz,
          &opc) != METIS_ERROR_INPUT || permutation_xadj[0] != 0 ||
      permutation_xadj[1] != 0 || permutation_xadj[2] != 0 ||
      invalid_perm[0] != 0 || invalid_perm[1] != IDX_MAX ||
      invalid_iperm[0] != 0 || invalid_iperm[1] != 1 ||
      maxlnz != UINT64_MAX || opc != UINT64_MAX ||
      gk_GetCurMemoryUsed() != 0) {
    gk_malloc_cleanup(0);
    return 8;
  }

  invalid_graph.xadj = permutation_xadj;
  maxlnz = opc = UINT64_MAX;
  if (ComputeFillIn(&invalid_graph, mismatch_perm, mismatch_iperm, &maxlnz,
          &opc) != METIS_ERROR_INPUT || permutation_xadj[0] != 0 ||
      permutation_xadj[1] != 0 || permutation_xadj[2] != 0 ||
      mismatch_perm[0] != 0 || mismatch_perm[1] != 1 ||
      mismatch_iperm[0] != 1 || mismatch_iperm[1] != 0 ||
      maxlnz != UINT64_MAX || opc != UINT64_MAX ||
      gk_GetCurMemoryUsed() != 0) {
    gk_malloc_cleanup(0);
    return 9;
  }

  memset(factor_xlnz, 0, sizeof(factor_xlnz));
  memset(factor_xnzsub, 0, sizeof(factor_xnzsub));
  memset(factor_nzsub, 0, sizeof(factor_nzsub));
  factor_maxlnz = -1;
  factor_maxsub = 1;
  if (smbfct(2, factor_xadj, factor_adjncy, factor_perm, factor_iperm,
          factor_xlnz, &factor_maxlnz, factor_xnzsub, factor_nzsub,
          &factor_maxsub) != 1 || factor_maxlnz != -1 ||
      factor_maxsub != 1 || gk_GetCurMemoryUsed() != 0) {
    gk_malloc_cleanup(0);
    return 10;
  }

  memset(factor_xlnz, 0, sizeof(factor_xlnz));
  memset(factor_xnzsub, 0, sizeof(factor_xnzsub));
  memset(factor_nzsub, 0, sizeof(factor_nzsub));
  factor_maxlnz = -1;
  factor_maxsub = 2;
  if (smbfct(2, factor_xadj, factor_adjncy, factor_perm, factor_iperm,
          factor_xlnz, &factor_maxlnz, factor_xnzsub, factor_nzsub,
          &factor_maxsub) != 0 || factor_maxlnz != 1 ||
      factor_maxsub != 1 || gk_GetCurMemoryUsed() != 0) {
    gk_malloc_cleanup(0);
    return 11;
  }

  for (allocation=1; allocation<=6; allocation++) {
    maxlnz = opc = UINT64_MAX;
    errno = 0;
    metis_smbfactor_test_fail_allocation(allocation);
    if (ComputeFillIn(&graph, perm, iperm, &maxlnz, &opc) !=
            METIS_ERROR_MEMORY || errno != ENOMEM ||
        memcmp(xadj, original_xadj, sizeof(xadj)) != 0 ||
        memcmp(adjncy, original_adjncy, sizeof(adjncy)) != 0 ||
        memcmp(perm, original_perm, sizeof(perm)) != 0 ||
        memcmp(iperm, original_iperm, sizeof(iperm)) != 0 ||
        maxlnz != UINT64_MAX || opc != UINT64_MAX ||
        gk_GetCurMemoryUsed() != 0) {
      gk_malloc_cleanup(0);
      return allocation+11;
    }
  }
  gk_malloc_cleanup(0);
  return 0;
}
