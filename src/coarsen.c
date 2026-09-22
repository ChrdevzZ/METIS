/*!
\file  
\brief Functions for computing matchings during graph coarsening

\date Started 7/23/97
\author George  
\author Copyright 1997-2011, Regents of the University of Minnesota 
\version\verbatim $Id: coarsen.c 20398 2016-11-22 17:17:12Z karypis $ \endverbatim
*/


#include "metislib.h"

#define UNMATCHEDFOR2HOP  0.10  /* The fraction of unmatched vertices that triggers 2-hop */
                                  

/*************************************************************************/
/*! This function takes a graph and creates a sequence of coarser graphs.
    It implements the coarsening phase of the multilevel paradigm. 
 */
/*************************************************************************/
graph_t *CoarsenGraph(ctrl_t *ctrl, graph_t *graph)
{
  idx_t i, eqewgts, level=0, matchstatus;

  IFSET(ctrl->dbglvl, METIS_DBG_TIME, gk_startcputimer(ctrl->CoarsenTmr));

  /* determine if the weights on the edges are all the same */
  for (eqewgts=1, i=1; i<graph->nedges; i++) {
    if (graph->adjwgt[0] != graph->adjwgt[i]) {
      eqewgts = 0;
      break;
    }
  }

  /* set the maximum allowed coarsest vertex weight */
  for (i=0; i<graph->ncon; i++)
    ctrl->maxvwgt[i] = 1.5*graph->tvwgt[i]/ctrl->CoarsenTo;

  do {
    IFSET(ctrl->dbglvl, METIS_DBG_COARSEN, PrintCGraphStats(ctrl, graph));

    /* allocate memory for cmap, if it has not already been done due to
       multiple cuts */
    if (graph->cmap == NULL) {
      graph->cmap = imalloc(graph->nvtxs, "CoarsenGraph: graph->cmap");
      if (graph->cmap == NULL)
        goto MEMORY_ERROR;
    }

    /* determine which matching scheme you will use */
    switch (ctrl->ctype) {
      case METIS_CTYPE_RM:
        matchstatus = Match_RM(ctrl, graph);
        break;
      case METIS_CTYPE_SHEM:
        if (eqewgts || graph->nedges == 0)
          matchstatus = Match_RM(ctrl, graph);
        else
          matchstatus = Match_SHEM(ctrl, graph);
        break;
      default:
        gk_errexit(SIGERR, "Unknown ctype: %d\n", ctrl->ctype);
        matchstatus = -1;
    }
    if (matchstatus < 0 || graph->coarser == NULL)
      goto MEMORY_ERROR;

    graph_WriteToDisk(ctrl, graph);

    graph = graph->coarser;
    eqewgts = 0;
    level++;

#if GKLIB_ASSERTIONS_ENABLED
    errno = 0;
    if (!CheckGraph(graph, 0, 1)) {
      if (errno == ENOMEM || errno == EOVERFLOW)
        goto MEMORY_ERROR;
      ASSERT(0);
    }
#endif

  } while (graph->nvtxs > ctrl->CoarsenTo && 
           graph->nvtxs < COARSEN_FRACTION*graph->finer->nvtxs && 
           graph->nedges > graph->nvtxs/2);

  IFSET(ctrl->dbglvl, METIS_DBG_COARSEN, PrintCGraphStats(ctrl, graph));
  IFSET(ctrl->dbglvl, METIS_DBG_TIME, gk_stopcputimer(ctrl->CoarsenTmr));

  return graph;

MEMORY_ERROR:
  ctrl->status = METIS_ERROR_MEMORY;
  IFSET(ctrl->dbglvl, METIS_DBG_TIME, gk_stopcputimer(ctrl->CoarsenTmr));
  return NULL;
}


/*************************************************************************/
/*! This function takes a graph and creates a sequence of nlevels coarser 
    graphs, where nlevels is an input parameter.
 */
/*************************************************************************/
graph_t *CoarsenGraphNlevels(ctrl_t *ctrl, graph_t *graph, idx_t nlevels)
{
  idx_t i, eqewgts, level, matchstatus;

  IFSET(ctrl->dbglvl, METIS_DBG_TIME, gk_startcputimer(ctrl->CoarsenTmr));

  /* determine if the weights on the edges are all the same */
  for (eqewgts=1, i=1; i<graph->nedges; i++) {
    if (graph->adjwgt[0] != graph->adjwgt[i]) {
      eqewgts = 0;
      break;
    }
  }

  /* set the maximum allowed coarsest vertex weight */
  for (i=0; i<graph->ncon; i++)
    ctrl->maxvwgt[i] = 1.5*graph->tvwgt[i]/ctrl->CoarsenTo;

  for (level=0; level<nlevels; level++) {
    IFSET(ctrl->dbglvl, METIS_DBG_COARSEN, PrintCGraphStats(ctrl, graph));

    /* allocate memory for cmap, if it has not already been done due to
       multiple cuts */
    if (graph->cmap == NULL) {
      graph->cmap = imalloc(graph->nvtxs, "CoarsenGraph: graph->cmap");
      if (graph->cmap == NULL)
        goto MEMORY_ERROR;
    }

    /* determine which matching scheme you will use */
    switch (ctrl->ctype) {
      case METIS_CTYPE_RM:
        matchstatus = Match_RM(ctrl, graph);
        break;
      case METIS_CTYPE_SHEM:
        if (eqewgts || graph->nedges == 0)
          matchstatus = Match_RM(ctrl, graph);
        else
          matchstatus = Match_SHEM(ctrl, graph);
        break;
      default:
        gk_errexit(SIGERR, "Unknown ctype: %d\n", ctrl->ctype);
        matchstatus = -1;
    }
    if (matchstatus < 0 || graph->coarser == NULL)
      goto MEMORY_ERROR;

    graph_WriteToDisk(ctrl, graph);

    graph = graph->coarser;
    eqewgts = 0;

#if GKLIB_ASSERTIONS_ENABLED
    errno = 0;
    if (!CheckGraph(graph, 0, 1)) {
      if (errno == ENOMEM || errno == EOVERFLOW)
        goto MEMORY_ERROR;
      ASSERT(0);
    }
#endif

    if (graph->nvtxs < ctrl->CoarsenTo || 
        graph->nvtxs > COARSEN_FRACTION*graph->finer->nvtxs || 
        graph->nedges < graph->nvtxs/2)
      break; 
  } 

  IFSET(ctrl->dbglvl, METIS_DBG_COARSEN, PrintCGraphStats(ctrl, graph));
  IFSET(ctrl->dbglvl, METIS_DBG_TIME, gk_stopcputimer(ctrl->CoarsenTmr));

  return graph;

MEMORY_ERROR:
  ctrl->status = METIS_ERROR_MEMORY;
  IFSET(ctrl->dbglvl, METIS_DBG_TIME, gk_stopcputimer(ctrl->CoarsenTmr));
  return NULL;
}


