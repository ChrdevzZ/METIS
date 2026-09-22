/*
 * Copyright 1997, Regents of the University of Minnesota
 *
 * meshpart.c
 *
 * This file contains routines for partitioning finite element meshes.
 *
 * Started 9/29/97
 * George
 *
 * $Id: meshpart.c 17513 2014-08-05 16:20:50Z dominique $
 *
 */

#include "metislib.h"
#include "input_validation.h"


/*************************************************************************
* This function partitions a finite element mesh by partitioning its nodal
* graph using KMETIS and then assigning elements in a load balanced fashion.
**************************************************************************/
int METIS_PartMeshNodal(idx_t *ne, idx_t *nn, idx_t *eptr, idx_t *eind, 
          idx_t *vwgt, idx_t *vsize, idx_t *nparts, real_t *tpwgts, 
          idx_t *options, idx_t *objval, idx_t *epart, idx_t *npart)
{
  volatile int sigrval=0, renumber=0, ptype;
  int error, sigrval2;
  idx_t new_objval;
  idx_t *cleanup_npart;
  idx_t * volatile xadj=NULL;
  idx_t * volatile adjncy=NULL;
  idx_t * volatile new_npart=NULL;
  idx_t ncon=1, pnumflag=0;
  volatile idx_t numflag;
  volatile int rstatus=METIS_OK;

  if (ne == NULL || nn == NULL || nparts == NULL || objval == NULL ||
      epart == NULL || npart == NULL)
    return METIS_ERROR_INPUT;
  numflag = GETOPTION(options, METIS_OPTION_NUMBERING, 0);
  ptype = GETOPTION(options, METIS_OPTION_PTYPE, METIS_PTYPE_KWAY);
  if (*nparts <= 0 ||
      (ptype != METIS_PTYPE_KWAY && ptype != METIS_PTYPE_RB))
    return METIS_ERROR_INPUT;
  rstatus = ValidateMeshInput(*ne, *nn, eptr, eind, numflag);
  if (rstatus != METIS_OK)
    return rstatus;
  if (!ValidateIntegerWeights(*nn, vwgt) ||
      !ValidateIntegerWeights(*nn, vsize))
    return METIS_ERROR_INPUT;

  /* set up malloc cleaning code and signal catchers */
  if (!gk_malloc_init()) {
    if (errno == 0)
      errno = ENOMEM;
    return METIS_ERROR_MEMORY;
  }

  if (!gk_sigtrap()) {
    gk_malloc_cleanup(0);
    errno = ENOMEM;
    return METIS_ERROR_MEMORY;
  }

  METIS_SIGCATCH(sigrval);
  if (sigrval != 0)
    goto SIGTHROW;

  renumber = numflag;

  /* renumber the mesh */
  if (renumber) {
    ChangeMesh2CNumbering(*ne, eptr, eind);
    options[METIS_OPTION_NUMBERING] = 0;
  }

  /* get the nodal graph */
  rstatus = METIS_MeshToNodal(ne, nn, eptr, eind, &pnumflag,
      (idx_t **)&xadj, (idx_t **)&adjncy);
  if (rstatus != METIS_OK)
    goto SIGTHROW;

  new_npart = iMallocNoSignal((size_t)*nn, "METIS_PartMeshNodal: npart",
      &sigrval2);
  if (new_npart == NULL) {
    rstatus = METIS_ERROR_MEMORY;
    goto SIGTHROW;
  }

  /* partition the graph */
  if (ptype == METIS_PTYPE_KWAY) 
    rstatus = METIS_PartGraphKway(nn, &ncon, (idx_t *)xadj,
                  (idx_t *)adjncy, vwgt, vsize, NULL,
                  nparts, tpwgts, NULL, options, &new_objval,
                  (idx_t *)new_npart);
  else 
    rstatus = METIS_PartGraphRecursive(nn, &ncon, (idx_t *)xadj,
                  (idx_t *)adjncy, vwgt, vsize, NULL,
                  nparts, tpwgts, NULL, options, &new_objval,
                  (idx_t *)new_npart);

  if (rstatus != METIS_OK)
    goto SIGTHROW;

  /* partition the other side of the mesh */
  rstatus = InduceRowPartFromColumnPart(*ne, eptr, eind, epart,
      (idx_t *)new_npart, *nparts, tpwgts);
  if (rstatus != METIS_OK)
    goto SIGTHROW;


SIGTHROW:
  error = errno;
  if (error == 0 && (sigrval != 0 || rstatus != METIS_OK))
    error = sigrval == SIGMEM || rstatus == METIS_ERROR_MEMORY ?
        ENOMEM : EINVAL;

  if (renumber) {
    ChangeMesh2FNumbering2(*ne, *nn, eptr, eind,
        sigrval == 0 && rstatus == METIS_OK ? epart : NULL,
        sigrval == 0 && rstatus == METIS_OK ? (idx_t *)new_npart : NULL);
    options[METIS_OPTION_NUMBERING] = 1;
  }

  if (sigrval == 0 && rstatus == METIS_OK) {
    icopy(*nn, (idx_t *)new_npart, npart);
    *objval = new_objval;
  }

  cleanup_npart = (idx_t *)new_npart;
  gk_free((void **)&cleanup_npart, LTERM);

  METIS_Free((idx_t *)xadj);
  METIS_Free((idx_t *)adjncy);

  gk_siguntrap();
  gk_malloc_cleanup(0);

  if (sigrval != 0 || rstatus != METIS_OK)
    errno = error;

  return rstatus == METIS_OK ? metis_rcode(sigrval) : rstatus;
}



