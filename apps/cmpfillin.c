/*
 * Copyright 1997, Regents of the University of Minnesota
 *
 * cmpfillin.c
 *
 * This file takes a graph and a fill-reducing ordering and computes
 * the fillin.
 *
 * Started 9/1/2004
 * George
 *
 * $Id: cmpfillin.c 9982 2011-05-25 17:18:00Z karypis $
 *
 */

#include "metisbin.h"


/*************************************************************************/
/*! Duplicates a command-line path under a cleanup-safe allocation trap. */
/*************************************************************************/
static int CopyInputFilename(char *source, char **r_filename)
{
  volatile int sigrval=0;

  *r_filename = NULL;
  if (!gk_sigtrap()) {
    errno = ENOMEM;
    return 0;
  }
  METIS_SIGCATCH(sigrval);
  if (sigrval == 0)
    *r_filename = gk_strdup(source);
  gk_siguntrap();

  if (sigrval != 0) {
    if (errno == 0)
      errno = ENOMEM;
    return 0;
  }
  return *r_filename != NULL;
}


/*************************************************************************/
/*! Allocates both ordering vectors without leaking a partial result. */
/*************************************************************************/
static int AllocateOrderVectors(idx_t count, idx_t **r_perm,
    idx_t **r_iperm)
{
  volatile int sigrval=0;

  *r_perm = *r_iperm = NULL;
  if (!gk_sigtrap()) {
    errno = ENOMEM;
    return 0;
  }
  METIS_SIGCATCH(sigrval);
  if (sigrval == 0) {
    *r_perm  = imalloc(count, "main: perm");
    *r_iperm = imalloc(count, "main: iperm");
  }
  gk_siguntrap();

  if (sigrval != 0 || *r_perm == NULL || *r_iperm == NULL) {
    gk_free((void **)r_perm, r_iperm, LTERM);
    if (errno == 0)
      errno = ENOMEM;
    return 0;
  }
  return 1;
}



/*************************************************************************
* Let the game begin
**************************************************************************/
int main(int argc, char *argv[])
{
  idx_t i;
  idx_t *perm=NULL, *iperm=NULL;
  graph_t *graph=NULL;
  params_t params;
  uint64_t maxlnz, opc;
  int status=METIS_OK;

  if (argc != 3) {
    printf("Usage: %s <GraphFile> <PermFile\n", argv[0]);
    return EXIT_FAILURE;
  }
    
  memset(&params, 0, sizeof(params));
  if (!CopyInputFilename(argv[1], &params.filename))
    return EXIT_FAILURE;
  status = ReadGraph(&params, &graph);
  if (status != METIS_OK)
    goto cleanup;
  if (graph->nvtxs <= 0) {
    printf("Empty graph. Nothing to do.\n");
    status = METIS_ERROR_INPUT;
    goto cleanup;
  }
  if (graph->ncon != 1) {
    printf("Ordering can only be applied to graphs with one constraint.\n");
    status = METIS_ERROR_INPUT;
    goto cleanup;
  }


  /* Read the external iperm vector */
  if (!AllocateOrderVectors(graph->nvtxs, &perm, &iperm)) {
    status = METIS_ERROR_MEMORY;
    goto cleanup;
  }
  status = ReadPOVector(graph, argv[2], iperm);
  if (status != METIS_OK)
    goto cleanup;

  for (i=0; i<graph->nvtxs; i++)
    perm[iperm[i]] = i;

  printf("**********************************************************************\n");
  printf("%s", METISTITLE);
  printf("Graph Information ---------------------------------------------------\n");
  printf("  Name: %s, #Vertices: %"PRIDX", #Edges: %"PRIDX"\n\n", argv[1], 
      graph->nvtxs, graph->nedges/2);
  printf("Fillin... -----------------------------------------------------------\n");

  status = ComputeFillIn(graph, perm, iperm, &maxlnz, &opc);
  if (status != METIS_OK)
    goto cleanup;
  
  printf("  Nonzeros: %6.3le \tOperation Count: %6.3le\n", (double)maxlnz, (double)opc);


  printf("**********************************************************************\n");
cleanup:
  FreeGraph(&graph);
  gk_free((void **)&perm, &iperm, &params.filename, LTERM);
  return status == METIS_OK ? EXIT_SUCCESS : EXIT_FAILURE;
}