/*************************************************************************/
/*! This function finds a matching by randomly selecting one of the 
    unmatched adjacent vertices. 
 */
/**************************************************************************/
idx_t Match_RM(ctrl_t *ctrl, graph_t *graph)
{
  idx_t i, pi, ii, j, jj, jjinc, k, nvtxs, ncon, cnvtxs, maxidx, 
        last_unmatched, avgdegree, bnum;
  idx_t *xadj, *vwgt, *adjncy, *adjwgt, *maxvwgt;
  idx_t *match, *cmap, *degrees, *perm, *tperm;
  size_t nunmatched=0;

  if (!WCOREPUSH)
    return -1;

  IFSET(ctrl->dbglvl, METIS_DBG_TIME, gk_startcputimer(ctrl->MatchTmr));

  nvtxs  = graph->nvtxs;
  ncon   = graph->ncon;
  xadj   = graph->xadj;
  vwgt   = graph->vwgt;
  adjncy = graph->adjncy;
  adjwgt = graph->adjwgt;
  cmap   = graph->cmap;

  maxvwgt  = ctrl->maxvwgt;

  match   = iwspacemalloc(ctrl, nvtxs);
  perm    = iwspacemalloc(ctrl, nvtxs);
  tperm   = iwspacemalloc(ctrl, nvtxs);
  degrees = iwspacemalloc(ctrl, nvtxs);
  if (match == NULL || perm == NULL || tperm == NULL || degrees == NULL)
    goto MEMORY_ERROR;
  iset(nvtxs, UNMATCHED, match);

  /* Determine a "random" traversal order that is biased towards 
     low-degree vertices */
  irandArrayPermute(nvtxs, tperm, nvtxs/8, 1);

  avgdegree = 4.0*(xadj[nvtxs]/nvtxs);
  for (i=0; i<nvtxs; i++) {
    bnum = sqrt(1+xadj[i+1]-xadj[i]);
    degrees[i] = (bnum > avgdegree ? avgdegree : bnum);
  }
  BucketSortKeysInc(ctrl, nvtxs, avgdegree, degrees, tperm, perm);
  if (ctrl->status != METIS_OK)
    goto MEMORY_ERROR;


  /* Traverse the vertices and compute the matching */
  for (cnvtxs=0, last_unmatched=0, pi=0; pi<nvtxs; pi++) {
    i = perm[pi];

    if (match[i] == UNMATCHED) {  /* Unmatched */
      maxidx = i;

      if ((ncon == 1 ? vwgt[i] < maxvwgt[0] : ivecle(ncon, vwgt+i*ncon, maxvwgt))) {
        /* Deal with island vertices. Find a non-island and match it with. 
           The matching ignores ctrl->maxvwgt requirements */
        if (xadj[i] == xadj[i+1]) {
          last_unmatched = gk_max(pi, last_unmatched)+1;
          for (; last_unmatched<nvtxs; last_unmatched++) {
            j = perm[last_unmatched];
            if (match[j] == UNMATCHED) {
              maxidx = j;
              break;
            }
          }
        }
        else {
          /* Find a random matching, subject to maxvwgt constraints */
          if (ncon == 1) {
            /* single constraint version */
            for (j=xadj[i]; j<xadj[i+1]; j++) {
              k = adjncy[j];
              if (match[k] == UNMATCHED &&
                  vwgt[k] <= maxvwgt[0]-vwgt[i]) {
                maxidx = k;
                break;
              }
            }

            /* If it did not match, record for a 2-hop matching. */
            if (maxidx == i && vwgt[i] < maxvwgt[0]-vwgt[i]) {
              nunmatched++;
              maxidx = UNMATCHED;
            }
          }
          else {
            /* multi-constraint version */
            for (j=xadj[i]; j<xadj[i+1]; j++) {
              k = adjncy[j];
              if (match[k] == UNMATCHED && 
                  ivecaxpylez(ncon, 1, vwgt+i*ncon, vwgt+k*ncon, maxvwgt)) {
                maxidx = k;
                break;
              }
            }

            /* If it did not match, record for a 2-hop matching. */
            if (maxidx == i && ivecaxpylez(ncon, 2, vwgt+i*ncon, vwgt+i*ncon, maxvwgt)) {
              nunmatched++;
              maxidx = UNMATCHED;
            }
          }
        }
      }

      if (maxidx != UNMATCHED) {
        cnvtxs++;  /* cmap[] is (re)assigned in the renumbering pass below */
        match[i] = maxidx;
        match[maxidx] = i;
      }
    }
  }

  //printf("nunmatched: %zu\n", nunmatched);

  /* see if a 2-hop matching is required/allowed */
  if (!ctrl->no2hop && nunmatched > UNMATCHEDFOR2HOP*nvtxs) 
    cnvtxs = Match_2Hop(ctrl, graph, perm, match, cnvtxs, nunmatched);
  if (cnvtxs < 0)
    goto MEMORY_ERROR;


  /* match the final unmatched vertices with themselves and reorder the vertices 
     of the coarse graph for memory-friendly contraction */
  for (cnvtxs=0, i=0; i<nvtxs; i++) {
    if (match[i] == UNMATCHED) {
      match[i] = i;
      cmap[i]  = cnvtxs++;
    }
    else {
      if (i <= match[i]) 
        cmap[i] = cmap[match[i]] = cnvtxs++;
    }
  }

  if (CreateCoarseGraph(ctrl, graph, cnvtxs, match) != METIS_OK)
    goto MEMORY_ERROR;

  IFSET(ctrl->dbglvl, METIS_DBG_TIME, gk_stopcputimer(ctrl->MatchTmr));

  WCOREPOP;

  return cnvtxs;

MEMORY_ERROR:
  ctrl->status = METIS_ERROR_MEMORY;
  IFSET(ctrl->dbglvl, METIS_DBG_TIME, gk_stopcputimer(ctrl->MatchTmr));
  WCOREPOP;
  return -1;
}


/**************************************************************************/
/*! This function finds a matching using the HEM heuristic. The vertices 
    are visited based on increasing degree to ensure that all vertices are 
    given a chance to match with something. 
 */
