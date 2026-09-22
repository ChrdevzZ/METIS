#include "metisbin.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif


void metis_io_test_fail_paired_commit(int commit);
void metis_io_test_fail_rollback(int fail);
void metis_io_test_fail_read(int read_count, int error);
void metis_io_test_fail_suffix_allocation(int error);
void metis_io_test_fail_open_allocation(int error);
void metis_io_test_fail_output_close(int flush_count, int close_count);
int metis_io_test_open_output(const char *filename, FILE **stream,
    char **tempname);
int metis_io_test_finish_output(FILE *stream, char **tempname,
    const char *filename);
int metis_io_test_finish_output_pair(FILE *first_stream, char **first_temp,
    const char *first_name, FILE *second_stream, char **second_temp,
    const char *second_name);


static int write_contents(const char *filename, const char *contents)
{
  FILE *stream;
  size_t length;

  stream = fopen(filename, "wb");
  if (stream == NULL)
    return 0;
  length = strlen(contents);
  if (fwrite(contents, 1, length, stream) != length) {
    fclose(stream);
    return 0;
  }
  return fclose(stream) == 0;
}


static int contents_equal(const char *filename, const char *expected)
{
  char buffer[64];
  FILE *stream;
  size_t count, length;

  stream = fopen(filename, "rb");
  if (stream == NULL)
    return 0;
  count = fread(buffer, 1, sizeof(buffer), stream);
  if (fclose(stream) != 0)
    return 0;
  length = strlen(expected);
  return count == length && memcmp(buffer, expected, length) == 0;
}


static char *copy_name(const char *filename)
{
  char *result;
  size_t length=strlen(filename);

  result = (char *)malloc(length+1);
  if (result != NULL)
    memcpy(result, filename, length+1);
  return result;
}


static int create_symbolic_link(const char *linkname, const char *target)
{
#ifdef _WIN32
  DWORD error;

  if (CreateSymbolicLinkA(linkname, target,
      SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE))
    return 1;
  error = GetLastError();
  if (error == ERROR_INVALID_PARAMETER &&
      CreateSymbolicLinkA(linkname, target, 0))
    return 1;
  if (error == ERROR_PRIVILEGE_NOT_HELD || error == ERROR_INVALID_PARAMETER ||
      error == ERROR_NOT_SUPPORTED)
    return 0;
  return -1;
#else
  if (symlink(target, linkname) == 0)
    return 1;
  return errno == EPERM || errno == ENOSYS ? 0 : -1;
#endif
}


static int create_hard_link(const char *linkname, const char *target)
{
#ifdef _WIN32
  return CreateHardLinkA(linkname, target, NULL) ? 1 : 0;
#else
  return link(target, linkname) == 0;
#endif
}


