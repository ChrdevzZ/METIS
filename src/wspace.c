/*!
\file 
\brief Functions dealing with memory allocation and workspace management

\date Started 2/24/96
\author George
\author Copyright 1997-2009, Regents of the University of Minnesota 
\version $Id: wspace.c 10492 2011-07-06 09:28:42Z karypis $
*/

#include "metislib.h"


/*************************************************************************/
/*! Multiplies two allocation dimensions without wrapping size_t. */
/*************************************************************************/
static int wspaceMultiplySize(size_t left, size_t right, size_t *result)
{
  if (left != 0 && right > SIZE_MAX/left)
    return 0;
  *result = left*right;
  return 1;
}


/*************************************************************************/
/*! Adds an allocation component without changing the total on overflow. */
/*************************************************************************/
static int wspaceAddSize(size_t *total, size_t value)
{
  if (value > SIZE_MAX-*total)
    return 0;
  *total += value;
  return 1;
}


/*************************************************************************/
/*! Computes the persistent workspace size using checked size_t arithmetic. */
/*************************************************************************/
static int wspaceComputeCoreSize(ctrl_t *ctrl, graph_t *graph,
    size_t *r_coresize)
{
  size_t bytes, count, part_count, total=0, vertex_count;

  if (ctrl == NULL || graph == NULL || ctrl->ncon <= 0 ||
      ctrl->nparts <= 0 || ctrl->nparts == IDX_MAX ||
      graph->nvtxs < 0 || graph->nvtxs == IDX_MAX ||
      (uintmax_t)ctrl->ncon > (uintmax_t)SIZE_MAX ||
      (uintmax_t)ctrl->nparts > (uintmax_t)SIZE_MAX-1 ||
      (uintmax_t)graph->nvtxs > (uintmax_t)SIZE_MAX-1)
    return 0;

  vertex_count = (size_t)graph->nvtxs+1;
  part_count = (size_t)ctrl->nparts+1;
  if (!wspaceMultiplySize(vertex_count,
          ctrl->optype == METIS_OP_PMETIS ? 3 : 4, &count) ||
      !wspaceMultiplySize(count, sizeof(idx_t), &bytes) ||
      !wspaceAddSize(&total, bytes) ||
      !wspaceMultiplySize(part_count, (size_t)ctrl->ncon, &count) ||
      !wspaceMultiplySize(count, 5, &count) ||
      !wspaceMultiplySize(count, sizeof(idx_t), &bytes) ||
      !wspaceAddSize(&total, bytes) ||
      !wspaceMultiplySize(count, sizeof(real_t), &bytes) ||
      !wspaceAddSize(&total, bytes))
    return 0;

  *r_coresize = total;
  return 1;
}


/*************************************************************************/
/*! Records an invalid workspace size for status-returning callers. */
/*************************************************************************/
static void wspaceReportSizeError(const char *function)
{
  (void)function;
  errno = EOVERFLOW;
}


/*************************************************************************/
/*! This function allocates memory for the workspace */
/*************************************************************************/
int AllocateWorkSpace(ctrl_t *ctrl, graph_t *graph)
{
  volatile int sigrval=0;
  size_t coresize;
  gk_mcore_t *old_mcore;
  gk_mcore_t * volatile new_mcore=NULL;

  if (ctrl == NULL || graph == NULL || ctrl->ncon <= 0 ||
      ctrl->nparts <= 0 || graph->nvtxs < 0) {
    errno = EINVAL;
    if (ctrl != NULL)
      ctrl->status = METIS_ERROR_INPUT;
    return METIS_ERROR_INPUT;
  }

  if (!wspaceComputeCoreSize(ctrl, graph, &coresize)) {
    wspaceReportSizeError("AllocateWorkSpace");
    if (ctrl != NULL)
      ctrl->status = METIS_ERROR_MEMORY;
    return METIS_ERROR_MEMORY;
  }
  if (!gk_sigtrap()) {
    errno = ENOMEM;
    ctrl->status = METIS_ERROR_MEMORY;
    return METIS_ERROR_MEMORY;
  }
  METIS_SIGCATCH(sigrval);
  if (sigrval == 0)
    new_mcore = gk_mcoreCreate(coresize);
  gk_siguntrap();
  if (sigrval != 0 || new_mcore == NULL) {
    if (errno == 0)
      errno = ENOMEM;
    ctrl->status = METIS_ERROR_MEMORY;
    return METIS_ERROR_MEMORY;
  }

  old_mcore = ctrl->mcore;
  ctrl->mcore = (gk_mcore_t *)new_mcore;
  ctrl->nbrpoolsize = 0;
  ctrl->nbrpoolcpos = 0;
  ctrl->status = METIS_OK;
  gk_mcoreDestroy(&old_mcore, ctrl->dbglvl&METIS_DBG_INFO);
  return METIS_OK;
}