/**************************************************************************/
idx_t Match_SHEM(ctrl_t *ctrl, graph_t *graph)
{
  idx_t i, pi, ii, j, jj, jjinc, k, nvtxs, ncon, cnvtxs, maxidx, maxwgt, 
        last_unmatched, avgdegree, bnum;
  idx_t *xadj, *vwgt, *adjncy, *adjwgt, *maxvwgt;
  idx_t *match, *cmap, *degrees, *perm, *tperm;
  size_t nunmatched=0;

  if (!WCOREPUSH)
    return -1;

  IFSET(ctrl->dbglvl, METIS_DBG_TIME, gk_startcputimer(ctrl->MatchTmr));

  nvtxs  = graph->nvtxs;
  ncon   = graph->ncon;
  xadj   = graph->xadj;
  vwgt   = graph->vwgt;
  adjncy = graph->adjncy;
  adjwgt = graph->adjwgt;
  cmap   = graph->cmap;

  maxvwgt  = ctrl->maxvwgt;

  match   = iwspacemalloc(ctrl, nvtxs);
  perm    = iwspacemalloc(ctrl, nvtxs);
  tperm   = iwspacemalloc(ctrl, nvtxs);
  degrees = iwspacemalloc(ctrl, nvtxs);
  if (match == NULL || perm == NULL || tperm == NULL || degrees == NULL)
    goto MEMORY_ERROR;
  iset(nvtxs, UNMATCHED, match);

  /* Determine a "random" traversal order that is biased towards low-degree vertices */
  irandArrayPermute(nvtxs, tperm, nvtxs/8, 1);

  avgdegree = 4.0*(xadj[nvtxs]/nvtxs);
  for (i=0; i<nvtxs; i++) {
    bnum = sqrt(1+xadj[i+1]-xadj[i]);
    degrees[i] = (bnum > avgdegree ? avgdegree : bnum);
  }
  BucketSortKeysInc(ctrl, nvtxs, avgdegree, degrees, tperm, perm);
  if (ctrl->status != METIS_OK)
    goto MEMORY_ERROR;


  /* Traverse the vertices and compute the matching */
  for (cnvtxs=0, last_unmatched=0, pi=0; pi<nvtxs; pi++) {
    i = perm[pi];

    if (match[i] == UNMATCHED) {  /* Unmatched */
      maxidx = i;
      maxwgt = -1;

      if ((ncon == 1 ? vwgt[i] < maxvwgt[0] : ivecle(ncon, vwgt+i*ncon, maxvwgt))) {
        /* Deal with island vertices. Find a non-island and match it with. 
           The matching ignores ctrl->maxvwgt requirements */
        if (xadj[i] == xadj[i+1]) { 
          last_unmatched = gk_max(pi, last_unmatched)+1;
          for (; last_unmatched<nvtxs; last_unmatched++) {
            j = perm[last_unmatched];
            if (match[j] == UNMATCHED) {
              maxidx = j;
              break;
            }
          }
        }
        else {
          /* Find a heavy-edge matching, subject to maxvwgt constraints */
          if (ncon == 1) {
            /* single constraint version */
            for (j=xadj[i]; j<xadj[i+1]; j++) {
              k = adjncy[j];
              if (maxwgt < adjwgt[j] && match[k] == UNMATCHED &&
                  vwgt[k] <= maxvwgt[0]-vwgt[i]) {
                maxidx = k;
                maxwgt = adjwgt[j];
              }
            }

            /* If it did not match, record for a 2-hop matching. */
            if (maxidx == i && vwgt[i] < maxvwgt[0]-vwgt[i]) {
              nunmatched++;
              maxidx = UNMATCHED;
            }
          }
          else {
            /* multi-constraint version */
            for (j=xadj[i]; j<xadj[i+1]; j++) {
              k = adjncy[j];
              if (match[k] == UNMATCHED && 
                  ivecaxpylez(ncon, 1, vwgt+i*ncon, vwgt+k*ncon, maxvwgt) &&
                  (maxwgt < adjwgt[j] || 
                   (maxwgt == adjwgt[j] && 
                    BetterVBalance(ncon, graph->invtvwgt, vwgt+i*ncon, 
                        vwgt+maxidx*ncon, vwgt+k*ncon)))) {
                maxidx = k;
                maxwgt = adjwgt[j];
              }
            }

            /* If it did not match, record for a 2-hop matching. */
            if (maxidx == i && ivecaxpylez(ncon, 2, vwgt+i*ncon, vwgt+i*ncon, maxvwgt)) {
              nunmatched++;
              maxidx = UNMATCHED;
            }
          }
        }
      }

      if (maxidx != UNMATCHED) {
        cnvtxs++;  /* cmap[] is (re)assigned in the renumbering pass below */
        match[i] = maxidx;
        match[maxidx] = i;
      }
    }
  }

  //printf("nunmatched: %zu\n", nunmatched);

  /* see if a 2-hop matching is required/allowed */
  if (!ctrl->no2hop && nunmatched > UNMATCHEDFOR2HOP*nvtxs) 
    cnvtxs = Match_2Hop(ctrl, graph, perm, match, cnvtxs, nunmatched);
  if (cnvtxs < 0)
    goto MEMORY_ERROR;


  /* match the final unmatched vertices with themselves and reorder the vertices 
     of the coarse graph for memory-friendly contraction */
  for (cnvtxs=0, i=0; i<nvtxs; i++) {
    if (match[i] == UNMATCHED) {
      match[i] = i;
      cmap[i] = cnvtxs++;
    }
    else {
      if (i <= match[i]) 
        cmap[i] = cmap[match[i]] = cnvtxs++;
    }
  }

  if (CreateCoarseGraph(ctrl, graph, cnvtxs, match) != METIS_OK)
    goto MEMORY_ERROR;

  IFSET(ctrl->dbglvl, METIS_DBG_TIME, gk_stopcputimer(ctrl->MatchTmr));

  WCOREPOP;

  return cnvtxs;

MEMORY_ERROR:
  ctrl->status = METIS_ERROR_MEMORY;
  IFSET(ctrl->dbglvl, METIS_DBG_TIME, gk_stopcputimer(ctrl->MatchTmr));
  WCOREPOP;
  return -1;
}


/*************************************************************************/
/*! This function matches the unmatched vertices using a 2-hop matching 
    that involves vertices that are two hops away from each other. */
/**************************************************************************/
idx_t Match_2Hop(ctrl_t *ctrl, graph_t *graph, idx_t *perm, idx_t *match, 
          idx_t cnvtxs, size_t nunmatched)
{

  cnvtxs = Match_2HopAny(ctrl, graph, perm, match, cnvtxs, &nunmatched, 2);
  if (cnvtxs < 0)
    return -1;
  cnvtxs = Match_2HopAll(ctrl, graph, perm, match, cnvtxs, &nunmatched, 64);
  if (cnvtxs < 0)
    return -1;
  if (nunmatched > 1.5*UNMATCHEDFOR2HOP*graph->nvtxs) 
    cnvtxs = Match_2HopAny(ctrl, graph, perm, match, cnvtxs, &nunmatched, 3);
  if (cnvtxs < 0)
    return -1;
  if (nunmatched > 2.0*UNMATCHEDFOR2HOP*graph->nvtxs) 
    cnvtxs = Match_2HopAny(ctrl, graph, perm, match, cnvtxs, &nunmatched, graph->nvtxs);

  return cnvtxs;
}


