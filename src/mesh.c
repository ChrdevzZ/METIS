/*
 * Copyright 1997, Regents of the University of Minnesota
 *
 * mesh.c
 *
 * This file contains routines for converting 3D and 4D finite element
 * meshes into dual or nodal graphs
 *
 * Started 8/18/97
 * George
 *
 * $Id: mesh.c 13804 2013-03-04 23:49:08Z karypis $
 *
 */

#include "metislib.h"
#include "input_validation.h"


/*****************************************************************************/
/*! Converts per-row counts into CSR offsets when the total is representable.

    The array is modified only for use as a newly constructed output. A
    failure therefore leaves no caller-owned state to restore.
*/
/*****************************************************************************/
static int MakeCSR(idx_t n, idx_t *ptr)
{
  idx_t count, i;
  uintmax_t total=0;

  for (i=0; i<n; i++) {
    count = ptr[i];
    if (count < 0 || (uintmax_t)count > (uintmax_t)IDX_MAX-total) {
      errno = EOVERFLOW;
      return 0;
    }
    ptr[i] = (idx_t)total;
    total += (uintmax_t)count;
  }
  if (total > (uintmax_t)SIZE_MAX/sizeof(idx_t)) {
    errno = EOVERFLOW;
    return 0;
  }
  ptr[n] = (idx_t)total;

  return 1;
}


/*****************************************************************************/
/*! This function creates a graph corresponding to the dual of a finite element
    mesh. 

    \param ne is the number of elements in the mesh.
    \param nn is the number of nodes in the mesh.
    \param eptr is an array of size ne+1 used to mark the start and end 
           locations in the nind array.
    \param eind is an array that stores for each element the set of node IDs 
           (indices) that it is made off. The length of this array is equal
           to the total number of nodes over all the mesh elements.
    \param ncommon is the minimum number of nodes that two elements must share
           in order to be connected via an edge in the dual graph.
    \param numflag is either 0 or 1 indicating if the numbering of the nodes
           starts from 0 or 1, respectively. The same numbering is used for the
           returned graph as well.
    \param r_xadj indicates where the adjacency list of each vertex is stored 
           in r_adjncy. The memory for this array is allocated by this routine. 
           It can be freed by calling METIS_free().
    \param r_adjncy stores the adjacency list of each vertex in the generated 
           dual graph. The memory for this array is allocated by this routine. 
           It can be freed by calling METIS_free().

*/
/*****************************************************************************/
int METIS_MeshToDual(idx_t *ne, idx_t *nn, idx_t *eptr, idx_t *eind, 
          idx_t *ncommon, idx_t *numflag,  idx_t **r_xadj, idx_t **r_adjncy)
{
  volatile int rstatus=METIS_OK, sigrval=0, renumber=0;
  int error, unique;

  if (r_xadj == NULL || r_adjncy == NULL || r_xadj == r_adjncy)
    return METIS_ERROR_INPUT;
  *r_xadj = *r_adjncy = NULL;
  if (ne == NULL || nn == NULL || ncommon == NULL || numflag == NULL)
    return METIS_ERROR_INPUT;
  rstatus = ValidateMeshInput(*ne, *nn, eptr, eind, *numflag);
  if (rstatus != METIS_OK)
    return rstatus;
  unique = ValidateMeshElementNodes(*ne, *nn, eptr, eind, *numflag);
  if (unique <= 0) {
    if (unique < 0) {
      if (errno == 0)
        errno = ENOMEM;
      return METIS_ERROR_MEMORY;
    }
    errno = EINVAL;
    return METIS_ERROR_INPUT;
  }

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


  /* renumber the mesh */
  if (*numflag == 1) {
    ChangeMesh2CNumbering(*ne, eptr, eind);
    renumber = 1;
  }

  /* create dual graph */
  rstatus = CreateGraphDual(*ne, *nn, eptr, eind, *ncommon,
      r_xadj, r_adjncy);
  if (rstatus != METIS_OK) {
    goto SIGTHROW;
  }


SIGTHROW:
  error = errno;
  if (error == 0 && (sigrval != 0 || rstatus != METIS_OK))
    error = sigrval == SIGMEM || rstatus == METIS_ERROR_MEMORY ?
        ENOMEM : EINVAL;

  if (renumber)
    ChangeMesh2FNumbering(*ne, eptr, eind, *ne, *r_xadj, *r_adjncy);

  gk_siguntrap();
  gk_malloc_cleanup(0);

  if (sigrval != 0 || rstatus != METIS_OK) {
    if (*r_xadj != NULL)
      free(*r_xadj);
    if (*r_adjncy != NULL)
      free(*r_adjncy);
    *r_xadj = *r_adjncy = NULL;
    errno = error;
  }

  return rstatus == METIS_OK ? metis_rcode(sigrval) : rstatus;
}


