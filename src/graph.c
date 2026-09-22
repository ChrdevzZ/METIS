/**
\file
\brief Functions that deal with setting up the graphs for METIS.

\date   Started 7/25/1997
\author George  
\author Copyright 1997-2009, Regents of the University of Minnesota 
\version\verbatim $Id: graph.c 15817 2013-11-25 14:58:41Z karypis $ \endverbatim
*/

#include "metislib.h"

#include <fcntl.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif


/*************************************************************************/
/*! This function sets up the graph from the user input */
/*************************************************************************/
int SetupGraph(ctrl_t *ctrl, idx_t nvtxs, idx_t ncon, idx_t *xadj,
             idx_t *adjncy, idx_t *vwgt, idx_t *vsize, idx_t *adjwgt,
             graph_t **r_graph)
{
  volatile int sigrval=0;
  volatile int rstatus=METIS_OK;
  idx_t i, j, nedges;
#if GKLIB_ASSERTIONS_ENABLED
  int isvalid;
#endif
  graph_t *cleanup_graph;
  graph_t * volatile graph=NULL;

  if (r_graph == NULL) {
    errno = EINVAL;
    return METIS_ERROR_INPUT;
  }
  *r_graph = NULL;
  if (ctrl == NULL || nvtxs <= 0 || ncon <= 0 || xadj == NULL) {
    errno = EINVAL;
    return METIS_ERROR_INPUT;
  }

  nedges = xadj[nvtxs];
  if (nedges < 0 || (nedges > 0 && adjncy == NULL)) {
    errno = EINVAL;
    return METIS_ERROR_INPUT;
  }
  if (nvtxs == IDX_MAX || nvtxs > IDX_MAX/ncon ||
      (vwgt == NULL &&
       (uintmax_t)(nvtxs*ncon) >
           (uintmax_t)SIZE_MAX/sizeof(idx_t)) ||
      (uintmax_t)ncon > (uintmax_t)SIZE_MAX/sizeof(idx_t) ||
      (uintmax_t)ncon > (uintmax_t)SIZE_MAX/sizeof(real_t) ||
      ((ctrl->objtype == METIS_OBJTYPE_VOL || adjwgt == NULL) &&
       (uintmax_t)nedges > (uintmax_t)SIZE_MAX/sizeof(idx_t)) ||
      (ctrl->objtype == METIS_OBJTYPE_VOL && vsize == NULL &&
       (uintmax_t)nvtxs > (uintmax_t)SIZE_MAX/sizeof(idx_t)) ||
      ((ctrl->optype == METIS_OP_PMETIS ||
        ctrl->optype == METIS_OP_OMETIS) &&
       (uintmax_t)nvtxs > (uintmax_t)SIZE_MAX/sizeof(idx_t))) {
    errno = EOVERFLOW;
    return METIS_ERROR_MEMORY;
  }

  if (!gk_sigtrap()) {
    errno = ENOMEM;
    return METIS_ERROR_MEMORY;
  }
  METIS_SIGCATCH(sigrval);
  if (sigrval != 0)
    goto SIGNAL_ERROR;

  /* allocate the graph and fill in the fields */
  graph = CreateGraph();
  if (graph == NULL)
    goto MEMORY_ERROR;

  graph->nvtxs  = nvtxs;
  graph->nedges = nedges;
  graph->ncon   = ncon;

  graph->xadj      = xadj;
  graph->free_xadj = 0;

  graph->adjncy      = adjncy;
  graph->free_adjncy = 0;

  graph->droppedewgt = 0;

  /* setup the vertex weights */
  if (vwgt) {
    graph->vwgt      = vwgt;
    graph->free_vwgt = 0;
  }
  else {
    graph->vwgt = ismalloc(ncon*nvtxs, 1, "SetupGraph: vwgt");
    if (graph->vwgt == NULL)
      goto MEMORY_ERROR;
  }

  if (ctrl->objtype == METIS_OBJTYPE_VOL) { 
    /* Setup the vsize */
    if (vsize) {
      graph->vsize      = vsize;
      graph->free_vsize = 0;
    }
    else {
      graph->vsize = ismalloc(nvtxs, 1, "SetupGraph: vsize");
      if (graph->vsize == NULL)
        goto MEMORY_ERROR;
    }

    /* Allocate memory for edge weights and initialize them to the sum of the vsize */
    graph->adjwgt = imalloc(graph->nedges, "SetupGraph: adjwgt");
    if (graph->adjwgt == NULL)
      goto MEMORY_ERROR;
    for (i=0; i<nvtxs; i++) {
      for (j=xadj[i]; j<xadj[i+1]; j++)
        graph->adjwgt[j] = 1+graph->vsize[i]+graph->vsize[adjncy[j]];
    }
  }
  else { /* For edgecut minimization */
    /* setup the edge weights */
    if (adjwgt) {
      graph->adjwgt      = adjwgt;
      graph->free_adjwgt = 0;
    }
    else {
      graph->adjwgt = ismalloc(graph->nedges, 1, "SetupGraph: adjwgt");
      if (graph->adjwgt == NULL)
        goto MEMORY_ERROR;
    }
  }


  /* setup various derived info */
  {
    int status = SetupGraph_tvwgt(graph);
    if (status != METIS_OK) {
      rstatus = status;
      sigrval = status == METIS_ERROR_MEMORY ? SIGMEM : SIGERR;
      goto SIGNAL_ERROR;
    }
  }

  if ((ctrl->optype == METIS_OP_PMETIS ||
       ctrl->optype == METIS_OP_OMETIS)) {
    int status = SetupGraph_label(graph);
    if (status != METIS_OK) {
      rstatus = status;
      sigrval = status == METIS_ERROR_MEMORY ? SIGMEM : SIGERR;
      goto SIGNAL_ERROR;
    }
  }

#if GKLIB_ASSERTIONS_ENABLED
  errno = 0;
  isvalid = CheckGraph((graph_t *)graph, ctrl->numflag, 1);
  if (!isvalid && (errno == ENOMEM || errno == EOVERFLOW))
    goto MEMORY_ERROR;
  ASSERT(isvalid);
#endif

  *r_graph = (graph_t *)graph;
  gk_siguntrap();
  return METIS_OK;

MEMORY_ERROR:
  sigrval = SIGMEM;

SIGNAL_ERROR:
  cleanup_graph = (graph_t *)graph;
  graph = NULL;
  gk_siguntrap();
  FreeGraph(&cleanup_graph);
  if (errno == 0)
    errno = sigrval == SIGMEM ? ENOMEM : EINVAL;
  if (rstatus != METIS_OK)
    return rstatus;
  if (sigrval != SIGMEM)
    return METIS_ERROR;
  return METIS_ERROR_MEMORY;
}