/*************************************************************************/
/*! This function matches the unmatched vertices whose degree is less than
    maxdegree using a 2-hop matching that involves vertices that are two 
    hops away from each other. 
    The requirement of the 2-hop matching is a simple non-empty overlap
    between the adjacency lists of the vertices. */
/**************************************************************************/
idx_t Match_2HopAny(ctrl_t *ctrl, graph_t *graph, idx_t *perm, idx_t *match, 
          idx_t cnvtxs, size_t *r_nunmatched, idx_t maxdegree)
{
  idx_t i, pi, ii, j, jj, k, nvtxs;
  idx_t *xadj, *adjncy, *colptr, *rowind;
  idx_t *cmap;
  size_t nunmatched;

  IFSET(ctrl->dbglvl, METIS_DBG_TIME, gk_startcputimer(ctrl->Aux3Tmr));

  nvtxs  = graph->nvtxs;
  xadj   = graph->xadj;
  adjncy = graph->adjncy;
  cmap   = graph->cmap;

  nunmatched = *r_nunmatched;

  /*IFSET(ctrl->dbglvl, METIS_DBG_COARSEN, printf("IN: nunmatched: %zu\t", nunmatched)); */

  /* create the inverted index */
  if (!WCOREPUSH) {
    IFSET(ctrl->dbglvl, METIS_DBG_TIME, gk_stopcputimer(ctrl->Aux3Tmr));
    return -1;
  }
  colptr = iwspacemalloc(ctrl, nvtxs+1);
  if (colptr == NULL)
    goto MEMORY_ERROR;
  iset(nvtxs, 0, colptr);
  for (i=0; i<nvtxs; i++) {
    if (match[i] == UNMATCHED && xadj[i+1]-xadj[i] < maxdegree) {
      for (j=xadj[i]; j<xadj[i+1]; j++)
        colptr[adjncy[j]]++;
    }
  }
  MAKECSR(i, nvtxs, colptr);

  rowind = iwspacemalloc(ctrl, colptr[nvtxs]);
  if (rowind == NULL)
    goto MEMORY_ERROR;
  for (pi=0; pi<nvtxs; pi++) {
    i = perm[pi];
    if (match[i] == UNMATCHED && xadj[i+1]-xadj[i] < maxdegree) {
      for (j=xadj[i]; j<xadj[i+1]; j++)
        rowind[colptr[adjncy[j]]++] = i;
    }
  }
  SHIFTCSR(i, nvtxs, colptr);

  /* compute matchings by going down the inverted index */
  for (pi=0; pi<nvtxs; pi++) {
    i = perm[pi];
    if (colptr[i+1]-colptr[i] < 2)
      continue;

    for (jj=colptr[i+1], j=colptr[i]; j<jj; j++) {
      if (match[rowind[j]] == UNMATCHED) {
        for (jj--; jj>j; jj--) {
          if (match[rowind[jj]] == UNMATCHED) {
            cnvtxs++;  /* cmap[] is assigned in the renumbering pass in the caller */
            match[rowind[j]]  = rowind[jj];
            match[rowind[jj]] = rowind[j];
            nunmatched -= 2;
            break;
          }
        }
      }
    }
  }
  WCOREPOP;

  /*IFSET(ctrl->dbglvl, METIS_DBG_COARSEN, printf("OUT: nunmatched: %zu\n", nunmatched)); */

  IFSET(ctrl->dbglvl, METIS_DBG_TIME, gk_stopcputimer(ctrl->Aux3Tmr));

  *r_nunmatched = nunmatched;
  return cnvtxs;

MEMORY_ERROR:
  ctrl->status = METIS_ERROR_MEMORY;
  WCOREPOP;
  IFSET(ctrl->dbglvl, METIS_DBG_TIME, gk_stopcputimer(ctrl->Aux3Tmr));
  return -1;
}


/*************************************************************************/
/*! This function matches the unmatched vertices whose degree is less than
    maxdegree using a 2-hop matching that involves vertices that are two 
    hops away from each other. 
    The requirement of the 2-hop matching is that of identical adjacency
    lists.
 */
/**************************************************************************/
idx_t Match_2HopAll(ctrl_t *ctrl, graph_t *graph, idx_t *perm, idx_t *match, 
          idx_t cnvtxs, size_t *r_nunmatched, idx_t maxdegree)
{
  idx_t i, pi, pk, ii, j, jj, k, nvtxs, mask, idegree, ncand;
  idx_t *xadj, *adjncy;
  idx_t *cmap, *mark;
  ikv_t *keys;
  size_t nunmatched;

  IFSET(ctrl->dbglvl, METIS_DBG_TIME, gk_startcputimer(ctrl->Aux3Tmr));

  nvtxs  = graph->nvtxs;
  xadj   = graph->xadj;
  adjncy = graph->adjncy;
  cmap   = graph->cmap;

  nunmatched = *r_nunmatched;
  mask = IDX_MAX/maxdegree;

  /*IFSET(ctrl->dbglvl, METIS_DBG_COARSEN, printf("IN: nunmatched: %zu\t", nunmatched)); */

  if (!WCOREPUSH) {
    IFSET(ctrl->dbglvl, METIS_DBG_TIME, gk_stopcputimer(ctrl->Aux3Tmr));
    return -1;
  }

  /* collapse vertices with identical adjacency lists */
  if ((uintmax_t)nunmatched > (uintmax_t)IDX_MAX)
    goto MEMORY_ERROR;
  keys = ikvwspacemalloc(ctrl, (idx_t)nunmatched);
  if (keys == NULL)
    goto MEMORY_ERROR;
  for (ncand=0, pi=0; pi<nvtxs; pi++) {
    i = perm[pi];
    idegree = xadj[i+1]-xadj[i];
    if (match[i] == UNMATCHED && idegree > 1 && idegree < maxdegree) {
      for (k=0, j=xadj[i]; j<xadj[i+1]; j++) 
        k += adjncy[j]%mask;
      keys[ncand].val = i;
      keys[ncand].key = (k%mask)*maxdegree + idegree;
      ncand++;
    }
  }
  ikvsorti(ncand, keys);

  mark = iwspacemalloc(ctrl, nvtxs);
  if (mark == NULL)
    goto MEMORY_ERROR;
  iset(nvtxs, 0, mark);
  for (pi=0; pi<ncand; pi++) {
    i = keys[pi].val;
    if (match[i] != UNMATCHED)
      continue;

    for (j=xadj[i]; j<xadj[i+1]; j++)
      mark[adjncy[j]] = i;

    for (pk=pi+1; pk<ncand; pk++) {
      k = keys[pk].val;
      if (match[k] != UNMATCHED)
        continue;

      if (keys[pi].key != keys[pk].key)
        break;
      if (xadj[i+1]-xadj[i] != xadj[k+1]-xadj[k])
        break;

      for (jj=xadj[k]; jj<xadj[k+1]; jj++) {
        if (mark[adjncy[jj]] != i)
          break;
      }
      if (jj == xadj[k+1]) {
        cnvtxs++;  /* cmap[] is assigned in the renumbering pass in the caller */
        match[i] = k;
        match[k] = i;
        nunmatched -= 2;
        break;
      }
    }
  }
  WCOREPOP;

  /*IFSET(ctrl->dbglvl, METIS_DBG_COARSEN, printf("OUT: ncand: %"PRIDX", nunmatched: %zu\n", ncand, nunmatched)); */

  IFSET(ctrl->dbglvl, METIS_DBG_TIME, gk_stopcputimer(ctrl->Aux3Tmr));

  *r_nunmatched = nunmatched;
  return cnvtxs;

MEMORY_ERROR:
  ctrl->status = METIS_ERROR_MEMORY;
  WCOREPOP;
  IFSET(ctrl->dbglvl, METIS_DBG_TIME, gk_stopcputimer(ctrl->Aux3Tmr));
  return -1;
}