/*************************************************************************/
/*! This function allocates refinement-specific memory for the workspace */
/*************************************************************************/
int AllocateRefinementWorkSpace(ctrl_t *ctrl, idx_t nbrpoolsize_max,
    idx_t nbrpoolsize)
{
  volatile int sigrval=0;
  cnbr_t *cleanup_cnbrpool;
  cnbr_t * volatile cnbrpool=NULL;
  vnbr_t *cleanup_vnbrpool;
  vnbr_t * volatile vnbrpool=NULL;
  double *cleanup_cnbrsqrt;
  double * volatile cnbrsqrt=NULL;
  idx_t *cleanup_pvec1, *cleanup_pvec2, *cleanup_maxnads, *cleanup_nads;
  idx_t * volatile pvec1=NULL, * volatile pvec2=NULL;
  idx_t * volatile maxnads=NULL, * volatile nads=NULL;
  idx_t **cleanup_adids, **cleanup_adwgts;
  idx_t ** volatile adids=NULL, ** volatile adwgts=NULL;
  idx_t i;
  size_t part_count, poolsize;

  if (ctrl == NULL || ctrl->nparts <= 0 || nbrpoolsize_max < 0 ||
      nbrpoolsize < 0 ||
      (ctrl->objtype != METIS_OBJTYPE_CUT &&
       ctrl->objtype != METIS_OBJTYPE_VOL)) {
    errno = EINVAL;
    if (ctrl != NULL)
      ctrl->status = METIS_ERROR_INPUT;
    return METIS_ERROR_INPUT;
  }
  if (ctrl->nparts == IDX_MAX ||
      (uintmax_t)ctrl->nparts > (uintmax_t)SIZE_MAX-1 ||
      (uintmax_t)nbrpoolsize_max > (uintmax_t)SIZE_MAX ||
      (uintmax_t)nbrpoolsize > (uintmax_t)SIZE_MAX ||
      (uintmax_t)ctrl->nparts+1 >
          (uintmax_t)SIZE_MAX/sizeof(double) ||
      (ctrl->minconn &&
       (uintmax_t)ctrl->nparts >
           (uintmax_t)SIZE_MAX/sizeof(idx_t *))) {
    wspaceReportSizeError("AllocateRefinementWorkSpace");
    if (ctrl != NULL)
      ctrl->status = METIS_ERROR_MEMORY;
    return METIS_ERROR_MEMORY;
  }

  part_count = (size_t)ctrl->nparts+1;
  poolsize = (size_t)nbrpoolsize;
  if ((ctrl->objtype == METIS_OBJTYPE_CUT &&
       poolsize > SIZE_MAX/sizeof(cnbr_t)) ||
      (ctrl->objtype == METIS_OBJTYPE_VOL &&
       poolsize > SIZE_MAX/sizeof(vnbr_t))) {
    wspaceReportSizeError("AllocateRefinementWorkSpace");
    ctrl->status = METIS_ERROR_MEMORY;
    return METIS_ERROR_MEMORY;
  }

  if (!gk_sigtrap()) {
    errno = ENOMEM;
    ctrl->status = METIS_ERROR_MEMORY;
    return METIS_ERROR_MEMORY;
  }
  METIS_SIGCATCH(sigrval);
  if (sigrval != 0)
    goto FAILURE;

  if (ctrl->objtype == METIS_OBJTYPE_CUT && poolsize > 0)
    cnbrpool = (cnbr_t *)gk_malloc(poolsize*sizeof(cnbr_t),
        "AllocateRefinementWorkSpace: cnbrpool");
  else if (ctrl->objtype == METIS_OBJTYPE_VOL && poolsize > 0)
    vnbrpool = (vnbr_t *)gk_malloc(poolsize*sizeof(vnbr_t),
        "AllocateRefinementWorkSpace: vnbrpool");
  if ((ctrl->objtype == METIS_OBJTYPE_CUT && poolsize > 0 &&
       cnbrpool == NULL) ||
      (ctrl->objtype == METIS_OBJTYPE_VOL && poolsize > 0 &&
       vnbrpool == NULL))
    goto FAILURE;


  /* Build the cnbrsqrt[] lookup table that replaces sqrt(nnbrs) in the k-way cut
     gain priority (see struct.h for the full rationale). nnbrs is in [0,nparts], so
     a table of nparts+1 doubles covers every index. Kept in double precision so the
     ed/sqrt(nnbrs) division reproduces the inline-sqrt result bit-for-bit. */
  cnbrsqrt = (double *)gk_malloc(part_count*sizeof(double),
      "AllocateRefinementWorkSpace: cnbrsqrt");
  if (cnbrsqrt == NULL)
    goto FAILURE;
  for (i=0; i<=ctrl->nparts; i++)
    cnbrsqrt[i] = sqrt((double)i);

  /* Allocate the memory for the sparse subdomain graph */
  if (ctrl->minconn) {
    pvec1   = imalloc(part_count, "AllocateRefinementWorkSpace: pvec1");
    pvec2   = imalloc(part_count, "AllocateRefinementWorkSpace: pvec2");
    maxnads = ismalloc(ctrl->nparts, INIT_MAXNAD,
        "AllocateRefinementWorkSpace: maxnads");
    nads    = imalloc(ctrl->nparts, "AllocateRefinementWorkSpace: nads");
    adids   = iAllocMatrix(ctrl->nparts, INIT_MAXNAD, 0,
        "AllocateRefinementWorkSpace: adids");
    adwgts  = iAllocMatrix(ctrl->nparts, INIT_MAXNAD, 0,
        "AllocateRefinementWorkSpace: adwgts");
    if (pvec1 == NULL || pvec2 == NULL || maxnads == NULL || nads == NULL ||
        adids == NULL || adwgts == NULL)
      goto FAILURE;
  }

  gk_siguntrap();

  if (ctrl->adids != NULL)
    iFreeMatrix(&ctrl->adids, ctrl->nparts, INIT_MAXNAD);
  if (ctrl->adwgts != NULL)
    iFreeMatrix(&ctrl->adwgts, ctrl->nparts, INIT_MAXNAD);
  gk_free((void **)&ctrl->cnbrpool, &ctrl->vnbrpool, &ctrl->cnbrsqrt,
      &ctrl->pvec1, &ctrl->pvec2, &ctrl->maxnads, &ctrl->nads, LTERM);

  ctrl->cnbrpool = (cnbr_t *)cnbrpool;
  ctrl->vnbrpool = (vnbr_t *)vnbrpool;
  ctrl->cnbrsqrt = (double *)cnbrsqrt;
  ctrl->pvec1 = (idx_t *)pvec1;
  ctrl->pvec2 = (idx_t *)pvec2;
  ctrl->maxnads = (idx_t *)maxnads;
  ctrl->nads = (idx_t *)nads;
  ctrl->adids = (idx_t **)adids;
  ctrl->adwgts = (idx_t **)adwgts;
  ctrl->nbrpoolsize_max = (size_t)nbrpoolsize_max;
  ctrl->nbrpoolsize = poolsize;
  ctrl->nbrpoolcpos = 0;
  ctrl->nbrpoolreallocs = 0;
  ctrl->status = METIS_OK;
  return METIS_OK;

FAILURE:
  cleanup_cnbrpool = (cnbr_t *)cnbrpool;
  cleanup_vnbrpool = (vnbr_t *)vnbrpool;
  cleanup_cnbrsqrt = (double *)cnbrsqrt;
  cleanup_pvec1 = (idx_t *)pvec1;
  cleanup_pvec2 = (idx_t *)pvec2;
  cleanup_maxnads = (idx_t *)maxnads;
  cleanup_nads = (idx_t *)nads;
  cleanup_adids = (idx_t **)adids;
  cleanup_adwgts = (idx_t **)adwgts;
  if (cleanup_adids != NULL)
    iFreeMatrix(&cleanup_adids, ctrl->nparts, INIT_MAXNAD);
  if (cleanup_adwgts != NULL)
    iFreeMatrix(&cleanup_adwgts, ctrl->nparts, INIT_MAXNAD);
  gk_free((void **)&cleanup_cnbrpool, &cleanup_vnbrpool, &cleanup_cnbrsqrt,
      &cleanup_pvec1, &cleanup_pvec2, &cleanup_maxnads, &cleanup_nads,
      LTERM);
  gk_siguntrap();
  if (errno == 0)
    errno = ENOMEM;
  ctrl->status = METIS_ERROR_MEMORY;
  return METIS_ERROR_MEMORY;
}


