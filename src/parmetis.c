/*
 * Copyright 1997, Regents of the University of Minnesota
 *
 * parmetis.c
 *
 * This file contains top level routines that are used by ParMETIS
 *
 * Started 10/14/97
 * George
 *
 * $Id: parmetis.c 10481 2011-07-05 18:01:23Z karypis $
 *
 */

#include "metislib.h"
#include "input_validation.h"


/*************************************************************************/
/*! This function is the entry point for the node ND code for ParMETIS.
    The difference between this routine and the standard METIS_NodeND are
    the following
    
    - It performs at least log2(npes) levels of nested dissection.
    - It stores the size of the log2(npes) top-level separators in the
      sizes array.
*/
/*************************************************************************/
int METIS_NodeNDP(idx_t nvtxs, idx_t *xadj, idx_t *adjncy, idx_t *vwgt,
           idx_t npes, idx_t *options, idx_t *perm, idx_t *iperm, idx_t *sizes) 
{
  volatile int rstatus=METIS_OK, sigrval=0;
  idx_t i, ii, j, l;
  volatile idx_t nnvtxs=0;
  idx_t numflag, objtype;
  graph_t *graph;
  ctrl_t * volatile ctrl=NULL;
  idx_t *cptr, *cind;

  numflag = GETOPTION(options, METIS_OPTION_NUMBERING, 0);
  objtype = GETOPTION(options, METIS_OPTION_OBJTYPE, METIS_OBJTYPE_NODE);
  if (perm == NULL || iperm == NULL || sizes == NULL || npes <= 0 ||
      numflag != 0)
    return METIS_ERROR_INPUT;
  if (npes > IDX_MAX/2 ||
      (uintmax_t)npes*2-1 > (uintmax_t)SIZE_MAX/sizeof(idx_t)) {
    errno = EOVERFLOW;
    return METIS_ERROR_MEMORY;
  }
  rstatus = ValidateGraphInput(nvtxs, 1, xadj, adjncy, vwgt, NULL, NULL,
      numflag, objtype);
  if (rstatus != METIS_OK)
    return rstatus;

  /* set up malloc cleaning code and signal catchers */
  if (!gk_malloc_init())
    return METIS_ERROR_MEMORY;

  if (!gk_sigtrap()) {
    gk_malloc_cleanup(0);
    return METIS_ERROR_MEMORY;
  }

  METIS_SIGCATCH(sigrval);
  if (sigrval != 0)
    goto SIGTHROW;

  rstatus = SetupCtrl(METIS_OP_OMETIS, options, 1, 3, NULL, NULL,
      (ctrl_t **)&ctrl);
  if (rstatus != METIS_OK)
    goto SIGTHROW;

  IFSET(ctrl->dbglvl, METIS_DBG_TIME, InitTimers(ctrl));
  IFSET(ctrl->dbglvl, METIS_DBG_TIME, gk_startcputimer(ctrl->TotalTmr));

  /* compress the graph; not that compression only happens if not prunning 
     has taken place. */
  if (ctrl->compress) {
    cptr = imalloc(nvtxs+1, "OMETIS: cptr");
    cind = imalloc(nvtxs, "OMETIS: cind");
    if (cptr == NULL || cind == NULL) {
      rstatus = METIS_ERROR_MEMORY;
      goto SIGTHROW;
    }

    graph = CompressGraph(ctrl, nvtxs, xadj, adjncy, vwgt, cptr, cind);
    if (graph == NULL) {
      if (ctrl->status != METIS_OK) {
        rstatus = ctrl->status;
        goto SIGTHROW;
      }
      /* if there was no compression, cleanup the compress flag */
      gk_free((void **)&cptr, &cind, LTERM);
      ctrl->compress = 0;
    }
    else {
      nnvtxs = graph->nvtxs;
    }
  }

  /* if no compression, setup the graph in the normal way. */
  if (ctrl->compress == 0) {
    rstatus = SetupGraph((ctrl_t *)ctrl, nvtxs, 1, xadj, adjncy, vwgt,
        NULL, NULL, &graph);
    if (rstatus != METIS_OK)
      goto SIGTHROW;
  }


  /* allocate workspace memory */
  rstatus = AllocateWorkSpace((ctrl_t *)ctrl, graph);
  if (rstatus != METIS_OK)
    goto SIGTHROW;


  /* do the nested dissection ordering  */
  iset(2*npes-1, 0, sizes);
  MlevelNestedDissectionP(ctrl, graph, iperm, graph->nvtxs, npes, 0, sizes);
  if (ctrl->status != METIS_OK) {
    rstatus = ctrl->status;
    goto SIGTHROW;
  }


  /* Uncompress the ordering */
  if (ctrl->compress) { 
    /* construct perm from iperm */
    for (i=0; i<nnvtxs; i++)
      perm[iperm[i]] = i; 
    for (l=ii=0; ii<nnvtxs; ii++) {
      i = perm[ii];
      for (j=cptr[i]; j<cptr[i+1]; j++)
        iperm[cind[j]] = l++;
    }

    gk_free((void **)&cptr, &cind, LTERM);
  }


  for (i=0; i<nvtxs; i++)
    perm[iperm[i]] = i;

  IFSET(ctrl->dbglvl, METIS_DBG_TIME, gk_stopcputimer(ctrl->TotalTmr));
  IFSET(ctrl->dbglvl, METIS_DBG_TIME, PrintTimers(ctrl));

  /* clean up */
  FreeCtrl((ctrl_t **)&ctrl);

SIGTHROW:
  if (ctrl != NULL)
    graph_CleanupDiskFiles((ctrl_t *)ctrl);

  gk_siguntrap();
  gk_malloc_cleanup(0);

  return rstatus == METIS_OK ? metis_rcode(sigrval) : rstatus;
}