/*****************************************************************************/
/*! This function creates a graph corresponding to (almost) the nodal of a 
    finite element mesh. In the nodal graph, each node is connected to the
    nodes corresponding to the union of nodes present in all the elements
    in which that node belongs. 

    \param ne is the number of elements in the mesh.
    \param nn is the number of nodes in the mesh.
    \param eptr is an array of size ne+1 used to mark the start and end 
           locations in the nind array.
    \param eind is an array that stores for each element the set of node IDs 
           (indices) that it is made off. The length of this array is equal
           to the total number of nodes over all the mesh elements.
    \param numflag is either 0 or 1 indicating if the numbering of the nodes
           starts from 0 or 1, respectively. The same numbering is used for the
           returned graph as well.
    \param r_xadj indicates where the adjacency list of each vertex is stored 
           in r_adjncy. The memory for this array is allocated by this routine. 
           It can be freed by calling METIS_free().
    \param r_adjncy stores the adjacency list of each vertex in the generated 
           dual graph. The memory for this array is allocated by this routine. 
           It can be freed by calling METIS_free().

*/
/*****************************************************************************/
int METIS_MeshToNodal(idx_t *ne, idx_t *nn, idx_t *eptr, idx_t *eind, 
          idx_t *numflag,  idx_t **r_xadj, idx_t **r_adjncy)
{
  volatile int rstatus=METIS_OK, sigrval=0, renumber=0;
  int error, unique;

  if (r_xadj == NULL || r_adjncy == NULL || r_xadj == r_adjncy)
    return METIS_ERROR_INPUT;
  *r_xadj = *r_adjncy = NULL;
  if (ne == NULL || nn == NULL || numflag == NULL)
    return METIS_ERROR_INPUT;
  rstatus = ValidateMeshInput(*ne, *nn, eptr, eind, *numflag);
  if (rstatus != METIS_OK)
    return rstatus;
  unique = ValidateMeshElementNodes(*ne, *nn, eptr, eind, *numflag);
  if (unique <= 0) {
    if (unique < 0) {
      if (errno == 0)
        errno = ENOMEM;
      return METIS_ERROR_MEMORY;
    }
    errno = EINVAL;
    return METIS_ERROR_INPUT;
  }

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


  /* renumber the mesh */
  if (*numflag == 1) {
    ChangeMesh2CNumbering(*ne, eptr, eind);
    renumber = 1;
  }

  /* create nodal graph */
  rstatus = CreateGraphNodal(*ne, *nn, eptr, eind, r_xadj, r_adjncy);
  if (rstatus != METIS_OK) {
    goto SIGTHROW;
  }


SIGTHROW:
  error = errno;
  if (error == 0 && (sigrval != 0 || rstatus != METIS_OK))
    error = sigrval == SIGMEM || rstatus == METIS_ERROR_MEMORY ?
        ENOMEM : EINVAL;

  if (renumber)
    ChangeMesh2FNumbering(*ne, eptr, eind, *nn, *r_xadj, *r_adjncy);

  gk_siguntrap();
  gk_malloc_cleanup(0);

  if (sigrval != 0 || rstatus != METIS_OK) {
    if (*r_xadj != NULL)
      free(*r_xadj);
    if (*r_adjncy != NULL)
      free(*r_adjncy);
    *r_xadj = *r_adjncy = NULL;
    errno = error;
  }

  return rstatus == METIS_OK ? metis_rcode(sigrval) : rstatus;
}