/*************************************************************************/
/*! This function frees the workspace */
/*************************************************************************/
void FreeWorkSpace(ctrl_t *ctrl)
{
  if (ctrl == NULL)
    return;
  gk_mcoreDestroy(&ctrl->mcore, ctrl->dbglvl&METIS_DBG_INFO);

  IFSET(ctrl->dbglvl, METIS_DBG_INFO,
      printf(" nbrpool statistics\n" 
             "        nbrpoolsize: %12zu   nbrpoolcpos: %12zu\n"
             "    nbrpoolreallocs: %12zu\n\n",
             ctrl->nbrpoolsize,  ctrl->nbrpoolcpos, 
             ctrl->nbrpoolreallocs));

  gk_free((void **)&ctrl->cnbrpool, &ctrl->vnbrpool, &ctrl->cnbrsqrt, LTERM);
  ctrl->nbrpoolsize_max = 0;
  ctrl->nbrpoolsize     = 0;
  ctrl->nbrpoolcpos     = 0;

  if (ctrl->minconn) {
    if (ctrl->adids != NULL)
      iFreeMatrix(&(ctrl->adids),  ctrl->nparts, INIT_MAXNAD);
    if (ctrl->adwgts != NULL)
      iFreeMatrix(&(ctrl->adwgts), ctrl->nparts, INIT_MAXNAD);

    gk_free((void **)&ctrl->pvec1, &ctrl->pvec2, 
        &ctrl->maxnads, &ctrl->nads, LTERM);
  }
}