int main(void)
{
  const char *first_name="metis-pair-first.out";
  const char *second_name="metis-pair-second.out";
  const char *first_temp_name="metis-pair-first.tmp";
  const char *second_temp_name="metis-pair-second.tmp";
  const char *first_backup="metis-pair-first.tmp.bak";
  const char *second_backup="metis-pair-second.tmp.bak";
  const char *graph_name="metis-read-error.graph";
  const char *mesh_name="metis-read-error.mesh";
  const char *permutation_name="metis-read-error.iperm";
  const char *tpwgts_name="metis-read-error.tpwgts";
  const char *transaction_name="metis-output-transaction.out";
  char *first_temp=NULL, *second_temp=NULL;
  char *transaction_temp=NULL;
  FILE *first_stream=NULL, *second_stream=NULL, *transaction_stream=NULL;
  graph_t graph, *read_graph=NULL;
  mesh_t *read_mesh=NULL;
  params_t params;
  idx_t xadj[] = {0, 1, 2};
  idx_t adjncy[] = {1, 0};
  idx_t adjwgt[] = {2, 0};
  idx_t vwgt[] = {2, -1};
  idx_t vsize[] = {2, -1};
  idx_t part[] = {0, 1};
  idx_t iperm[] = {1, 0};
  idx_t read_vector[] = {-1, -1};
  real_t original_tpwgts[] = {0.5, 0.5};
  int link_status, result=1, status;
#ifdef _WIN32
  HANDLE output_handle=INVALID_HANDLE_VALUE;
#endif

  remove(first_name);
  remove(second_name);
  remove(first_temp_name);
  remove(second_temp_name);
  remove(first_backup);
  remove(second_backup);
  remove(graph_name);
  remove(mesh_name);
  remove(permutation_name);
  remove(tpwgts_name);
  remove(transaction_name);

  if (!write_contents(first_name, "old-first") ||
      !write_contents(second_name, "old-second"))
    goto cleanup;
  first_stream = fopen(first_temp_name, "wb");
  second_stream = fopen(second_temp_name, "wb");
  first_temp = copy_name(first_temp_name);
  second_temp = copy_name(second_temp_name);
  if (first_stream == NULL || second_stream == NULL ||
      first_temp == NULL || second_temp == NULL)
    goto cleanup;
  if (fwrite("new-first", 1, 9, first_stream) != 9 ||
      fwrite("new-second", 1, 10, second_stream) != 10)
    goto cleanup;

  metis_io_test_fail_paired_commit(1);
  errno = 0;
  status = metis_io_test_finish_output_pair(first_stream, &first_temp,
      first_name, second_stream, &second_temp, second_name);
  first_stream = NULL;
  second_stream = NULL;
  if (status || errno != EIO)
    goto cleanup;
  if (first_temp != NULL || second_temp != NULL ||
      !contents_equal(first_name, "old-first") ||
      !contents_equal(second_name, "old-second") ||
      gk_fexists((char *)first_temp_name) ||
      gk_fexists((char *)second_temp_name) ||
      gk_fexists((char *)first_backup) ||
      gk_fexists((char *)second_backup))
    goto cleanup;

  first_stream = fopen(first_temp_name, "wb");
  second_stream = fopen(second_temp_name, "wb");
  first_temp = copy_name(first_temp_name);
  second_temp = copy_name(second_temp_name);
  if (first_stream == NULL || second_stream == NULL ||
      first_temp == NULL || second_temp == NULL)
    goto cleanup;
  if (fwrite("new-first", 1, 9, first_stream) != 9 ||
      fwrite("new-second", 1, 10, second_stream) != 10)
    goto cleanup;

  metis_io_test_fail_paired_commit(2);
  errno = 0;
  status = metis_io_test_finish_output_pair(first_stream, &first_temp,
      first_name, second_stream, &second_temp, second_name);
  first_stream = NULL;
  second_stream = NULL;
  if (status || errno != EIO)
    goto cleanup;
  if (first_temp != NULL || second_temp != NULL ||
      !contents_equal(first_name, "old-first") ||
      !contents_equal(second_name, "old-second") ||
      gk_fexists((char *)first_temp_name) ||
      gk_fexists((char *)second_temp_name) ||
      gk_fexists((char *)first_backup) ||
      gk_fexists((char *)second_backup))
    goto cleanup;

  first_stream = fopen(first_temp_name, "wb");
  second_stream = fopen(second_temp_name, "wb");
  first_temp = copy_name(first_temp_name);
  second_temp = copy_name(second_temp_name);
  if (first_stream == NULL || second_stream == NULL ||
      first_temp == NULL || second_temp == NULL)
    goto cleanup;
  if (fwrite("new-first", 1, 9, first_stream) != 9 ||
      fwrite("new-second", 1, 10, second_stream) != 10)
    goto cleanup;

  metis_io_test_fail_paired_commit(2);
  metis_io_test_fail_rollback(1);
  errno = 0;
  status = metis_io_test_finish_output_pair(first_stream, &first_temp,
      first_name, second_stream, &second_temp, second_name);
  first_stream = NULL;
  second_stream = NULL;
  if (status || errno != EIO)
    goto cleanup;
  metis_io_test_fail_rollback(0);
  if (first_temp != NULL || second_temp != NULL ||
      !contents_equal(first_name, "new-first") ||
      !contents_equal(second_name, "old-second") ||
      !contents_equal(first_backup, "old-first") ||
      gk_fexists((char *)first_temp_name) ||
      gk_fexists((char *)second_temp_name) ||
      gk_fexists((char *)second_backup))
    goto cleanup;

  remove(first_name);
  remove(second_name);
  remove(first_backup);

  if (!write_contents(first_name, "old-first") ||
      !write_contents(second_name, "old-second"))
    goto cleanup;
  first_stream = fopen(first_temp_name, "wb");
  second_stream = fopen(second_temp_name, "wb");
  first_temp = copy_name(first_temp_name);
  second_temp = copy_name(second_temp_name);
  if (first_stream == NULL || second_stream == NULL ||
      first_temp == NULL || second_temp == NULL)
    goto cleanup;
  if (fwrite("new-first", 1, 9, first_stream) != 9 ||
      fwrite("new-second", 1, 10, second_stream) != 10)
    goto cleanup;

  metis_io_test_fail_paired_commit(0);
  status = metis_io_test_finish_output_pair(first_stream, &first_temp,
      first_name, second_stream, &second_temp, second_name);
  first_stream = NULL;
  second_stream = NULL;
  if (!status)
    goto cleanup;
  if (first_temp != NULL || second_temp != NULL ||
      !contents_equal(first_name, "new-first") ||
      !contents_equal(second_name, "new-second") ||
      gk_fexists((char *)first_backup) ||
      gk_fexists((char *)second_backup))
    goto cleanup;

  remove(first_name);
  remove(second_name);

  /* Atomic replacement must reject names whose link identity it cannot
     preserve, instead of silently replacing only one directory entry. */
  if (!write_contents("metis-output-link-target", "link-target"))
    goto cleanup;
  link_status = create_symbolic_link("metis-output-symbolic",
      "metis-output-link-target");
  if (link_status < 0)
    goto cleanup;
  if (link_status == 1) {
    errno = 0;
    status = metis_io_test_open_output("metis-output-symbolic",
        &first_stream, &first_temp);
    if (status == METIS_OK || first_stream != NULL || first_temp != NULL ||
        errno == 0 ||
        !contents_equal("metis-output-link-target", "link-target"))
      goto cleanup;
  }

  if (!create_hard_link("metis-output-hard", "metis-output-link-target"))
    goto cleanup;
  errno = 0;
  status = metis_io_test_open_output("metis-output-hard", &first_stream,
      &first_temp);
  if (status == METIS_OK || first_stream != NULL || first_temp != NULL ||
      errno == 0 ||
      !contents_equal("metis-output-link-target", "link-target"))
    goto cleanup;

  if (!write_contents(transaction_name, "old-output"))
    goto cleanup;
  if (metis_io_test_open_output(transaction_name, &transaction_stream,
      &transaction_temp) != METIS_OK ||
      fwrite("new-output", 1, 10, transaction_stream) != 10)
    goto cleanup;
  metis_io_test_fail_output_close(1, 0);
  errno = 0;
  status = metis_io_test_finish_output(transaction_stream, &transaction_temp,
      transaction_name);
  transaction_stream = NULL;
  if (status || errno != EIO)
    goto cleanup;
  if (transaction_temp != NULL ||
      !contents_equal(transaction_name, "old-output"))
    goto cleanup;

  if (metis_io_test_open_output(transaction_name, &transaction_stream,
      &transaction_temp) != METIS_OK ||
      fwrite("new-output", 1, 10, transaction_stream) != 10)
    goto cleanup;
  metis_io_test_fail_output_close(0, 1);
  errno = 0;
  status = metis_io_test_finish_output(transaction_stream, &transaction_temp,
      transaction_name);
  transaction_stream = NULL;
  if (status || errno != EIO)
    goto cleanup;
  metis_io_test_fail_output_close(0, 0);
  if (transaction_temp != NULL ||
      !contents_equal(transaction_name, "old-output"))
    goto cleanup;

#ifdef _WIN32
  if (metis_io_test_open_output(transaction_name, &transaction_stream,
      &transaction_temp) != METIS_OK ||
      fwrite("new-output", 1, 10, transaction_stream) != 10)
    goto cleanup;
  output_handle = CreateFileA(transaction_name, GENERIC_READ, 0, NULL,
      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (output_handle == INVALID_HANDLE_VALUE)
    goto cleanup;
  errno = 0;
  status = metis_io_test_finish_output(transaction_stream, &transaction_temp,
      transaction_name);
  transaction_stream = NULL;
  if (status || errno != EACCES)
    goto cleanup;
  CloseHandle(output_handle);
  output_handle = INVALID_HANDLE_VALUE;
  if (transaction_temp != NULL ||
      !contents_equal(transaction_name, "old-output"))
    goto cleanup;
#endif

  if (!write_contents(graph_name, "2 1\n2\n1\n") ||
      !write_contents(mesh_name, "1\n1 2\n") ||
      !write_contents(permutation_name, "0\n1\n") ||
      !write_contents(tpwgts_name, "0=0.5\n1=0.5\n"))
    goto cleanup;
  memset(&params, 0, sizeof(params));
  gk_set_exit_on_error(1);
  params.filename = (char *)graph_name;
  metis_io_test_fail_read(2, EACCES);
  errno = 0;
  if (ReadGraph(&params, &read_graph) != METIS_ERROR ||
      errno != EACCES || read_graph != NULL)
    goto cleanup;
  metis_io_test_fail_read(2, EOVERFLOW);
  errno = 0;
  if (ReadGraph(&params, &read_graph) != METIS_ERROR_MEMORY ||
      errno != EOVERFLOW || read_graph != NULL)
    goto cleanup;
  params.filename = (char *)mesh_name;
  metis_io_test_fail_read(3, EACCES);
  errno = 0;
  if (ReadMesh(&params, &read_mesh) != METIS_ERROR ||
      errno != EACCES || read_mesh != NULL)
    goto cleanup;
  metis_io_test_fail_read(3, EOVERFLOW);
  errno = 0;
  if (ReadMesh(&params, &read_mesh) != METIS_ERROR_MEMORY ||
      errno != EOVERFLOW || read_mesh != NULL)
    goto cleanup;
  params.nparts = 2;
  params.tpwgtsfile = (char *)tpwgts_name;
  params.tpwgts = original_tpwgts;
  metis_io_test_fail_read(2, EACCES);
  errno = 0;
  if (ReadTPwgts(&params, 1) != METIS_ERROR || errno != EACCES ||
      params.tpwgts != original_tpwgts)
    goto cleanup;
  metis_io_test_fail_read(2, EOVERFLOW);
  errno = 0;
  if (ReadTPwgts(&params, 1) != METIS_ERROR_MEMORY ||
      errno != EOVERFLOW || params.tpwgts != original_tpwgts)
    goto cleanup;
  metis_io_test_fail_read(0, 0);

  memset(&graph, 0, sizeof(graph));
  graph.nvtxs = 2;
  graph.ncon = 1;
  graph.nedges = 2;
  graph.xadj = xadj;
  graph.adjncy = adjncy;
  graph.adjwgt = adjwgt;
  metis_io_test_fail_read(2, EACCES);
  errno = 0;
  if (ReadPOVector(&graph, (char *)permutation_name, read_vector) !=
      METIS_ERROR || errno != EACCES ||
      read_vector[0] != -1 || read_vector[1] != -1)
    goto cleanup;
  metis_io_test_fail_read(2, EOVERFLOW);
  errno = 0;
  if (ReadPOVector(&graph, (char *)permutation_name, read_vector) !=
      METIS_ERROR_MEMORY || errno != EOVERFLOW ||
      read_vector[0] != -1 || read_vector[1] != -1)
    goto cleanup;
  metis_io_test_fail_read(0, 0);

  gk_set_exit_on_error(0);
  errno = 0;
  if (WriteGraph(&graph, "metis-missing-output-dir/invalid.graph") !=
      METIS_ERROR_INPUT || errno != EINVAL) {
    gk_set_exit_on_error(1);
    goto cleanup;
  }
  graph.adjwgt = NULL;
  graph.vwgt = vwgt;
  errno = 0;
  if (WriteGraph(&graph, "metis-missing-output-dir/invalid.graph") !=
      METIS_ERROR_INPUT || errno != EINVAL) {
    gk_set_exit_on_error(1);
    goto cleanup;
  }
  graph.vwgt = NULL;
  graph.vsize = vsize;
  errno = 0;
  if (WriteGraph(&graph, "metis-missing-output-dir/invalid.graph") !=
      METIS_ERROR_INPUT || errno != EINVAL) {
    gk_set_exit_on_error(1);
    goto cleanup;
  }
  graph.vsize = NULL;
  graph.nedges = 1;
  errno = 0;
  if (WriteGraph(&graph, "metis-missing-output-dir/invalid.graph") !=
      METIS_ERROR_INPUT || errno != EINVAL) {
    gk_set_exit_on_error(1);
    goto cleanup;
  }
  gk_set_exit_on_error(1);
  graph.nvtxs = 0;
  graph.nedges = 0;
  graph.adjncy = NULL;
  errno = 0;
  if (WriteGraph(&graph, "metis-zero.graph") != METIS_ERROR_INPUT ||
      errno != EINVAL) {
    gk_set_exit_on_error(1);
    goto cleanup;
  }

  graph.nvtxs = 2;
  graph.nedges = 2;
  graph.adjncy = adjncy;
  metis_io_test_fail_suffix_allocation(ENOMEM);
  errno = 0;
  if (WritePartition("metis-memory", part, 2, 2) != METIS_ERROR_MEMORY ||
      errno != ENOMEM)
    goto cleanup;
  metis_io_test_fail_open_allocation(EOVERFLOW);
  errno = 0;
  if (WriteGraph(&graph, "metis-memory.graph") != METIS_ERROR_MEMORY ||
      errno != EOVERFLOW)
    goto cleanup;

  gk_set_exit_on_error(0);
  metis_io_test_fail_suffix_allocation(EOVERFLOW);
  errno = 0;
  if (WritePermutation("metis-memory", iperm, 2) != METIS_ERROR_MEMORY ||
      errno != EOVERFLOW)
    goto cleanup;
  metis_io_test_fail_open_allocation(ENOMEM);
  errno = 0;
  if (WriteMeshPartition("metis-memory", 2, 2, part, 2, part) !=
      METIS_ERROR_MEMORY || errno != ENOMEM)
    goto cleanup;

  gk_set_exit_on_error(1);
  errno = 0;
  if (WritePartition("metis-missing-output-dir/result", part, 2, 2) !=
      METIS_ERROR || errno == 0)
    goto cleanup;
  errno = 0;
  if (WriteMeshPartition("metis-missing-output-dir/result", 2, 2, part,
      2, part) != METIS_ERROR || errno == 0)
    goto cleanup;
  errno = 0;
  if (WritePermutation("metis-missing-output-dir/result", iperm, 2) !=
      METIS_ERROR || errno == 0)
    goto cleanup;
  errno = 0;
  if (WriteGraph(&graph, "metis-missing-output-dir/result.graph") !=
      METIS_ERROR || errno == 0)
    goto cleanup;

  result = 0;

cleanup:
#ifdef _WIN32
  if (output_handle != INVALID_HANDLE_VALUE)
    CloseHandle(output_handle);
#endif
  if (first_stream != NULL)
    fclose(first_stream);
  if (second_stream != NULL)
    fclose(second_stream);
  if (transaction_stream != NULL)
    fclose(transaction_stream);
  free(first_temp);
  free(second_temp);
  if (transaction_temp != NULL)
    remove(transaction_temp);
  free(transaction_temp);
  FreeGraph(&read_graph);
  FreeMesh(&read_mesh);
  remove(first_name);
  remove(second_name);
  remove(first_temp_name);
  remove(second_temp_name);
  remove(first_backup);
  remove(second_backup);
  remove("metis-output-symbolic");
  remove("metis-output-hard");
  remove("metis-output-link-target");
  remove(graph_name);
  remove(mesh_name);
  remove(permutation_name);
  remove(tpwgts_name);
  remove(transaction_name);
  remove("metis-zero.graph");
  remove("metis-memory.part.2");
  remove("metis-memory.epart.2");
  remove("metis-memory.npart.2");
  remove("metis-memory.iperm");
  remove("metis-memory.graph");
  gk_set_exit_on_error(1);
  return result;
}