/*************************************************************************/
/*! Set's up the tvwgt/invtvwgt info */
/*************************************************************************/
int SetupGraph_tvwgt(graph_t *graph)
{
  volatile int sigrval=0;
  idx_t i;
  idx_t *cleanup_tvwgt;
  idx_t * volatile new_tvwgt=NULL;
  real_t *cleanup_invtvwgt;
  real_t * volatile new_invtvwgt=NULL;

  if (graph == NULL || graph->ncon <= 0 || graph->nvtxs < 0 ||
      (graph->nvtxs > 0 && graph->vwgt == NULL)) {
    errno = EINVAL;
    return METIS_ERROR_INPUT;
  }
  if ((uintmax_t)graph->ncon > (uintmax_t)SIZE_MAX/sizeof(idx_t) ||
      (uintmax_t)graph->ncon > (uintmax_t)SIZE_MAX/sizeof(real_t)) {
    errno = EOVERFLOW;
    return METIS_ERROR_MEMORY;
  }

  if (graph->tvwgt == NULL || graph->invtvwgt == NULL) {
    if (!gk_sigtrap()) {
      errno = ENOMEM;
      return METIS_ERROR_MEMORY;
    }
    METIS_SIGCATCH(sigrval);
    if (sigrval == 0) {
      if (graph->tvwgt == NULL)
        new_tvwgt = imalloc(graph->ncon, "SetupGraph_tvwgt: tvwgt");
      if (graph->invtvwgt == NULL)
        new_invtvwgt = rmalloc(graph->ncon,
            "SetupGraph_tvwgt: invtvwgt");
    }
    gk_siguntrap();
    if (sigrval != 0 ||
        (graph->tvwgt == NULL && new_tvwgt == NULL) ||
        (graph->invtvwgt == NULL && new_invtvwgt == NULL)) {
      cleanup_tvwgt = (idx_t *)new_tvwgt;
      cleanup_invtvwgt = (real_t *)new_invtvwgt;
      gk_free((void **)&cleanup_tvwgt, &cleanup_invtvwgt, LTERM);
      if (errno == 0)
        errno = ENOMEM;
      return METIS_ERROR_MEMORY;
    }

    if (graph->tvwgt == NULL)
      graph->tvwgt = (idx_t *)new_tvwgt;
    if (graph->invtvwgt == NULL)
      graph->invtvwgt = (real_t *)new_invtvwgt;
  }

  for (i=0; i<graph->ncon; i++) {
    graph->tvwgt[i]    = graph->nvtxs > 0 ?
        isum(graph->nvtxs, graph->vwgt+i, graph->ncon) : 0;
    graph->invtvwgt[i] = 1.0/(graph->tvwgt[i] > 0 ? graph->tvwgt[i] : 1);
  }
  return METIS_OK;
}