/*************************************************************************/
/*! This function allocate space from the workspace/heap */
/*************************************************************************/
void *wspacemalloc(ctrl_t *ctrl, size_t nbytes)
{
  void *memory;

  if (ctrl == NULL || ctrl->mcore == NULL) {
    if (ctrl != NULL)
      ctrl->status = METIS_ERROR_MEMORY;
    return NULL;
  }
  memory = gk_mcoreMalloc(ctrl->mcore, nbytes);
  if (memory == NULL)
    ctrl->status = METIS_ERROR_MEMORY;
  return memory;
}


/*************************************************************************/
/*! This function sets a marker in the stack of malloc ops to be used
    subsequently for freeing purposes */
/*************************************************************************/
int wspacepush(ctrl_t *ctrl)
{
  size_t cmop;

  if (ctrl == NULL || ctrl->mcore == NULL) {
    if (ctrl != NULL)
      ctrl->status = METIS_ERROR_MEMORY;
    return 0;
  }

  cmop = ctrl->mcore->cmop;
  gk_mcorePush(ctrl->mcore);
  if (cmop == SIZE_MAX || ctrl->mcore->cmop != cmop+1 ||
      ctrl->mcore->mops[cmop].type != GK_MOPT_MARK) {
    ctrl->status = METIS_ERROR_MEMORY;
    return 0;
  }

  return 1;
}


/*************************************************************************/
/*! This function frees all mops since the last push */
/*************************************************************************/
void wspacepop(ctrl_t *ctrl)
{
  size_t i;

  if (ctrl == NULL || ctrl->mcore == NULL) {
    if (ctrl != NULL)
      ctrl->status = METIS_ERROR_MEMORY;
    return;
  }

  for (i=ctrl->mcore->cmop; i>0; i--) {
    if (ctrl->mcore->mops[i-1].type == GK_MOPT_MARK)
      break;
  }
  if (i == 0) {
    ctrl->status = METIS_ERROR_MEMORY;
    return;
  }

  gk_mcorePop(ctrl->mcore);
}


/*************************************************************************/
/*! This function allocate space from the core  */
/*************************************************************************/
idx_t *iwspacemalloc(ctrl_t *ctrl, idx_t n)
{
  if (n < 0 || (uintmax_t)n > (uintmax_t)SIZE_MAX/sizeof(idx_t)) {
    wspaceReportSizeError("iwspacemalloc");
    if (ctrl != NULL)
      ctrl->status = METIS_ERROR_MEMORY;
    return NULL;
  }
  return (idx_t *)wspacemalloc(ctrl, (size_t)n*sizeof(idx_t));
}


/*************************************************************************/
/*! This function allocate space from the core */
/*************************************************************************/
real_t *rwspacemalloc(ctrl_t *ctrl, idx_t n)
{
  if (n < 0 || (uintmax_t)n > (uintmax_t)SIZE_MAX/sizeof(real_t)) {
    wspaceReportSizeError("rwspacemalloc");
    if (ctrl != NULL)
      ctrl->status = METIS_ERROR_MEMORY;
    return NULL;
  }
  return (real_t *)wspacemalloc(ctrl, (size_t)n*sizeof(real_t));
}


/*************************************************************************/
/*! This function allocate space from the core  */
/*************************************************************************/
ikv_t *ikvwspacemalloc(ctrl_t *ctrl, idx_t n)
{
  if (n < 0 || (uintmax_t)n > (uintmax_t)SIZE_MAX/sizeof(ikv_t)) {
    wspaceReportSizeError("ikvwspacemalloc");
    if (ctrl != NULL)
      ctrl->status = METIS_ERROR_MEMORY;
    return NULL;
  }
  return (ikv_t *)wspacemalloc(ctrl, (size_t)n*sizeof(ikv_t));
}


