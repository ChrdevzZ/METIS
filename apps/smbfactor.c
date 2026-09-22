/*
 * Copyright 1997, Regents of the University of Minnesota
 *
 * smbfactor.c
 *
 * This file performs the symbolic factorization of a matrix
 *
 * Started 8/1/97
 * George
 *
 * $Id: smbfactor.c 10154 2011-06-09 21:27:35Z karypis $
 *
 */

#include "metisbin.h"


#ifdef METIS_SMBFACTOR_TEST
static int factor_allocation_count;
static int factor_failed_allocation;


/*************************************************************************/
/*! Selects the allocation that the symbolic-factorization test will fail. */
/*************************************************************************/
void metis_smbfactor_test_fail_allocation(int allocation)
{
  factor_allocation_count = 0;
  factor_failed_allocation = allocation;
}
#endif


/*************************************************************************/
/*! Allocates one factorization array without exposing an allocation signal
    to the application driver. */
/*************************************************************************/
static int AllocateFactorArray(size_t count, idx_t value, const char *message,
    idx_t **r_array)
{
  volatile int sigrval=0;

  *r_array = NULL;
  if (!gk_sigtrap()) {
    errno = ENOMEM;
    return 0;
  }
  METIS_SIGCATCH(sigrval);
  if (sigrval == 0) {
#ifdef METIS_SMBFACTOR_TEST
    factor_allocation_count++;
    if (factor_allocation_count == factor_failed_allocation) {
      errno = ENOMEM;
      gk_errexit(SIGMEM, "Injected symbolic-factorization allocation failure");
    }
    else
#endif
      *r_array = ismalloc(count, value, message);
  }
  gk_siguntrap();

  if (sigrval != 0) {
    if (errno == 0)
      errno = ENOMEM;
    return 0;
  }
  return *r_array != NULL;
}