/*****************************************************************************/
/*! This function creates the dual of a finite element mesh */
/*****************************************************************************/
int CreateGraphDual(idx_t ne, idx_t nn, idx_t *eptr, idx_t *eind, idx_t ncommon,
          idx_t **r_xadj, idx_t **r_adjncy)
{
  idx_t i, j, nnbrs;
  idx_t *nptr=NULL, *nind=NULL;
  idx_t *xadj=NULL, *adjncy=NULL;
  idx_t *marker=NULL, *nbrs=NULL;
  int error, sigrval;
  size_t nadjncy;

  if (r_xadj == NULL || r_adjncy == NULL || r_xadj == r_adjncy)
    return METIS_ERROR_INPUT;
  *r_xadj = *r_adjncy = NULL;

  if (ncommon < 1) {
    printf("  Increased ncommon to 1, as it was initially %"PRIDX"\n", ncommon);
    ncommon = 1;
  }

  /* construct the node-element list first */
  nptr = iMallocNoSignal((size_t)nn+1, "CreateGraphDual: nptr", &sigrval);
  if (nptr == NULL)
    goto MEMORY_ERROR;
  iset(nn+1, 0, nptr);
  nind = iMallocNoSignal((size_t)eptr[ne], "CreateGraphDual: nind",
      &sigrval);
  if (nind == NULL)
    goto MEMORY_ERROR;

  for (i=0; i<ne; i++) {
    for (j=eptr[i]; j<eptr[i+1]; j++)
      nptr[eind[j]]++;
  }
  MAKECSR(i, nn, nptr);

  for (i=0; i<ne; i++) {
    for (j=eptr[i]; j<eptr[i+1]; j++)
      nind[nptr[eind[j]]++] = i;
  }
  SHIFTCSR(i, nn, nptr);


  /* Allocate memory for xadj, since you know its size.
     These are done using standard malloc as they are returned
     to the calling function */
  xadj = (idx_t *)malloc(((size_t)ne+1)*sizeof(idx_t));
  if (xadj == NULL)
    goto MEMORY_ERROR;
  iset(ne+1, 0, xadj);

  /* allocate memory for working arrays used by FindCommonElements */
  marker = iMallocNoSignal((size_t)ne, "CreateGraphDual: marker", &sigrval);
  if (marker == NULL)
    goto MEMORY_ERROR;
  iset(ne, 0, marker);
  nbrs = iMallocNoSignal((size_t)ne, "CreateGraphDual: nbrs", &sigrval);
  if (nbrs == NULL)
    goto MEMORY_ERROR;

  for (i=0; i<ne; i++) {
    xadj[i] = FindCommonElements(i, eptr[i+1]-eptr[i], eind+eptr[i], nptr, 
                  nind, eptr, ncommon, marker, nbrs);
  }
  if (!MakeCSR(ne, xadj))
    goto MEMORY_ERROR;

  /* Allocate memory for adjncy, since you now know its size.
     These are done using standard malloc as they are returned
     to the calling function */
  nadjncy = xadj[ne] == 0 ? 1 : (size_t)xadj[ne];
  adjncy = (idx_t *)malloc(nadjncy*sizeof(idx_t));
  if (adjncy == NULL)
    goto MEMORY_ERROR;

  for (i=0; i<ne; i++) {
    nnbrs = FindCommonElements(i, eptr[i+1]-eptr[i], eind+eptr[i], nptr, 
                nind, eptr, ncommon, marker, nbrs);
    for (j=0; j<nnbrs; j++)
      adjncy[xadj[i]++] = nbrs[j];
  }
  SHIFTCSR(i, ne, xadj);
  
  gk_free((void **)&nptr, &nind, &marker, &nbrs, LTERM);

  *r_xadj = xadj;
  *r_adjncy = adjncy;
  return METIS_OK;

MEMORY_ERROR:
  error = errno == 0 ? ENOMEM : errno;
  free(xadj);
  free(adjncy);
  gk_free((void **)&nptr, &nind, &marker, &nbrs, LTERM);
  errno = error;
  return METIS_ERROR_MEMORY;
}


