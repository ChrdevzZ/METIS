#include <metis.h>
#include <math.h>
#include <stdint.h>

int main(void)
{
  idx_t nvtxs=1, ncon=IDX_MAX, nparts=2, objval=0;
  idx_t xadj[2] = {0, 0};
  idx_t part[1] = {0};
  idx_t options[METIS_NOPTIONS];
  idx_t perm[1] = {17}, iperm[1] = {19};
  idx_t ne, nn, ncommon, numflag, eptr[2], eind[1];
  idx_t *mesh_xadj=(idx_t *)(uintptr_t)1;
  idx_t *mesh_adjncy=(idx_t *)(uintptr_t)1;
  real_t tpwgts[2] = {(real_t)NAN, (real_t)NAN};
  real_t ubvec[1] = {(real_t)INFINITY};
  int status;

  if (METIS_SetDefaultOptions(options) != METIS_OK)
    return 10;

  status = METIS_PartGraphKway(&nvtxs, &ncon, xadj, NULL, NULL, NULL, NULL,
      &nparts, NULL, NULL, NULL, &objval, part);

  if (status != METIS_ERROR_MEMORY)
    return 1;

  if ((uintmax_t)IDX_MAX > (uintmax_t)(SIZE_MAX/sizeof(idx_t))) {
    ncon = (idx_t)(SIZE_MAX/sizeof(idx_t) + 1);
    nparts = 1;
    status = METIS_PartGraphKway(&nvtxs, &ncon, xadj, NULL, NULL, NULL,
        NULL, &nparts, NULL, NULL, NULL, &objval, part);
    if (status != METIS_ERROR_MEMORY)
      return 4;
  }

  {
    idx_t pn=2, pncon=IDX_MAX, pnp=1, pobj=113;
    idx_t pxadj[] = {0, 0, 0}, ppart[] = {127, 131};

    status = METIS_PartGraphKway(&pn, &pncon, pxadj, NULL, NULL, NULL,
        NULL, &pnp, NULL, NULL, NULL, &pobj, ppart);
    if (status != METIS_ERROR_MEMORY || pobj != 113 ||
        ppart[0] != 127 || ppart[1] != 131)
      return 55;
  }

#if IDXTYPEWIDTH == 64
  {
    idx_t tn=1, tncon=1, tnp=1, tobj=137;
    idx_t txadj[] = {0, (idx_t)(SIZE_MAX/sizeof(idx_t)+1)};
    idx_t tpart[] = {139}, tperm[] = {149}, tiperm[] = {151};

    status = METIS_PartGraphKway(&tn, &tncon, txadj, NULL, NULL, NULL,
        NULL, &tnp, NULL, NULL, NULL, &tobj, tpart);
    if (status != METIS_ERROR_MEMORY || tobj != 137 || tpart[0] != 139)
      return 56;
    status = METIS_NodeND(&tn, txadj, NULL, NULL, NULL, tperm, tiperm);
    if (status != METIS_ERROR_MEMORY || tperm[0] != 149 ||
        tiperm[0] != 151)
      return 57;
  }

  {
    idx_t tne=1, tnn=1, tncommon=1, tnumflag=0, tnparts=1;
    idx_t teptr[] = {0, (idx_t)(SIZE_MAX/sizeof(idx_t)+1)};
    idx_t tepart[] = {157}, tnpart[] = {163}, tobj=167;
    idx_t *txadj=(idx_t *)(uintptr_t)1;
    idx_t *tadjncy=(idx_t *)(uintptr_t)1;

    status = METIS_MeshToDual(&tne, &tnn, teptr, NULL, &tncommon,
        &tnumflag, &txadj, &tadjncy);
    if (status != METIS_ERROR_MEMORY || txadj != NULL || tadjncy != NULL)
      return 58;
    txadj = (idx_t *)(uintptr_t)1;
    tadjncy = (idx_t *)(uintptr_t)1;
    status = METIS_MeshToNodal(&tne, &tnn, teptr, NULL, &tnumflag,
        &txadj, &tadjncy);
    if (status != METIS_ERROR_MEMORY || txadj != NULL || tadjncy != NULL)
      return 59;
    status = METIS_PartMeshDual(&tne, &tnn, teptr, NULL, NULL, NULL,
        &tncommon, &tnparts, NULL, NULL, &tobj, tepart, tnpart);
    if (status != METIS_ERROR_MEMORY || tobj != 167 || tepart[0] != 157 ||
        tnpart[0] != 163)
      return 60;
    status = METIS_PartMeshNodal(&tne, &tnn, teptr, NULL, NULL, NULL,
        &tnparts, NULL, NULL, &tobj, tepart, tnpart);
    if (status != METIS_ERROR_MEMORY || tobj != 167 || tepart[0] != 157 ||
        tnpart[0] != 163)
      return 61;
  }
#endif

  {
    idx_t hn=IDX_MAX, hncon=1, hnparts=1, hobj=71;
    idx_t hxadj[] = {0}, hpart[] = {73};
    idx_t hperm[] = {79}, hiperm[] = {83}, hsizes[] = {89};
    idx_t hwhere[] = {0}, hhmarker[] = {0}, hold2new[] = {97};
    idx_t hsepsize=101;

    status = METIS_PartGraphKway(&hn, &hncon, hxadj, NULL, NULL, NULL,
        NULL, &hnparts, NULL, NULL, NULL, &hobj, hpart);
    if (status != METIS_ERROR_MEMORY || hobj != 71 || hpart[0] != 73)
      return 44;
    status = METIS_PartGraphRecursive(&hn, &hncon, hxadj, NULL, NULL,
        NULL, NULL, &hnparts, NULL, NULL, NULL, &hobj, hpart);
    if (status != METIS_ERROR_MEMORY || hobj != 71 || hpart[0] != 73)
      return 45;
    status = METIS_NodeND(&hn, hxadj, NULL, NULL, NULL, hperm, hiperm);
    if (status != METIS_ERROR_MEMORY || hperm[0] != 79 || hiperm[0] != 83)
      return 46;
    status = METIS_NodeNDP(hn, hxadj, NULL, NULL, 1, NULL, hperm, hiperm,
        hsizes);
    if (status != METIS_ERROR_MEMORY || hperm[0] != 79 ||
        hiperm[0] != 83 || hsizes[0] != 89)
      return 47;
    status = METIS_ComputeVertexSeparator(&hn, hxadj, NULL, NULL, NULL,
        &hsepsize, hpart);
    if (status != METIS_ERROR_MEMORY || hsepsize != 101 || hpart[0] != 73)
      return 48;
    status = METIS_NodeRefine(hn, hxadj, NULL, NULL, hwhere, hhmarker,
        (real_t)1.03);
    if (status != METIS_ERROR_MEMORY || hwhere[0] != 0)
      return 49;
    status = METIS_CacheFriendlyReordering(hn, hxadj, NULL, hpart,
        hold2new);
    if (status != METIS_ERROR_MEMORY || hold2new[0] != 97)
      return 50;
  }

  {
    idx_t hne=IDX_MAX, hnn=1, hncommon=1, hnumflag=0, hnparts=1;
    idx_t heptr[] = {0, 1}, heind[] = {0};
    idx_t hepart[] = {103}, hnpart[] = {107}, hobj=109;
    idx_t *hxadj=(idx_t *)(uintptr_t)1;
    idx_t *hadjncy=(idx_t *)(uintptr_t)1;

    status = METIS_MeshToDual(&hne, &hnn, heptr, heind, &hncommon,
        &hnumflag, &hxadj, &hadjncy);
    if (status != METIS_ERROR_MEMORY || hxadj != NULL || hadjncy != NULL)
      return 51;
    status = METIS_PartMeshDual(&hne, &hnn, heptr, heind, NULL, NULL,
        &hncommon, &hnparts, NULL, NULL, &hobj, hepart, hnpart);
    if (status != METIS_ERROR_MEMORY || hobj != 109 || hepart[0] != 103 ||
        hnpart[0] != 107)
      return 52;

    hne = 1;
    hnn = IDX_MAX;
    hxadj = (idx_t *)(uintptr_t)1;
    hadjncy = (idx_t *)(uintptr_t)1;
    status = METIS_MeshToNodal(&hne, &hnn, heptr, heind, &hnumflag,
        &hxadj, &hadjncy);
    if (status != METIS_ERROR_MEMORY || hxadj != NULL || hadjncy != NULL)
      return 53;
    status = METIS_PartMeshNodal(&hne, &hnn, heptr, heind, NULL, NULL,
        &hnparts, NULL, NULL, &hobj, hepart, hnpart);
    if (status != METIS_ERROR_MEMORY || hobj != 109 || hepart[0] != 103 ||
        hnpart[0] != 107)
      return 54;
  }

  ncon = 1;
  nparts = 2;
  status = METIS_PartGraphKway(&nvtxs, &ncon, xadj, NULL, NULL, NULL, NULL,
      &nparts, tpwgts, NULL, NULL, &objval, part);
  if (status != METIS_ERROR_INPUT)
    return 2;

  tpwgts[0] = tpwgts[1] = (real_t)0.5;
  status = METIS_PartGraphKway(&nvtxs, &ncon, xadj, NULL, NULL, NULL, NULL,
      &nparts, tpwgts, ubvec, NULL, &objval, part);

  if (status != METIS_ERROR_INPUT)
    return 3;

  nvtxs = -1;
  ncon = 1;
  nparts = 1;
  status = METIS_PartGraphKway(&nvtxs, &ncon, xadj, NULL, NULL, NULL, NULL,
      &nparts, NULL, NULL, NULL, &objval, part);
  if (status != METIS_ERROR_INPUT)
    return 11;
  status = METIS_PartGraphRecursive(&nvtxs, &ncon, xadj, NULL, NULL, NULL,
      NULL, &nparts, NULL, NULL, NULL, &objval, part);
  if (status != METIS_ERROR_INPUT)
    return 12;
  status = METIS_NodeND(&nvtxs, xadj, NULL, NULL, NULL, perm, iperm);
  if (status != METIS_ERROR_INPUT || perm[0] != 17 || iperm[0] != 19)
    return 13;

  ne = -1;
  nn = 1;
  ncommon = 1;
  numflag = 0;
  eptr[0] = eptr[1] = 0;
  eind[0] = 0;
  status = METIS_MeshToDual(&ne, &nn, eptr, eind, &ncommon, &numflag,
      &mesh_xadj, &mesh_adjncy);
  if (status != METIS_ERROR_INPUT || mesh_xadj != NULL ||
      mesh_adjncy != NULL)
    return 14;

  ne = 1;
  nn = 1;
  eptr[0] = eptr[1] = 0;
  mesh_xadj = (idx_t *)(uintptr_t)1;
  mesh_adjncy = (idx_t *)(uintptr_t)1;
  status = METIS_MeshToNodal(&ne, &nn, eptr, eind, &numflag,
      &mesh_xadj, &mesh_adjncy);
  if (status != METIS_ERROR_INPUT || mesh_xadj != NULL ||
      mesh_adjncy != NULL)
    return 15;

  nvtxs = 1;
  xadj[0] = 0;
  xadj[1] = 1;
  status = METIS_PartGraphKway(&nvtxs, &ncon, xadj, NULL, NULL, NULL, NULL,
      &nparts, NULL, NULL, NULL, &objval, part);
  if (status != METIS_ERROR_INPUT)
    return 16;

  {
    idx_t wn=2, wncon=1, wnparts=2, wobj=0;
    idx_t wxadj[] = {0, 0, 0};
    idx_t wadjncy[] = {1, 0};
    idx_t wvwgt[] = {IDX_MAX, 1};
    idx_t wvsize[] = {IDX_MAX, 0};
    idx_t wadjwgt[] = {IDX_MAX, 1};
    idx_t wpart[] = {7, 9};

    status = METIS_PartGraphKway(&wn, &wncon, wxadj, NULL, wvwgt, NULL,
        NULL, &wnparts, NULL, NULL, NULL, &wobj, wpart);
    if (status != METIS_ERROR_INPUT || wpart[0] != 7 || wpart[1] != 9)
      return 18;

    wxadj[1] = 1;
    wxadj[2] = 2;
    status = METIS_PartGraphKway(&wn, &wncon, wxadj, wadjncy, NULL, NULL,
        wadjwgt, &wnparts, NULL, NULL, NULL, &wobj, wpart);
    if (status != METIS_ERROR_INPUT)
      return 19;

    if (METIS_SetDefaultOptions(options) != METIS_OK)
      return 20;
    options[METIS_OPTION_OBJTYPE] = METIS_OBJTYPE_VOL;
    status = METIS_PartGraphKway(&wn, &wncon, wxadj, wadjncy, NULL, wvsize,
        NULL, &wnparts, NULL, NULL, options, &wobj, wpart);
    if (status != METIS_ERROR_INPUT)
      return 21;

    if (METIS_SetDefaultOptions(options) != METIS_OK)
      return 42;
    wadjwgt[0] = wadjwgt[1] = 0;
    wpart[0] = 7;
    wpart[1] = 9;
    status = METIS_PartGraphKway(&wn, &wncon, wxadj, wadjncy, NULL, NULL,
        wadjwgt, &wnparts, NULL, NULL, options, &wobj, wpart);
    if (status != METIS_ERROR_INPUT || wpart[0] != 7 || wpart[1] != 9)
      return 43;

    options[METIS_OPTION_OBJTYPE] = METIS_OBJTYPE_VOL;
    status = METIS_PartGraphKway(&wn, &wncon, wxadj, wadjncy, NULL, NULL,
        wadjwgt, &wnparts, NULL, NULL, options, &wobj, wpart);
    if (status != METIS_ERROR_INPUT || wpart[0] != 7 || wpart[1] != 9)
      return 65;
  }

  {
    idx_t on=1, oncon=1, onparts=1, oobj=0;
    idx_t oxadj[] = {0, 0};
    idx_t opart[] = {0};
    const idx_t option_ids[] = {
      METIS_OPTION_NO2HOP, METIS_OPTION_ONDISK, METIS_OPTION_DROPEDGES
    };
    size_t i;

    for (i=0; i<sizeof(option_ids)/sizeof(option_ids[0]); i++) {
      if (METIS_SetDefaultOptions(options) != METIS_OK)
        return 22;
      options[option_ids[i]] = 2;
      status = METIS_PartGraphKway(&on, &oncon, oxadj, NULL, NULL, NULL,
          NULL, &onparts, NULL, NULL, options, &oobj, opart);
      if (status != METIS_ERROR_INPUT)
        return 23;
    }
    if (METIS_SetDefaultOptions(options) != METIS_OK)
      return 24;
    options[METIS_OPTION_NIPARTS] = 0;
    status = METIS_PartGraphKway(&on, &oncon, oxadj, NULL, NULL, NULL,
        NULL, &onparts, NULL, NULL, options, &oobj, opart);
    if (status != METIS_ERROR_INPUT)
      return 25;
  }

  {
    idx_t mne=1, mnn=2, mncommon=1, mnumflag=0, mnparts=1, mobj=0;
    idx_t meptr[] = {0, 2}, meind[] = {0, 1};
    idx_t mepart[] = {0}, mnpart[] = {0};
    idx_t *mxadj=NULL, *madjncy=NULL;

    status = METIS_MeshToDual(&mne, &mnn, meptr, meind, &mncommon,
        &mnumflag, &mxadj, &madjncy);
    if (status != METIS_OK || mxadj == NULL || madjncy == NULL ||
        mxadj[0] != 0 || mxadj[1] != 0)
      return 26;
    METIS_Free(mxadj);
    METIS_Free(madjncy);
    mxadj = madjncy = NULL;
    mnn = 1;
    meptr[1] = 1;
    status = METIS_MeshToNodal(&mne, &mnn, meptr, meind, &mnumflag,
        &mxadj, &madjncy);
    if (status != METIS_OK || mxadj == NULL || madjncy == NULL ||
        mxadj[0] != 0 || mxadj[1] != 0)
      return 27;
    METIS_Free(mxadj);
    METIS_Free(madjncy);

    if (METIS_SetDefaultOptions(options) != METIS_OK)
      return 28;
    options[METIS_OPTION_PTYPE] = 77;
    status = METIS_PartMeshNodal(&mne, &mnn, meptr, meind, NULL, NULL,
        &mnparts, NULL, options, &mobj, mepart, mnpart);
    if (status != METIS_ERROR_INPUT)
      return 29;
  }

  {
    idx_t dne=1, dnn=1, dncommon=1, dnumflag=0;
    idx_t dnparts=1, dobjval=41, depart[] = {43}, dnpart[] = {47};
    idx_t deptr[] = {0, 2}, deind[] = {0, 0};
    idx_t *dxadj=(idx_t *)(uintptr_t)1;
    idx_t *dadjncy=(idx_t *)(uintptr_t)1;

    status = METIS_MeshToDual(&dne, &dnn, deptr, deind, &dncommon,
        &dnumflag, &dxadj, &dadjncy);
    if (status != METIS_ERROR_INPUT || dxadj != NULL || dadjncy != NULL)
      return 38;
    dxadj = (idx_t *)(uintptr_t)1;
    dadjncy = (idx_t *)(uintptr_t)1;
    status = METIS_MeshToNodal(&dne, &dnn, deptr, deind, &dnumflag,
        &dxadj, &dadjncy);
    if (status != METIS_ERROR_INPUT || dxadj != NULL || dadjncy != NULL)
      return 39;

    status = METIS_PartMeshDual(&dne, &dnn, deptr, deind, NULL, NULL,
        &dncommon, &dnparts, NULL, NULL, &dobjval, depart, dnpart);
    if (status != METIS_ERROR_INPUT || dobjval != 41 || depart[0] != 43 ||
        dnpart[0] != 47)
      return 40;
    status = METIS_PartMeshNodal(&dne, &dnn, deptr, deind, NULL, NULL,
        &dnparts, NULL, NULL, &dobjval, depart, dnpart);
    if (status != METIS_ERROR_INPUT || dobjval != 41 || depart[0] != 43 ||
        dnpart[0] != 47)
      return 41;
  }

  {
    idx_t ane=1, ann=1, ancommon=1, anumflag=0;
    idx_t aeptr[] = {0, 1}, aeind[] = {0};
    idx_t *aoutput=(idx_t *)(uintptr_t)1;

    status = METIS_MeshToDual(&ane, &ann, aeptr, aeind, &ancommon,
        &anumflag, &aoutput, &aoutput);
    if (status != METIS_ERROR_INPUT ||
        aoutput != (idx_t *)(uintptr_t)1)
      return 62;
    aoutput = (idx_t *)(uintptr_t)1;
    status = METIS_MeshToNodal(&ane, &ann, aeptr, aeind, &anumflag,
        &aoutput, &aoutput);
    if (status != METIS_ERROR_INPUT ||
        aoutput != (idx_t *)(uintptr_t)1)
      return 63;
    if (aeptr[0] != 0 || aeptr[1] != 1 || aeind[0] != 0)
      return 64;
  }

  {
    idx_t en=1, exadj[] = {0, 0};
    idx_t eperm[] = {31}, eiperm[] = {37}, esizes[] = {41};
    idx_t eseparator=43, epart[] = {47}, ewhere[] = {3};
    idx_t ehmarker[] = {2}, eold2new[] = {53}, epartid[] = {-1};

    status = METIS_NodeNDP(-1, exadj, NULL, NULL, 1, NULL, eperm, eiperm,
        esizes);
    if (status != METIS_ERROR_INPUT || eperm[0] != 31 ||
        eiperm[0] != 37 || esizes[0] != 41)
      return 30;
    status = METIS_NodeNDP(en, exadj, NULL, NULL, IDX_MAX/2+1, NULL,
        eperm, eiperm, esizes);
    if (status != METIS_ERROR_MEMORY || eperm[0] != 31 ||
        eiperm[0] != 37 || esizes[0] != 41)
      return 35;
    status = METIS_ComputeVertexSeparator(NULL, exadj, NULL, NULL, NULL,
        &eseparator, epart);
    if (status != METIS_ERROR_INPUT || eseparator != 43 || epart[0] != 47)
      return 31;
    status = METIS_NodeRefine(en, exadj, NULL, NULL, ewhere, ehmarker,
        (real_t)1.03);
    if (status != METIS_ERROR_INPUT || ewhere[0] != 3)
      return 32;
    status = METIS_CacheFriendlyReordering(en, exadj, NULL, epartid,
        eold2new);
    if (status != METIS_ERROR_INPUT || eold2new[0] != 53)
      return 33;
  }

  {
    idx_t fn=2, fncon=1, fnparts=2, fobj=59;
    idx_t fxadj[] = {1, 1, 1}, fpart[] = {61, 67};

    if (METIS_SetDefaultOptions(options) != METIS_OK)
      return 36;
    options[METIS_OPTION_NUMBERING] = 1;
    options[METIS_OPTION_CONTIG] = 1;
    status = METIS_PartGraphKway(&fn, &fncon, fxadj, NULL, NULL, NULL,
        NULL, &fnparts, NULL, NULL, options, &fobj, fpart);
    if (status == METIS_OK || fxadj[0] != 1 || fxadj[1] != 1 ||
        fxadj[2] != 1 || fobj != 59 || fpart[0] != 61 || fpart[1] != 67)
      return 37;
  }

  {
    idx_t rn=2, rxadj[] = {0, 1, 2}, radjncy[] = {1, 0};
    idx_t rwhere[] = {0, 1}, rhmarker[] = {2, 2};

    status = METIS_NodeRefine(rn, rxadj, NULL, radjncy, rwhere, rhmarker,
        (real_t)1.03);
    if (status != METIS_ERROR_INPUT)
      return 34;
  }

  if (METIS_SetDefaultOptions(NULL) != METIS_ERROR_INPUT)
    return 17;

  return 0;
}
