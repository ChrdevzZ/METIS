#include "metislib.h"


static int write_sentinel(const char *filename)
{
  FILE *stream;

  stream = fopen(filename, "wb");
  if (stream == NULL)
    return 0;
  if (fwrite("keep", 1, 4, stream) != 4) {
    fclose(stream);
    return 0;
  }
  return fclose(stream) == 0;
}


static int sentinel_is_unchanged(const char *filename)
{
  char buffer[5];
  FILE *stream;
  size_t count;

  stream = fopen(filename, "rb");
  if (stream == NULL)
    return 0;
  count = fread(buffer, 1, sizeof(buffer), stream);
  if (fclose(stream) != 0)
    return 0;
  return count == 4 && memcmp(buffer, "keep", 4) == 0;
}


int main(void)
{
  char created[64], sentinel[64];
  ctrl_t *ctrl;
  graph_t graph;
  idx_t xadj[2] = {0, 16777216};
  int length;

  memset(&graph, 0, sizeof(graph));
  if (!gk_malloc_init())
    return 1;
  ctrl = NULL;
  if (SetupCtrl(METIS_OP_KMETIS, NULL, 1, 2, NULL, NULL, &ctrl) !=
      METIS_OK) {
    gk_malloc_cleanup(0);
    return 1;
  }
  ctrl->ondisk = 1;
  graph.nvtxs = 1;
  graph.ncon = 1;
  graph.nedges = xadj[1];
  graph.xadj = xadj;

  length = snprintf(sentinel, sizeof(sentinel), "metis%d.1", (int)ctrl->pid);
  if (length < 0 || (size_t)length >= sizeof(sentinel) ||
      !write_sentinel(sentinel))
    return 1;

  graph_WriteToDisk(ctrl, &graph);
  if (!sentinel_is_unchanged(sentinel) || !graph.ondisk || graph.gID <= 1) {
    gk_rmpath(sentinel);
    return 2;
  }

  length = snprintf(created, sizeof(created), "metis%d.%d",
      (int)ctrl->pid, graph.gID);
  if (length < 0 || (size_t)length >= sizeof(created)) {
    gk_rmpath(sentinel);
    return 3;
  }
  if (!gk_fexists(created)) {
    gk_rmpath(sentinel);
    return 4;
  }

  graph_ReadFromDisk(ctrl, &graph);
  if (graph.ondisk || graph.gID != 0 || gk_fexists(created) ||
      !sentinel_is_unchanged(sentinel)) {
    gk_rmpath(sentinel);
    return 5;
  }

  FreeCtrl(&ctrl);
  if (gk_rmpath(sentinel) != 0)
    return 6;
  gk_malloc_cleanup(0);
  return 0;
}
