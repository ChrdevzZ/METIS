#include <metis.h>

#include <stdio.h>

#ifdef INTEROP_WITH_FORTRAN
extern int interop_fortran_test(void);
#endif

static int test_c_api(void)
{
  idx_t nvtxs = 4, ncon = 1, nparts = 2, edgecut, i;
  idx_t xadj[] = {0, 2, 4, 6, 8};
  idx_t adjncy[] = {1, 3, 0, 2, 1, 3, 0, 2};
  idx_t vwgt[] = {1, 1, 1, 1};
  idx_t adjwgt[] = {1, 1, 1, 1, 1, 1, 1, 1};
  idx_t part[4], partition_weights[] = {0, 0};
  idx_t perm[4], iperm[4], options[METIS_NOPTIONS];
  real_t tpwgts[] = {(real_t)0.25, (real_t)0.75};
  real_t ubvec[] = {(real_t)1.05};
  idx_t ne = 2, nn = 4, numflag = 0;
  idx_t eptr[] = {0, 3, 6};
  idx_t eind[] = {0, 1, 2, 1, 2, 3};
  idx_t *mesh_xadj = NULL, *mesh_adjncy = NULL;
  int xadj_status, adjncy_status;

  if (sizeof(idx_t)*8 != IDXTYPEWIDTH || sizeof(real_t)*8 != REALTYPEWIDTH)
    return 1;
  if (METIS_SetDefaultOptions(options) != METIS_OK)
    return 2;
  if (METIS_PartGraphKway(&nvtxs, &ncon, xadj, adjncy, vwgt, NULL,
      adjwgt, &nparts, tpwgts, ubvec, options, &edgecut, part) != METIS_OK)
    return 3;
  for (i=0; i<nvtxs; i++) {
    if (part[i] < 0 || part[i] >= nparts)
      return 4;
    partition_weights[part[i]] += vwgt[i];
  }
  if (partition_weights[0] != 1 || partition_weights[1] != 3)
    return 5;
  if (METIS_NodeND(&nvtxs, xadj, adjncy, vwgt, options,
      perm, iperm) != METIS_OK)
    return 6;
  for (i=0; i<nvtxs; i++) {
    if (perm[i] < 0 || perm[i] >= nvtxs || iperm[perm[i]] != i)
      return 7;
  }

  if (METIS_MeshToNodal(&ne, &nn, eptr, eind, &numflag,
      &mesh_xadj, &mesh_adjncy) != METIS_OK)
    return 8;
  if (mesh_xadj == NULL || mesh_adjncy == NULL || mesh_xadj[nn] != 10) {
    if (mesh_xadj != NULL)
      METIS_Free(mesh_xadj);
    if (mesh_adjncy != NULL)
      METIS_Free(mesh_adjncy);
    return 9;
  }
  xadj_status = METIS_Free(mesh_xadj);
  adjncy_status = METIS_Free(mesh_adjncy);
  if (xadj_status != METIS_OK || adjncy_status != METIS_OK)
    return 10;

  return 0;
}

int main(void)
{
  int status = test_c_api();
  if (status != 0) {
    fprintf(stderr, "C interop check failed: %d\n", status);
    return status;
  }
#ifdef INTEROP_WITH_FORTRAN
  status = interop_fortran_test();
  if (status != 0) {
    fprintf(stderr, "Fortran interop check failed: %d\n", status);
    return status;
  }
#endif
  return 0;
}