/*************************************************************************/
/*! This function sets up data structures for fill-in computations */
/*************************************************************************/
int ComputeFillIn(graph_t *graph, idx_t *perm, idx_t *iperm,
         uint64_t *r_maxlnz, uint64_t *r_opc)
{
  idx_t i, nvtxs, maxlnz, maxsub;
  idx_t *xadj, *adjncy;
  idx_t *xlnz=NULL, *xnzsub=NULL, *nzsub=NULL;
  size_t count;
  /* Fill statistics can exceed size_t on a 32-bit host. */
  uint64_t knz, opc=0;
  int factor_status, renumbered=0, saved_errno=0, status=METIS_ERROR_MEMORY;

/*
  printf("\nSymbolic factorization... --------------------------------------------\n");
*/

  if (graph == NULL || perm == NULL || iperm == NULL ||
      r_maxlnz == NULL || r_opc == NULL)
    return METIS_ERROR_INPUT;
  nvtxs  = graph->nvtxs;
  xadj   = graph->xadj;
  adjncy = graph->adjncy;
  if (nvtxs < 0 || xadj == NULL)
    return METIS_ERROR_INPUT;
  if ((uintmax_t)nvtxs+2 > (uintmax_t)SIZE_MAX/sizeof(idx_t) ||
      (uintmax_t)nvtxs > (uintmax_t)(IDX_MAX-1)/8) {
    errno = EOVERFLOW;
    return METIS_ERROR_MEMORY;
  }
  if (xadj[nvtxs] < 0 || (xadj[nvtxs] > 0 && adjncy == NULL))
    return METIS_ERROR_INPUT;
  if ((uintmax_t)nvtxs+(uintmax_t)xadj[nvtxs] >
      (uintmax_t)(IDX_MAX-1)/8) {
    errno = EOVERFLOW;
    return METIS_ERROR_MEMORY;
  }
  if (xadj[0] != 0)
    return METIS_ERROR_INPUT;
  for (i=0; i<nvtxs; i++) {
    if (xadj[i] < 0 || xadj[i] > xadj[i+1])
      return METIS_ERROR_INPUT;
  }
  for (i=0; i<xadj[nvtxs]; i++) {
    if (adjncy[i] < 0 || adjncy[i] >= nvtxs)
      return METIS_ERROR_INPUT;
  }
  for (i=0; i<nvtxs; i++) {
    if (perm[i] < 0 || perm[i] >= nvtxs ||
        iperm[i] < 0 || iperm[i] >= nvtxs)
      return METIS_ERROR_INPUT;
  }
  for (i=0; i<nvtxs; i++) {
    if (iperm[perm[i]] != i || perm[iperm[i]] != i)
      return METIS_ERROR_INPUT;
  }

  maxsub = 8*(nvtxs+xadj[nvtxs]);
  if ((uintmax_t)maxsub+1 > (uintmax_t)SIZE_MAX/sizeof(idx_t)) {
    errno = EOVERFLOW;
    return METIS_ERROR_MEMORY;
  }

  /* Allocate before relabeling so allocation failure leaves the graph and
     permutation arrays unchanged. */
  count = (size_t)nvtxs+2;
  if (!AllocateFactorArray(count, 0, "ComputeFillIn: xlnz", &xlnz) ||
      !AllocateFactorArray(count, 0, "ComputeFillIn: xnzsub", &xnzsub) ||
      !AllocateFactorArray((size_t)maxsub+1, 0, "ComputeFillIn: nzsub",
          &nzsub))
    goto cleanup;

  /* Relabel the vertices so that it starts from 1 */
  for (i=0; i<xadj[nvtxs]; i++)
    adjncy[i]++;
  for (i=0; i<nvtxs+1; i++)
    xadj[i]++;
  for (i=0; i<nvtxs; i++) {
    iperm[i]++;
    perm[i]++;
  }
  renumbered = 1;

  
  /* Call sparspak's routine. */
  factor_status = smbfct(nvtxs, xadj, adjncy, perm, iperm, xlnz, &maxlnz,
      xnzsub, nzsub, &maxsub);
  while (factor_status > 0) {
    printf("Realocating nzsub...\n");
    gk_free((void **)&nzsub, LTERM);

    if (maxsub > (IDX_MAX-1)/2 ||
        (uintmax_t)maxsub >
            ((uintmax_t)SIZE_MAX/sizeof(idx_t)-1)/2) {
      errno = EOVERFLOW;
      goto cleanup;
    }
    maxsub *= 2;
    if (!AllocateFactorArray((size_t)maxsub+1, 0,
        "ComputeFillIn: nzsub", &nzsub))
      goto cleanup;
    factor_status = smbfct(nvtxs, xadj, adjncy, perm, iperm, xlnz,
        &maxlnz, xnzsub, nzsub, &maxsub);
  }
  if (factor_status < 0)
    goto cleanup;

  for (i=0; i<nvtxs; i++)
    xlnz[i+1]--;
  for (i=0; i<nvtxs; i++) {
    knz = (uint64_t)(xlnz[i+2]-xlnz[i+1]);
    if (knz > 0 && knz-1 > (UINT64_MAX-opc)/knz) {
      errno = EOVERFLOW;
      goto cleanup;
    }
    opc += knz*(knz-1);
  }

  *r_maxlnz = (uint64_t)maxlnz;
  *r_opc    = opc;
  status = METIS_OK;

cleanup:
  if (status != METIS_OK)
    saved_errno = errno != 0 ? errno : ENOMEM;
  gk_free((void **)&xlnz, &xnzsub, &nzsub, LTERM);

  /* Relabel the vertices so that it starts from 0 */
  if (renumbered) {
    for (i=0; i<nvtxs; i++) {
      iperm[i]--;
      perm[i]--;
    }
    for (i=0; i<nvtxs+1; i++)
      xadj[i]--;
    for (i=0; i<xadj[nvtxs]; i++)
      adjncy[i]--;
  }
  if (status != METIS_OK)
    errno = saved_errno;

  return status;
}