/*************************************************************************/
/*! This function is similar to MlevelNestedDissection with the difference
    that it also records separator sizes for the top log2(npes) levels */
/**************************************************************************/
void MlevelNestedDissectionP(ctrl_t *ctrl, graph_t *graph, idx_t *order, 
         idx_t lastvtx, idx_t npes, idx_t cpos, idx_t *sizes)
{
  idx_t i, j, nvtxs, nbnd;
  idx_t *label, *bndind;
  graph_t *lgraph, *rgraph;

  nvtxs = graph->nvtxs;

  if (nvtxs == 0) {
    FreeGraph(&graph);
    return;
  }

  MlevelNodeBisectionMultiple(ctrl, graph);
  if (ctrl->status != METIS_OK) {
    FreeGraph(&graph);
    return;
  }

  IFSET(ctrl->dbglvl, METIS_DBG_SEPINFO, 
      printf("Nvtxs: %6"PRIDX", [%6"PRIDX" %6"PRIDX" %6"PRIDX"]\n", 
        graph->nvtxs, graph->pwgts[0], graph->pwgts[1], graph->pwgts[2]));

  if (cpos < npes-1) {
    sizes[2*npes-2-cpos]       = graph->pwgts[2];
    sizes[2*npes-2-(2*cpos+1)] = graph->pwgts[1];
    sizes[2*npes-2-(2*cpos+2)] = graph->pwgts[0];
  }

  /* Order the nodes in the separator */
  nbnd   = graph->nbnd;
  bndind = graph->bndind;
  label  = graph->label;
  for (i=0; i<nbnd; i++) 
    order[label[bndind[i]]] = --lastvtx;

  SplitGraphOrder(ctrl, graph, &lgraph, &rgraph);
  if (ctrl->status != METIS_OK) {
    FreeGraph(&graph);
    FreeGraph(&lgraph);
    FreeGraph(&rgraph);
    return;
  }

  /* Free the memory of the top level graph */
  FreeGraph(&graph);

  if ((lgraph->nvtxs > MMDSWITCH || 2*cpos+2 < npes-1) && lgraph->nedges > 0) 
    MlevelNestedDissectionP(ctrl, lgraph, order, lastvtx-rgraph->nvtxs, npes, 2*cpos+2, sizes);
  else {
    MMDOrder(ctrl, lgraph, order, lastvtx-rgraph->nvtxs); 
    FreeGraph(&lgraph);
  }
  if (ctrl->status != METIS_OK) {
    FreeGraph(&rgraph);
    return;
  }
  if ((rgraph->nvtxs > MMDSWITCH || 2*cpos+1 < npes-1) && rgraph->nedges > 0) 
    MlevelNestedDissectionP(ctrl, rgraph, order, lastvtx, npes, 2*cpos+1, sizes);
  else {
    MMDOrder(ctrl, rgraph, order, lastvtx); 
    FreeGraph(&rgraph);
  }
}


/*************************************************************************/
/*! This function bisects a graph by computing a vertex separator 
*/
/**************************************************************************/
int METIS_ComputeVertexSeparator(idx_t *nvtxs, idx_t *xadj, idx_t *adjncy, 
           idx_t *vwgt, idx_t *options, idx_t *r_sepsize, idx_t *part) 
{
  volatile int renumber=0, rstatus=METIS_OK, sigrval=0;
  idx_t numflag, objtype;
  graph_t *graph;
  ctrl_t * volatile ctrl=NULL;

  if (nvtxs == NULL || r_sepsize == NULL || part == NULL)
    return METIS_ERROR_INPUT;
  numflag = GETOPTION(options, METIS_OPTION_NUMBERING, 0);
  objtype = GETOPTION(options, METIS_OPTION_OBJTYPE, METIS_OBJTYPE_NODE);
  rstatus = ValidateGraphInput(*nvtxs, 1, xadj, adjncy, vwgt, NULL, NULL,
      numflag, objtype);
  if (rstatus != METIS_OK)
    return rstatus;

  /* set up malloc cleaning code and signal catchers */
  if (!gk_malloc_init())
    return METIS_ERROR_MEMORY;

  if (!gk_sigtrap()) {
    gk_malloc_cleanup(0);
    return METIS_ERROR_MEMORY;
  }

  METIS_SIGCATCH(sigrval);
  if (sigrval != 0)
    goto SIGTHROW;

  rstatus = SetupCtrl(METIS_OP_OMETIS, options, 1, 3, NULL, NULL,
      (ctrl_t **)&ctrl);
  if (rstatus != METIS_OK)
    goto SIGTHROW;

  if (ctrl->numflag == 1) {
    Change2CNumbering(*nvtxs, xadj, adjncy);
    renumber = 1;
  }

  InitRandom(ctrl->seed);

  rstatus = SetupGraph((ctrl_t *)ctrl, *nvtxs, 1, xadj, adjncy, vwgt,
      NULL, NULL, &graph);
  if (rstatus != METIS_OK)
    goto SIGTHROW;

  rstatus = AllocateWorkSpace((ctrl_t *)ctrl, graph);
  if (rstatus != METIS_OK)
    goto SIGTHROW;

  /*============================================================
   * Perform the bisection
   *============================================================*/ 
  ctrl->CoarsenTo = 100;

  MlevelNodeBisectionMultiple(ctrl, graph);
  if (ctrl->status != METIS_OK) {
    rstatus = ctrl->status;
    goto SIGTHROW;
  }

  *r_sepsize = graph->pwgts[2];
  icopy(*nvtxs, graph->where, part);

  FreeGraph(&graph);

  FreeCtrl((ctrl_t **)&ctrl);

SIGTHROW:
  if (ctrl != NULL)
    graph_CleanupDiskFiles((ctrl_t *)ctrl);

  if (renumber)
    Change2FNumbering2(*nvtxs, xadj, adjncy);

  gk_siguntrap();
  gk_malloc_cleanup(0);

  return rstatus == METIS_OK ? metis_rcode(sigrval) : rstatus;
}