/*************************************************************************
* This function partitions a finite element mesh by partitioning its dual
* graph using KMETIS and then assigning nodes in a load balanced fashion.
**************************************************************************/
int METIS_PartMeshDual(idx_t *ne, idx_t *nn, idx_t *eptr, idx_t *eind, 
          idx_t *vwgt, idx_t *vsize, idx_t *ncommon, idx_t *nparts, 
          real_t *tpwgts, idx_t *options, idx_t *objval, idx_t *epart, 
          idx_t *npart) 
{
  volatile int sigrval=0, renumber=0, ptype;
  int error, sigrval2;
  idx_t new_objval;
  idx_t *cleanup_epart;
  idx_t i, j;
  idx_t * volatile xadj=NULL;
  idx_t * volatile adjncy=NULL;
  idx_t * volatile new_epart=NULL;
  idx_t *nptr=NULL, *nind=NULL;
  idx_t ncon=1, pnumflag=0;
  volatile idx_t numflag;
  volatile int rstatus=METIS_OK;

  if (ne == NULL || nn == NULL || ncommon == NULL || nparts == NULL ||
      objval == NULL || epart == NULL || npart == NULL)
    return METIS_ERROR_INPUT;
  numflag = GETOPTION(options, METIS_OPTION_NUMBERING, 0);
  ptype = GETOPTION(options, METIS_OPTION_PTYPE, METIS_PTYPE_KWAY);
  if (*nparts <= 0 ||
      (ptype != METIS_PTYPE_KWAY && ptype != METIS_PTYPE_RB))
    return METIS_ERROR_INPUT;
  rstatus = ValidateMeshInput(*ne, *nn, eptr, eind, numflag);
  if (rstatus != METIS_OK)
    return rstatus;
  if (!ValidateIntegerWeights(*ne, vwgt) ||
      !ValidateIntegerWeights(*ne, vsize))
    return METIS_ERROR_INPUT;

  /* set up malloc cleaning code and signal catchers */
  if (!gk_malloc_init()) {
    if (errno == 0)
      errno = ENOMEM;
    return METIS_ERROR_MEMORY;
  }

  if (!gk_sigtrap()) {
    gk_malloc_cleanup(0);
    errno = ENOMEM;
    return METIS_ERROR_MEMORY;
  }

  METIS_SIGCATCH(sigrval);
  if (sigrval != 0)
    goto SIGTHROW;

  renumber = numflag;

  /* renumber the mesh */
  if (renumber) {
    ChangeMesh2CNumbering(*ne, eptr, eind);
    options[METIS_OPTION_NUMBERING] = 0;
  }

  /* get the dual graph */
  rstatus = METIS_MeshToDual(ne, nn, eptr, eind, ncommon, &pnumflag,
      (idx_t **)&xadj, (idx_t **)&adjncy);
  if (rstatus != METIS_OK)
    goto SIGTHROW;

  new_epart = iMallocNoSignal((size_t)*ne, "METIS_PartMeshDual: epart",
      &sigrval2);
  if (new_epart == NULL) {
    rstatus = METIS_ERROR_MEMORY;
    goto SIGTHROW;
  }

  /* partition the graph */
  if (ptype == METIS_PTYPE_KWAY) 
    rstatus = METIS_PartGraphKway(ne, &ncon, (idx_t *)xadj,
                  (idx_t *)adjncy, vwgt, vsize, NULL,
                  nparts, tpwgts, NULL, options, &new_objval,
                  (idx_t *)new_epart);
  else 
    rstatus = METIS_PartGraphRecursive(ne, &ncon, (idx_t *)xadj,
                  (idx_t *)adjncy, vwgt, vsize, NULL,
                  nparts, tpwgts, NULL, options, &new_objval,
                  (idx_t *)new_epart);

  if (rstatus != METIS_OK)
    goto SIGTHROW;


  /* construct the node-element list */
  nptr = iMallocNoSignal((size_t)*nn+1, "METIS_PartMeshDual: nptr",
      &sigrval2);
  if (nptr == NULL) {
    rstatus = METIS_ERROR_MEMORY;
    goto SIGTHROW;
  }
  iset(*nn+1, 0, nptr);
  nind = iMallocNoSignal((size_t)eptr[*ne], "METIS_PartMeshDual: nind",
      &sigrval2);
  if (nind == NULL) {
    rstatus = METIS_ERROR_MEMORY;
    goto SIGTHROW;
  }

  for (i=0; i<*ne; i++) {
    for (j=eptr[i]; j<eptr[i+1]; j++)
      nptr[eind[j]]++;
  }
  MAKECSR(i, *nn, nptr);

  for (i=0; i<*ne; i++) {
    for (j=eptr[i]; j<eptr[i+1]; j++)
      nind[nptr[eind[j]]++] = i;
  }
  SHIFTCSR(i, *nn, nptr);

  /* partition the other side of the mesh */
  rstatus = InduceRowPartFromColumnPart(*nn, nptr, nind, npart,
      (idx_t *)new_epart, *nparts, tpwgts);
  if (rstatus != METIS_OK)
    goto SIGTHROW;


SIGTHROW:
  error = errno;
  if (error == 0 && (sigrval != 0 || rstatus != METIS_OK))
    error = sigrval == SIGMEM || rstatus == METIS_ERROR_MEMORY ?
        ENOMEM : EINVAL;

  gk_free((void **)&nptr, &nind, LTERM);

  if (renumber) {
    ChangeMesh2FNumbering2(*ne, *nn, eptr, eind,
        sigrval == 0 && rstatus == METIS_OK ? (idx_t *)new_epart : NULL,
        sigrval == 0 && rstatus == METIS_OK ? npart : NULL);
    options[METIS_OPTION_NUMBERING] = 1;
  }

  if (sigrval == 0 && rstatus == METIS_OK) {
    icopy(*ne, (idx_t *)new_epart, epart);
    *objval = new_objval;
  }

  cleanup_epart = (idx_t *)new_epart;
  gk_free((void **)&cleanup_epart, LTERM);

  METIS_Free((idx_t *)xadj);
  METIS_Free((idx_t *)adjncy);

  gk_siguntrap();
  gk_malloc_cleanup(0);

  if (sigrval != 0 || rstatus != METIS_OK)
    errno = error;

  return rstatus == METIS_OK ? metis_rcode(sigrval) : rstatus;
}