/*****************************************************************************/
/*! This function finds all elements that share at least ncommon nodes with 
    the ``query'' element. 
*/
/*****************************************************************************/
idx_t FindCommonElements(idx_t qid, idx_t elen, idx_t *eind, idx_t *nptr, 
          idx_t *nind, idx_t *eptr, idx_t ncommon, idx_t *marker, idx_t *nbrs)
{
  idx_t i, ii, j, jj, k, l, overlap;

  /* find all elements that share at least one node with qid */
  for (k=0, i=0; i<elen; i++) {
    j = eind[i];
    for (ii=nptr[j]; ii<nptr[j+1]; ii++) {
      jj = nind[ii];

      if (marker[jj] == 0) 
        nbrs[k++] = jj;
      marker[jj]++;
    }
  }

  /* put qid into the neighbor list (in case it is not there) so that it
     will be removed in the next step */
  if (marker[qid] == 0)
    nbrs[k++] = qid;
  marker[qid] = 0;

  /* compact the list to contain only those with at least ncommon nodes */
  for (j=0, i=0; i<k; i++) {
    overlap = marker[l = nbrs[i]];
    if (overlap >= ncommon || 
        overlap >= elen-1 || 
        overlap >= eptr[l+1]-eptr[l]-1)
      nbrs[j++] = l;
    marker[l] = 0;
  }

  return j;
}


/*****************************************************************************/
/*! This function creates the (almost) nodal of a finite element mesh */
/*****************************************************************************/
int CreateGraphNodal(idx_t ne, idx_t nn, idx_t *eptr, idx_t *eind,
          idx_t **r_xadj, idx_t **r_adjncy)
{
  idx_t i, j, nnbrs;
  idx_t *nptr=NULL, *nind=NULL;
  idx_t *xadj=NULL, *adjncy=NULL;
  idx_t *marker=NULL, *nbrs=NULL;
  int error, sigrval;
  size_t nadjncy;

  if (r_xadj == NULL || r_adjncy == NULL || r_xadj == r_adjncy)
    return METIS_ERROR_INPUT;
  *r_xadj = *r_adjncy = NULL;

  /* construct the node-element list first */
  nptr = iMallocNoSignal((size_t)nn+1, "CreateGraphNodal: nptr", &sigrval);
  if (nptr == NULL)
    goto MEMORY_ERROR;
  iset(nn+1, 0, nptr);
  nind = iMallocNoSignal((size_t)eptr[ne], "CreateGraphNodal: nind",
      &sigrval);
  if (nind == NULL)
    goto MEMORY_ERROR;

  for (i=0; i<ne; i++) {
    for (j=eptr[i]; j<eptr[i+1]; j++)
      nptr[eind[j]]++;
  }
  MAKECSR(i, nn, nptr);

  for (i=0; i<ne; i++) {
    for (j=eptr[i]; j<eptr[i+1]; j++)
      nind[nptr[eind[j]]++] = i;
  }
  SHIFTCSR(i, nn, nptr);


  /* Allocate memory for xadj, since you know its size.
     These are done using standard malloc as they are returned
     to the calling function */
  xadj = (idx_t *)malloc(((size_t)nn+1)*sizeof(idx_t));
  if (xadj == NULL)
    goto MEMORY_ERROR;
  iset(nn+1, 0, xadj);

  /* allocate memory for working arrays used by FindCommonElements */
  marker = iMallocNoSignal((size_t)nn, "CreateGraphNodal: marker", &sigrval);
  if (marker == NULL)
    goto MEMORY_ERROR;
  iset(nn, 0, marker);
  nbrs = iMallocNoSignal((size_t)nn, "CreateGraphNodal: nbrs", &sigrval);
  if (nbrs == NULL)
    goto MEMORY_ERROR;

  for (i=0; i<nn; i++) {
    xadj[i] = FindCommonNodes(i, nptr[i+1]-nptr[i], nind+nptr[i], eptr, 
                  eind, marker, nbrs);
  }
  if (!MakeCSR(nn, xadj))
    goto MEMORY_ERROR;

  /* Allocate memory for adjncy, since you now know its size.
     These are done using standard malloc as they are returned
     to the calling function */
  nadjncy = xadj[nn] == 0 ? 1 : (size_t)xadj[nn];
  adjncy = (idx_t *)malloc(nadjncy*sizeof(idx_t));
  if (adjncy == NULL)
    goto MEMORY_ERROR;

  for (i=0; i<nn; i++) {
    nnbrs = FindCommonNodes(i, nptr[i+1]-nptr[i], nind+nptr[i], eptr, 
                eind, marker, nbrs);
    for (j=0; j<nnbrs; j++)
      adjncy[xadj[i]++] = nbrs[j];
  }
  SHIFTCSR(i, nn, xadj);
  
  gk_free((void **)&nptr, &nind, &marker, &nbrs, LTERM);

  *r_xadj = xadj;
  *r_adjncy = adjncy;
  return METIS_OK;

MEMORY_ERROR:
  error = errno == 0 ? ENOMEM : errno;
  free(xadj);
  free(adjncy);
  gk_free((void **)&nptr, &nind, &marker, &nbrs, LTERM);
  errno = error;
  return METIS_ERROR_MEMORY;
}