/*************************************************************************/
/*! Set's up the label info */
/*************************************************************************/
int SetupGraph_label(graph_t *graph)
{
  volatile int sigrval=0;
  idx_t i;
  idx_t * volatile new_label=NULL;

  if (graph == NULL || graph->nvtxs < 0) {
    errno = EINVAL;
    return METIS_ERROR_INPUT;
  }
  if ((uintmax_t)graph->nvtxs > (uintmax_t)SIZE_MAX/sizeof(idx_t)) {
    errno = EOVERFLOW;
    return METIS_ERROR_MEMORY;
  }

  if (graph->label == NULL) {
    if (!gk_sigtrap()) {
      errno = ENOMEM;
      return METIS_ERROR_MEMORY;
    }
    METIS_SIGCATCH(sigrval);
    if (sigrval == 0)
      new_label = imalloc(graph->nvtxs, "SetupGraph_label: label");
    gk_siguntrap();
    if (sigrval != 0 || new_label == NULL) {
      if (errno == 0)
        errno = ENOMEM;
      return METIS_ERROR_MEMORY;
    }
    graph->label = (idx_t *)new_label;
  }

  for (i=0; i<graph->nvtxs; i++)
    graph->label[i] = i;
  return METIS_OK;
}


/*************************************************************************/
/*! Setup the various arrays for the split graph */
/*************************************************************************/
int SetupSplitGraph(graph_t *graph, idx_t snvtxs, idx_t snedges,
    graph_t **r_sgraph)
{
  volatile int sigrval=0;
  graph_t *cleanup_graph;
  graph_t * volatile sgraph=NULL;

  if (r_sgraph == NULL)
    return METIS_ERROR_INPUT;
  *r_sgraph = NULL;
  if (graph == NULL || graph->ncon <= 0 || snvtxs < 0 || snedges < 0) {
    errno = EINVAL;
    return METIS_ERROR_INPUT;
  }
  if (snvtxs == IDX_MAX || snvtxs > IDX_MAX/graph->ncon ||
      (uintmax_t)(snvtxs+1) > (uintmax_t)SIZE_MAX/sizeof(idx_t) ||
      (uintmax_t)(snvtxs*graph->ncon) >
          (uintmax_t)SIZE_MAX/sizeof(idx_t) ||
      (uintmax_t)snedges > (uintmax_t)SIZE_MAX/sizeof(idx_t)) {
    errno = EOVERFLOW;
    return METIS_ERROR_MEMORY;
  }

  if (!gk_sigtrap()) {
    errno = ENOMEM;
    return METIS_ERROR_MEMORY;
  }
  METIS_SIGCATCH(sigrval);
  if (sigrval != 0)
    goto SIGNAL_ERROR;

  sgraph = CreateGraph();
  if (sgraph == NULL)
    goto MEMORY_ERROR;

  sgraph->nvtxs  = snvtxs;
  sgraph->nedges = snedges;
  sgraph->ncon   = graph->ncon;

  /* Allocate memory for the split graph */
  sgraph->xadj        = imalloc(snvtxs+1, "SetupSplitGraph: xadj");
  sgraph->vwgt        = imalloc(sgraph->ncon*snvtxs, "SetupSplitGraph: vwgt");
  sgraph->adjncy      = imalloc(snedges > 0 ? snedges : 1,
                                "SetupSplitGraph: adjncy");
  sgraph->adjwgt      = imalloc(snedges > 0 ? snedges : 1,
                                "SetupSplitGraph: adjwgt");
  sgraph->label       = imalloc(snvtxs,   "SetupSplitGraph: label");
  sgraph->tvwgt       = imalloc(sgraph->ncon, "SetupSplitGraph: tvwgt");
  sgraph->invtvwgt    = rmalloc(sgraph->ncon, "SetupSplitGraph: invtvwgt");

  if (graph->vsize)
    sgraph->vsize     = imalloc(snvtxs,   "SetupSplitGraph: vsize");

  if (sgraph->xadj == NULL || sgraph->vwgt == NULL ||
      sgraph->adjncy == NULL || sgraph->adjwgt == NULL ||
      sgraph->label == NULL || sgraph->tvwgt == NULL ||
      sgraph->invtvwgt == NULL ||
      (graph->vsize != NULL && sgraph->vsize == NULL))
    goto MEMORY_ERROR;

  *r_sgraph = (graph_t *)sgraph;
  gk_siguntrap();
  return METIS_OK;

MEMORY_ERROR:
  sigrval = SIGMEM;

SIGNAL_ERROR:
  cleanup_graph = (graph_t *)sgraph;
  sgraph = NULL;
  gk_siguntrap();
  FreeGraph(&cleanup_graph);
  if (errno == 0)
    errno = sigrval == SIGMEM ? ENOMEM : EINVAL;
  if (sigrval != SIGMEM)
    return METIS_ERROR;
  return METIS_ERROR_MEMORY;
}


