/*
 * input_validation.h
 *
 * Private validation helpers for public API array boundaries.
 */

#ifndef _LIBMETIS_INPUT_VALIDATION_H_
#define _LIBMETIS_INPUT_VALIDATION_H_


/*************************************************************************/
/*! Validates a public graph view and its representable aggregate weights.

    Dimensions, CSR offsets and indices, allocation products, weight totals,
    and objective-specific derived volume weights are checked before setup can
    index or sum them. Symmetry and duplicate-edge checks remain part of the
    caller contract and the graph-checking tools.

    \retval METIS_OK if the graph view is valid.
    \retval METIS_ERROR_INPUT if a dimension, offset, index, or weight is
            invalid.
    \retval METIS_ERROR_MEMORY if required storage cannot be represented.
*/
/*************************************************************************/
static inline int ValidateGraphInput(idx_t nvtxs, idx_t ncon,
    const idx_t *xadj, const idx_t *adjncy, const idx_t *vwgt,
    const idx_t *vsize, const idx_t *adjwgt, idx_t numflag, idx_t objtype)
{
  idx_t base, i, j, nedges;
  uintmax_t ewgt, srcsize, total, weight;

  if (nvtxs <= 0 || ncon <= 0 || xadj == NULL ||
      (numflag != 0 && numflag != 1))
    return METIS_ERROR_INPUT;
  if (nvtxs == IDX_MAX ||
      (uintmax_t)nvtxs > (uintmax_t)SIZE_MAX/sizeof(idx_t)-1 ||
      nvtxs > IDX_MAX/ncon ||
      (uintmax_t)(nvtxs*ncon) > (uintmax_t)SIZE_MAX/sizeof(idx_t)) {
    errno = EOVERFLOW;
    return METIS_ERROR_MEMORY;
  }

  base = numflag;
  if (xadj[0] != base || xadj[nvtxs] < base)
    return METIS_ERROR_INPUT;
  nedges = xadj[nvtxs]-base;
  if ((uintmax_t)nedges > (uintmax_t)SIZE_MAX/sizeof(idx_t)) {
    errno = EOVERFLOW;
    return METIS_ERROR_MEMORY;
  }
  if (nedges > 0 && adjncy == NULL)
    return METIS_ERROR_INPUT;

  for (i=0; i<nvtxs; i++) {
    if (xadj[i] < base || xadj[i] > xadj[i+1] ||
        xadj[i+1] > xadj[nvtxs])
      return METIS_ERROR_INPUT;
  }
  for (j=0; j<nedges; j++) {
    if (adjncy[j] < base || adjncy[j]-base >= nvtxs)
      return METIS_ERROR_INPUT;
  }

  if (vwgt != NULL) {
    for (j=0; j<ncon; j++) {
      total = 0;
      for (i=0; i<nvtxs; i++) {
        if (vwgt[i*ncon+j] < 0)
          return METIS_ERROR_INPUT;
        weight = (uintmax_t)vwgt[i*ncon+j];
        if (total > (uintmax_t)IDX_MAX-weight)
          return METIS_ERROR_INPUT;
        total += weight;
      }
    }
  }

  if (adjwgt != NULL) {
    total = 0;
    for (j=0; j<nedges; j++) {
      if (adjwgt[j] <= 0)
        return METIS_ERROR_INPUT;
      weight = (uintmax_t)adjwgt[j];
      if (total > (uintmax_t)IDX_MAX-weight)
        return METIS_ERROR_INPUT;
      total += weight;
    }
  }

  if (objtype == METIS_OBJTYPE_CUT)
    return METIS_OK;

  if (objtype == METIS_OBJTYPE_NODE)
    return METIS_OK;
  if (objtype != METIS_OBJTYPE_VOL)
    return METIS_ERROR_INPUT;

  total = 0;
  for (i=0; i<nvtxs; i++) {
    weight = vsize == NULL ? 1 : (uintmax_t)vsize[i];
    if ((vsize != NULL && vsize[i] < 0) ||
        total > (uintmax_t)IDX_MAX-weight)
      return METIS_ERROR_INPUT;
    total += weight;
  }

  total = 0;
  for (i=0; i<nvtxs; i++) {
    srcsize = vsize == NULL ? 1 : (uintmax_t)vsize[i];

    for (j=xadj[i]-base; j<xadj[i+1]-base; j++) {
      weight = vsize == NULL ? 1 : (uintmax_t)vsize[adjncy[j]-base];
      if (weight >= (uintmax_t)IDX_MAX ||
          srcsize > (uintmax_t)IDX_MAX-1-weight)
        return METIS_ERROR_INPUT;
      ewgt = 1+srcsize+weight;
      if (total > (uintmax_t)IDX_MAX-ewgt)
        return METIS_ERROR_INPUT;
      total += ewgt;
    }
  }

  return METIS_OK;
}


