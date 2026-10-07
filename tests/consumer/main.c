#include <metis.h>
#include <limits.h>

static int metis_consumer_run(void)
{
  idx_t options[METIS_NOPTIONS];
  idx_t nvtxs=4, ncon=1, nparts=2, objval, i, j, cuts=0;
  idx_t xadj[] = {0, 2, 4, 6, 8};
  idx_t adjncy[] = {1, 3, 0, 2, 1, 3, 0, 2};
  idx_t part[4], perm[4], iperm[4], counts[] = {0, 0};
  idx_t ne=2, nn=3, numflag=0;
  idx_t eptr[] = {0, 2, 4}, eind[] = {0, 1, 1, 2};
  idx_t *mesh_xadj=NULL, *mesh_adjncy=NULL;
  int status;

  if (sizeof(idx_t)*CHAR_BIT != IDXTYPEWIDTH ||
      sizeof(real_t)*CHAR_BIT != REALTYPEWIDTH ||
      METIS_SetDefaultOptions(options) != METIS_OK)
    return 1;
  options[METIS_OPTION_SEED] = 12345;
  status = METIS_PartGraphKway(&nvtxs, &ncon, xadj, adjncy, NULL, NULL,
      NULL, &nparts, NULL, NULL, options, &objval, part);
  if (status != METIS_OK)
    return 2;
  for (i=0; i<nvtxs; i++) {
    if (part[i] < 0 || part[i] >= nparts)
      return 3;
    counts[part[i]]++;
    for (j=xadj[i]; j<xadj[i+1]; j++)
      cuts += part[i] != part[adjncy[j]];
  }
  if (objval != cuts/2 || counts[0] != 2 || counts[1] != 2)
    return 4;
  status = METIS_NodeND(&nvtxs, xadj, adjncy, NULL, options, perm, iperm);
  if (status != METIS_OK)
    return 5;
  for (i=0; i<nvtxs; i++) {
    if (perm[i] < 0 || perm[i] >= nvtxs || iperm[perm[i]] != i)
      return 6;
  }
  status = METIS_MeshToNodal(&ne, &nn, eptr, eind, &numflag,
      &mesh_xadj, &mesh_adjncy);
  if (status == METIS_OK && (mesh_xadj == NULL || mesh_adjncy == NULL ||
      mesh_xadj[0] != 0 || mesh_xadj[nn] != 4))
    status = METIS_ERROR;
  METIS_Free(mesh_xadj);
  METIS_Free(mesh_adjncy);
  return status == METIS_OK ? 0 : 7;
}


#ifndef METIS_CONSUMER_NO_MAIN
int main(void)
{
  return metis_consumer_run();
}
#endif