/*************************************************************************/
/*! This function creates and initializes a graph_t data structure */
/*************************************************************************/
graph_t *CreateGraph(void)
{
  volatile int sigrval=0;
  graph_t * volatile graph=NULL;

  if (!gk_sigtrap()) {
    errno = ENOMEM;
    return NULL;
  }
  METIS_SIGCATCH(sigrval);
  if (sigrval == 0)
    graph = (graph_t *)gk_malloc(sizeof(graph_t), "CreateGraph: graph");
  gk_siguntrap();
  if (sigrval != 0 || graph == NULL) {
    if (errno == 0)
      errno = ENOMEM;
    return NULL;
  }

  InitGraph((graph_t *)graph);

  return (graph_t *)graph;
}


/*************************************************************************/
/*! This function initializes a graph_t data structure */
/*************************************************************************/
void InitGraph(graph_t *graph) 
{
  memset((void *)graph, 0, sizeof(graph_t));

  /* graph size constants */
  graph->nvtxs     = -1;
  graph->nedges    = -1;
  graph->ncon      = -1;
  graph->mincut    = -1;
  graph->minvol    = -1;
  graph->nbnd      = -1;

  /* memory for the graph structure */
  graph->xadj      = NULL;
  graph->vwgt      = NULL;
  graph->vsize     = NULL;
  graph->adjncy    = NULL;
  graph->adjwgt    = NULL;
  graph->label     = NULL;
  graph->cmap      = NULL;
  graph->tvwgt     = NULL;
  graph->invtvwgt  = NULL;

  /* by default these are set to true, but the can be explicitly changed afterwards */
  graph->free_xadj   = 1;
  graph->free_vwgt   = 1;
  graph->free_vsize  = 1;
  graph->free_adjncy = 1;
  graph->free_adjwgt = 1;


  /* memory for the partition/refinement structure */
  graph->where     = NULL;
  graph->pwgts     = NULL;
  graph->id        = NULL;
  graph->ed        = NULL;
  graph->bndptr    = NULL;
  graph->bndind    = NULL;
  graph->nrinfo    = NULL;
  graph->ckrinfo   = NULL;
  graph->vkrinfo   = NULL;

  /* linked-list structure */
  graph->coarser   = NULL;
  graph->finer     = NULL;

}


/*************************************************************************/
/*! This function frees the memory storing the structure of the graph */
/*************************************************************************/
void FreeSData(graph_t *graph) 
{
  /* free graph structure */
  if (graph->free_xadj)
    gk_free((void **)&graph->xadj, LTERM);
  if (graph->free_vwgt)
    gk_free((void **)&graph->vwgt, LTERM);
  if (graph->free_vsize)
    gk_free((void **)&graph->vsize, LTERM);
  if (graph->free_adjncy)
    gk_free((void **)&graph->adjncy, LTERM);
  if (graph->free_adjwgt)
    gk_free((void **)&graph->adjwgt, LTERM);
}


/*************************************************************************/
/*! This function frees the refinement/partition memory stored in a graph */
/*************************************************************************/
void FreeRData(graph_t *graph) 
{

  /* The following is for the -minconn and -contig to work properly in
     the vol-refinement routines */
  if ((void *)graph->ckrinfo == (void *)graph->vkrinfo)
    graph->ckrinfo = NULL;


  /* free partition/refinement structure */
  gk_free((void **)&graph->where, &graph->pwgts, &graph->id, &graph->ed, 
      &graph->bndptr, &graph->bndind, &graph->nrinfo, &graph->ckrinfo, 
      &graph->vkrinfo, LTERM);
}


/*************************************************************************/
/*! This function deallocates any memory stored in a graph */
/*************************************************************************/
void FreeGraph(graph_t **r_graph) 
{
  graph_t *graph;

  if (r_graph == NULL || *r_graph == NULL)
    return;
  graph = *r_graph;

  /* free the graph structure's fields */
  FreeSData(graph);

  /* free the partition/refinement fields */
  FreeRData(graph);

  gk_free((void **)&graph->tvwgt, &graph->invtvwgt, &graph->label, 
      &graph->cmap, &graph, LTERM);

  *r_graph = NULL;
}