/*************************************************************************/
/*! Returns true when each supplied non-NULL integer weight is nonnegative
    and their sum is representable by idx_t. */
/*************************************************************************/
static inline int ValidateIntegerWeights(idx_t n, const idx_t *weights)
{
  idx_t i;
  uintmax_t total=0, weight;

  if (n < 0)
    return 0;
  if (weights != NULL) {
    for (i=0; i<n; i++) {
      if (weights[i] < 0)
        return 0;
      weight = (uintmax_t)weights[i];
      if (total > (uintmax_t)IDX_MAX-weight)
        return 0;
      total += weight;
    }
  }

  return 1;
}


/*************************************************************************/
/*! Validates mesh dimensions, offsets, and node indices before access.

    Empty elements are rejected because the mesh conversion routines require
    every element to contribute at least one node. Both C and one-based input
    are checked without modifying caller-owned arrays.

    \retval METIS_OK if the mesh view is valid.
    \retval METIS_ERROR_INPUT if a dimension, offset, or index is invalid.
    \retval METIS_ERROR_MEMORY if required storage cannot be represented.
*/
/*************************************************************************/
static inline int ValidateMeshInput(idx_t ne, idx_t nn, const idx_t *eptr,
    const idx_t *eind, idx_t numflag)
{
  idx_t base, i, j, nind;

  if (ne <= 0 || nn <= 0 || eptr == NULL ||
      (numflag != 0 && numflag != 1))
    return METIS_ERROR_INPUT;
  if (ne == IDX_MAX || nn == IDX_MAX ||
      (uintmax_t)ne > (uintmax_t)SIZE_MAX/sizeof(idx_t)-1 ||
      (uintmax_t)nn > (uintmax_t)SIZE_MAX/sizeof(idx_t)-1) {
    errno = EOVERFLOW;
    return METIS_ERROR_MEMORY;
  }

  base = numflag;
  if (eptr[0] != base || eptr[ne] < base)
    return METIS_ERROR_INPUT;
  nind = eptr[ne]-base;
  if ((uintmax_t)nind > (uintmax_t)SIZE_MAX/sizeof(idx_t)) {
    errno = EOVERFLOW;
    return METIS_ERROR_MEMORY;
  }
  if (nind > 0 && eind == NULL)
    return METIS_ERROR_INPUT;

  for (i=0; i<ne; i++) {
    if (eptr[i] < base || eptr[i] >= eptr[i+1] ||
        eptr[i+1] > eptr[ne])
      return METIS_ERROR_INPUT;
  }
  for (j=0; j<nind; j++) {
    if (eind[j] < base || eind[j]-base >= nn)
      return METIS_ERROR_INPUT;
  }

  return METIS_OK;
}


/*************************************************************************/
/*! Checks that every element contains each node at most once.

    Repeated membership would make the element-incidence algorithms count the
    same overlap repeatedly and can overflow their idx_t markers. The return
    value is one for a valid mesh, zero for duplicate membership, and -1 when
    the temporary marker cannot be allocated.
*/
/*************************************************************************/
static inline int ValidateMeshElementNodes(idx_t ne, idx_t nn,
    const idx_t *eptr, const idx_t *eind, idx_t numflag)
{
  unsigned char *seen;
  idx_t i, j, k, istart, iend;

  seen = (unsigned char *)calloc((size_t)nn, sizeof(*seen));
  if (seen == NULL)
    return -1;

  for (i=0; i<ne; i++) {
    istart = eptr[i]-numflag;
    iend = eptr[i+1]-numflag;
    for (j=istart; j<iend; j++) {
      k = eind[j]-numflag;
      if (seen[k]) {
        free(seen);
        return 0;
      }
      seen[k] = 1;
    }
    for (j=istart; j<iend; j++)
      seen[eind[j]-numflag] = 0;
  }

  free(seen);
  return 1;
}


#endif
