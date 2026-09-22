/*
 * proto.h 
 *
 * This file contains function prototypes
 *
 * Started 11/1/99
 * George
 *
 * $Id: proto.h 10513 2011-07-07 22:06:03Z karypis $
 *
 */

#ifndef _PROTOBIN_H_
#define _PROTOBIN_H_


/* io.c */ 
int ReadGraph(params_t *, graph_t **);
int ReadMesh(params_t *, mesh_t **);
int ReadTPwgts(params_t *params, idx_t ncon);
int ReadPOVector(graph_t *graph, char *filename, idx_t *vector);
int WritePartition(char *, idx_t *, idx_t, idx_t);
int WriteMeshPartition(char *, idx_t, idx_t, idx_t *, idx_t, idx_t *);
int WritePermutation(char *, idx_t *, idx_t);
int WriteGraph(graph_t *graph, char *filename);


/* smbfactor.c */
int ComputeFillIn(graph_t *graph, idx_t *perm, idx_t *iperm,
         uint64_t *r_maxlnz, uint64_t *r_opc);
idx_t smbfct(idx_t neqns, idx_t *xadj, idx_t *adjncy, idx_t *perm, 
          idx_t *invp, idx_t *xlnz, idx_t *maxlnz, idx_t *xnzsub, 
          idx_t *nzsub, idx_t *maxsub);


/* cmdline.c */
params_t *parse_cmdline(int argc, char *argv[]);

/* gpmetis.c */
void GPPrintInfo(params_t *params, graph_t *graph);
int GPReportResults(params_t *params, graph_t *graph, idx_t *part, idx_t edgecut);

/* ndmetis.c */
void NDPrintInfo(params_t *params, graph_t *graph);
int NDReportResults(params_t *params, graph_t *graph, idx_t *perm, idx_t *iperm);

/* mpmetis.c */
void MPPrintInfo(params_t *params, mesh_t *mesh);
void MPReportResults(params_t *params, mesh_t *mesh, idx_t *epart, idx_t *npart, 
         idx_t edgecut);

/* m2gmetis.c */
void M2GPrintInfo(params_t *params, mesh_t *mesh);
void M2GReportResults(params_t *params, mesh_t *mesh, graph_t *graph);

/* stat.c */
int ComputePartitionInfo(params_t *params, graph_t *graph, idx_t *where);


#endif 