/*************************************************************************/
/*! This function finds a matching by selecting an adjacent vertex based
    on the Jaccard coefficient of the adjaceny lists.
 */
/**************************************************************************/
idx_t Match_JC(ctrl_t *ctrl, graph_t *graph)
{
  idx_t i, pi, ii, iii, j, jj, jjj, jjinc, k, nvtxs, ncon, cnvtxs, maxidx, 
        last_unmatched, avgdegree, bnum;
  idx_t *xadj, *vwgt, *adjncy, *adjwgt, *maxvwgt;
  idx_t *match, *cmap, *degrees, *perm, *tperm, *vec, *marker;
  idx_t mytwgt, xtwgt, ctwgt;
  real_t bscore, score;

  if (!WCOREPUSH)
    return -1;

  IFSET(ctrl->dbglvl, METIS_DBG_TIME, gk_startcputimer(ctrl->MatchTmr));

  nvtxs  = graph->nvtxs;
  ncon   = graph->ncon;
  xadj   = graph->xadj;
  vwgt   = graph->vwgt;
  adjncy = graph->adjncy;
  adjwgt = graph->adjwgt;
  cmap   = graph->cmap;

  maxvwgt  = ctrl->maxvwgt;

  match   = iwspacemalloc(ctrl, nvtxs);
  perm    = iwspacemalloc(ctrl, nvtxs);
  tperm   = iwspacemalloc(ctrl, nvtxs);
  degrees = iwspacemalloc(ctrl, nvtxs);
  if (match == NULL || perm == NULL || tperm == NULL || degrees == NULL)
    goto MEMORY_ERROR;
  iset(nvtxs, UNMATCHED, match);

  irandArrayPermute(nvtxs, tperm, nvtxs/8, 1);

  avgdegree = 4.0*(xadj[nvtxs]/nvtxs);
  for (i=0; i<nvtxs; i++) {
    bnum = sqrt(1+xadj[i+1]-xadj[i]);
    degrees[i] = (bnum > avgdegree ? avgdegree : bnum);
  }
  BucketSortKeysInc(ctrl, nvtxs, avgdegree, degrees, tperm, perm);
  if (ctrl->status != METIS_OK)
    goto MEMORY_ERROR;

  /* point to the wspace vectors that are not needed any more */
  vec    = tperm;
  marker = degrees;
  iset(nvtxs, -1, vec);
  iset(nvtxs, -1, marker);

  for (cnvtxs=0, last_unmatched=0, pi=0; pi<nvtxs; pi++) {
    i = perm[pi];

    if (match[i] == UNMATCHED) {  /* Unmatched */
      maxidx = i;

      if ((ncon == 1 ? vwgt[i] < maxvwgt[0] : ivecle(ncon, vwgt+i*ncon, maxvwgt))) {
        /* Deal with island vertices. Find a non-island and match it with. 
           The matching ignores ctrl->maxvwgt requirements */
        if (xadj[i] == xadj[i+1]) {
          last_unmatched = gk_max(pi, last_unmatched)+1;
          for (; last_unmatched<nvtxs; last_unmatched++) {
            j = perm[last_unmatched];
            if (match[j] == UNMATCHED) {
              maxidx = j;
              break;
            }
          }
        }
        else {
          if (ncon == 1) {
            /* Find a max JC pair, subject to maxvwgt constraints */
            if (xadj[i+1]-xadj[i] < avgdegree) {
              marker[i] = i;
              bscore = 0.0;
              mytwgt = 0;
              for (j=xadj[i]; j<xadj[i+1]; j++) {
                mytwgt += 1;//adjwgt[j];
                vec[adjncy[j]] = 1;//adjwgt[j];
              }

              /* single constraint pairing */
#ifdef XXX
              for (j=xadj[i]; j<xadj[i+1]; j++) {
                ii = adjncy[j];
                if (marker[ii] == i || match[ii] != UNMATCHED ||
                    vwgt[ii] > maxvwgt[0]-vwgt[i])
                  continue;

                ctwgt = xtwgt = 0;
                for (jj=xadj[ii]; jj<xadj[ii+1]; jj++) {
                  xtwgt += adjwgt[jj];
                  if (vec[adjncy[jj]] > 0)
                    ctwgt += vec[adjncy[jj]] + adjwgt[jj];
                  else if (adjncy[jj] == i) {
                    ctwgt += adjwgt[jj];
                    xtwgt -= adjwgt[jj];
                  }
                }

                score = 1.0*ctwgt/(mytwgt+xtwgt-ctwgt);
                if (score > bscore) {
                  bscore = score;
                  maxidx = ii;
                }
                marker[ii] = i;
              }
#endif

              for (j=xadj[i]; j<xadj[i+1]; j++) {
                ii = adjncy[j];
                for (jj=xadj[ii]; jj<xadj[ii+1]; jj++) {
                  iii = adjncy[jj];
  
                  if (marker[iii] == i || match[iii] != UNMATCHED ||
                      vwgt[iii] > maxvwgt[0]-vwgt[i])
                    continue;
  
                  ctwgt = xtwgt = 0;
                  for (jjj=xadj[iii]; jjj<xadj[iii+1]; jjj++) {
                    xtwgt += 1;//adjwgt[jjj];
                    if (vec[adjncy[jjj]] > 0)
                      ctwgt += 2;//vec[adjncy[jjj]] + adjwgt[jjj];
                    else if (adjncy[jjj] == i) 
                      ctwgt += 10*adjwgt[jjj];
                  }
  
                  score = 1.0*ctwgt/(mytwgt+xtwgt);
                  //printf("%"PRIDX" %"PRIDX" %"PRIDX" %.4f\n", mytwgt, xtwgt, ctwgt, score);
                  if (score > bscore) {
                    bscore = score;
                    maxidx = iii;
                  }
                  marker[iii] = i;
                }
              }
  
              /* reset vec array */
              for (j=xadj[i]; j<xadj[i+1]; j++) 
                vec[adjncy[j]] = -1;
            }
          }
          else {
            /* multi-constraint version */
            for (j=xadj[i]; j<xadj[i+1]; j++) {
              k = adjncy[j];
              if (match[k] == UNMATCHED && 
                  ivecaxpylez(ncon, 1, vwgt+i*ncon, vwgt+k*ncon, maxvwgt)) {
                maxidx = k;
                break;
              }
            }
          }
        }
      }

      if (maxidx != UNMATCHED) {
        cnvtxs++;  /* cmap[] is (re)assigned in the renumbering pass below */
        match[i] = maxidx;
        match[maxidx] = i;
      }
    }
  }


  /* match the final unmatched vertices with themselves and reorder the vertices 
     of the coarse graph for memory-friendly contraction */
  for (cnvtxs=0, i=0; i<nvtxs; i++) {
    if (match[i] == UNMATCHED) {
      match[i] = i;
      cmap[i]  = cnvtxs++;
    }
    else {
      if (i <= match[i]) 
        cmap[i] = cmap[match[i]] = cnvtxs++;
    }
  }

  if (CreateCoarseGraph(ctrl, graph, cnvtxs, match) != METIS_OK)
    goto MEMORY_ERROR;

  IFSET(ctrl->dbglvl, METIS_DBG_TIME, gk_stopcputimer(ctrl->MatchTmr));

  WCOREPOP;

  return cnvtxs;

MEMORY_ERROR:
  ctrl->status = METIS_ERROR_MEMORY;
  IFSET(ctrl->dbglvl, METIS_DBG_TIME, gk_stopcputimer(ctrl->MatchTmr));
  WCOREPOP;
  return -1;
}


