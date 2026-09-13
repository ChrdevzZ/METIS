/* METIS public API regression checks. */
#include <metis.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(c) do { if (!(c)) { \
  fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #c); return 1; \
} } while (0)

static int CheckGraph(idx_t nvtxs, idx_t nedges, const idx_t *xadj,
    const idx_t *adjncy, const idx_t *adjwgt)
{
  idx_t i, j, k;

  if (nvtxs < 0 || nedges < 0 || xadj[0] != 0 || xadj[nvtxs] != nedges)
    return 0;

  for (i=0; i<nvtxs; i++) {
    if (xadj[i] < 0 || xadj[i] > xadj[i+1] || xadj[i+1] > nedges)
      return 0;
  }

  for (i=0; i<nvtxs; i++) {
    for (j=xadj[i]; j<xadj[i+1]; j++) {
      if (adjncy[j] < 0 || adjncy[j] >= nvtxs || adjncy[j] == i)
        return 0;

      for (k=xadj[i]; k<j; k++) {
        if (adjncy[k] == adjncy[j])
          return 0;
      }

      for (k=xadj[adjncy[j]]; k<xadj[adjncy[j]+1]; k++) {
        if (adjncy[k] == i && (!adjwgt || adjwgt[k] == adjwgt[j]))
          break;
      }
      if (k == xadj[adjncy[j]+1])
        return 0;
    }
  }

  return 1;
}


static int CheckPartition(idx_t nvtxs, idx_t nparts, const idx_t *xadj,
    const idx_t *adjncy, const idx_t *adjwgt, const idx_t *part, idx_t cut)
{
  idx_t i, j, edgewgt, actual = 0;

  for (i=0; i<nvtxs; i++)
    if (part[i] < 0 || part[i] >= nparts)
      return 0;

  for (i=0; i<nvtxs; i++) {
    for (j=xadj[i]; j<xadj[i+1]; j++) {
      if (i < adjncy[j] && part[i] != part[adjncy[j]]) {
        edgewgt = adjwgt ? adjwgt[j] : 1;
        if (edgewgt < 0 || actual > IDX_MAX-edgewgt)
          return 0;
        actual += edgewgt;
      }
    }
  }

  return actual == cut;
}


static int CheckAdjacencySets(idx_t nvtxs, idx_t nedges, const idx_t *xadj,
    const idx_t *adjncy, const int *expected)
{
  idx_t i, j, k, degree;

  if (!CheckGraph(nvtxs, nedges, xadj, adjncy, NULL))
    return 0;

  for (i=0; i<nvtxs; i++) {
    degree = 0;
    for (k=0; k<nvtxs; k++)
      degree += expected[i*nvtxs+k] != 0;
    if (xadj[i+1]-xadj[i] != degree)
      return 0;

    for (j=xadj[i]; j<xadj[i+1]; j++) {
      if (!expected[i*nvtxs+adjncy[j]])
        return 0;
    }

    for (k=0; k<nvtxs; k++) {
      if (!expected[i*nvtxs+k])
        continue;
      for (j=xadj[i]; j<xadj[i+1]; j++) {
        if (adjncy[j] == k)
          break;
      }
      if (j == xadj[i+1])
        return 0;
    }
  }

  return 1;
}