/*************************************************************************/
/*! Validates the separator state consumed by METIS_NodeRefine. */
/*************************************************************************/
static int ValidateNodeRefinement(idx_t nvtxs, idx_t *xadj, idx_t *adjncy,
    idx_t *vwgt, idx_t *where, idx_t *hmarker, real_t ubfactor)
{
  int status;
  idx_t i, j, other;
  uintmax_t maxpwgt, pwgts[2] = {0, 0};

  if (where == NULL || hmarker == NULL || !isfinite(ubfactor) ||
      ubfactor <= 1.0)
    return METIS_ERROR_INPUT;
  status = ValidateGraphInput(nvtxs, 1, xadj, adjncy, vwgt, NULL, NULL,
      0, METIS_OBJTYPE_NODE);
  if (status != METIS_OK)
    return status;

  for (i=0; i<nvtxs; i++) {
    if (where[i] < 0 || where[i] > 2 ||
        hmarker[i] < -1 || hmarker[i] > 2)
      return METIS_ERROR_INPUT;
    if (where[i] < 2)
      pwgts[where[i]] += (uintmax_t)(vwgt == NULL ? 1 : vwgt[i]);

    if (where[i] != 2) {
      for (j=xadj[i]; j<xadj[i+1]; j++) {
        other = where[adjncy[j]];
        if (other != 2 && other != where[i])
          return METIS_ERROR_INPUT;
      }
    }
  }

  maxpwgt = gk_max(pwgts[0], pwgts[1]);
  if ((long double)ubfactor*(long double)maxpwgt >= (long double)IDX_MAX)
    return METIS_ERROR_INPUT;

  return METIS_OK;
}


/*************************************************************************/
/*! This function is the entry point of a node-based separator refinement
    of the nodes with an hmarker[] of 0. */
/*************************************************************************/
int METIS_NodeRefine(idx_t nvtxs, idx_t *xadj, idx_t *vwgt, idx_t *adjncy, 
           idx_t *where, idx_t *hmarker, real_t ubfactor)
{
  volatile int rstatus=METIS_OK, sigrval=0;
  graph_t *graph;
  ctrl_t * volatile ctrl=NULL;

  rstatus = ValidateNodeRefinement(nvtxs, xadj, adjncy, vwgt, where,
      hmarker, ubfactor);
  if (rstatus != METIS_OK)
    return rstatus;

  /* set up malloc cleaning code and signal catchers */
  if (!gk_malloc_init())
    return METIS_ERROR_MEMORY;

  if (!gk_sigtrap()) {
    gk_malloc_cleanup(0);
    return METIS_ERROR_MEMORY;
  }

  METIS_SIGCATCH(sigrval);
  if (sigrval != 0)
    goto SIGTHROW;

  /* set up the run time parameters */
  rstatus = SetupCtrl(METIS_OP_OMETIS, NULL, 1, 3, NULL, NULL,
      (ctrl_t **)&ctrl);
  if (rstatus != METIS_OK)
    goto SIGTHROW;

  /* set up the graph */
  rstatus = SetupGraph((ctrl_t *)ctrl, nvtxs, 1, xadj, adjncy, vwgt,
      NULL, NULL, &graph);
  if (rstatus != METIS_OK)
    goto SIGTHROW;

  /* allocate workspace memory */
  rstatus = AllocateWorkSpace((ctrl_t *)ctrl, graph);
  if (rstatus != METIS_OK)
    goto SIGTHROW;

  /* set up the memory and the input partition */
  rstatus = Allocate2WayNodePartitionMemory(ctrl, graph);
  if (rstatus != METIS_OK)
    goto SIGTHROW;
  icopy(nvtxs, where, graph->where);

  Compute2WayNodePartitionParams(ctrl, graph);

  FM_2WayNodeRefine1SidedP(ctrl, graph, hmarker, ubfactor, 10); 
  if (ctrl->status != METIS_OK) {
    rstatus = ctrl->status;
    goto SIGTHROW;
  }
  /* FM_2WayNodeRefine2SidedP(ctrl, graph, hmarker, ubfactor, 10); */

  icopy(nvtxs, graph->where, where);

  FreeGraph(&graph);
  FreeCtrl((ctrl_t **)&ctrl);

SIGTHROW:
  if (ctrl != NULL)
    graph_CleanupDiskFiles((ctrl_t *)ctrl);

  gk_siguntrap();
  gk_malloc_cleanup(0);

  return rstatus == METIS_OK ? metis_rcode(sigrval) : rstatus;
}


/*************************************************************************/
/*! This function performs a node-based 1-sided FM refinement that moves
    only nodes whose hmarker[] == -1. It is used by Parmetis. */