/*****************************************************************************/
/*! This function finds the union of nodes that are in the same elements with
    the ``query'' node. 
*/
/*****************************************************************************/
idx_t FindCommonNodes(idx_t qid, idx_t nelmnts, idx_t *elmntids, idx_t *eptr, 
          idx_t *eind, idx_t *marker, idx_t *nbrs)
{
  idx_t i, ii, j, jj, k;

  /* find all nodes that share at least one element with qid */
  marker[qid] = 1;  /* this is to prevent self-loops */
  for (k=0, i=0; i<nelmnts; i++) {
    j = elmntids[i];
    for (ii=eptr[j]; ii<eptr[j+1]; ii++) {
      jj = eind[ii];
      if (marker[jj] == 0) {
        nbrs[k++] = jj;
        marker[jj] = 1;
      }
    }
  }

  /* reset the marker */
  marker[qid] = 0;
  for (i=0; i<k; i++) {
    marker[nbrs[i]] = 0;
  }

  return k;
}



/*************************************************************************/
/*! This function creates and initializes a mesh_t structure */
/*************************************************************************/
mesh_t *CreateMesh(void)
{
  mesh_t *mesh;

  mesh = (mesh_t *)gk_malloc(sizeof(mesh_t), "CreateMesh: mesh");
  if (mesh == NULL)
    return NULL;

  InitMesh(mesh);

  return mesh;
}


/*************************************************************************/
/*! This function initializes a mesh_t data structure */
/*************************************************************************/
void InitMesh(mesh_t *mesh) 
{
  memset((void *)mesh, 0, sizeof(mesh_t));
}


/*************************************************************************/
/*! This function deallocates any memory stored in a mesh */
/*************************************************************************/
void FreeMesh(mesh_t **r_mesh) 
{
  mesh_t *mesh;

  if (r_mesh == NULL || *r_mesh == NULL)
    return;
  mesh = *r_mesh;
  
  gk_free((void **)&mesh->eptr, &mesh->eind, &mesh->ewgt, &mesh, LTERM);

  *r_mesh = NULL;
}