/*************************************************************************/
/*!
  PURPOSE - THIS ROUTINE PERFORMS SYMBOLIC FACTORIZATION               
  ON A PERMUTED LINEAR SYSTEM AND IT ALSO SETS UP THE               
  COMPRESSED DATA STRUCTURE FOR THE SYSTEM.                         

  INPUT PARAMETERS -                                               
     NEQNS - NUMBER OF EQUATIONS.                                 
     (XADJ, ADJNCY) - THE ADJACENCY STRUCTURE.                   
     (PERM, INVP) - THE PERMUTATION VECTOR AND ITS INVERSE.     

  UPDATED PARAMETERS -                                         
     MAXSUB - SIZE OF THE SUBSCRIPT ARRAY NZSUB.  ON RETURN,  
            IT CONTAINS THE NUMBER OF SUBSCRIPTS USED        

  OUTPUT PARAMETERS -                                       
     XLNZ - INDEX INTO THE NONZERO STORAGE VECTOR LNZ.   
     (XNZSUB, NZSUB) - THE COMPRESSED SUBSCRIPT VECTORS. 
     MAXLNZ - THE NUMBER OF NONZEROS FOUND.             
*/
/*************************************************************************/
idx_t smbfct(idx_t neqns, idx_t *xadj, idx_t *adjncy, idx_t *perm, idx_t *invp, 
	       idx_t *xlnz, idx_t *maxlnz, idx_t *xnzsub, idx_t *nzsub, 
               idx_t *maxsub)
{
  /* Local variables */
  idx_t node, rchm, mrgk, lmax, i, j, k, m, nabor, nzbeg, nzend;
  idx_t kxsub, jstop, jstrt, mrkflg, inz, knz, flag;
  idx_t *mrglnk=NULL, *marker=NULL, *rchlnk=NULL;
  int saved_errno=0;

  if (neqns == 0) {
    *maxlnz = 0;
    *maxsub = 0;
    return 0;
  }
  if (neqns < 0) {
    errno = EINVAL;
    return -1;
  }
  if (neqns == IDX_MAX || (uintmax_t)neqns+1 >
      (uintmax_t)SIZE_MAX/sizeof(idx_t)) {
    errno = EOVERFLOW;
    return -1;
  }
  if (!AllocateFactorArray((size_t)neqns+1, 0, "smbfct: rchlnk", &rchlnk) ||
      !AllocateFactorArray((size_t)neqns+1, 0, "smbfct: marker", &marker) ||
      !AllocateFactorArray((size_t)neqns+1, 0, "smbfct: mgrlnk", &mrglnk)) {
    saved_errno = errno != 0 ? errno : ENOMEM;
    gk_free((void **)&rchlnk, &marker, &mrglnk, LTERM);
    errno = saved_errno;
    return -1;
  }

  /* Function Body */
  flag    = 0;
  nzbeg   = 1;
  nzend   = 0;
  xlnz[1] = 1;

  /* FOR EACH COLUMN KNZ COUNTS THE NUMBER OF NONZEROS IN COLUMN K ACCUMULATED IN RCHLNK. */
  for (k=1; k<=neqns; k++) {
    xnzsub[k] = nzend;
    node      = perm[k-1];
    knz       = 0;
    mrgk      = mrglnk[k];
    mrkflg    = 0;
    marker[k] = k;
    if (mrgk != 0) {
      assert(mrgk > 0 && mrgk <= neqns);
      marker[k] = marker[mrgk];
    }

    if (xadj[node-1] >= xadj[node]) {
      xlnz[k+1] = xlnz[k];
      continue;
    }

    /* USE RCHLNK TO LINK THROUGH THE STRUCTURE OF A(*,K) BELOW DIAGONAL */
    assert(k <= neqns && k > 0);
    rchlnk[k] = neqns+1;
    for (j=xadj[node-1]; j<xadj[node]; j++) {
      nabor = invp[adjncy[j-1]-1];
      if (nabor <= k) 
        continue;
      rchm = k;

      do {
        m    = rchm;
        assert(m > 0 && m <= neqns);
        rchm = rchlnk[m];
      } while (rchm <= nabor); 

      knz++;
      assert(m > 0 && m <= neqns);
      rchlnk[m]     = nabor;
      assert(nabor > 0 && nabor <= neqns);
      rchlnk[nabor] = rchm;
      assert(k > 0 && k <= neqns);
      if (marker[nabor] != marker[k]) 
        mrkflg = 1;
    }


    /* TEST FOR MASS SYMBOLIC ELIMINATION */
    lmax = 0;
    assert(mrgk >= 0 && mrgk <= neqns);
    if (mrkflg != 0 || mrgk == 0 || mrglnk[mrgk] != 0) 
      goto L350;
    xnzsub[k] = xnzsub[mrgk] + 1;
    knz = xlnz[mrgk + 1] - xlnz[mrgk] - 1;
    goto L1400;


L350:
    /* LINK THROUGH EACH COLUMN I THAT AFFECTS L(*,K) */
    i = k;
    assert(i > 0 && i <= neqns);
    while ((i = mrglnk[i]) != 0) {
      assert(i > 0 && i <= neqns);
      inz   = xlnz[i+1] - xlnz[i] - 1;
      jstrt = xnzsub[i] + 1;
      jstop = xnzsub[i] + inz;

      if (inz > lmax) { 
        lmax      = inz;
        xnzsub[k] = jstrt;
      }

      /* MERGE STRUCTURE OF L(*,I) IN NZSUB INTO RCHLNK. */ 
      rchm = k;
      for (j=jstrt; j<=jstop; j++) {
        nabor = nzsub[j];
        do {
          m    = rchm;
          assert(m > 0 && m <= neqns);
          rchm = rchlnk[m];
        } while (rchm < nabor);

        if (rchm != nabor) {
          knz++;
          assert(m > 0 && m <= neqns);
          rchlnk[m]     = nabor;
          assert(nabor > 0 && nabor <= neqns);
          rchlnk[nabor] = rchm;
          rchm = nabor;
        }
      }
    }


    /* CHECK IF SUBSCRIPTS DUPLICATE THOSE OF ANOTHER COLUMN */
    if (knz == lmax) 
      goto L1400;

    /* OR IF TAIL OF K-1ST COLUMN MATCHES HEAD OF KTH */
    if (nzbeg > nzend) 
      goto L1200;

    assert(k > 0 && k <= neqns);
    i = rchlnk[k];
    for (jstrt = nzbeg; jstrt <= nzend; ++jstrt) {
      if (nzsub[jstrt] < i) 
        continue;

      if (nzsub[jstrt] == i) 
        goto L1000;
      else 
        goto L1200;
    }
    goto L1200;


L1000:
    xnzsub[k] = jstrt;
    for (j = jstrt; j <= nzend; ++j) {
      if (nzsub[j] != i) 
        goto L1200;
      
      assert(i > 0 && i <= neqns);
      i = rchlnk[i];
      if (i > neqns) 
        goto L1400;
    }
    nzend = jstrt - 1;


    /* COPY THE STRUCTURE OF L(*,K) FROM RCHLNK TO THE DATA STRUCTURE (XNZSUB, NZSUB) */
L1200:
    if (knz < 0 || nzend < 0) {
      errno = EOVERFLOW;
      flag = -1;
      break;
    }
    if (nzend >= *maxsub || knz >= *maxsub-nzend) {
      flag = 1; /* Out of memory */
      break;
    }
    nzbeg = nzend + 1;
    nzend += knz;

    i = k;
    for (j=nzbeg; j<=nzend; j++) {
      assert(i > 0 && i <= neqns);
      i = rchlnk[i];
      nzsub[j]  = i;
      assert(i > 0 && i <= neqns);
      marker[i] = k;
    }
    xnzsub[k] = nzbeg;
    assert(k > 0 && k <= neqns);
    marker[k] = k;

    /*
     * UPDATE THE VECTOR MRGLNK.  NOTE COLUMN L(*,K) JUST FOUND   
     * IS REQUIRED TO DETERMINE COLUMN L(*,J), WHERE              
     * L(J,K) IS THE FIRST NONZERO IN L(*,K) BELOW DIAGONAL.      
     */
L1400:
    if (knz > 1) { 
      kxsub = xnzsub[k];
      i = nzsub[kxsub];
      assert(i > 0 && i <= neqns);
      assert(k > 0 && k <= neqns);
      mrglnk[k] = mrglnk[i];
      mrglnk[i] = k;
    }

    if (knz > IDX_MAX-xlnz[k]) {
      errno = EOVERFLOW;
      flag = -1;
      break;
    }
    xlnz[k + 1] = xlnz[k] + knz;
  }

  if (flag == 0) {
    *maxlnz = xlnz[neqns] - 1;
    *maxsub = xnzsub[neqns];
    xnzsub[neqns + 1] = xnzsub[neqns];
  }

  if (flag < 0)
    saved_errno = errno != 0 ? errno : EOVERFLOW;
  gk_free((void **)&rchlnk, &mrglnk, &marker, LTERM);
  if (flag < 0)
    errno = saved_errno;

  return flag;
  
} 