/*************************************************************************/
void FM_2WayNodeRefine1SidedP(ctrl_t *ctrl, graph_t *graph, 
          idx_t *hmarker, real_t ubfactor, idx_t npasses)
{
  idx_t i, ii, j, k, jj, kk, nvtxs, nbnd, nswaps, nmind, nbad, qsize;
  idx_t *xadj, *vwgt, *adjncy, *where, *pwgts, *edegrees, *bndind, *bndptr;
  idx_t *mptr, *mind, *swaps, *inqueue;
  rpq_t *queue; 
  nrinfo_t *rinfo;
  idx_t higain, oldgain, mincut, initcut, mincutorder;	
  idx_t pass, from, to, limit;
  idx_t badmaxpwgt, mindiff, newdiff;
  size_t mindsize;

  if (!WCOREPUSH)
    return;

  ASSERT(graph->mincut == graph->pwgts[2]);

  nvtxs  = graph->nvtxs;
  xadj   = graph->xadj;
  adjncy = graph->adjncy;
  vwgt   = graph->vwgt;

  bndind = graph->bndind;
  bndptr = graph->bndptr;
  where  = graph->where;
  pwgts  = graph->pwgts;
  rinfo  = graph->nrinfo;

  if ((uintmax_t)nvtxs > (uintmax_t)SIZE_MAX/2 ||
      2*(size_t)nvtxs > SIZE_MAX/sizeof(idx_t)) {
    ctrl->status = METIS_ERROR_MEMORY;
    WCOREPOP;
    return;
  }
  mindsize = 2*(size_t)nvtxs;

  queue = rpqCreate(nvtxs);
      
  inqueue = iwspacemalloc(ctrl, nvtxs);
  swaps   = iwspacemalloc(ctrl, nvtxs);
  mptr    = iwspacemalloc(ctrl, nvtxs+1);
  mind    = (idx_t *)wspacemalloc(ctrl, mindsize*sizeof(idx_t));
  if (queue == NULL || inqueue == NULL || swaps == NULL || mptr == NULL ||
      mind == NULL) {
    ctrl->status = METIS_ERROR_MEMORY;
    if (queue != NULL)
      rpqDestroy(queue);
    WCOREPOP;
    return;
  }
  iset(nvtxs, -1, inqueue);

  badmaxpwgt = (idx_t)(ubfactor*gk_max(pwgts[0], pwgts[1]));

  IFSET(ctrl->dbglvl, METIS_DBG_REFINE,
    printf("Partitions-N1: [%6"PRIDX" %6"PRIDX"] Nv-Nb[%6"PRIDX" %6"PRIDX"] "
           "MaxPwgt[%6"PRIDX"]. ISep: %6"PRIDX"\n", 
           pwgts[0], pwgts[1], graph->nvtxs, graph->nbnd, badmaxpwgt, 
           graph->mincut));

  to = (pwgts[0] < pwgts[1] ? 1 : 0);
  for (pass=0; pass<npasses; pass++) {
    from = to; 
    to   = (from+1)%2;

    rpqReset(queue);

    mincutorder = -1;
    initcut = mincut = graph->mincut;
    nbnd = graph->nbnd;

    /* use the swaps array in place of the traditional perm array to save memory */
    irandArrayPermute(nbnd, swaps, nbnd, 1);
    for (ii=0; ii<nbnd; ii++) {
      i = bndind[swaps[ii]];
      ASSERT(where[i] == 2);
      if (hmarker[i] == -1 || hmarker[i] == to) {
        rpqInsert(queue, i, vwgt[i]-rinfo[i].edegrees[from]);
        inqueue[i] = pass;
      }
    }
    qsize = rpqLength(queue);

    ASSERT(CheckNodeBnd(graph, nbnd));
    ASSERT(CheckNodePartitionParams(graph));

    limit = nbnd;

    /******************************************************
    * Get into the FM loop
    *******************************************************/
    mptr[0] = nmind = nbad = 0;
    mindiff = iabs(pwgts[0]-pwgts[1]);
    for (nswaps=0; nswaps<nvtxs; nswaps++) {
      if ((higain = rpqGetTop(queue)) == -1) 
        break;

      ASSERT(bndptr[higain] != -1);

      /* The following check is to ensure we break out if there is a possibility
         of over-running the mind array.  */
      if ((uintmax_t)nmind+
          (uintmax_t)(xadj[higain+1]-xadj[higain]) >=
          (uintmax_t)mindsize-1)
        break;

      inqueue[higain] = -1;

      if (pwgts[to]+vwgt[higain] > badmaxpwgt) { /* Skip this vertex */
        if (nbad++ > limit) 
          break; 
        else {
          nswaps--;
          continue;  
        }
      }

      pwgts[2] -= (vwgt[higain]-rinfo[higain].edegrees[from]);

      newdiff = iabs(pwgts[to]+vwgt[higain] - (pwgts[from]-rinfo[higain].edegrees[from]));
      if (pwgts[2] < mincut || (pwgts[2] == mincut && newdiff < mindiff)) {
        mincut      = pwgts[2];
        mincutorder = nswaps;
        mindiff     = newdiff;
        nbad        = 0;
      }
      else {
        if (nbad++ > limit) {
          pwgts[2] += (vwgt[higain]-rinfo[higain].edegrees[from]);
          break; /* No further improvement, break out */
        }
      }

      BNDDelete(nbnd, bndind, bndptr, higain);
      pwgts[to] += vwgt[higain];
      where[higain] = to;
      swaps[nswaps] = higain;  


      /**********************************************************
      * Update the degrees of the affected nodes
      ***********************************************************/
      for (j=xadj[higain]; j<xadj[higain+1]; j++) {
        k = adjncy[j];
        if (where[k] == 2) { /* For the in-separator vertices modify their edegree[to] */
          rinfo[k].edegrees[to] += vwgt[higain];
        }
        else if (where[k] == from) { /* This vertex is pulled into the separator */
          ASSERTP(bndptr[k] == -1, ("%"PRIDX" %"PRIDX" %"PRIDX"\n", k, bndptr[k], where[k]));
          BNDInsert(nbnd, bndind, bndptr, k);

          mind[nmind++] = k;  /* Keep track for rollback */
          where[k]      = 2;
          pwgts[from]  -= vwgt[k];

          edegrees = rinfo[k].edegrees;
          edegrees[0] = edegrees[1] = 0;
          for (jj=xadj[k]; jj<xadj[k+1]; jj++) {
            kk = adjncy[jj];
            if (where[kk] != 2) 
              edegrees[where[kk]] += vwgt[kk];
            else {
              oldgain = vwgt[kk]-rinfo[kk].edegrees[from];
              rinfo[kk].edegrees[from] -= vwgt[k];

              /* Update the gain of this node if it was not skipped */
              if (inqueue[kk] == pass)
                rpqUpdate(queue, kk, oldgain+vwgt[k]); 
            }
          }

          /* Insert the new vertex into the priority queue. Safe due to one-sided moves */
          if (hmarker[k] == -1 || hmarker[k] == to) {
            rpqInsert(queue, k, vwgt[k]-edegrees[from]);
            inqueue[k] = pass;
          }
        }
      }
      mptr[nswaps+1] = nmind;


      IFSET(ctrl->dbglvl, METIS_DBG_MOVEINFO,
            printf("Moved %6"PRIDX" to %3"PRIDX", Gain: %5"PRIDX" [%5"PRIDX"] \t[%5"PRIDX" %5"PRIDX" %5"PRIDX"] [%3"PRIDX" %2"PRIDX"]\n", 
                   higain, to, (vwgt[higain]-rinfo[higain].edegrees[from]), 
                   vwgt[higain], pwgts[0], pwgts[1], pwgts[2], nswaps, limit));

    }


    /****************************************************************
    * Roll back computation 
    *****************************************************************/
    for (nswaps--; nswaps>mincutorder; nswaps--) {
      higain = swaps[nswaps];

      ASSERT(CheckNodePartitionParams(graph));
      ASSERT(where[higain] == to);

      INC_DEC(pwgts[2], pwgts[to], vwgt[higain]);
      where[higain] = 2;
      BNDInsert(nbnd, bndind, bndptr, higain);

      edegrees = rinfo[higain].edegrees;
      edegrees[0] = edegrees[1] = 0;
      for (j=xadj[higain]; j<xadj[higain+1]; j++) {
        k = adjncy[j];
        if (where[k] == 2) 
          rinfo[k].edegrees[to] -= vwgt[higain];
        else
          edegrees[where[k]] += vwgt[k];
      }

      /* Push nodes out of the separator */
      for (j=mptr[nswaps]; j<mptr[nswaps+1]; j++) {
        k = mind[j];
        ASSERT(where[k] == 2);
        where[k] = from;
        INC_DEC(pwgts[from], pwgts[2], vwgt[k]);
        BNDDelete(nbnd, bndind, bndptr, k);
        for (jj=xadj[k]; jj<xadj[k+1]; jj++) {
          kk = adjncy[jj];
          if (where[kk] == 2) 
            rinfo[kk].edegrees[from] += vwgt[k];
        }
      }
    }

    ASSERT(mincut == pwgts[2]);

    IFSET(ctrl->dbglvl, METIS_DBG_REFINE,
      printf("\tMinimum sep: %6"PRIDX" at %5"PRIDX", PWGTS: [%6"PRIDX" %6"PRIDX"], NBND: %6"PRIDX", QSIZE: %6"PRIDX"\n", 
          mincut, mincutorder, pwgts[0], pwgts[1], nbnd, qsize));

    graph->mincut = mincut;
    graph->nbnd   = nbnd;

    if (pass%2 == 1 && (mincutorder == -1 || mincut >= initcut))
      break;
  }

  rpqDestroy(queue);

  WCOREPOP;
}