/*************************************************************************/
/*! Reserves a pool interval and commits its state only after growth.

    The configured maximum bounds reservations made during one refinement
    pass. Requests that cannot fit that bound, idx_t, or size_t leave the
    pool pointer, capacity, cursor, and reallocation count unchanged.
*/
/*************************************************************************/
static idx_t nbrpoolGetNext(ctrl_t *ctrl, idx_t nnbrs, void **r_pool,
    size_t elmsize, const char *message)
{
  void *new_pool;
  size_t growth, limit, newcpos, newsize, requested, start;

  if (ctrl == NULL || r_pool == NULL || elmsize == 0 ||
      ctrl->nparts <= 0 || nnbrs < 0) {
    errno = EINVAL;
    if (ctrl != NULL)
      ctrl->status = METIS_ERROR_INPUT;
    return -1;
  }

  requested = (size_t)gk_min(ctrl->nparts, nnbrs);
  start = ctrl->nbrpoolcpos;
  if (requested > SIZE_MAX-start || start > (size_t)IDX_MAX) {
    wspaceReportSizeError("nbrpoolGetNext");
    ctrl->status = METIS_ERROR_MEMORY;
    return -1;
  }
  newcpos = start+requested;
  if (newcpos > (size_t)IDX_MAX) {
    wspaceReportSizeError("nbrpoolGetNext");
    ctrl->status = METIS_ERROR_MEMORY;
    return -1;
  }

  if (newcpos > ctrl->nbrpoolsize) {
    limit = SIZE_MAX/elmsize;
    if (newcpos > ctrl->nbrpoolsize_max || newcpos > limit) {
      wspaceReportSizeError("nbrpoolGetNext");
      ctrl->status = METIS_ERROR_MEMORY;
      return -1;
    }

    growth = requested > SIZE_MAX/10 ? SIZE_MAX : 10*requested;
    growth = gk_max(growth, ctrl->nbrpoolsize/2);
    if (ctrl->nbrpoolsize >= ctrl->nbrpoolsize_max ||
        growth > ctrl->nbrpoolsize_max-ctrl->nbrpoolsize)
      newsize = ctrl->nbrpoolsize_max;
    else
      newsize = ctrl->nbrpoolsize+growth;
    newsize = gk_min(newsize, limit);
    if (newsize < newcpos) {
      wspaceReportSizeError("nbrpoolGetNext");
      ctrl->status = METIS_ERROR_MEMORY;
      return -1;
    }

    new_pool = gk_realloc(*r_pool, newsize*elmsize, message);
    if (new_pool == NULL) {
      ctrl->status = METIS_ERROR_MEMORY;
      return -1;
    }

    *r_pool = new_pool;
    ctrl->nbrpoolsize = newsize;
    if (ctrl->nbrpoolreallocs != SIZE_MAX)
      ctrl->nbrpoolreallocs++;
  }

  ctrl->nbrpoolcpos = newcpos;
  return (idx_t)start;
}


/*************************************************************************/
/*! This function resets the cnbrpool */
/*************************************************************************/
void cnbrpoolReset(ctrl_t *ctrl)
{
  ctrl->nbrpoolcpos = 0;
}


/*************************************************************************/
/*! This function gets the next free index from cnbrpool */
/*************************************************************************/
idx_t cnbrpoolGetNext(ctrl_t *ctrl, idx_t nnbrs)
{
  if (ctrl == NULL) {
    errno = EINVAL;
    return -1;
  }
  return nbrpoolGetNext(ctrl, nnbrs, (void **)&ctrl->cnbrpool,
      sizeof(cnbr_t), "cnbrpoolGet: cnbrpool");
}


/*************************************************************************/
/*! This function resets the vnbrpool */
/*************************************************************************/
void vnbrpoolReset(ctrl_t *ctrl)
{
  ctrl->nbrpoolcpos = 0;
}


/*************************************************************************/
/*! This function gets the next free index from vnbrpool */
/*************************************************************************/
idx_t vnbrpoolGetNext(ctrl_t *ctrl, idx_t nnbrs)
{
  if (ctrl == NULL) {
    errno = EINVAL;
    return -1;
  }
  return nbrpoolGetNext(ctrl, nnbrs, (void **)&ctrl->vnbrpool,
      sizeof(vnbr_t), "vnbrpoolGet: vnbrpool");
}