/*************************************************************************/
/*! This function prints various stats for each graph during coarsening 
 */
/*************************************************************************/
void PrintCGraphStats(ctrl_t *ctrl, graph_t *graph)
{
  idx_t i;

  printf("%10"PRIDX" %10"PRIDX" %10"PRIDX" [%"PRIDX"] [", 
      graph->nvtxs, graph->nedges, isum(graph->nedges, graph->adjwgt, 1), ctrl->CoarsenTo);

  for (i=0; i<graph->ncon; i++)
    printf(" %8"PRIDX":%8"PRIDX, ctrl->maxvwgt[i], graph->tvwgt[i]);
  printf(" ]\n");
}


/*************************************************************************/
/*! This function creates the coarser graph. Depending on the size of the
    candidate adjacency lists it either uses a hash table or an array
    to do duplicate detection.
 */
/*************************************************************************/
int CreateCoarseGraph(ctrl_t *ctrl, graph_t *graph, idx_t cnvtxs,
         idx_t *match)
{
  idx_t j, jj, k, kk, l, m, istart, iend, nvtxs, nedges, ncon, 
        cnedges, v, u, mask;
  idx_t *xadj, *vwgt, *vsize, *adjncy, *adjwgt;
  idx_t *cmap, *htable, *dtable;
  idx_t *cxadj, *cvwgt, *cvsize, *cadjncy, *cadjwgt;
  graph_t *cgraph=NULL;
  int dovsize, dropedges;
  idx_t cv, nkeys, droppedewgt;
  idx_t *medianewgts=NULL, *medianenoise=NULL, *noise=NULL;
  ikv_t *keys=NULL;

  if (!WCOREPUSH)
    return METIS_ERROR_MEMORY;

  dovsize   = (ctrl->objtype == METIS_OBJTYPE_VOL ? 1 : 0);
  dropedges = ctrl->dropedges;

  mask = HTLENGTH;

  IFSET(ctrl->dbglvl, METIS_DBG_TIME, gk_startcputimer(ctrl->ContractTmr));

  nvtxs   = graph->nvtxs;
  ncon    = graph->ncon;
  xadj    = graph->xadj;
  vwgt    = graph->vwgt;
  vsize   = graph->vsize;
  adjncy  = graph->adjncy;
  adjwgt  = graph->adjwgt;
  cmap    = graph->cmap;

  /* Setup structures for dropedges */
  if (dropedges) {
    for (nkeys=0, v=0; v<nvtxs; v++) 
      nkeys = gk_max(nkeys, xadj[v+1]-xadj[v]);
    if (nkeys > (IDX_MAX-1)/2)
      goto MEMORY_ERROR;
    nkeys = 2*nkeys+1;

    keys        = ikvwspacemalloc(ctrl, nkeys);
    noise       = iwspacemalloc(ctrl, cnvtxs);
    medianewgts = iwspacemalloc(ctrl, cnvtxs);
    medianenoise = iwspacemalloc(ctrl, cnvtxs);
    if (keys == NULL || noise == NULL || medianewgts == NULL ||
        medianenoise == NULL)
      goto MEMORY_ERROR;
    iset(cnvtxs, -1, medianewgts);

    for (v=0; v<cnvtxs; v++) 
      noise[v] = irandInRange(128);
  }

  /* Initialize the coarser graph */
  cgraph   = SetupCoarseGraph(graph, cnvtxs, dovsize);
  if (cgraph == NULL)
    goto MEMORY_ERROR;
  cxadj    = cgraph->xadj;
  cvwgt    = cgraph->vwgt;
  cvsize   = cgraph->vsize;
  cadjncy  = cgraph->adjncy;
  cadjwgt  = cgraph->adjwgt;

  htable = iwspacemalloc(ctrl, mask+1);   /* hash table */
  dtable = iwspacemalloc(ctrl, cnvtxs);   /* direct table */
  if (htable == NULL || dtable == NULL)
    goto MEMORY_ERROR;
  iset(mask+1, -1, htable);
  iset(cnvtxs, -1, dtable);

  cxadj[0] = cnvtxs = cnedges = 0;
  for (v=0; v<nvtxs; v++) {
    if ((u = match[v]) < v)
      continue;

    ASSERT(cmap[v] == cnvtxs);
    ASSERT(cmap[match[v]] == cnvtxs);

    /* take care of the vertices */
    if (ncon == 1)
      cvwgt[cnvtxs] = vwgt[v];
    else
      icopy(ncon, vwgt+v*ncon, cvwgt+cnvtxs*ncon);

    if (dovsize)
      cvsize[cnvtxs] = vsize[v];

    if (v != u) { 
      if (ncon == 1)
        cvwgt[cnvtxs] += vwgt[u];
      else
        iaxpy(ncon, 1, vwgt+u*ncon, 1, cvwgt+cnvtxs*ncon, 1);

      if (dovsize)
        cvsize[cnvtxs] += vsize[u];
    }


    /* take care of the edges */ 
    if ((uintmax_t)(xadj[v+1]-xadj[v])+
        (uintmax_t)(xadj[u+1]-xadj[u]) < (uintmax_t)(mask>>2)) { /* use mask */
      /* put the ID of the contracted node itself at the start, so that it can be 
       * removed easily */
      htable[cnvtxs&mask] = 0;
      cadjncy[0] = cnvtxs;
      nedges = 1;

      istart = xadj[v];
      iend   = xadj[v+1];
      for (j=istart; j<iend; j++) {
        k = cmap[adjncy[j]];
        for (kk=k&mask; htable[kk]!=-1 && cadjncy[htable[kk]]!=k; kk=((kk+1)&mask));
        if ((m = htable[kk]) == -1) {
          cadjncy[nedges] = k;
          cadjwgt[nedges] = adjwgt[j];
          htable[kk] = nedges++;
        }
        else {
          cadjwgt[m] += adjwgt[j];
        }
      }
  
      if (v != u) { 
        istart = xadj[u];
        iend   = xadj[u+1];
        for (j=istart; j<iend; j++) {
          k = cmap[adjncy[j]];
          for (kk=k&mask; htable[kk]!=-1 && cadjncy[htable[kk]]!=k; kk=((kk+1)&mask));
          if ((m = htable[kk]) == -1) {
            cadjncy[nedges] = k;
            cadjwgt[nedges] = adjwgt[j];
            htable[kk]      = nedges++;
          }
          else {
            cadjwgt[m] += adjwgt[j];
          }
        }
      }

      /* reset the htable -- reverse order (LIFO) is critical to prevent cadjncy[-1]
       * indexing due to a remove of an earlier entry */
      for (j=nedges-1; j>=0; j--) {
        k = cadjncy[j];
        for (kk=k&mask; cadjncy[htable[kk]]!=k; kk=((kk+1)&mask));
        htable[kk] = -1;  
      }

      /* remove the contracted vertex from the list */
      cadjncy[0] = cadjncy[--nedges];
      cadjwgt[0] = cadjwgt[nedges];
    }
    else {
      nedges = 0;
      istart = xadj[v];
      iend   = xadj[v+1];
      for (j=istart; j<iend; j++) {
        k = cmap[adjncy[j]];
        if ((m = dtable[k]) == -1) {
          cadjncy[nedges] = k;
          cadjwgt[nedges] = adjwgt[j];
          dtable[k] = nedges++;
        }
        else {
          cadjwgt[m] += adjwgt[j];
        }
      }

      if (v != u) { 
        istart = xadj[u];
        iend   = xadj[u+1];
        for (j=istart; j<iend; j++) {
          k = cmap[adjncy[j]];
          if ((m = dtable[k]) == -1) {
            cadjncy[nedges] = k;
            cadjwgt[nedges] = adjwgt[j];
            dtable[k] = nedges++;
          }
          else {
            cadjwgt[m] += adjwgt[j];
          }
        }

        /* Remove the contracted self-loop, when present */
        if ((j = dtable[cnvtxs]) != -1) {
          ASSERT(cadjncy[j] == cnvtxs);
          cadjncy[j]        = cadjncy[--nedges];
          cadjwgt[j]        = cadjwgt[nedges];
          dtable[cnvtxs] = -1;
        }
      }

      /* Zero out the dtable */
      for (j=0; j<nedges; j++)
        dtable[cadjncy[j]] = -1;  
    }


    /* Determine the median weight/noise pair of the incident edges, which will
       be used to decide whether to keep an edge. */
    if (dropedges) {
      ASSERTP(nedges < nkeys, ("%"PRIDX", %"PRIDX"\n", nkeys, nedges));
      medianewgts[cnvtxs] = 0;  /* default for island nodes */
      medianenoise[cnvtxs] = 8;
      if (nedges > 0) {
        for (j=0; j<nedges; j++) {
          keys[j].key = cadjwgt[j];
          keys[j].val = noise[cnvtxs] + noise[cadjncy[j]];
        }
        ikvsortii(nedges, keys);
        k = gk_min(nedges-1,
            (idx_t)(((uintmax_t)(xadj[v+1]-xadj[v])+
                     (uintmax_t)(xadj[u+1]-xadj[u]))>>1));
        medianewgts[cnvtxs] = keys[nedges-1-k].key;
        medianenoise[cnvtxs] = keys[nedges-1-k].val;
      }
    }

    cadjncy         += nedges;
    cadjwgt         += nedges;
    cnedges         += nedges;
    cxadj[++cnvtxs]  = cnedges;
  }


  /* compact the adjacency structure of the coarser graph to keep only +ve edges */
  if (dropedges) { 
    droppedewgt = 0;

    cadjncy  = cgraph->adjncy;
    cadjwgt  = cgraph->adjwgt;

    cnedges = 0;
    for (u=0; u<cnvtxs; u++) {
      istart = cxadj[u];
      iend   = cxadj[u+1];
      for (j=istart; j<iend; j++) {
        v = cadjncy[j];
        ASSERTP(medianewgts[u] >= 0, ("%"PRIDX" %"PRIDX"\n", u, medianewgts[u]));
        ASSERTP(medianewgts[v] >= 0, ("%"PRIDX" %"PRIDX" %"PRIDX"\n", v, medianewgts[v], cnvtxs));
        k = (medianewgts[u] < medianewgts[v] ||
             (medianewgts[u] == medianewgts[v] &&
              medianenoise[u] <= medianenoise[v]) ? u : v);
        if (cadjwgt[j] > medianewgts[k] ||
            (cadjwgt[j] == medianewgts[k] &&
             noise[u]+noise[v] >= medianenoise[k])) {
          cadjncy[cnedges]   = cadjncy[j];
          cadjwgt[cnedges++] = cadjwgt[j];
        }
        else 
          droppedewgt += cadjwgt[j];
      }
      cxadj[u] = cnedges;
    }
    SHIFTCSR(j, cnvtxs, cxadj);

    cgraph->droppedewgt = droppedewgt;
  }

  cgraph->nedges = cnedges;

  for (j=0; j<ncon; j++) {
    cgraph->tvwgt[j]    = isum(cgraph->nvtxs, cgraph->vwgt+j, ncon);
    cgraph->invtvwgt[j] = 1.0/(cgraph->tvwgt[j] > 0 ? cgraph->tvwgt[j] : 1);
  }

  ReAdjustMemory(ctrl, graph, cgraph);

  IFSET(ctrl->dbglvl, METIS_DBG_TIME, gk_stopcputimer(ctrl->ContractTmr));

  WCOREPOP;
  return METIS_OK;

MEMORY_ERROR:
  ctrl->status = METIS_ERROR_MEMORY;
  if (cgraph != NULL) {
    graph->coarser = NULL;
    FreeGraph(&cgraph);
  }
  IFSET(ctrl->dbglvl, METIS_DBG_TIME, gk_stopcputimer(ctrl->ContractTmr));
  WCOREPOP;
  return METIS_ERROR_MEMORY;
}


