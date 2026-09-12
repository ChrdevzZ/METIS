/* METIS public API regression checks. */
#include <metis.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(c) do { if (!(c)) { \
  fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #c); return 1; \
} } while (0)

int main(void)
{
  idx_t n = 6, ncon = 1, nparts = 2, cut, i;
  idx_t xadj[] = {0, 2, 4, 6, 8, 10, 12};
  idx_t adjncy[] = {1, 5, 0, 2, 1, 3, 2, 4, 3, 5, 0, 4};
  idx_t part[6], perm[6], iperm[6], options[METIS_NOPTIONS];
  idx_t ne = 2, nn = 4, eptr[] = {0, 3, 6};
  idx_t eind[] = {0, 1, 2, 1, 2, 3}, ncommon = 2, numflag = 0;
  idx_t *mxadj = NULL, *madjncy = NULL, epart[2], npart[4];
  CHECK(sizeof(idx_t)*8 == IDXTYPEWIDTH);
  CHECK(sizeof(real_t)*8 == REALTYPEWIDTH);
#if IDXTYPEWIDTH == 64
  {
    const idx_t wide = ((idx_t)1 << 32) + 1;
    idx_t wn = 4, wncon = 1, wnparts = 2, wcut, wi;
    idx_t wxadj[] = {0, 2, 4, 6, 8};
    idx_t wadjncy[] = {1, 3, 0, 2, 1, 3, 0, 2};
    idx_t wvwgt[] = {wide, wide + 2, wide + 4, wide + 6};
    idx_t wadjwgt[] = {wide, wide + 2, wide, wide + 4,
                       wide + 2, wide + 6, wide + 4, wide + 6};
    idx_t wpart[4], woptions[METIS_NOPTIONS];

    CHECK(iabs(wide) == wide);
    CHECK(iabs(-wide) == wide);
    CHECK(METIS_SetDefaultOptions(woptions) == METIS_OK);
    woptions[METIS_OPTION_SEED] = 12345;
    CHECK(METIS_PartGraphKway(&wn, &wncon, wxadj, wadjncy, wvwgt, NULL,
        wadjwgt, &wnparts, NULL, NULL, woptions, &wcut, wpart) == METIS_OK);
    CHECK(wcut > (idx_t)INT32_MAX);
    for (wi=0; wi<wn; wi++)
      CHECK(wpart[wi] >= 0 && wpart[wi] < wnparts);
    CHECK(METIS_PartGraphRecursive(&wn, &wncon, wxadj, wadjncy, wvwgt, NULL,
        wadjwgt, &wnparts, NULL, NULL, woptions, &wcut, wpart) == METIS_OK);
    CHECK(wcut > (idx_t)INT32_MAX);
  }
#endif
  CHECK(METIS_SetDefaultOptions(options) == METIS_OK);
  options[METIS_OPTION_SEED] = 12345;
  CHECK(METIS_PartGraphKway(&n, &ncon, xadj, adjncy, NULL, NULL, NULL,
      &nparts, NULL, NULL, options, &cut, part) == METIS_OK);
  CHECK(cut >= 0 && cut <= 6);
  for (i=0; i<n; i++)
    CHECK(part[i] >= 0 && part[i] < nparts);
  CHECK(METIS_PartGraphRecursive(&n, &ncon, xadj, adjncy, NULL, NULL, NULL,
      &nparts, NULL, NULL, options, &cut, part) == METIS_OK);
  CHECK(METIS_NodeND(&n, xadj, adjncy, NULL, options, perm, iperm) == METIS_OK);
  for (i=0; i<n; i++) {
    CHECK(perm[i] >= 0 && perm[i] < n);
    CHECK(iperm[perm[i]] == i);
  }
  CHECK(METIS_MeshToDual(&ne, &nn, eptr, eind, &ncommon, &numflag,
      &mxadj, &madjncy) == METIS_OK);
  CHECK(mxadj[ne] == 2);
  CHECK(METIS_Free(mxadj) == METIS_OK);
  CHECK(METIS_Free(madjncy) == METIS_OK);
  CHECK(METIS_MeshToNodal(&ne, &nn, eptr, eind, &numflag,
      &mxadj, &madjncy) == METIS_OK);
  CHECK(mxadj[nn] == 10);
  METIS_Free(mxadj);
  METIS_Free(madjncy);
  CHECK(METIS_PartMeshDual(&ne, &nn, eptr, eind, NULL, NULL, &ncommon,
      &nparts, NULL, options, &cut, epart, npart) == METIS_OK);
  CHECK(METIS_PartMeshNodal(&ne, &nn, eptr, eind, NULL, NULL, &nparts,
      NULL, options, &cut, epart, npart) == METIS_OK);
  options[METIS_OPTION_OBJTYPE] = 42;
  CHECK(METIS_PartGraphKway(&n, &ncon, xadj, adjncy, NULL, NULL, NULL,
      &nparts, NULL, NULL, options, &cut, part) == METIS_ERROR_INPUT);
  return 0;
}