int main(void)
{
  idx_t n = 6, ncon = 1, nparts = 2, cut, i;
  idx_t xadj[] = {0, 2, 4, 6, 8, 10, 12};
  idx_t adjncy[] = {1, 5, 0, 2, 1, 3, 2, 4, 3, 5, 0, 4};
  idx_t part[6], perm[6], iperm[6], options[METIS_NOPTIONS];
  idx_t ne = 2, nn = 4, eptr[] = {0, 3, 6};
  idx_t eind[] = {0, 1, 2, 1, 2, 3}, ncommon = 2, numflag = 0;
  idx_t *mxadj = NULL, *madjncy = NULL, epart[2], npart[4];
  const int dual_adjacency[] = {
    0, 1,
    1, 0
  };
  const int nodal_adjacency[] = {
    0, 1, 1, 0,
    1, 0, 1, 1,
    1, 1, 0, 1,
    0, 1, 1, 0
  };

  CHECK(sizeof(idx_t)*8 == IDXTYPEWIDTH);
  CHECK(sizeof(real_t)*8 == REALTYPEWIDTH);
  CHECK(CheckGraph(n, 12, xadj, adjncy, NULL));
  {
#if IDXTYPEWIDTH == 64
    const idx_t unit = (idx_t)1 << 30;
#else
    const idx_t unit = 16;
#endif
    idx_t rxadj[] = {0, 1, 2, 2, 4};
    idx_t radjncy[] = {3, 3, 0, 1};
    idx_t rvwgt[] = {1, unit, 3*unit+2, unit};
    idx_t rwhere[] = {0, 1, 1, 2};
    idx_t hmarker[] = {2, 2, 2, 0};
    idx_t weights[] = {0, 0, 0};

    /* Moving vertex 3 preserves separator weight while improving balance.
       In the 64-bit case, both weight differences exceed a signed int. */
    CHECK(CheckGraph(4, 4, rxadj, radjncy, NULL));
    CHECK(METIS_NodeRefine(4, rxadj, rvwgt, radjncy, rwhere, hmarker,
        (real_t)1.03) == METIS_OK);
    CHECK(rwhere[0] == 0 && rwhere[1] == 2 &&
        rwhere[2] == 1 && rwhere[3] == 0);
    for (i=0; i<4; i++)
      weights[rwhere[i]] += rvwgt[i];
    CHECK(weights[2] == unit);
    CHECK(iabs(weights[0]-weights[1]) == 2*unit+1);
  }
#if IDXTYPEWIDTH == 64
  {
    const idx_t wide = ((idx_t)1 << 32) + 1;
    idx_t wn = 4, wncon = 1, wnparts = 2, wcut;
    idx_t wxadj[] = {0, 2, 4, 6, 8};
    idx_t wadjncy[] = {1, 3, 0, 2, 1, 3, 0, 2};
    idx_t wvwgt[] = {wide, wide + 2, wide + 4, wide + 6};
    idx_t wadjwgt[] = {wide, wide + 2, wide, wide + 4,
                       wide + 4, wide + 6, wide + 2, wide + 6};
    idx_t wpart[4], woptions[METIS_NOPTIONS];

    CHECK(iabs(wide) == wide);
    CHECK(iabs(-wide) == wide);
    CHECK(CheckGraph(wn, 8, wxadj, wadjncy, wadjwgt));
    CHECK(METIS_SetDefaultOptions(woptions) == METIS_OK);
    woptions[METIS_OPTION_SEED] = 12345;
    CHECK(METIS_PartGraphKway(&wn, &wncon, wxadj, wadjncy, wvwgt, NULL,
        wadjwgt, &wnparts, NULL, NULL, woptions, &wcut, wpart) == METIS_OK);
    CHECK(wcut > (idx_t)INT32_MAX);
    CHECK(CheckPartition(wn, wnparts, wxadj, wadjncy, wadjwgt, wpart,
        wcut));
    CHECK(METIS_PartGraphRecursive(&wn, &wncon, wxadj, wadjncy, wvwgt, NULL,
        wadjwgt, &wnparts, NULL, NULL, woptions, &wcut, wpart) == METIS_OK);
    CHECK(wcut > (idx_t)INT32_MAX);
    CHECK(CheckPartition(wn, wnparts, wxadj, wadjncy, wadjwgt, wpart,
        wcut));
  }
#endif
  CHECK(METIS_SetDefaultOptions(options) == METIS_OK);
  options[METIS_OPTION_SEED] = 12345;
  CHECK(METIS_PartGraphKway(&n, &ncon, xadj, adjncy, NULL, NULL, NULL,
      &nparts, NULL, NULL, options, &cut, part) == METIS_OK);
  CHECK(cut >= 0 && cut <= 6);
  CHECK(CheckPartition(n, nparts, xadj, adjncy, NULL, part, cut));
  CHECK(METIS_PartGraphRecursive(&n, &ncon, xadj, adjncy, NULL, NULL, NULL,
      &nparts, NULL, NULL, options, &cut, part) == METIS_OK);
  CHECK(CheckPartition(n, nparts, xadj, adjncy, NULL, part, cut));
  CHECK(METIS_NodeND(&n, xadj, adjncy, NULL, options, perm, iperm) == METIS_OK);
  for (i=0; i<n; i++) {
    CHECK(perm[i] >= 0 && perm[i] < n);
    CHECK(iperm[perm[i]] == i);
  }
  CHECK(METIS_MeshToDual(&ne, &nn, eptr, eind, &ncommon, &numflag,
      &mxadj, &madjncy) == METIS_OK);
  CHECK(CheckAdjacencySets(ne, 2, mxadj, madjncy, dual_adjacency));
  CHECK(METIS_Free(mxadj) == METIS_OK);
  CHECK(METIS_Free(madjncy) == METIS_OK);
  CHECK(METIS_MeshToNodal(&ne, &nn, eptr, eind, &numflag,
      &mxadj, &madjncy) == METIS_OK);
  CHECK(CheckAdjacencySets(nn, 10, mxadj, madjncy, nodal_adjacency));
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