/*************************************************************************/
/*! Setup the various arrays for the coarse graph 
 */
/*************************************************************************/
graph_t *SetupCoarseGraph(graph_t *graph, idx_t cnvtxs, int dovsize)
{
  graph_t *cgraph=NULL;
  int sigrval;
  size_t scnvtxs, snedges, sncon, nvwgt;

  if (graph == NULL || cnvtxs < 0 || graph->ncon <= 0 ||
      graph->nedges < 0) {
    errno = EINVAL;
    return NULL;
  }
  if (cnvtxs > IDX_MAX/graph->ncon ||
      (uintmax_t)cnvtxs > (uintmax_t)SIZE_MAX ||
      (uintmax_t)graph->nedges > (uintmax_t)SIZE_MAX ||
      (uintmax_t)graph->ncon > (uintmax_t)SIZE_MAX) {
    errno = EOVERFLOW;
    return NULL;
  }
  scnvtxs = (size_t)cnvtxs;
  snedges = (size_t)graph->nedges;
  sncon = (size_t)graph->ncon;
  if (scnvtxs == SIZE_MAX || snedges == SIZE_MAX ||
      scnvtxs+1 > SIZE_MAX/sizeof(idx_t) ||
      snedges+1 > SIZE_MAX/sizeof(idx_t) ||
      sncon > SIZE_MAX/sizeof(idx_t) ||
      sncon > SIZE_MAX/sizeof(real_t) ||
      sncon > SIZE_MAX/(scnvtxs == 0 ? 1 : scnvtxs)) {
    errno = EOVERFLOW;
    return NULL;
  }
  nvwgt = sncon*scnvtxs;
  if (nvwgt > SIZE_MAX/sizeof(idx_t)) {
    errno = EOVERFLOW;
    return NULL;
  }

  cgraph = CreateGraph();
  if (cgraph == NULL)
    goto MEMORY_ERROR;

  cgraph->nvtxs = cnvtxs;
  cgraph->ncon  = graph->ncon;

  /* Allocate memory for the coarser graph.
     NOTE: The +1 in the adjwgt/adjncy is to allow the optimization of self-loop
           detection by adding ahead of time the self-loop. That optimization
           requires a +1 adjncy/adjwgt array for the limit case where the 
           coarser graph is of the same size of the previous graph. */
  cgraph->xadj = iMallocNoSignal(scnvtxs+1,
      "SetupCoarseGraph: xadj", &sigrval);
  if (cgraph->xadj == NULL)
    goto MEMORY_ERROR;
  cgraph->adjncy = iMallocNoSignal(snedges+1,
      "SetupCoarseGraph: adjncy", &sigrval);
  if (cgraph->adjncy == NULL)
    goto MEMORY_ERROR;
  cgraph->adjwgt = iMallocNoSignal(snedges+1,
      "SetupCoarseGraph: adjwgt", &sigrval);
  if (cgraph->adjwgt == NULL)
    goto MEMORY_ERROR;
  cgraph->vwgt = iMallocNoSignal(nvwgt,
      "SetupCoarseGraph: vwgt", &sigrval);
  if (cgraph->vwgt == NULL)
    goto MEMORY_ERROR;
  cgraph->tvwgt = iMallocNoSignal(sncon,
      "SetupCoarseGraph: tvwgt", &sigrval);
  if (cgraph->tvwgt == NULL)
    goto MEMORY_ERROR;
  cgraph->invtvwgt = rMallocNoSignal(sncon,
      "SetupCoarseGraph: invtvwgt", &sigrval);
  if (cgraph->xadj == NULL || cgraph->adjncy == NULL ||
      cgraph->adjwgt == NULL || cgraph->vwgt == NULL ||
      cgraph->tvwgt == NULL || cgraph->invtvwgt == NULL)
    goto MEMORY_ERROR;

  if (dovsize) {
    cgraph->vsize = iMallocNoSignal(scnvtxs,
        "SetupCoarseGraph: vsize", &sigrval);
    if (cgraph->vsize == NULL)
      goto MEMORY_ERROR;
  }

  cgraph->finer  = graph;
  graph->coarser = cgraph;

  return cgraph;

MEMORY_ERROR:
  if (errno == 0)
    errno = ENOMEM;
  FreeGraph(&cgraph);
  return NULL;
}