/*************************************************************************/
/*! This function performs a node-based (two-sided) FM refinement that 
    moves only nodes whose hmarker[] == -1. It is used by Parmetis. */
/*************************************************************************/
void FM_2WayNodeRefine2SidedP(ctrl_t *ctrl, graph_t *graph, 
          idx_t *hmarker, real_t ubfactor, idx_t npasses)
{
  idx_t i, ii, j, k, jj, kk, nvtxs, nbnd, nswaps, nmind;
  idx_t *xadj, *vwgt, *adjncy, *where, *pwgts, *edegrees, *bndind, *bndptr;
  idx_t *mptr, *mind, *moved, *swaps;
  rpq_t *queues[2]; 
  nrinfo_t *rinfo;
  idx_t higain, oldgain, mincut, initcut, mincutorder;	
  idx_t pass, to, other, limit;
  idx_t badmaxpwgt, mindiff, newdiff;
  idx_t u[2], g[2];
  size_t mindsize;

  if (!WCOREPUSH)
    return;

  nvtxs  = graph->nvtxs;
  xadj   = graph->xadj;
  adjncy = graph->adjncy;
  vwgt   = graph->vwgt;

  bndind = graph->bndind;
  bndptr = graph->bndptr;
  where  = graph->where;
  pwgts  = graph->pwgts;
  rinfo  = graph->nrinfo;

  if ((uintmax_t)nvtxs > (uintmax_t)SIZE_MAX/2 ||
      2*(size_t)nvtxs > SIZE_MAX/sizeof(idx_t)) {
    ctrl->status = METIS_ERROR_MEMORY;
    WCOREPOP;
    return;
  }
  mindsize = 2*(size_t)nvtxs;

  queues[0] = rpqCreate(nvtxs);
  queues[1] = rpqCreate(nvtxs);

  moved = iwspacemalloc(ctrl, nvtxs);
  swaps = iwspacemalloc(ctrl, nvtxs);
  mptr  = iwspacemalloc(ctrl, nvtxs+1);
  mind  = (idx_t *)wspacemalloc(ctrl, mindsize*sizeof(idx_t));
  if (queues[0] == NULL || queues[1] == NULL || moved == NULL ||
      swaps == NULL || mptr == NULL || mind == NULL) {
    ctrl->status = METIS_ERROR_MEMORY;
    if (queues[0] != NULL)
      rpqDestroy(queues[0]);
    if (queues[1] != NULL)
      rpqDestroy(queues[1]);
    WCOREPOP;
    return;
  }

  IFSET(ctrl->dbglvl, METIS_DBG_REFINE,
    printf("Partitions: [%6"PRIDX" %6"PRIDX"] Nv-Nb[%6"PRIDX" %6"PRIDX"]. ISep: %6"PRIDX"\n", pwgts[0], pwgts[1], graph->nvtxs, graph->nbnd, graph->mincut));

  badmaxpwgt = (idx_t)(ubfactor*gk_max(pwgts[0], pwgts[1]));

  for (pass=0; pass<npasses; pass++) {
    iset(nvtxs, -1, moved);
    rpqReset(queues[0]);
    rpqReset(queues[1]);

    mincutorder = -1;
    initcut = mincut = graph->mincut;
    nbnd = graph->nbnd;

    /* use the swaps array in place of the traditional perm array to save memory */
    irandArrayPermute(nbnd, swaps, nbnd, 1);
    for (ii=0; ii<nbnd; ii++) {
      i = bndind[swaps[ii]];
      ASSERT(where[i] == 2);
      if (hmarker[i] == -1) {
        rpqInsert(queues[0], i, vwgt[i]-rinfo[i].edegrees[1]);
        rpqInsert(queues[1], i, vwgt[i]-rinfo[i].edegrees[0]);
        moved[i] = -5;
      }
      else if (hmarker[i] != 2) {
        rpqInsert(queues[hmarker[i]], i, vwgt[i]-rinfo[i].edegrees[(hmarker[i]+1)%2]);
        moved[i] = -(10+hmarker[i]);
      }
    }

    ASSERT(CheckNodeBnd(graph, nbnd));
    ASSERT(CheckNodePartitionParams(graph));

    limit = nbnd;

    /******************************************************
    * Get into the FM loop
    *******************************************************/
    mptr[0] = nmind = 0;
    mindiff = iabs(pwgts[0]-pwgts[1]);
    to = (pwgts[0] < pwgts[1] ? 0 : 1);
    for (nswaps=0; nswaps<nvtxs; nswaps++) {
      u[0] = rpqSeeTopVal(queues[0]);  
      u[1] = rpqSeeTopVal(queues[1]);
      if (u[0] != -1 && u[1] != -1) {
        g[0] = vwgt[u[0]]-rinfo[u[0]].edegrees[1];
        g[1] = vwgt[u[1]]-rinfo[u[1]].edegrees[0];

        to = (g[0] > g[1] ? 0 : (g[0] < g[1] ? 1 : pass%2)); 

        if (pwgts[to]+vwgt[u[to]] > badmaxpwgt) 
          to = (to+1)%2;
      }
      else if (u[0] == -1 && u[1] == -1) {
        break;
      }
      else if (u[0] != -1 && pwgts[0]+vwgt[u[0]] <= badmaxpwgt) {
        to = 0;
      }
      else if (u[1] != -1 && pwgts[1]+vwgt[u[1]] <= badmaxpwgt) {
        to = 1;
      }
      else
        break;

      other = (to+1)%2;

      higain = rpqGetTop(queues[to]);
      if ((ctrl->dbglvl&METIS_DBG_MOVEINFO) && u[other] == -1)
        g[to] = vwgt[higain]-rinfo[higain].edegrees[other];

      /* Delete its matching entry in the other queue */
      if (moved[higain] == -5) 
        rpqDelete(queues[other], higain);

      ASSERT(bndptr[higain] != -1);

      /* The following check is to ensure we break out if there is a possibility
         of over-running the mind array.  */
      if ((uintmax_t)nmind+
          (uintmax_t)(xadj[higain+1]-xadj[higain]) >=
          (uintmax_t)mindsize-1)
        break;

      pwgts[2] -= (vwgt[higain]-rinfo[higain].edegrees[other]);

      newdiff = iabs(pwgts[to]+vwgt[higain] - (pwgts[other]-rinfo[higain].edegrees[other]));
      if (pwgts[2] < mincut || (pwgts[2] == mincut && newdiff < mindiff)) {
        mincut      = pwgts[2];
        mincutorder = nswaps;
        mindiff     = newdiff;
      }
      else {
        if (nswaps - mincutorder > limit) {
          pwgts[2] += (vwgt[higain]-rinfo[higain].edegrees[other]);
          break; /* No further improvement, break out */
        }
      }

      BNDDelete(nbnd, bndind, bndptr, higain);
      pwgts[to] += vwgt[higain];
      where[higain] = to;
      moved[higain] = nswaps;
      swaps[nswaps] = higain;  


      /**********************************************************
      * Update the degrees of the affected nodes
      ***********************************************************/
      for (j=xadj[higain]; j<xadj[higain+1]; j++) {
        k = adjncy[j];
        if (where[k] == 2) { /* For the in-separator vertices modify their edegree[to] */
          oldgain = vwgt[k]-rinfo[k].edegrees[to];
          rinfo[k].edegrees[to] += vwgt[higain];
          if (moved[k] == -5 || moved[k] == -(10+other)) 
            rpqUpdate(queues[other], k, oldgain-vwgt[higain]);
        }
        else if (where[k] == other) { /* This vertex is pulled into the separator */
          ASSERTP(bndptr[k] == -1, ("%"PRIDX" %"PRIDX" %"PRIDX"\n", k, bndptr[k], where[k]));
          BNDInsert(nbnd, bndind, bndptr, k);

          mind[nmind++] = k;  /* Keep track for rollback */
          where[k] = 2;
          pwgts[other] -= vwgt[k];

          edegrees = rinfo[k].edegrees;
          edegrees[0] = edegrees[1] = 0;
          for (jj=xadj[k]; jj<xadj[k+1]; jj++) {
            kk = adjncy[jj];
            if (where[kk] != 2) 
              edegrees[where[kk]] += vwgt[kk];
            else {
              oldgain = vwgt[kk]-rinfo[kk].edegrees[other];
              rinfo[kk].edegrees[other] -= vwgt[k];
              if (moved[kk] == -5 || moved[kk] == -(10+to))
                rpqUpdate(queues[to], kk, oldgain+vwgt[k]);
            }
          }

          /* Insert the new vertex into the priority queue (if it has not been moved). */
          if (moved[k] == -1 && (hmarker[k] == -1 || hmarker[k] == to)) {
            rpqInsert(queues[to], k, vwgt[k]-edegrees[other]);
            moved[k] = -(10+to);
          }
#ifdef FULLMOVES  /* this does not work as well as the above partial one */
          if (moved[k] == -1) {
            if (hmarker[k] == -1) {
              rpqInsert(queues[0], k, vwgt[k]-edegrees[1]);
              rpqInsert(queues[1], k, vwgt[k]-edegrees[0]);
              moved[k] = -5;
            }
            else if (hmarker[k] != 2) {
              rpqInsert(queues[hmarker[k]], k, vwgt[k]-edegrees[(hmarker[k]+1)%2]);
              moved[k] = -(10+hmarker[k]);
            }
          }
#endif
        }
      }
      mptr[nswaps+1] = nmind;

      if (ctrl->dbglvl&METIS_DBG_MOVEINFO) {
        if (u[other] == -1) {
          printf("Moved %6"PRIDX" to %3"PRIDX", Gain: %5"PRIDX" [  N/A] "
                 "[%4"PRIDX"  N/A] \t[%5"PRIDX" %5"PRIDX" %5"PRIDX"]\n",
                 higain, to, g[to], vwgt[u[to]],
                 pwgts[0], pwgts[1], pwgts[2]);
        }
        else {
          printf("Moved %6"PRIDX" to %3"PRIDX", Gain: %5"PRIDX" [%5"PRIDX"] "
                 "[%4"PRIDX" %4"PRIDX"] \t[%5"PRIDX" %5"PRIDX" %5"PRIDX"]\n",
                 higain, to, g[to], g[other], vwgt[u[to]], vwgt[u[other]],
                 pwgts[0], pwgts[1], pwgts[2]);
        }
      }

    }


    /****************************************************************
    * Roll back computation 
    *****************************************************************/
    for (nswaps--; nswaps>mincutorder; nswaps--) {
      higain = swaps[nswaps];

      ASSERT(CheckNodePartitionParams(graph));

      to = where[higain];
      other = (to+1)%2;
      INC_DEC(pwgts[2], pwgts[to], vwgt[higain]);
      where[higain] = 2;
      BNDInsert(nbnd, bndind, bndptr, higain);

      edegrees = rinfo[higain].edegrees;
      edegrees[0] = edegrees[1] = 0;
      for (j=xadj[higain]; j<xadj[higain+1]; j++) {
        k = adjncy[j];
        if (where[k] == 2) 
          rinfo[k].edegrees[to] -= vwgt[higain];
        else
          edegrees[where[k]] += vwgt[k];
      }

      /* Push nodes out of the separator */
      for (j=mptr[nswaps]; j<mptr[nswaps+1]; j++) {
        k = mind[j];
        ASSERT(where[k] == 2);
        where[k] = other;
        INC_DEC(pwgts[other], pwgts[2], vwgt[k]);
        BNDDelete(nbnd, bndind, bndptr, k);
        for (jj=xadj[k]; jj<xadj[k+1]; jj++) {
          kk = adjncy[jj];
          if (where[kk] == 2) 
            rinfo[kk].edegrees[other] += vwgt[k];
        }
      }
    }

    ASSERT(mincut == pwgts[2]);

    IFSET(ctrl->dbglvl, METIS_DBG_REFINE,
      printf("\tMinimum sep: %6"PRIDX" at %5"PRIDX", PWGTS: [%6"PRIDX" %6"PRIDX"], NBND: %6"PRIDX"\n", mincut, mincutorder, pwgts[0], pwgts[1], nbnd));

    graph->mincut = mincut;
    graph->nbnd = nbnd;

    if (mincutorder == -1 || mincut >= initcut)
      break;
  }

  rpqDestroy(queues[0]);
  rpqDestroy(queues[1]);

  WCOREPOP;
}