#ifdef _WIN32
/*************************************************************************/
/*! Maps the Windows errors used by the on-disk graph helpers to errno. */
/*************************************************************************/
static void graph_SetWindowsFileError(DWORD error)
{
  switch (error) {
    case ERROR_FILE_EXISTS:
    case ERROR_ALREADY_EXISTS:
      errno = EEXIST;
      break;
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
      errno = ENOENT;
      break;
    case ERROR_ACCESS_DENIED:
    case ERROR_SHARING_VIOLATION:
      errno = EACCES;
      break;
    default:
      errno = EIO;
      break;
  }
}
#endif


/*************************************************************************/
/*! Creates a new on-disk graph file without following an existing entry.

    The returned stream owns the underlying operating-system handle. The
    caller must close it with fclose().
*/
/*************************************************************************/
static FILE *graph_CreateDiskFile(const char *filename)
{
  FILE *stream;
  int descriptor, saved_errno;

#ifdef _WIN32
  HANDLE handle;

  handle = CreateFileA(filename, GENERIC_WRITE, 0, NULL, CREATE_NEW,
      FILE_ATTRIBUTE_NORMAL, NULL);
  if (handle == INVALID_HANDLE_VALUE) {
    graph_SetWindowsFileError(GetLastError());
    return NULL;
  }
  descriptor = _open_osfhandle((intptr_t)handle, _O_WRONLY | _O_BINARY);
  if (descriptor == -1) {
    saved_errno = errno;
    CloseHandle(handle);
    errno = saved_errno;
    return NULL;
  }
  stream = _fdopen(descriptor, "wb");
#else
  int flags = O_WRONLY | O_CREAT | O_EXCL;

#ifdef O_CLOEXEC
  flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
  flags |= O_NOFOLLOW;
#endif
  descriptor = open(filename, flags, 0600);
  if (descriptor == -1)
    return NULL;
  stream = fdopen(descriptor, "wb");
#endif

  if (stream == NULL) {
    saved_errno = errno;
#ifdef _WIN32
    _close(descriptor);
    DeleteFileA(filename);
#else
    close(descriptor);
    unlink(filename);
#endif
    errno = saved_errno;
  }
  return stream;
}