/*************************************************************************/
/*! This function re-adjusts the amount of memory that was allocated if
    it will lead to significant savings 
 */
/*************************************************************************/
void ReAdjustMemory(ctrl_t *ctrl, graph_t *graph, graph_t *cgraph) 
{
  idx_t *new_adjncy, *new_adjwgt;
  int sigrval, saved_errno;

  if (cgraph->nedges > 10000 && cgraph->nedges < 0.9*graph->nedges) {
    new_adjncy = imalloc(cgraph->nedges,
        "ReAdjustMemory: adjncy");
    if (new_adjncy == NULL)
      return;
    icopy(cgraph->nedges, cgraph->adjncy, new_adjncy);

    new_adjwgt = iReallocNoSignal(cgraph->adjwgt,
        (size_t)cgraph->nedges, "ReAdjustMemory: adjwgt",
        &sigrval);
    if (new_adjwgt == NULL) {
      saved_errno = errno != 0 ? errno : ENOMEM;
      gk_free((void **)&new_adjncy, LTERM);
      errno = saved_errno;
      if (sigrval != 0)
        gk_errexit(sigrval,
            "ReAdjustMemory: failed to resize adjacency arrays");
      return;
    }

    gk_free((void **)&cgraph->adjncy, LTERM);
    cgraph->adjncy = new_adjncy;
    cgraph->adjwgt = new_adjwgt;
  }
}