/*************************************************************************/
/*! This function computes a cache-friendly permutation of each partition.
    The resulting permutation is returned in old2new, which is a vector of 
    size nvtxs such for vertex i, old2new[i] is its new vertex number. 
*/
/**************************************************************************/
int METIS_CacheFriendlyReordering(idx_t nvtxs, idx_t *xadj, idx_t *adjncy, 
           idx_t *part, idx_t *old2new) 
{
  volatile int rstatus=METIS_OK, sigrval=0;
  idx_t degree, i, j, k, first, last, lastlevel, maxdegree, nparts;
  idx_t *cot, *pos, *pwgts;
  ikv_t *levels;
  uintmax_t key, level;

  if (part == NULL || old2new == NULL)
    return METIS_ERROR_INPUT;
  rstatus = ValidateGraphInput(nvtxs, 1, xadj, adjncy, NULL, NULL, NULL,
      0, METIS_OBJTYPE_NODE);
  if (rstatus != METIS_OK)
    return rstatus;

  nparts = 0;
  for (i=0; i<nvtxs; i++) {
    if (part[i] < 0 || part[i] > IDX_MAX-2)
      return METIS_ERROR_INPUT;
    nparts = gk_max(nparts, part[i]);
  }
  nparts++;
  if ((uintmax_t)nparts+1 > (uintmax_t)SIZE_MAX/sizeof(idx_t)) {
    errno = EOVERFLOW;
    return METIS_ERROR_MEMORY;
  }

  /* set up malloc cleaning code and signal catchers */
  if (!gk_malloc_init())
    return METIS_ERROR_MEMORY;

  if (!gk_sigtrap()) {
    gk_malloc_cleanup(0);
    return METIS_ERROR_MEMORY;
  }

  METIS_SIGCATCH(sigrval);
  if (sigrval != 0)
    goto SIGTHROW;

  InitRandom(123);

  /* This array ([C]losed[O]pen[T]odo => cot) serves three purposes.
     Positions from [0...first) is the current iperm[] vector of the explored vertices;
     Positions from [first...last) is the OPEN list (i.e., visited vertices);
     Positions from [last...nvtxs) is the todo list. */
  cot = imalloc(nvtxs, "METIS_CacheFriendlyReordering: cor");
  if (cot == NULL) {
    rstatus = METIS_ERROR_MEMORY;
    goto SIGTHROW;
  }
  iincset(nvtxs, 0, cot);

  /* This array will function like pos + touched of the CC method */
  pos = imalloc(nvtxs, "METIS_CacheFriendlyReordering: pos");
  if (pos == NULL) {
    rstatus = METIS_ERROR_MEMORY;
    goto SIGTHROW;
  }
  iincset(nvtxs, 0, pos);

  /* pick a random starting vertex */
  i = irandInRange(nvtxs);
  pos[0] = cot[0] = i;
  pos[i] = cot[i] = 0;

  /* compute a BFS ordering */
  first = last = 0;
  lastlevel = 0;
  maxdegree = 0;
  while (first < nvtxs) {
    if (first == last) { /* Find another starting vertex */
      k = cot[last];
      ASSERT(pos[k] >= 0);
      pos[k] = --lastlevel; /* mark node as being visited by assigning its current level (-ve) */
      last++;
    }

    i = cot[first++];
    maxdegree = (maxdegree < xadj[i+1]-xadj[i] ? xadj[i+1]-xadj[i] : maxdegree);
    for (j=xadj[i]; j<xadj[i+1]; j++) {
      k = adjncy[j];
      /* if a node has been already been visited, its pos[] will be -1 */
      if (pos[k] >= 0) {
        /* pos[k] is the location within cot of where k resides (it is in the 'todo' part); 
           put in that location cot[last] that we are about to overwrite 
           and update pos[cot[last]] to reflect that. */
        cot[pos[k]]    = cot[last];
        pos[cot[last]] = pos[k];

        cot[last++] = k;  /* put node at the end of the "queue" */
        pos[k]      = pos[i]-1; /* mark node as being visited by assigning to next level */
        lastlevel   = pos[k]; /* for correctly advancing the levels in case of disconnected graphs */
      }
    }
  }
//  printf("lastlevel: %d\n", (int)-lastlevel);

  /* sort based on decreasing level and decreasing degree (RCM) */
  levels = ikvmalloc(nvtxs, "METIS_CacheFriendlyReordering: levels");
  if (levels == NULL) {
    rstatus = METIS_ERROR_MEMORY;
    goto SIGTHROW;
  }
  if (maxdegree == IDX_MAX) {
    rstatus = METIS_ERROR_INPUT;
    goto SIGTHROW;
  }
  maxdegree++;
  for (i=0; i<nvtxs; i++) {
    degree = xadj[i+1]-xadj[i];
    level = (uintmax_t)(-pos[i]);
    if (level > ((uintmax_t)IDX_MAX-(uintmax_t)degree)/
                (uintmax_t)maxdegree) {
      rstatus = METIS_ERROR_INPUT;
      goto SIGTHROW;
    }
    key = level*(uintmax_t)maxdegree+(uintmax_t)degree;
    levels[i].val = i;
    levels[i].key = (idx_t)key;
  }
  ikvsortd(nvtxs, levels); 

  /* figure out the partitions */
  pwgts  = ismalloc(nparts+1, 0, "METIS_CacheFriendlyReordering: pwgts");
  if (pwgts == NULL) {
    rstatus = METIS_ERROR_MEMORY;
    goto SIGTHROW;
  }

  for (i=0; i<nvtxs; i++) 
    pwgts[part[i]]++;
  MAKECSR(i, nparts, pwgts);

  for (i=0; i<nvtxs; i++) 
    old2new[levels[i].val] = pwgts[part[levels[i].val]]++;

#ifdef XXX
  for (i=0; i<nvtxs; i++)
    for (j=xadj[i]; j<xadj[i+1]; j++)
      printf("COO: %d %d\n", (int)old2new[i], (int)old2new[adjncy[j]]);
#endif 

  gk_free((void **)&cot, &pos, &levels, &pwgts, LTERM);

SIGTHROW:
  gk_siguntrap();
  gk_malloc_cleanup(0);

  return rstatus == METIS_OK ? metis_rcode(sigrval) : rstatus;
}