/*************************************************************************/
/*! Opens an existing regular on-disk graph file. */
/*************************************************************************/
static FILE *graph_OpenDiskFile(const char *filename)
{
  FILE *stream;
  int descriptor, saved_errno;

#ifdef _WIN32
  BY_HANDLE_FILE_INFORMATION information;
  HANDLE handle;

  handle = CreateFileA(filename, GENERIC_READ, FILE_SHARE_READ, NULL,
      OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN,
      NULL);
  if (handle == INVALID_HANDLE_VALUE) {
    graph_SetWindowsFileError(GetLastError());
    return NULL;
  }
  if (!GetFileInformationByHandle(handle, &information)) {
    graph_SetWindowsFileError(GetLastError());
    saved_errno = errno;
    CloseHandle(handle);
    errno = saved_errno;
    return NULL;
  }
  if (information.dwFileAttributes &
      (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) {
    CloseHandle(handle);
    errno = EINVAL;
    return NULL;
  }
  descriptor = _open_osfhandle((intptr_t)handle, _O_RDONLY | _O_BINARY);
  if (descriptor == -1) {
    saved_errno = errno;
    CloseHandle(handle);
    errno = saved_errno;
    return NULL;
  }
  stream = _fdopen(descriptor, "rb");
#else
  struct stat status;
#ifndef O_NOFOLLOW
  struct stat linkstatus;
#endif
  int flags = O_RDONLY;

#ifdef O_CLOEXEC
  flags |= O_CLOEXEC;
#endif
#ifndef O_NOFOLLOW
  if (lstat(filename, &linkstatus) != 0)
    return NULL;
  if (!S_ISREG(linkstatus.st_mode)) {
    errno = EINVAL;
    return NULL;
  }
#else
  flags |= O_NOFOLLOW;
#endif
  descriptor = open(filename, flags);
  if (descriptor == -1)
    return NULL;
  if (fstat(descriptor, &status) != 0) {
    saved_errno = errno;
    close(descriptor);
    errno = saved_errno;
    return NULL;
  }
  if (!S_ISREG(status.st_mode)) {
    close(descriptor);
    errno = EINVAL;
    return NULL;
  }
#ifndef O_NOFOLLOW
  if (status.st_dev != linkstatus.st_dev ||
      status.st_ino != linkstatus.st_ino) {
    close(descriptor);
    errno = EINVAL;
    return NULL;
  }
#endif
  stream = fdopen(descriptor, "rb");
#endif

  if (stream == NULL) {
    saved_errno = errno;
#ifdef _WIN32
    _close(descriptor);
#else
    close(descriptor);
#endif
    errno = saved_errno;
  }
  return stream;
}


/*************************************************************************/
/*! Selects and exclusively creates a bounded, process-local graph file. */
/*************************************************************************/
static FILE *graph_CreateNewDiskFile(ctrl_t *ctrl, int *next_gID,
    int *r_gID, char *filename, size_t filename_size)
{
  FILE *stream;
  int attempt, candidate, length;

  if (filename_size > 0)
    filename[0] = '\0';

  for (attempt=0; attempt<1024; attempt++) {
    if (*next_gID == INT_MAX) {
      errno = EOVERFLOW;
      return NULL;
    }
    candidate = (*next_gID)++;
    length = snprintf(filename, filename_size, "metis%d.%d",
        (int)ctrl->pid, candidate);
    if (length < 0 || (size_t)length >= filename_size) {
      errno = EOVERFLOW;
      return NULL;
    }

    stream = graph_CreateDiskFile(filename);
    if (stream != NULL) {
      *r_gID = candidate;
      return stream;
    }
    if (errno != EEXIST)
      return NULL;
  }

  errno = EEXIST;
  return NULL;
}


/*************************************************************************/
/*! Adds an on-disk item count without changing the total on overflow. */
/*************************************************************************/
static int graph_AddDiskItems(size_t *total, size_t count)
{
  if (count > SIZE_MAX-*total)
    return 0;
  *total += count;
  return 1;
}


struct ondisk_file_t {
  struct ondisk_file_t *next;
  int gID;
};


/*************************************************************************/
/*! Removes a registered spill only after its stream has been closed. */
/*************************************************************************/
static int graph_RemoveDiskFile(ctrl_t *ctrl, int gID)
{
  struct ondisk_file_t **link, *entry;
  char filename[64];
  int length;

  length = snprintf(filename, sizeof(filename), "metis%d.%d",
      (int)ctrl->pid, gID);
  if (length < 0 || (size_t)length >= sizeof(filename)) {
    errno = EOVERFLOW;
    return -1;
  }
  if (gk_rmpath(filename) != 0)
    return -1;

  for (link=&ctrl->ondisk_files; *link != NULL; link=&(*link)->next) {
    if ((*link)->gID == gID) {
      entry = *link;
      *link = entry->next;
      free(entry);
      break;
    }
  }
  return 0;
}


/*************************************************************************/
/*! Releases every still-live spill at the public API recovery boundary. */
/*************************************************************************/
void graph_CleanupDiskFiles(ctrl_t *ctrl)
{
  struct ondisk_file_t *entry;
  char filename[64];
  int length;

  if (ctrl == NULL)
    return;
  while ((entry = ctrl->ondisk_files) != NULL) {
    ctrl->ondisk_files = entry->next;
    length = snprintf(filename, sizeof(filename), "metis%d.%d",
        (int)ctrl->pid, entry->gID);
    if (length >= 0 && (size_t)length < sizeof(filename))
      (void)gk_rmpath(filename);
    free(entry);
  }
}


/*************************************************************************/
/*! This function writes the key contents of the graph on disk and frees
    the associated memory */
/*************************************************************************/
void graph_WriteToDisk(ctrl_t *ctrl, graph_t *graph) 
{
  static int gID = 1;
  int new_gID, old_gID;
  char outfile[1024];
  FILE *fpout;
  struct ondisk_file_t *entry;
  size_t edge_count, item_count, vertex_count, vertex_weight_count;
  int failed=0;

  if (ctrl->ondisk == 0)
    return;
  if (graph->ondisk != 0)
    return;  /* a previously written graph already has a live spill */

  if (graph->nvtxs < 0 || graph->ncon <= 0 || graph->xadj == NULL ||
      graph->xadj[graph->nvtxs] < 0 ||
      (uintmax_t)graph->nvtxs > (uintmax_t)SIZE_MAX-1 ||
      (uintmax_t)graph->ncon > (uintmax_t)SIZE_MAX-1 ||
      (uintmax_t)graph->xadj[graph->nvtxs] > (uintmax_t)SIZE_MAX) {
    gk_errexit(SIGERR, "Invalid graph dimensions for on-disk storage.\n");
    return;
  }
  vertex_count = (size_t)graph->nvtxs;
  edge_count = (size_t)graph->xadj[graph->nvtxs];
  if (vertex_count > SIZE_MAX/(size_t)graph->ncon) {
    gk_errexit(SIGERR, "On-disk graph size overflow.\n");
    return;
  }
  vertex_weight_count = vertex_count*(size_t)graph->ncon;
  item_count = 0;
  if (!graph_AddDiskItems(&item_count, vertex_count+1) ||
      !graph_AddDiskItems(&item_count, vertex_weight_count) ||
      !graph_AddDiskItems(&item_count, edge_count) ||
      !graph_AddDiskItems(&item_count, edge_count) ||
      (ctrl->objtype == METIS_OBJTYPE_VOL &&
       !graph_AddDiskItems(&item_count, vertex_count)) ||
      item_count > SIZE_MAX/sizeof(idx_t)) {
    gk_errexit(SIGERR, "On-disk graph byte size overflow.\n");
    return;
  }

  if (sizeof(idx_t)*item_count < 128*1024*1024)
    return;

  /* Reserve ownership before creating a file; allocation failure cannot
     leave an unregistered spill behind. */
  entry = (struct ondisk_file_t *)malloc(sizeof(*entry));
  if (entry == NULL) {
    printf("Failed to track on-disk graph file.\n");
    return;
  }
  old_gID = graph->gID;
  if ((fpout = graph_CreateNewDiskFile(ctrl, &gID, &new_gID, outfile,
      sizeof(outfile))) == NULL) {
    free(entry);
    printf("Failed to create on-disk graph file%s%s.\n",
        outfile[0] == '\0' ? "" : " ", outfile);
    return;
  }

  if ((graph->free_xadj && graph->xadj == NULL) ||
      (graph->free_vwgt && graph->vwgt == NULL) ||
      (graph->free_adjncy && edge_count > 0 && graph->adjncy == NULL) ||
      (graph->free_adjwgt && edge_count > 0 && graph->adjwgt == NULL) ||
      (ctrl->objtype == METIS_OBJTYPE_VOL && graph->free_vsize &&
       graph->vsize == NULL))
    goto error;

  if (graph->free_xadj) {
    if (fwrite(graph->xadj, sizeof(idx_t), vertex_count+1, fpout) !=
        vertex_count+1)
      goto error;
  }
  if (graph->free_vwgt) {
    if (fwrite(graph->vwgt, sizeof(idx_t), vertex_weight_count, fpout) !=
        vertex_weight_count)
      goto error;
  }
  if (graph->free_adjncy) {
    if (fwrite(graph->adjncy, sizeof(idx_t), edge_count, fpout) != edge_count)
      goto error;
  }
  if (graph->free_adjwgt) {
    if (fwrite(graph->adjwgt, sizeof(idx_t), edge_count, fpout) != edge_count)
      goto error;
  }
  if (ctrl->objtype == METIS_OBJTYPE_VOL) { 
    if (graph->free_vsize) {
      if (fwrite(graph->vsize, sizeof(idx_t), vertex_count, fpout) !=
          vertex_count)
        goto error;
    }
  }

  if (fflush(fpout) != 0 || ferror(fpout))
    failed = 1;
  if (fclose(fpout) != 0)
    failed = 1;
  if (failed)
    goto closed_error;

  entry->gID = new_gID;
  entry->next = ctrl->ondisk_files;
  ctrl->ondisk_files = entry;
  if (old_gID > 0)
    (void)graph_RemoveDiskFile(ctrl, old_gID);

  if (graph->free_xadj)
    gk_free((void **)&graph->xadj, LTERM);
  if (graph->free_vwgt)
    gk_free((void **)&graph->vwgt, LTERM);
  if (graph->free_vsize)
    gk_free((void **)&graph->vsize, LTERM);
  if (graph->free_adjncy)
    gk_free((void **)&graph->adjncy, LTERM);
  if (graph->free_adjwgt)
    gk_free((void **)&graph->adjwgt, LTERM);

  graph->gID    = new_gID;
  graph->ondisk = 1;
  return;

error:
  fclose(fpout);
closed_error:
  printf("Failed on writing %s\n", outfile);
  gk_rmpath(outfile);
  free(entry);
}


/*************************************************************************/
/*! This function reads the key contents of a graph from the disk */
/*************************************************************************/
void graph_ReadFromDisk(ctrl_t *ctrl, graph_t *graph) 
{
  idx_t i, nvtxs, *xadj;
  idx_t *new_xadj=NULL, *new_vwgt=NULL, *new_adjncy=NULL;
  idx_t *new_adjwgt=NULL, *new_vsize=NULL;
  char infile[1024];
  FILE *fpin=NULL;
  size_t edge_count, vertex_count, vertex_weight_count;
  int failed, name_length, trailing;

  if (graph->ondisk == 0)
    return;  /* this graph is not on the disk */

  if (graph->nvtxs < 0 || graph->ncon <= 0 || graph->nedges < 0 ||
      (uintmax_t)graph->nvtxs > (uintmax_t)SIZE_MAX-1 ||
      (uintmax_t)graph->nedges > (uintmax_t)SIZE_MAX ||
      (size_t)graph->nvtxs > SIZE_MAX/(size_t)graph->ncon) {
    gk_errexit(SIGERR, "Invalid graph dimensions for on-disk restore.\n");
    return;
  }
  vertex_count = (size_t)graph->nvtxs;
  edge_count = (size_t)graph->nedges;
  vertex_weight_count = vertex_count*(size_t)graph->ncon;

  name_length = snprintf(infile, sizeof(infile), "metis%d.%d",
      (int)ctrl->pid, graph->gID);
  if (name_length < 0 || (size_t)name_length >= sizeof(infile)) {
    gk_errexit(SIGERR, "Failed to format on-disk graph filename.\n");
    return;
  }

  nvtxs = graph->nvtxs;
  if (graph->free_xadj) {
    new_xadj = imalloc(vertex_count+1, "graph_ReadFromDisk: xadj");
    if (new_xadj == NULL)
      goto error;
  }

  if (graph->free_vwgt) {
    if (vertex_weight_count > 0) {
      new_vwgt = imalloc(vertex_weight_count, "graph_ReadFromDisk: vwgt");
      if (new_vwgt == NULL)
        goto error;
    }
  }

  if (graph->free_adjncy) {
    if (edge_count > 0) {
      new_adjncy = imalloc(edge_count, "graph_ReadFromDisk: adjncy");
      if (new_adjncy == NULL)
        goto error;
    }
  }

  if (graph->free_adjwgt) {
    if (edge_count > 0) {
      new_adjwgt = imalloc(edge_count, "graph_ReadFromDisk: adjwgt");
      if (new_adjwgt == NULL)
        goto error;
    }
  }

  if (ctrl->objtype == METIS_OBJTYPE_VOL) {
    if (graph->free_vsize) {
      if (vertex_count > 0) {
        new_vsize = imalloc(vertex_count, "graph_ReadFromDisk: vsize");
        if (new_vsize == NULL)
          goto error;
      }
    }
  }

  /* Open only after allocation so a memory signal cannot leak the stream. */
  if ((fpin = graph_OpenDiskFile(infile)) == NULL) {
    gk_free((void **)&new_xadj, &new_vwgt, &new_adjncy, &new_adjwgt,
        &new_vsize, LTERM);
    gk_errexit(SIGERR, "Failed to open graph %s from disk.\n", infile);
    return;
  }

  if (graph->free_xadj &&
      fread(new_xadj, sizeof(idx_t), vertex_count+1, fpin) != vertex_count+1)
    goto error;
  xadj = graph->free_xadj ? new_xadj : graph->xadj;
  if (xadj == NULL || xadj[0] != 0 || xadj[nvtxs] != graph->nedges)
    goto error;
  for (i=0; i<nvtxs; i++) {
    if (xadj[i] > xadj[i+1])
      goto error;
  }

  if (graph->free_vwgt && vertex_weight_count > 0 &&
      fread(new_vwgt, sizeof(idx_t), vertex_weight_count, fpin) !=
      vertex_weight_count)
    goto error;
  if (graph->free_adjncy && edge_count > 0) {
    if (fread(new_adjncy, sizeof(idx_t), edge_count, fpin) != edge_count)
      goto error;
    for (i=0; i<(idx_t)edge_count; i++) {
      if (new_adjncy[i] < 0 || new_adjncy[i] >= nvtxs)
        goto error;
    }
  }
  if (graph->free_adjwgt && edge_count > 0 &&
      fread(new_adjwgt, sizeof(idx_t), edge_count, fpin) != edge_count)
    goto error;
  if (ctrl->objtype == METIS_OBJTYPE_VOL && graph->free_vsize &&
      vertex_count > 0 &&
      fread(new_vsize, sizeof(idx_t), vertex_count, fpin) != vertex_count)
    goto error;

  trailing = fgetc(fpin);
  failed = trailing != EOF || ferror(fpin);
  if (fclose(fpin) != 0)
    failed = 1;
  fpin = NULL;
  if (failed)
    goto error;

  if (graph->free_xadj)
    graph->xadj = new_xadj;
  if (graph->free_vwgt)
    graph->vwgt = new_vwgt;
  if (graph->free_adjncy)
    graph->adjncy = new_adjncy;
  if (graph->free_adjwgt)
    graph->adjwgt = new_adjwgt;
  if (ctrl->objtype == METIS_OBJTYPE_VOL && graph->free_vsize)
    graph->vsize = new_vsize;
  (void)graph_RemoveDiskFile(ctrl, graph->gID);

  graph->gID    = 0;
  graph->ondisk = 0;
  return;

error:
  if (fpin != NULL)
    fclose(fpin);
  gk_free((void **)&new_xadj, &new_vwgt, &new_adjncy, &new_adjwgt,
      &new_vsize, LTERM);
  (void)graph_RemoveDiskFile(ctrl, graph->gID);
  graph->ondisk = 0;
  gk_errexit(SIGERR, "Failed to restore graph %s from the disk.\n", infile);
}