/*************************************************************************/
/*! Induces a partitioning of the rows based on a a partitioning of the
    columns. It is used by both the Nodal and Dual routines. */
/*************************************************************************/
int InduceRowPartFromColumnPart(idx_t nrows, idx_t *rowptr, idx_t *rowind,
         idx_t *rpart, idx_t *cpart, idx_t nparts, real_t *tpwgts)
{
  idx_t i, j, me;
  idx_t nnbrs, *pwgts=NULL, *nbrdom=NULL, *nbrwgt=NULL, *nbrmrk=NULL;
  idx_t *itpwgts=NULL;
  int error, sigrval;

  pwgts = iMallocNoSignal((size_t)nparts,
      "InduceRowPartFromColumnPart: pwgts", &sigrval);
  if (pwgts == NULL)
    goto MEMORY_ERROR;
  nbrdom = iMallocNoSignal((size_t)nparts,
      "InduceRowPartFromColumnPart: nbrdom", &sigrval);
  if (nbrdom == NULL)
    goto MEMORY_ERROR;
  nbrwgt = iMallocNoSignal((size_t)nparts,
      "InduceRowPartFromColumnPart: nbrwgt", &sigrval);
  if (nbrwgt == NULL)
    goto MEMORY_ERROR;
  nbrmrk = iMallocNoSignal((size_t)nparts,
      "InduceRowPartFromColumnPart: nbrmrk", &sigrval);
  if (nbrmrk == NULL)
    goto MEMORY_ERROR;
  itpwgts = iMallocNoSignal((size_t)nparts,
      "InduceRowPartFromColumnPart: itpwgts", &sigrval);
  if (itpwgts == NULL)
    goto MEMORY_ERROR;

  iset(nparts, 0, pwgts);
  iset(nparts, 0, nbrdom);
  iset(nparts, 0, nbrwgt);
  iset(nparts, -1, nbrmrk);

  iset(nrows, -1, rpart);

  /* setup the integer target partition weights */
  if (tpwgts == NULL) {
    iset(nparts, 1+nrows/nparts, itpwgts);
  }
  else {
    for (i=0; i<nparts; i++)
      itpwgts[i] = 1+nrows*tpwgts[i];
  }

  /* first assign the rows consisting only of columns that belong to 
     a single partition. Assign rows that are empty to -2 (un-assigned) */
  for (i=0; i<nrows; i++) {
    if (rowptr[i+1]-rowptr[i] == 0) {
      rpart[i] = -2;
      continue;
    }

    me = cpart[rowind[rowptr[i]]];
    for (j=rowptr[i]+1; j<rowptr[i+1]; j++) {
      if (cpart[rowind[j]] != me)
        break;
    }
    if (j == rowptr[i+1]) {
      rpart[i] = me;
      pwgts[me]++;
    }
  }

  /* next assign the rows consisting of columns belonging to multiple
     partitions in a  balanced way */
  for (i=0; i<nrows; i++) {
    if (rpart[i] == -1) { 
      for (nnbrs=0, j=rowptr[i]; j<rowptr[i+1]; j++) {
        me = cpart[rowind[j]];
        if (nbrmrk[me] == -1) {
          nbrdom[nnbrs] = me; 
          nbrwgt[nnbrs] = 1; 
          nbrmrk[me] = nnbrs++;
        }
        else {
          nbrwgt[nbrmrk[me]]++;
        }
      }
      ASSERT(nnbrs > 0);

      /* assign it first to the domain with most things in common */
      rpart[i] = nbrdom[iargmax(nnbrs, nbrwgt,1)];

      /* if overweight, assign it to the light domain */
      if (pwgts[rpart[i]] > itpwgts[rpart[i]]) {
        for (j=0; j<nnbrs; j++) {
          if (pwgts[nbrdom[j]] < itpwgts[nbrdom[j]] ||
              pwgts[nbrdom[j]]-itpwgts[nbrdom[j]] < pwgts[rpart[i]]-itpwgts[rpart[i]]) {
            rpart[i] = nbrdom[j];
            break;
          }
        }
      }
      pwgts[rpart[i]]++;

      /* reset nbrmrk array */
      for (j=0; j<nnbrs; j++) 
        nbrmrk[nbrdom[j]] = -1;
    }
  }

  gk_free((void **)&pwgts, &nbrdom, &nbrwgt, &nbrmrk, &itpwgts, LTERM);
  return METIS_OK;

MEMORY_ERROR:
  error = errno == 0 ? ENOMEM : errno;
  gk_free((void **)&pwgts, &nbrdom, &nbrwgt, &nbrmrk, &itpwgts, LTERM);
  errno = error;
  return METIS_ERROR_MEMORY;
}
