/*
 * Copyright 1997, Regents of the University of Minnesota
 *
 * io.c
 *
 * This file contains routines related to I/O
 *
 * Started 8/28/94
 * George
 *
 * $Id: io.c 17513 2014-08-05 16:20:50Z dominique $
 *
 */

#include "metisbin.h"

#include <fcntl.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#ifdef METIS_IO_TRANSACTION_TEST
static int metis_io_test_read_count;
static int metis_io_test_failed_read;
static int metis_io_test_read_errno;
static int metis_io_test_suffix_errno;
static int metis_io_test_open_errno;
static int metis_io_test_close_count;
static int metis_io_test_failed_flush;
static int metis_io_test_failed_close;
static int metis_io_test_commit_count;
static int metis_io_test_failed_commit;
static int metis_io_test_failed_rollback;
#endif


/*************************************************************************/
/*! Parses one complete idx_t decimal token and advances the input cursor.

    Whitespace before the token is skipped. One denotes a valid token, zero
    denotes end-of-record, and -1 denotes malformed or overflowing input.
*/
/*************************************************************************/
static int ReadIntegerToken(char **cursor, idx_t *value)
{
  char *start, *endptr;
  idx_t parsed;

  start = *cursor;
  while (isspace((unsigned char)*start))
    start++;
  if (*start == '\0') {
    *cursor = start;
    return 0;
  }

  if (!ParseIndex(start, &endptr, &parsed) ||
      (*endptr != '\0' && !isspace((unsigned char)*endptr))) {
    if (errno == 0)
      errno = EINVAL;
    return -1;
  }

  *value = parsed;
  *cursor = endptr;
  return 1;
}


/*************************************************************************/
/*! Advances an input cursor past consecutive whitespace characters. */
/*************************************************************************/
static void SkipWhitespace(char **cursor)
{
  while (isspace((unsigned char)**cursor))
    (*cursor)++;
}


/*************************************************************************/
/*! Reads one complete external-input record with POSIX buffer ownership.

    The buffer returned by gk_getline() remains owned by the caller and must be
    released with free(). Clean end-of-file is reported by -1, a stream error
    by -2, and an embedded NUL byte by -3.
*/
/*************************************************************************/
static ssize_t ReadLine(char **line, size_t *capacity, FILE *stream)
{
  ssize_t nread;

#ifdef METIS_IO_TRANSACTION_TEST
  metis_io_test_read_count++;
  if (metis_io_test_read_count == metis_io_test_failed_read) {
    errno = metis_io_test_read_errno;
    return -2;
  }
#endif
  nread = gk_getline(line, capacity, stream);
  if (nread == -1) {
    if (!ferror(stream) && feof(stream))
      return -1;
    if (errno == 0)
      errno = EIO;
    return -2;
  }
  if (memchr(*line, '\0', (size_t)nread) != NULL) {
    errno = EINVAL;
    return -3;
  }

  return nread;
}


/*************************************************************************/
/*! Returns the output error class associated with the current errno. */
/*************************************************************************/
static int OutputErrorStatus(void)
{
  return errno == ENOMEM || errno == EOVERFLOW ?
      METIS_ERROR_MEMORY : METIS_ERROR;
}


/*************************************************************************/
/*! Reports an error without changing its status or errno. */
/*************************************************************************/
static int ReportError(int status, int saved_errno, const char *f_str, ...)
{
  va_list argp;

  va_start(argp, f_str);
  vfprintf(stderr, f_str, argp);
  va_end(argp);
  fflush(stderr);
  errno = saved_errno;

  return status;
}


/*************************************************************************/
/*! Allocates an output filename by appending a checked suffix.

    The returned buffer is allocated by GKlib and must be released with
    gk_free(). Allocation signals are converted to an ordinary memory status.
*/
/*************************************************************************/
static int AppendOutputSuffix(const char *filename, const char *suffix,
    char **r_result)
{
  volatile int sigrval=0;
  char * volatile result=NULL;
  size_t filename_length, suffix_length;

  *r_result = NULL;
  filename_length = strlen(filename);
  suffix_length = strlen(suffix);
  if (suffix_length == SIZE_MAX ||
      filename_length > SIZE_MAX-suffix_length-1) {
    errno = EOVERFLOW;
    return METIS_ERROR_MEMORY;
  }

  if (!gk_sigtrap()) {
    errno = ENOMEM;
    return METIS_ERROR_MEMORY;
  }
  METIS_SIGCATCH(sigrval);
  if (sigrval == 0) {
#ifdef METIS_IO_TRANSACTION_TEST
    if (metis_io_test_suffix_errno != 0) {
      errno = metis_io_test_suffix_errno;
      metis_io_test_suffix_errno = 0;
      gk_errexit(SIGMEM, "Injected output filename allocation failure.");
    }
    else
#endif
      result = gk_malloc(filename_length+suffix_length+1, "output filename");
  }
  gk_siguntrap();

  if (sigrval != 0 || result == NULL) {
    if (errno == 0)
      errno = ENOMEM;
    return OutputErrorStatus();
  }
  memcpy((char *)result, filename, filename_length);
  memcpy((char *)result+filename_length, suffix, suffix_length+1);

  *r_result = (char *)result;
  return METIS_OK;
}


static int InspectOutputTarget(const char *filename, int *r_exists);
static int FinishOutputFile(FILE *stream, char **r_tempname,
    const char *filename);


/*************************************************************************/
/*! Opens an exclusive temporary output beside its final destination.

    The returned stream never truncates filename. The malloc-allocated
    temporary path is returned for FinishOutputFile() to commit or remove.
*/
/*************************************************************************/
static int OpenOutputFile(const char *filename, FILE **r_stream,
    char **r_tempname)
{
  FILE *stream;
  char *tempname;
  size_t filename_length;
  unsigned int attempt;
  int destination_exists=0, fd=-1, saved_errno=0;
#ifndef _WIN32
  struct stat destination_status, temporary_status;
  long process_id=(long)getpid();
#else
  LARGE_INTEGER counter;
  unsigned long process_id=(unsigned long)GetCurrentProcessId();
  unsigned long thread_id=(unsigned long)GetCurrentThreadId();
#endif

  *r_stream = NULL;
  *r_tempname = NULL;
  if (filename == NULL || filename[0] == '\0') {
    errno = EINVAL;
    return METIS_ERROR;
  }

  filename_length = strlen(filename);
  if (filename_length > SIZE_MAX-96) {
    errno = EOVERFLOW;
    return METIS_ERROR_MEMORY;
  }
#ifdef METIS_IO_TRANSACTION_TEST
  if (metis_io_test_open_errno != 0) {
    errno = metis_io_test_open_errno;
    metis_io_test_open_errno = 0;
    return OutputErrorStatus();
  }
#endif
  tempname = malloc(filename_length+96);
  if (tempname == NULL) {
    errno = ENOMEM;
    return METIS_ERROR_MEMORY;
  }
  memcpy(tempname, filename, filename_length);

#ifndef _WIN32
  if (lstat(filename, &destination_status) == 0) {
    if (!S_ISREG(destination_status.st_mode) ||
        destination_status.st_nlink != 1) {
      free(tempname);
      errno = EINVAL;
      return METIS_ERROR;
    }
    destination_exists = 1;
  }
  else if (errno != ENOENT) {
    saved_errno = errno;
    free(tempname);
    errno = saved_errno;
    return OutputErrorStatus();
  }
#else
  if (!InspectOutputTarget(filename, &destination_exists)) {
    saved_errno = errno;
    free(tempname);
    errno = saved_errno;
    return OutputErrorStatus();
  }
#endif

#ifndef _WIN32
  for (attempt=0; attempt<65536; attempt++) {
    if (snprintf(tempname+filename_length, 96, ".tmp.%ld.%u",
        process_id, attempt) < 0) {
      saved_errno = EINVAL;
      break;
    }
    fd = open(tempname, O_WRONLY | O_CREAT | O_EXCL, 0666);
    if (fd >= 0)
      break;
    if (errno != EEXIST) {
      saved_errno = errno;
      break;
    }
  }
#else
  QueryPerformanceCounter(&counter);
  for (attempt=0; attempt<65536; attempt++) {
    if (snprintf(tempname+filename_length, 96, ".tmp.%lu.%lu.%08lx.%u",
        process_id, thread_id, (unsigned long)counter.LowPart, attempt) < 0) {
      saved_errno = EINVAL;
      break;
    }
#ifdef _MSC_VER
    fd = _open(tempname, _O_WRONLY | _O_CREAT | _O_EXCL | _O_TEXT,
               _S_IREAD | _S_IWRITE);
#else
    fd = open(tempname, O_WRONLY | O_CREAT | O_EXCL, 0600);
#endif
    if (fd >= 0)
      break;
    if (errno != EEXIST) {
      saved_errno = errno;
      break;
    }
  }
#endif
  if (fd < 0) {
    if (saved_errno == 0)
      saved_errno = errno != 0 ? errno : EEXIST;
    free(tempname);
    errno = saved_errno;
    return OutputErrorStatus();
  }

#ifndef _WIN32
  /* open() applies the process umask for a new destination. When replacing
     a regular file, retain its owner and permission bits at rename. */
  if (destination_exists) {
    if (fstat(fd, &temporary_status) != 0 ||
        ((temporary_status.st_uid != destination_status.st_uid ||
          temporary_status.st_gid != destination_status.st_gid) &&
         fchown(fd, destination_status.st_uid, destination_status.st_gid) != 0) ||
        fchmod(fd, destination_status.st_mode & 07777) != 0) {
      saved_errno = errno != 0 ? errno : EIO;
      close(fd);
      remove(tempname);
      free(tempname);
      errno = saved_errno;
      return OutputErrorStatus();
    }
  }
#endif

#ifdef _MSC_VER
  stream = _fdopen(fd, "w");
#else
  stream = fdopen(fd, "w");
#endif
  if (stream == NULL) {
    saved_errno = errno != 0 ? errno : EIO;
#ifdef _MSC_VER
    _close(fd);
#else
    close(fd);
#endif
    remove(tempname);
    free(tempname);
    errno = saved_errno;
    return OutputErrorStatus();
  }

  *r_stream = stream;
  *r_tempname = tempname;
  return METIS_OK;
}


#ifdef _WIN32
/*************************************************************************/
/*! Maps the Win32 file errors used by output transactions to errno. */
/*************************************************************************/
static void SetOutputErrnoFromWindows(DWORD error)
{
  switch (error) {
    case ERROR_ACCESS_DENIED:
    case ERROR_SHARING_VIOLATION:
    case ERROR_LOCK_VIOLATION:
      errno = EACCES;
      break;
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
      errno = ENOENT;
      break;
    case ERROR_ALREADY_EXISTS:
    case ERROR_FILE_EXISTS:
      errno = EEXIST;
      break;
    case ERROR_DISK_FULL:
    case ERROR_HANDLE_DISK_FULL:
      errno = ENOSPC;
      break;
    case ERROR_NOT_ENOUGH_MEMORY:
    case ERROR_OUTOFMEMORY:
      errno = ENOMEM;
      break;
    default:
      errno = EIO;
      break;
  }
}
#endif


/*************************************************************************/
/*! Closes a complete temporary output without committing its pathname. */
/*************************************************************************/
static int CloseOutputFile(FILE *stream)
{
  int failed=0, saved_errno=0;
#ifdef METIS_IO_TRANSACTION_TEST
  int close_count, fail_flush, fail_close;
#endif

  if (stream == NULL) {
    errno = EINVAL;
    return 0;
  }
#ifdef METIS_IO_TRANSACTION_TEST
  close_count = ++metis_io_test_close_count;
  fail_flush = close_count == metis_io_test_failed_flush;
  fail_close = close_count == metis_io_test_failed_close;
#endif
  if (ferror(stream)) {
    failed = 1;
    saved_errno = errno != 0 ? errno : EIO;
  }
#ifdef METIS_IO_TRANSACTION_TEST
  if (fail_flush) {
    if (!failed)
      saved_errno = EIO;
    failed = 1;
  }
  else
#endif
  if (fflush(stream) != 0) {
    if (!failed)
      saved_errno = errno != 0 ? errno : EIO;
    failed = 1;
  }
#ifdef METIS_IO_TRANSACTION_TEST
  if (fail_close) {
    if (fclose(stream) != 0 && !failed)
      saved_errno = errno != 0 ? errno : EIO;
    if (!failed)
      saved_errno = EIO;
    failed = 1;
  }
  else
#endif
  if (fclose(stream) != 0) {
    if (!failed)
      saved_errno = errno != 0 ? errno : EIO;
    failed = 1;
  }

  if (failed) {
    errno = saved_errno;
    return 0;
  }
  return 1;
}


/*************************************************************************/
/*! Commits one complete temporary path while retaining target metadata. */
/*************************************************************************/
static int CommitOutputPath(const char *tempname, const char *filename)
{
#ifdef _WIN32
  DWORD error;

  if (ReplaceFileA(filename, tempname, NULL, REPLACEFILE_WRITE_THROUGH,
                   NULL, NULL))
    return 1;

  error = GetLastError();
  if ((error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) &&
      MoveFileExA(tempname, filename, MOVEFILE_WRITE_THROUGH))
    return 1;
  if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
    error = GetLastError();
  SetOutputErrnoFromWindows(error);
  return 0;
#else
  return rename(tempname, filename) == 0;
#endif
}


/*************************************************************************/
/*! Moves an output path, replacing its destination when one exists. */
/*************************************************************************/
#ifndef _WIN32
static int MoveOutputPath(const char *source, const char *destination)
{
  return rename(source, destination) == 0;
}
#endif


/*************************************************************************/
/*! Reserves a unique backup pathname derived from an output temporary. */
/*************************************************************************/
static int PrepareOutputBackup(const char *tempname, char **r_backup)
{
  char *backup;
  size_t length;

  *r_backup = NULL;
  length = strlen(tempname);
  if (length > SIZE_MAX-5) {
    errno = EOVERFLOW;
    return 0;
  }
  backup = malloc(length+5);
  if (backup == NULL) {
    errno = ENOMEM;
    return 0;
  }
  memcpy(backup, tempname, length);
  memcpy(backup+length, ".bak", 5);

#ifdef _WIN32
  if (GetFileAttributesA(backup) != INVALID_FILE_ATTRIBUTES) {
    free(backup);
    errno = EEXIST;
    return 0;
  }
  {
    DWORD error=GetLastError();

    if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND) {
      free(backup);
      SetOutputErrnoFromWindows(error);
      return 0;
    }
  }
#else
  {
    struct stat status;

    if (lstat(backup, &status) == 0) {
      free(backup);
      errno = EEXIST;
      return 0;
    }
    if (errno != ENOENT) {
      int saved_errno=errno;

      free(backup);
      errno = saved_errno;
      return 0;
    }
  }
#endif

  *r_backup = backup;
  return 1;
}


/*************************************************************************/
/*! Checks whether an output target is absent or an ordinary file. */
/*************************************************************************/
static int InspectOutputTarget(const char *filename, int *r_exists)
{
#ifdef _WIN32
  BY_HANDLE_FILE_INFORMATION information;
  DWORD attributes, error;
  HANDLE handle;

  attributes = GetFileAttributesA(filename);

  if (attributes == INVALID_FILE_ATTRIBUTES) {
    error = GetLastError();

    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
      *r_exists = 0;
      return 1;
    }
    SetOutputErrnoFromWindows(error);
    return 0;
  }
  if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ||
      (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
    errno = EACCES;
    return 0;
  }
  handle = CreateFileA(filename, FILE_READ_ATTRIBUTES,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
      OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, NULL);
  if (handle == INVALID_HANDLE_VALUE) {
    SetOutputErrnoFromWindows(GetLastError());
    return 0;
  }
  if (!GetFileInformationByHandle(handle, &information)) {
    error = GetLastError();
    CloseHandle(handle);
    SetOutputErrnoFromWindows(error);
    return 0;
  }
  CloseHandle(handle);
  if (information.nNumberOfLinks != 1) {
    errno = EINVAL;
    return 0;
  }
#else
  struct stat status;

  if (lstat(filename, &status) != 0) {
    if (errno == ENOENT) {
      *r_exists = 0;
      return 1;
    }
    return 0;
  }
  if (!S_ISREG(status.st_mode) || status.st_nlink != 1) {
    errno = EINVAL;
    return 0;
  }
#endif

  *r_exists = 1;
  return 1;
}


/*************************************************************************/
/*! Commits one member of an output pair and retains its prior contents. */
/*************************************************************************/
static int CommitPairedOutput(const char *tempname, const char *filename,
    const char *backup, int existed)
{
#ifdef METIS_IO_TRANSACTION_TEST
  metis_io_test_commit_count++;
  if (metis_io_test_commit_count == metis_io_test_failed_commit) {
    errno = EIO;
    return 0;
  }
#endif
#ifdef _WIN32
  if (existed) {
    DWORD error;

    if (!CreateHardLinkA(backup, filename, NULL)) {
      SetOutputErrnoFromWindows(GetLastError());
      return 0;
    }
    if (ReplaceFileA(filename, tempname, NULL, REPLACEFILE_WRITE_THROUGH,
                     NULL, NULL))
      return 1;
    error = GetLastError();
    DeleteFileA(backup);
    SetOutputErrnoFromWindows(error);
    return 0;
  }
  if (MoveFileExA(tempname, filename, MOVEFILE_WRITE_THROUGH))
    return 1;
  SetOutputErrnoFromWindows(GetLastError());
  return 0;
#else
  if (existed && link(filename, backup) != 0)
    return 0;
  if (MoveOutputPath(tempname, filename))
    return 1;

  if (existed) {
    int saved_errno=errno;

    unlink(backup);
    errno = saved_errno;
  }
  return 0;
#endif
}


/*************************************************************************/
/*! Restores one member of a partially committed output pair. */
/*************************************************************************/
static int RestorePairedOutput(const char *filename, const char *backup,
    int existed)
{
#ifdef METIS_IO_TRANSACTION_TEST
  if (metis_io_test_failed_rollback) {
    errno = EBUSY;
    return 0;
  }
#endif
  if (existed)
    return CommitOutputPath(backup, filename);

  if (remove(filename) != 0 && errno != ENOENT)
    return 0;
  return 1;
}


/*************************************************************************/
/*! Closes and commits two related outputs as one failure transaction.

    Both streams are complete before either destination changes. Existing
    contents are retained under private backup names until both replacements
    succeed, allowing an error in the second commit to restore the first.
*/
/*************************************************************************/
static int FinishOutputPair(FILE *first_stream, char **r_first_temp,
    const char *first_name, FILE *second_stream, char **r_second_temp,
    const char *second_name)
{
  char *first_backup=NULL, *second_backup=NULL;
  int first_exists=0, second_exists=0, first_committed=0;
  int failed=0, rollback_errno=0, saved_errno=0;

  if (!CloseOutputFile(first_stream)) {
    failed = 1;
    saved_errno = errno != 0 ? errno : EIO;
  }
  if (!CloseOutputFile(second_stream) && !failed) {
    failed = 1;
    saved_errno = errno != 0 ? errno : EIO;
  }

  if (!failed &&
      (!PrepareOutputBackup(*r_first_temp, &first_backup) ||
       !PrepareOutputBackup(*r_second_temp, &second_backup) ||
       !InspectOutputTarget(first_name, &first_exists) ||
       !InspectOutputTarget(second_name, &second_exists))) {
    failed = 1;
    saved_errno = errno != 0 ? errno : EIO;
  }
  if (!failed && !CommitPairedOutput(*r_first_temp, first_name,
                                     first_backup, first_exists)) {
    failed = 1;
    saved_errno = errno != 0 ? errno : EIO;
  }
  else if (!failed) {
    first_committed = 1;
  }
  if (!failed && !CommitPairedOutput(*r_second_temp, second_name,
                                     second_backup, second_exists)) {
    failed = 1;
    saved_errno = errno != 0 ? errno : EIO;
  }

  if (failed && first_committed &&
      !RestorePairedOutput(first_name, first_backup, first_exists)) {
    rollback_errno = errno != 0 ? errno : EIO;
    if (first_exists) {
      fprintf(stderr, "Error: could not restore output %s; its previous "
          "contents remain in %s: %s.\n", first_name, first_backup,
          strerror(rollback_errno));
    }
    else {
      fprintf(stderr, "Error: could not remove partially committed output "
          "%s: %s.\n", first_name, strerror(rollback_errno));
    }
  }

  if (!failed) {
    /* Both destinations are committed. A backup cleanup failure cannot
       turn this into a failed transaction because rollback is no longer
       possible once another backup has been removed. */
    if (first_exists && remove(first_backup) != 0)
      fprintf(stderr, "Warning: could not remove output backup %s.\n",
          first_backup);
    if (second_exists && remove(second_backup) != 0)
      fprintf(stderr, "Warning: could not remove output backup %s.\n",
          second_backup);
  }
  /* Temporary and obsolete backup cleanup is best effort. */
  remove(*r_first_temp);
  remove(*r_second_temp);
  free(*r_first_temp);
  free(*r_second_temp);
  free(first_backup);
  free(second_backup);
  *r_first_temp = NULL;
  *r_second_temp = NULL;

  if (failed) {
    errno = saved_errno != 0 ? saved_errno : EIO;
    return 0;
  }
  return 1;
}


#ifdef METIS_IO_TRANSACTION_TEST
/*************************************************************************/
/*! Selects the stream read that the input fixture must reject. */
/*************************************************************************/
void metis_io_test_fail_read(int read_count, int error)
{
  metis_io_test_read_count = 0;
  metis_io_test_failed_read = read_count;
  metis_io_test_read_errno = error;
}


/*************************************************************************/
/*! Selects a simulated suffix allocation failure. */
/*************************************************************************/
void metis_io_test_fail_suffix_allocation(int error)
{
  metis_io_test_suffix_errno = error;
}


/*************************************************************************/
/*! Selects a simulated temporary-path allocation failure. */
/*************************************************************************/
void metis_io_test_fail_open_allocation(int error)
{
  metis_io_test_open_errno = error;
}


/*************************************************************************/
/*! Selects the output closes whose flush or close must fail. */
/*************************************************************************/
void metis_io_test_fail_output_close(int flush_count, int close_count)
{
  metis_io_test_close_count = 0;
  metis_io_test_failed_flush = flush_count;
  metis_io_test_failed_close = close_count;
}


/*************************************************************************/
/*! Selects the paired commit that the transaction fixture must reject. */
/*************************************************************************/
void metis_io_test_fail_paired_commit(int commit)
{
  metis_io_test_commit_count = 0;
  metis_io_test_failed_commit = commit;
}


/*************************************************************************/
/*! Selects whether the transaction fixture must reject pair rollback. */
/*************************************************************************/
void metis_io_test_fail_rollback(int fail)
{
  metis_io_test_failed_rollback = fail;
}


/*************************************************************************/
/*! Exposes temporary creation only to the compiled-in transaction test. */
/*************************************************************************/
int metis_io_test_open_output(const char *filename, FILE **stream,
    char **tempname)
{
  return OpenOutputFile(filename, stream, tempname);
}


/*************************************************************************/
/*! Exposes the private output transaction to its compiled-in fixture. */
/*************************************************************************/
int metis_io_test_finish_output(FILE *stream, char **tempname,
    const char *filename)
{
  return FinishOutputFile(stream, tempname, filename);
}


/*************************************************************************/
/*! Exposes the private pair transaction only to its compiled-in fixture. */
/*************************************************************************/
int metis_io_test_finish_output_pair(FILE *first_stream, char **first_temp,
    const char *first_name, FILE *second_stream, char **second_temp,
    const char *second_name)
{
  return FinishOutputPair(first_stream, first_temp, first_name,
      second_stream, second_temp, second_name);
}
#endif


/*************************************************************************/
/*! Closes and atomically commits a complete temporary output.

    Any stream, flush, close, or replacement failure attempts to remove only
    the temporary file. An existing destination remains untouched until
    commit.
*/
/*************************************************************************/
static int FinishOutputFile(FILE *stream, char **r_tempname,
    const char *filename)
{
  int saved_errno=0;
  char *tempname=*r_tempname;

  if (!CloseOutputFile(stream) || !CommitOutputPath(tempname, filename)) {
    saved_errno = errno != 0 ? errno : EIO;
    remove(tempname);
    free(tempname);
    *r_tempname = NULL;
    errno = saved_errno;
    return 0;
  }

  free(tempname);
  *r_tempname = NULL;
  return 1;
}


/*************************************************************************/
/*! Closes and attempts to remove an uncommitted temporary output.

    errno is preserved so cleanup does not replace the operation that caused
    the transaction to be abandoned.
*/
/*************************************************************************/
static void DiscardOutputFile(FILE *stream, char **r_tempname)
{
  int saved_errno=errno;

  if (stream != NULL)
    fclose(stream);
  if (*r_tempname != NULL) {
    remove(*r_tempname);
    free(*r_tempname);
    *r_tempname = NULL;
  }
  errno = saved_errno;
}


/*************************************************************************/
/*! Reads the first non-comment record from an input stream. */
/*************************************************************************/
static ssize_t ReadHeaderLine(char **line, size_t *capacity, FILE *stream)
{
  ssize_t status;

  do {
    status = ReadLine(line, capacity, stream);
  } while (status >= 0 && (*line)[0] == '%');

  return status;
}


/*************************************************************************/
/*! Parses the two required and two optional fields of a graph header. */
/*************************************************************************/
static int ParseGraphHeader(char *line, idx_t *nvtxs, idx_t *nedges,
    idx_t *fmt, idx_t *ncon)
{
  char *cursor=line;
  idx_t extra;
  int status;

  *fmt = 0;
  *ncon = 0;
  if (ReadIntegerToken(&cursor, nvtxs) != 1 ||
      ReadIntegerToken(&cursor, nedges) != 1)
    return 0;

  status = ReadIntegerToken(&cursor, fmt);
  if (status < 0)
    return 0;
  if (status == 0)
    return 1;

  status = ReadIntegerToken(&cursor, ncon);
  if (status < 0)
    return 0;
  if (status == 0)
    return 1;

  return ReadIntegerToken(&cursor, &extra) == 0;
}


/*************************************************************************/
/*! Parses the required element count and optional mesh constraint count. */
/*************************************************************************/
static int ParseMeshHeader(char *line, idx_t *ne, idx_t *ncon)
{
  char *cursor=line;
  idx_t extra;
  int status;

  *ncon = 0;
  if (ReadIntegerToken(&cursor, ne) != 1)
    return 0;
  status = ReadIntegerToken(&cursor, ncon);
  if (status < 0)
    return 0;
  if (status == 0)
    return 1;

  return ReadIntegerToken(&cursor, &extra) == 0;
}


/*************************************************************************/
/*! Allocates graph input arrays while converting allocation signals to an
    ordinary failure that the caller can clean up.

    METIS command-line readers are single-threaded. The short-lived trap is
    removed before this helper returns, so the caller retains ownership of any
    open input stream.
*/
/*************************************************************************/
static int AllocateGraphInput(graph_t *graph, idx_t ncon, idx_t readew)
{
  volatile int sigrval=0;

  if (!gk_sigtrap()) {
    errno = ENOMEM;
    return 0;
  }
  METIS_SIGCATCH(sigrval);
  if (sigrval == 0) {
    graph->xadj = ismalloc(graph->nvtxs+1, 0, "ReadGraph: xadj");
    graph->adjncy = graph->nedges > 0 ?
        imalloc(graph->nedges, "ReadGraph: adjncy") : NULL;
    graph->vwgt = ismalloc(ncon*graph->nvtxs, 1, "ReadGraph: vwgt");
    graph->adjwgt = readew ?
        imalloc(graph->nedges, "ReadGraph: adjwgt") : NULL;
    graph->vsize = ismalloc(graph->nvtxs, 1, "ReadGraph: vsize");
  }
  gk_siguntrap();

  if (sigrval != 0) {
    if (errno == 0)
      errno = ENOMEM;
    return 0;
  }
  return graph->xadj != NULL &&
      (graph->nedges == 0 || graph->adjncy != NULL) &&
      graph->vwgt != NULL && graph->vsize != NULL &&
      (graph->nedges == 0 || !readew || graph->adjwgt != NULL);
}


/*************************************************************************/
/*! Allocates the fixed-size mesh arrays under a cleanup-safe signal trap. */
/*************************************************************************/
static int AllocateMeshInput(mesh_t *mesh, idx_t ncon, size_t eindcap)
{
  volatile int sigrval=0;

  if (!gk_sigtrap()) {
    errno = ENOMEM;
    return 0;
  }
  METIS_SIGCATCH(sigrval);
  if (sigrval == 0) {
    mesh->eptr = ismalloc(mesh->ne+1, 0, "ReadMesh: eptr");
    mesh->eind = imalloc(eindcap, "ReadMesh: eind");
    mesh->ewgt = ismalloc((ncon == 0 ? 1 : ncon)*mesh->ne, 1,
        "ReadMesh: ewgt");
  }
  gk_siguntrap();

  if (sigrval != 0) {
    if (errno == 0)
      errno = ENOMEM;
    return 0;
  }
  return mesh->eptr != NULL && mesh->eind != NULL && mesh->ewgt != NULL;
}


/*************************************************************************/
/*! Grows the mesh index array without allowing an allocation signal to
    bypass the caller's open-stream cleanup.
*/
/*************************************************************************/
static int GrowMeshInput(mesh_t *mesh, size_t newcap)
{
  volatile int sigrval=0;
  idx_t * volatile new_eind=NULL;

  if (!gk_sigtrap()) {
    errno = ENOMEM;
    return 0;
  }
  METIS_SIGCATCH(sigrval);
  if (sigrval == 0)
    new_eind = irealloc(mesh->eind, newcap, "ReadMesh: eind");
  gk_siguntrap();

  if (sigrval != 0) {
    if (errno == 0)
      errno = ENOMEM;
    return 0;
  }
  if (new_eind == NULL)
    return 0;
  mesh->eind = new_eind;
  return 1;
}



/*************************************************************************/
/*! This function reads in a sparse graph */
/*************************************************************************/
int ReadGraph(params_t *params, graph_t **r_graph)
{
  idx_t i, k, l, fmt, ncon, readew, readvw, readvs, edge, ewgt;
  idx_t *xadj, *adjncy, *vwgt, *adjwgt, *vsize;
  char *line=NULL, fmtstr[256], *curstr;
  size_t lnlen=0;
  ssize_t line_status;
  int saved_errno=0, status=METIS_ERROR, token_status;
  FILE *fpin=NULL;
  graph_t *graph=NULL;

  if (r_graph == NULL) {
    errno = EINVAL;
    return METIS_ERROR_INPUT;
  }
  *r_graph = NULL;
  if (params == NULL || params->filename == NULL) {
    errno = EINVAL;
    return ReportError(METIS_ERROR_INPUT, errno,
        "A graph filename is required.\n");
  }

  graph = CreateGraph();
  if (graph == NULL)
    return METIS_ERROR_MEMORY;

  fpin = fopen(params->filename, "r");
  if (fpin == NULL)
    goto read_failure;

  line_status = ReadHeaderLine(&line, &lnlen, fpin);
  if (line_status == -2) {
    if (errno == ENOMEM || errno == EOVERFLOW)
      goto allocation_failure;
    goto read_failure;
  }
  if (line_status < 0 ||
      !ParseGraphHeader(line, &graph->nvtxs, &graph->nedges, &fmt, &ncon))
    goto invalid_input;

  if (graph->nvtxs <= 0 || graph->nedges < 0)
    goto invalid_input;
        
  if (fmt < 0 || fmt > 111 || fmt/100 > 1 || (fmt/10)%10 > 1 || fmt%10 > 1)
    goto invalid_input;

  sprintf(fmtstr, "%03"PRIDX, fmt%1000);
  readvs = (fmtstr[0] == '1');
  readvw = (fmtstr[1] == '1');
  readew = (fmtstr[2] == '1');
    
  /*printf("%s %"PRIDX" %"PRIDX" %"PRIDX"\n", fmtstr, readvs, readvw, readew); */


  if (ncon > 0 && !readvw)
    goto invalid_input;

  if (graph->nedges > IDX_MAX/2 || graph->nvtxs == IDX_MAX) {
    errno = EOVERFLOW;
    goto allocation_failure;
  }
  if (ncon < 0)
    goto invalid_input;

  graph->nedges *= 2;
  ncon = graph->ncon = (ncon == 0 ? 1 : ncon);
  if (graph->nvtxs > IDX_MAX/ncon) {
    errno = EOVERFLOW;
    goto allocation_failure;
  }
  if ((uintmax_t)(graph->nvtxs+1) > (uintmax_t)SIZE_MAX/sizeof(idx_t) ||
      (uintmax_t)graph->nedges > (uintmax_t)SIZE_MAX/sizeof(idx_t) ||
      (uintmax_t)(graph->nvtxs*ncon) >
        (uintmax_t)SIZE_MAX/sizeof(idx_t)) {
    errno = EOVERFLOW;
    goto allocation_failure;
  }

  if (!AllocateGraphInput(graph, ncon, readew))
    goto allocation_failure;
  xadj = graph->xadj;
  adjncy = graph->adjncy;
  vwgt = graph->vwgt;
  adjwgt = graph->adjwgt;
  vsize = graph->vsize;

  /*----------------------------------------------------------------------
   * Read the sparse graph file
   *---------------------------------------------------------------------*/
  for (xadj[0]=0, k=0, i=0; i<graph->nvtxs; i++) {
    line_status = ReadHeaderLine(&line, &lnlen, fpin);
    if (line_status == -2) {
      if (errno == ENOMEM || errno == EOVERFLOW)
        goto allocation_failure;
      goto read_failure;
    }
    if (line_status < 0)
      goto invalid_input;

    curstr = line;
    /* Tokens are parsed strictly by ReadIntegerToken. */

    /* Read vertex sizes */
    if (readvs) {
      if (ReadIntegerToken(&curstr, &vsize[i]) != 1)
        goto invalid_input;
      if (vsize[i] < 0)
        goto invalid_input;
    }


    /* Read vertex weights */
    if (readvw) {
      for (l=0; l<ncon; l++) {
        if (ReadIntegerToken(&curstr, &vwgt[i*ncon+l]) != 1)
          goto invalid_input;
        if (vwgt[i*ncon+l] < 0)
          goto invalid_input;
      }
    }

    while (1) {
      token_status = ReadIntegerToken(&curstr, &edge);
      if (token_status < 0)
        goto invalid_input;
      if (token_status == 0)
        break; /* End of line */

      if (edge < 1 || edge > graph->nvtxs)
        goto invalid_input;

      ewgt = 1;
      if (readew) {
        if (ReadIntegerToken(&curstr, &ewgt) != 1)
          goto invalid_input;
        if (ewgt <= 0)
          goto invalid_input;
      }

      if (k == graph->nedges)
        goto invalid_input;

      adjncy[k] = edge-1;
      if (readew) adjwgt[k] = ewgt;
      k++;
    } 
    xadj[i+1] = k;
  }
  while ((line_status = ReadLine(&line, &lnlen, fpin)) >= 0) {
    curstr = line;
    while (isspace((unsigned char)*curstr))
      curstr++;
    if (*curstr != '\0' && *curstr != '%')
      goto invalid_input;
  }
  if (line_status == -2) {
    if (errno == ENOMEM || errno == EOVERFLOW)
      goto allocation_failure;
    goto read_failure;
  }
  if (line_status < -1 || k != graph->nedges)
    goto invalid_input;
  if (fclose(fpin) != 0) {
    fpin = NULL;
    goto read_failure;
  }
  fpin = NULL;

  free(line);

  *r_graph = graph;
  return METIS_OK;

invalid_input:
  status = METIS_ERROR_INPUT;
  errno = EINVAL;
read_failure:
  saved_errno = errno != 0 ? errno : EIO;
  if (fpin != NULL)
    fclose(fpin);
  free(line);
  if (graph != NULL)
    FreeGraph(&graph);
  return ReportError(status, saved_errno,
      "Invalid or unreadable graph file %s.\n", params->filename);

allocation_failure:
  saved_errno = errno != 0 ? errno : ENOMEM;
  if (fpin != NULL)
    fclose(fpin);
  free(line);
  FreeGraph(&graph);
  return ReportError(METIS_ERROR_MEMORY, saved_errno,
      "Memory allocation failed while reading %s.\n", params->filename);
}


/*************************************************************************/
/*! This function reads in a mesh */
/*************************************************************************/
int ReadMesh(params_t *params, mesh_t **r_mesh)
{
  idx_t i, k, l, ncon, node;
  idx_t *eptr, *eind, *ewgt;
  size_t eindcap, newcap;
  char *line=NULL, *curstr;
  size_t lnlen=0;
  ssize_t line_status;
  int saved_errno=0, status=METIS_ERROR, token_status, unsupported_ncon=0;
  FILE *fpin=NULL;
  mesh_t *mesh=NULL;

  if (r_mesh == NULL) {
    errno = EINVAL;
    return METIS_ERROR_INPUT;
  }
  *r_mesh = NULL;
  if (params == NULL || params->filename == NULL) {
    errno = EINVAL;
    return ReportError(METIS_ERROR_INPUT, errno,
        "A mesh filename is required.\n");
  }

  mesh = CreateMesh();
  if (mesh == NULL)
    return METIS_ERROR_MEMORY;

  fpin = fopen(params->filename, "r");
  if (fpin == NULL)
    goto read_failure;
  line_status = ReadHeaderLine(&line, &lnlen, fpin);
  if (line_status == -2) {
    if (errno == ENOMEM || errno == EOVERFLOW)
      goto allocation_failure;
    goto read_failure;
  }
  if (line_status < 0 || !ParseMeshHeader(line, &mesh->ne, &mesh->ncon))
    goto invalid_input;

  if (mesh->ne <= 0) 
    goto invalid_input;
  if (mesh->ncon < 0)
    goto invalid_input;
  if (mesh->ncon > 1) {
    unsupported_ncon = 1;
    goto invalid_input;
  }
  if (mesh->ne == IDX_MAX) {
    errno = EOVERFLOW;
    goto allocation_failure;
  }
        
  ncon = mesh->ncon;
  if (ncon > 0 && mesh->ne > IDX_MAX/ncon) {
    errno = EOVERFLOW;
    goto allocation_failure;
  }
  eindcap = (size_t)mesh->ne;
  if ((uintmax_t)(mesh->ne+1) > (uintmax_t)SIZE_MAX/sizeof(idx_t) ||
      (uintmax_t)eindcap > (uintmax_t)SIZE_MAX/sizeof(idx_t) ||
      (uintmax_t)((ncon == 0 ? 1 : ncon)*mesh->ne) >
        (uintmax_t)SIZE_MAX/sizeof(idx_t)) {
    errno = EOVERFLOW;
    goto allocation_failure;
  }

  if (!AllocateMeshInput(mesh, ncon, eindcap))
    goto allocation_failure;
  eptr = mesh->eptr;
  eind = mesh->eind;
  ewgt = mesh->ewgt;

  /*----------------------------------------------------------------------
   * Read the mesh file
   *---------------------------------------------------------------------*/
  for (eptr[0]=0, k=0, i=0; i<mesh->ne; i++) {
    line_status = ReadHeaderLine(&line, &lnlen, fpin);
    if (line_status == -2) {
      if (errno == ENOMEM || errno == EOVERFLOW)
        goto allocation_failure;
      goto read_failure;
    }
    if (line_status < 0)
      goto invalid_input;

    curstr = line;
    /* Tokens are parsed strictly by ReadIntegerToken. */

    /* Read element weights */
    for (l=0; l<ncon; l++) {
      if (ReadIntegerToken(&curstr, &ewgt[i*ncon+l]) != 1)
        goto invalid_input;
      if (ewgt[i*ncon+l] < 0)
        goto invalid_input;
    }

    while (1) {
      token_status = ReadIntegerToken(&curstr, &node);
      if (token_status < 0)
        goto invalid_input;
      if (token_status == 0)
        break; /* End of line */

      if (node < 1 || node == IDX_MAX)
        goto invalid_input;

      if ((size_t)k == eindcap) {
        if (eindcap == (size_t)IDX_MAX) {
          errno = EOVERFLOW;
          goto allocation_failure;
        }
        newcap = eindcap > (size_t)IDX_MAX/2 ?
            (size_t)IDX_MAX : 2*eindcap;
        if (!GrowMeshInput(mesh, newcap))
          goto allocation_failure;
        eind = mesh->eind;
        eindcap = newcap;
      }
      eind[k++] = node-1;
    } 
    if (eptr[i] == k)
      goto invalid_input;
    eptr[i+1] = k;
  }
  while ((line_status = ReadLine(&line, &lnlen, fpin)) >= 0) {
    curstr = line;
    while (isspace((unsigned char)*curstr))
      curstr++;
    if (*curstr != '\0' && *curstr != '%')
      goto invalid_input;
  }
  if (line_status == -2) {
    if (errno == ENOMEM || errno == EOVERFLOW)
      goto allocation_failure;
    goto read_failure;
  }
  if (line_status < -1)
    goto invalid_input;
  if (fclose(fpin) != 0) {
    fpin = NULL;
    goto read_failure;
  }
  fpin = NULL;

  mesh->ncon = (ncon == 0 ? 1 : ncon);
  mesh->nn   = imax(eptr[mesh->ne], eind, 1)+1;

  free(line);

  *r_mesh = mesh;
  return METIS_OK;

invalid_input:
  status = METIS_ERROR_INPUT;
  errno = EINVAL;
read_failure:
  saved_errno = errno != 0 ? errno : EIO;
  if (fpin != NULL)
    fclose(fpin);
  free(line);
  if (mesh != NULL)
    FreeMesh(&mesh);
  if (unsupported_ncon)
    return ReportError(status, saved_errno,
        "Mesh input supports at most one balancing constraint.\n");
  return ReportError(status, saved_errno,
      "Invalid or unreadable mesh file %s.\n", params->filename);

allocation_failure:
  saved_errno = errno != 0 ? errno : ENOMEM;
  if (fpin != NULL)
    fclose(fpin);
  free(line);
  FreeMesh(&mesh);
  return ReportError(METIS_ERROR_MEMORY, saved_errno,
      "Memory allocation failed while reading %s.\n", params->filename);
}


static idx_t ReadTPwgtsInteger(char *string, char **endptr)
{
  idx_t value=0;

  if (!ParseIndex(string, endptr, &value))
    errno = ERANGE;

  return value;
}


/*************************************************************************/
/*! Allocates an initialized target-weight vector under a local signal trap. */
/*************************************************************************/
static int AllocateTPwgts(idx_t count, real_t **r_tpwgts)
{
  volatile int sigrval=0;

  *r_tpwgts = NULL;
  if (!gk_sigtrap()) {
    errno = ENOMEM;
    return 0;
  }
  METIS_SIGCATCH(sigrval);
  if (sigrval == 0)
    *r_tpwgts = rsmalloc(count, -1.0, "ReadTPwgts: tpwgts");
  gk_siguntrap();

  if (sigrval != 0) {
    if (errno == 0)
      errno = ENOMEM;
    return 0;
  }
  return *r_tpwgts != NULL;
}


/*************************************************************************/
/*! This function reads in the target partition weights. If no file is 
    specified the weights are set to 1/nparts */
/*************************************************************************/
int ReadTPwgts(params_t *params, idx_t ncon)
{
  enum {
    TPWGTS_ERROR_GENERIC,
    TPWGTS_ERROR_FROM,
    TPWGTS_ERROR_TO,
    TPWGTS_ERROR_FROM_CONSTRAINT,
    TPWGTS_ERROR_TO_CONSTRAINT,
    TPWGTS_ERROR_WEIGHT,
    TPWGTS_ERROR_WEIGHT_MISSING,
    TPWGTS_ERROR_TRAILING,
    TPWGTS_ERROR_PARTITION_RANGE,
    TPWGTS_ERROR_CONSTRAINT_RANGE,
    TPWGTS_ERROR_PARTITION_WEIGHT,
    TPWGTS_ERROR_TOTAL_WEIGHT
  } failure_reason=TPWGTS_ERROR_GENERIC;
  idx_t i, j, from, to, fromcnum, tocnum, nleft;
  real_t awgt=0.0, twgt;
  char *line=NULL, *curstr, *newstr;
  size_t lnlen=0;
  ssize_t line_status;
  int saved_errno=0, status=METIS_ERROR;
  FILE *fpin=NULL;
  real_t *tpwgts=NULL;

  if (params == NULL || params->nparts <= 0 || ncon <= 0) {
    errno = EINVAL;
    return ReportError(METIS_ERROR_INPUT, errno,
        "The target-weight dimensions are invalid.\n");
  }
  if (params->nparts > IDX_MAX/ncon ||
      (uintmax_t)(params->nparts*ncon) >
      (uintmax_t)SIZE_MAX/sizeof(real_t)) {
    errno = EOVERFLOW;
    return ReportError(METIS_ERROR_MEMORY, errno,
        "The target-weight allocation size is too large.\n");
  }

  if (!AllocateTPwgts(params->nparts*ncon, &tpwgts))
    goto allocation_failure;

  if (params->tpwgtsfile == NULL) {
    for (i=0; i<params->nparts; i++) {
      for (j=0; j<ncon; j++)
        tpwgts[i*ncon+j] =
            (real_t)(1.0/(double)params->nparts);
    }
    goto success;
  }

  fpin = fopen(params->tpwgtsfile, "r");
  if (fpin == NULL)
    goto read_failure;

  while ((line_status = ReadLine(&line, &lnlen, fpin)) >= 0) {
    /* start extracting the fields */

    curstr = line;
    newstr = NULL;
    SkipWhitespace(&curstr);

    from = ReadTPwgtsInteger(curstr, &newstr);
    if (newstr == curstr || errno == ERANGE) {
      failure_reason = TPWGTS_ERROR_FROM;
      goto invalid_input;
    }
    curstr = newstr;

    SkipWhitespace(&curstr);
    if (curstr[0] == '-') {
      curstr++;
      SkipWhitespace(&curstr);
      to = ReadTPwgtsInteger(curstr, &newstr);
      if (newstr == curstr || errno == ERANGE) {
        failure_reason = TPWGTS_ERROR_TO;
        goto invalid_input;
      }
      curstr = newstr;
    }
    else {
      to = from;
    }

    SkipWhitespace(&curstr);
    if (curstr[0] == ':') {
      curstr++;
      SkipWhitespace(&curstr);
      fromcnum = ReadTPwgtsInteger(curstr, &newstr);
      if (newstr == curstr || errno == ERANGE) {
        failure_reason = TPWGTS_ERROR_FROM_CONSTRAINT;
        goto invalid_input;
      }
      curstr = newstr;

      SkipWhitespace(&curstr);
      if (curstr[0] == '-') {
        curstr++;
        SkipWhitespace(&curstr);
        tocnum = ReadTPwgtsInteger(curstr, &newstr);
        if (newstr == curstr || errno == ERANGE) {
          failure_reason = TPWGTS_ERROR_TO_CONSTRAINT;
          goto invalid_input;
        }
        curstr = newstr;
      }
      else {
        tocnum = fromcnum;
      }
    }
    else {
      fromcnum = 0;
      tocnum   = ncon-1;
    }

    SkipWhitespace(&curstr);
    if (curstr[0] == '=') {
      curstr++;
      SkipWhitespace(&curstr);
      errno = 0;
      awgt = strtoreal(curstr, &newstr);
      if (newstr == curstr || errno == ERANGE) {
        failure_reason = TPWGTS_ERROR_WEIGHT;
        goto invalid_input;
      }
      curstr = newstr;
    }
    else {
      failure_reason = TPWGTS_ERROR_WEIGHT_MISSING;
      goto invalid_input;
    }

    SkipWhitespace(&curstr);
    if (curstr[0] != '\0') {
      failure_reason = TPWGTS_ERROR_TRAILING;
      goto invalid_input;
    }

    /*printf("Read: %"PRIDX"-%"PRIDX":%"PRIDX"-%"PRIDX"=%"PRREAL"\n",
        from, to, fromcnum, tocnum, awgt);*/

    if (from < 0 || to < 0 || from > to ||
        from >= params->nparts || to >= params->nparts) {
      failure_reason = TPWGTS_ERROR_PARTITION_RANGE;
      goto invalid_input;
    }
    if (fromcnum < 0 || tocnum < 0 || fromcnum > tocnum ||
        fromcnum >= ncon || tocnum >= ncon) {
      failure_reason = TPWGTS_ERROR_CONSTRAINT_RANGE;
      goto invalid_input;
    }
    if (!isfinite(awgt) || awgt <= 0.0 || awgt >= 1.0) {
      failure_reason = TPWGTS_ERROR_PARTITION_WEIGHT;
      goto invalid_input;
    }
    for (i=from; i<=to; i++) {
      for (j=fromcnum; j<=tocnum; j++)
        tpwgts[i*ncon+j] = awgt;
    }
  }
  if (line_status == -2) {
    if (errno == ENOMEM || errno == EOVERFLOW)
      goto allocation_failure;
    goto read_failure;
  }
  if (line_status < -1)
    goto invalid_input;

  if (fclose(fpin) != 0) {
    fpin = NULL;
    goto read_failure;
  }
  fpin = NULL;

  /* Assign weight to the unspecified constraints x partitions */
  for (j=0; j<ncon; j++) {
    /* Sum up the specified weights for the jth constraint */
    for (twgt=0.0, nleft=params->nparts, i=0; i<params->nparts; i++) {
      if (tpwgts[i*ncon+j] > 0) {
        twgt += tpwgts[i*ncon+j];
        nleft--;
      }
    }

    /* Rescale the weights to be on the safe side */
    if (nleft == 0) 
      rscale(params->nparts, (real_t)(1.0/(double)twgt),
          tpwgts+j, ncon);
  
    /* Assign the left-over weight to the remaining partitions */
    if (nleft > 0) {
      if (twgt >= 1) {
        failure_reason = TPWGTS_ERROR_TOTAL_WEIGHT;
        goto invalid_input;
      }
  
      awgt = (real_t)((1.0-(double)twgt)/(double)nleft);
      for (i=0; i<params->nparts; i++)
        tpwgts[i*ncon+j] =
            (tpwgts[i*ncon+j] < 0 ? awgt : tpwgts[i*ncon+j]);
    }
  }

success:
  free(line);
  gk_free((void **)&params->tpwgts, LTERM);
  params->tpwgts = tpwgts;
  return METIS_OK;

invalid_input:
  status = METIS_ERROR_INPUT;
  errno = EINVAL;
read_failure:
  saved_errno = errno != 0 ? errno : EIO;
  if (fpin != NULL)
    fclose(fpin);
  free(line);
  gk_free((void **)&tpwgts, LTERM);
  switch (failure_reason) {
    case TPWGTS_ERROR_FROM:
      return ReportError(status, saved_errno,
          "The 'from' component in the tpwgts file is incorrect.\n");
    case TPWGTS_ERROR_TO:
      return ReportError(status, saved_errno,
          "The 'to' component in the tpwgts file is incorrect.\n");
    case TPWGTS_ERROR_FROM_CONSTRAINT:
      return ReportError(status, saved_errno,
          "The 'fromcnum' component in the tpwgts file is incorrect.\n");
    case TPWGTS_ERROR_TO_CONSTRAINT:
      return ReportError(status, saved_errno,
          "The 'tocnum' component in the tpwgts file is incorrect.\n");
    case TPWGTS_ERROR_WEIGHT:
      return ReportError(status, saved_errno,
          "The 'wgt' component in the tpwgts file is incorrect.\n");
    case TPWGTS_ERROR_WEIGHT_MISSING:
      return ReportError(status, saved_errno,
          "The 'wgt' component in the tpwgts file is missing.\n");
    case TPWGTS_ERROR_TRAILING:
      return ReportError(status, saved_errno,
          "The tpwgts line contains trailing characters.\n");
    case TPWGTS_ERROR_PARTITION_RANGE:
      return ReportError(status, saved_errno,
          "Invalid partition range in the tpwgts file.\n");
    case TPWGTS_ERROR_CONSTRAINT_RANGE:
      return ReportError(status, saved_errno,
          "Invalid constraint number range in the tpwgts file.\n");
    case TPWGTS_ERROR_PARTITION_WEIGHT:
      return ReportError(status, saved_errno,
          "Invalid partition weight in the tpwgts file.\n");
    case TPWGTS_ERROR_TOTAL_WEIGHT:
      return ReportError(status, saved_errno,
          "The total specified target partition weights meet or exceed 1.0.\n");
    default:
      return ReportError(status, saved_errno,
          "Invalid or unreadable target-weight file %s.\n",
          params->tpwgtsfile != NULL ? params->tpwgtsfile : "(null)");
  }

allocation_failure:
  saved_errno = errno != 0 ? errno : ENOMEM;
  if (fpin != NULL)
    fclose(fpin);
  free(line);
  gk_free((void **)&tpwgts, LTERM);
  return ReportError(METIS_ERROR_MEMORY, saved_errno,
      "Memory allocation failed while reading target weights.\n");
}


/*************************************************************************/
/*! Allocates temporary permutation arrays under a local signal trap. */
/*************************************************************************/
static int AllocatePOVectors(idx_t count, idx_t **r_seen, idx_t **r_values)
{
  volatile int sigrval=0;

  *r_seen = NULL;
  *r_values = NULL;
  if (!gk_sigtrap()) {
    errno = ENOMEM;
    return 0;
  }
  METIS_SIGCATCH(sigrval);
  if (sigrval == 0) {
    *r_seen = ismalloc(count, 0, "ReadPOVector: seen");
    *r_values = imalloc(count, "ReadPOVector: values");
  }
  gk_siguntrap();

  if (sigrval != 0) {
    if (errno == 0)
      errno = ENOMEM;
    return 0;
  }
  return *r_seen != NULL && *r_values != NULL;
}


/*************************************************************************/
/*! This function reads in a partition/ordering vector  */
/**************************************************************************/
int ReadPOVector(graph_t *graph, char *filename, idx_t *vector)
{
  idx_t i, value, *seen=NULL, *values=NULL;
  char *line=NULL, *cursor;
  size_t lnlen=0;
  ssize_t line_status;
  int saved_errno=0, status=METIS_ERROR, token_status;
  FILE *fpin=NULL;

  if (graph == NULL || filename == NULL || vector == NULL ||
      graph->nvtxs < 0) {
    errno = EINVAL;
    return ReportError(METIS_ERROR_INPUT, errno,
        "Invalid permutation input arguments.\n");
  }
  if ((uintmax_t)graph->nvtxs > (uintmax_t)SIZE_MAX/sizeof(idx_t)) {
    errno = EOVERFLOW;
    return ReportError(METIS_ERROR_MEMORY, errno,
        "The permutation allocation size is too large.\n");
  }
  if (!AllocatePOVectors(graph->nvtxs, &seen, &values))
    goto allocation_failure;

  fpin = fopen(filename, "r");
  if (fpin == NULL)
    goto read_failure;
  i = 0;
  while ((line_status = ReadLine(&line, &lnlen, fpin)) >= 0) {
    cursor = line;
    while ((token_status = ReadIntegerToken(&cursor, &value)) == 1) {
      if (i >= graph->nvtxs || value < 0 || value >= graph->nvtxs ||
          seen[value])
        goto invalid_input;
      seen[value] = 1;
      values[i++] = value;
    }
    if (token_status < 0)
      goto invalid_input;
  }
  if (line_status == -2) {
    if (errno == ENOMEM || errno == EOVERFLOW)
      goto allocation_failure;
    goto read_failure;
  }
  if (line_status < -1 || i != graph->nvtxs)
    goto invalid_input;
  if (fclose(fpin) != 0) {
    fpin = NULL;
    goto read_failure;
  }
  fpin = NULL;

  memcpy(vector, values, (size_t)graph->nvtxs*sizeof(idx_t));
  free(line);
  gk_free((void **)&seen, &values, LTERM);
  return METIS_OK;

invalid_input:
  status = METIS_ERROR_INPUT;
  errno = EINVAL;
read_failure:
  saved_errno = errno != 0 ? errno : EIO;
  if (fpin != NULL)
    fclose(fpin);
  free(line);
  gk_free((void **)&seen, &values, LTERM);
  return ReportError(status, saved_errno,
      "Invalid or unreadable permutation file %s.\n",
      filename != NULL ? filename : "(null)");

allocation_failure:
  saved_errno = errno != 0 ? errno : ENOMEM;
  if (fpin != NULL)
    fclose(fpin);
  free(line);
  gk_free((void **)&seen, &values, LTERM);
  return ReportError(METIS_ERROR_MEMORY, saved_errno,
      "Memory allocation failed while reading %s.\n",
      filename != NULL ? filename : "(null)");
}


/*************************************************************************/
/*! This function writes out the partition vector */
/*************************************************************************/
int WritePartition(char *fname, idx_t *part, idx_t n, idx_t nparts)
{
  FILE *fpout;
  idx_t i;
  int saved_errno, status, suffix_length;
  char suffix[64], *filename=NULL, *tempname=NULL;

  if (fname == NULL || n < 0 || nparts <= 0 || (n > 0 && part == NULL)) {
    errno = EINVAL;
    return ReportError(METIS_ERROR_INPUT, errno,
        "Invalid partition output arguments.\n");
  }
  for (i=0; i<n; i++) {
    if (part[i] < 0 || part[i] >= nparts) {
      errno = EINVAL;
      return ReportError(METIS_ERROR_INPUT, errno,
          "Invalid partition number in output vector.\n");
    }
  }

  suffix_length = snprintf(suffix, sizeof(suffix), ".part.%"PRIDX, nparts);
  if (suffix_length < 0 || (size_t)suffix_length >= sizeof(suffix)) {
    errno = EINVAL;
    return ReportError(METIS_ERROR, errno,
        "Failed to format the output filename.\n");
  }
  status = AppendOutputSuffix(fname, suffix, &filename);
  if (status != METIS_OK) {
    saved_errno = errno;
    return ReportError(status, saved_errno,
        "Failed to allocate the output filename: %s\n",
        strerror(saved_errno));
  }

  status = OpenOutputFile(filename, &fpout, &tempname);
  if (status != METIS_OK) {
    saved_errno = errno;
    gk_free((void **)&filename, LTERM);
    errno = saved_errno;
    return ReportError(status, saved_errno,
        "Failed to open output file: %s\n",
        strerror(saved_errno));
  }

  for (i=0; i<n; i++)
    fprintf(fpout,"%" PRIDX "\n", part[i]);

  if (!FinishOutputFile(fpout, &tempname, filename)) {
    saved_errno = errno;
    status = OutputErrorStatus();
    gk_free((void **)&filename, LTERM);
    errno = saved_errno;
    return ReportError(status, saved_errno,
        "Failed to write output file: %s\n",
        strerror(saved_errno));
  }
  gk_free((void **)&filename, LTERM);
  return METIS_OK;
}


/*************************************************************************/
/*! This function writes out the partition vectors for a mesh */
/*************************************************************************/
int WriteMeshPartition(char *fname, idx_t nparts, idx_t ne, idx_t *epart,
       idx_t nn, idx_t *npart)
{
  FILE *efpout, *nfpout;
  idx_t i;
  int saved_errno, status, suffix_length;
  char suffix[64], *efilename=NULL, *nfilename=NULL;
  char *etempname=NULL, *ntempname=NULL;

  if (fname == NULL || nparts <= 0 || ne < 0 || nn < 0 ||
      (ne > 0 && epart == NULL) || (nn > 0 && npart == NULL)) {
    errno = EINVAL;
    return ReportError(METIS_ERROR_INPUT, errno,
        "Invalid mesh-partition output arguments.\n");
  }
  for (i=0; i<ne; i++) {
    if (epart[i] < 0 || epart[i] >= nparts) {
      errno = EINVAL;
      return ReportError(METIS_ERROR_INPUT, errno,
          "Invalid element partition number in output vector.\n");
    }
  }
  for (i=0; i<nn; i++) {
    if (npart[i] < 0 || npart[i] >= nparts) {
      errno = EINVAL;
      return ReportError(METIS_ERROR_INPUT, errno,
          "Invalid node partition number in output vector.\n");
    }
  }

  suffix_length = snprintf(suffix, sizeof(suffix), ".epart.%"PRIDX, nparts);
  if (suffix_length < 0 || (size_t)suffix_length >= sizeof(suffix)) {
    errno = EINVAL;
    return ReportError(METIS_ERROR, errno,
        "Failed to format the output filename.\n");
  }
  status = AppendOutputSuffix(fname, suffix, &efilename);
  if (status != METIS_OK) {
    saved_errno = errno;
    return ReportError(status, saved_errno,
        "Failed to allocate the output filename: %s\n",
        strerror(saved_errno));
  }
  suffix_length = snprintf(suffix, sizeof(suffix), ".npart.%"PRIDX, nparts);
  if (suffix_length < 0 || (size_t)suffix_length >= sizeof(suffix)) {
    saved_errno = EINVAL;
    gk_free((void **)&efilename, LTERM);
    errno = saved_errno;
    return ReportError(METIS_ERROR, saved_errno,
        "Failed to format the output filename.\n");
  }
  status = AppendOutputSuffix(fname, suffix, &nfilename);
  if (status != METIS_OK) {
    saved_errno = errno;
    gk_free((void **)&efilename, LTERM);
    errno = saved_errno;
    return ReportError(status, saved_errno,
        "Failed to allocate the output filename: %s\n",
        strerror(saved_errno));
  }

  status = OpenOutputFile(efilename, &efpout, &etempname);
  if (status != METIS_OK) {
    saved_errno = errno;
    gk_free((void **)&efilename, &nfilename, LTERM);
    errno = saved_errno;
    return ReportError(status, saved_errno,
        "Failed to open output file: %s\n",
        strerror(saved_errno));
  }

  for (i=0; i<ne; i++)
    fprintf(efpout,"%" PRIDX "\n", epart[i]);
  if (ferror(efpout)) {
    saved_errno = errno != 0 ? errno : EIO;
    status = OutputErrorStatus();
    DiscardOutputFile(efpout, &etempname);
    gk_free((void **)&efilename, &nfilename, LTERM);
    errno = saved_errno;
    return ReportError(status, saved_errno,
        "Failed to write output file: %s\n",
        strerror(saved_errno));
  }

  status = OpenOutputFile(nfilename, &nfpout, &ntempname);
  if (status != METIS_OK) {
    saved_errno = errno;
    DiscardOutputFile(efpout, &etempname);
    gk_free((void **)&efilename, &nfilename, LTERM);
    errno = saved_errno;
    return ReportError(status, saved_errno,
        "Failed to open output file: %s\n",
        strerror(saved_errno));
  }

  for (i=0; i<nn; i++)
    fprintf(nfpout, "%" PRIDX "\n", npart[i]);

  if (!FinishOutputPair(efpout, &etempname, efilename,
                        nfpout, &ntempname, nfilename)) {
    saved_errno = errno;
    status = OutputErrorStatus();
    gk_free((void **)&efilename, &nfilename, LTERM);
    errno = saved_errno;
    return ReportError(status, saved_errno,
        "Failed to write output file: %s\n",
        strerror(saved_errno));
  }
  gk_free((void **)&efilename, &nfilename, LTERM);

  return METIS_OK;
}


/*************************************************************************/
/*! This function writes out the permutation vector */
/*************************************************************************/
int WritePermutation(char *fname, idx_t *iperm, idx_t n)
{
  FILE *fpout;
  idx_t i;
  int saved_errno, status;
  char *filename=NULL, *tempname=NULL;

  if (fname == NULL || n < 0 || (n > 0 && iperm == NULL)) {
    errno = EINVAL;
    return ReportError(METIS_ERROR_INPUT, errno,
        "Invalid permutation output arguments.\n");
  }
  for (i=0; i<n; i++) {
    if (iperm[i] < 0 || iperm[i] >= n) {
      errno = EINVAL;
      return ReportError(METIS_ERROR_INPUT, errno,
          "Invalid permutation number in output vector.\n");
    }
  }

  status = AppendOutputSuffix(fname, ".iperm", &filename);
  if (status != METIS_OK) {
    saved_errno = errno;
    return ReportError(status, saved_errno,
        "Failed to allocate the output filename: %s\n",
        strerror(saved_errno));
  }

  status = OpenOutputFile(filename, &fpout, &tempname);
  if (status != METIS_OK) {
    saved_errno = errno;
    gk_free((void **)&filename, LTERM);
    errno = saved_errno;
    return ReportError(status, saved_errno,
        "Failed to open output file: %s\n",
        strerror(saved_errno));
  }

  for (i=0; i<n; i++)
    fprintf(fpout, "%" PRIDX "\n", iperm[i]);

  if (!FinishOutputFile(fpout, &tempname, filename)) {
    saved_errno = errno;
    status = OutputErrorStatus();
    gk_free((void **)&filename, LTERM);
    errno = saved_errno;
    return ReportError(status, saved_errno,
        "Failed to write output file: %s\n",
        strerror(saved_errno));
  }
  gk_free((void **)&filename, LTERM);
  return METIS_OK;
}


/*************************************************************************/
/*! This function writes a graph into a file  */
/*************************************************************************/
int WriteGraph(graph_t *graph, char *filename)
{
  idx_t i, j, nvtxs, ncon;
  idx_t *xadj, *adjncy, *adjwgt, *vwgt, *vsize;
  int hasvwgt=0, hasewgt=0, hasvsize=0, saved_errno, status;
  char *tempname=NULL;
  FILE *fpout;

  if (graph == NULL || filename == NULL || filename[0] == '\0' ||
      graph->nvtxs <= 0 ||
      graph->ncon <= 0 || graph->xadj == NULL ||
      (graph->ncon > 1 && graph->vwgt == NULL) ||
      graph->nvtxs > IDX_MAX/graph->ncon ||
      (uintmax_t)(graph->nvtxs*graph->ncon) >
          (uintmax_t)SIZE_MAX/sizeof(idx_t)) {
    errno = EINVAL;
    return ReportError(METIS_ERROR_INPUT, errno,
        "Invalid graph or output filename.\n");
  }

  nvtxs  = graph->nvtxs;
  ncon   = graph->ncon;
  xadj   = graph->xadj;
  adjncy = graph->adjncy;
  vwgt   = graph->vwgt;
  vsize  = graph->vsize;
  adjwgt = graph->adjwgt;
  hasvwgt = ncon > 1;

  if (xadj[0] != 0 || xadj[nvtxs] < 0 ||
      graph->nedges != xadj[nvtxs] || xadj[nvtxs]%2 != 0 ||
      (uintmax_t)xadj[nvtxs] >
          (uintmax_t)SIZE_MAX/sizeof(idx_t) ||
      (xadj[nvtxs] > 0 && adjncy == NULL)) {
    errno = EINVAL;
    return ReportError(METIS_ERROR_INPUT, errno,
        "Cannot write an invalid graph structure.\n");
  }
  for (i=0; i<nvtxs; i++) {
    if (xadj[i] > xadj[i+1]) {
      errno = EINVAL;
      return ReportError(METIS_ERROR_INPUT, errno,
          "Cannot write a graph with a non-monotone xadj array.\n");
    }
    for (j=xadj[i]; j<xadj[i+1]; j++) {
      if (adjncy[j] < 0 || adjncy[j] >= nvtxs) {
        errno = EINVAL;
        return ReportError(METIS_ERROR_INPUT, errno,
            "Cannot write a graph with an out-of-range adjacency.\n");
      }
    }
  }

  /* determine if the graph has non-unity vwgt, vsize, or adjwgt */
  if (vwgt) {
    for (i=0; i<nvtxs*ncon; i++) {
      if (vwgt[i] < 0) {
        errno = EINVAL;
        return ReportError(METIS_ERROR_INPUT, errno,
            "Cannot write a graph with a negative vertex weight.\n");
      }
      if (vwgt[i] != 1) {
        hasvwgt = 1;
      }
    }
  }
  if (vsize) {
    for (i=0; i<nvtxs; i++) {
      if (vsize[i] < 0) {
        errno = EINVAL;
        return ReportError(METIS_ERROR_INPUT, errno,
            "Cannot write a graph with a negative vertex size.\n");
      }
      if (vsize[i] != 1) {
        hasvsize = 1;
      }
    }
  }
  if (adjwgt) { 
    for (i=0; i<xadj[nvtxs]; i++) {
      if (adjwgt[i] <= 0) {
        errno = EINVAL;
        return ReportError(METIS_ERROR_INPUT, errno,
            "Cannot write a graph with a non-positive edge weight.\n");
      }
      if (adjwgt[i] != 1) {
        hasewgt = 1;
      }
    }
  }

  status = OpenOutputFile(filename, &fpout, &tempname);
  if (status != METIS_OK) {
    saved_errno = errno;
    return ReportError(status, saved_errno,
        "Failed to open output file %s: %s\n",
        filename, strerror(saved_errno));
  }

  /* write the header line */
  fprintf(fpout, "%"PRIDX" %"PRIDX, nvtxs, xadj[nvtxs]/2);
  if (hasvwgt || hasvsize || hasewgt) {
    fprintf(fpout, " %d%d%d", hasvsize, hasvwgt, hasewgt);
    if (hasvwgt)
      fprintf(fpout, " %"PRIDX, graph->ncon);
  }


  /* write the rest of the graph */
  for (i=0; i<nvtxs; i++) {
    fprintf(fpout, "\n");
    if (hasvsize) 
      fprintf(fpout, " %"PRIDX, vsize[i]);

    if (hasvwgt) {
      for (j=0; j<ncon; j++)
        fprintf(fpout, " %"PRIDX, vwgt[i*ncon+j]);
    }

    for (j=xadj[i]; j<xadj[i+1]; j++) {
      fprintf(fpout, " %"PRIDX, adjncy[j]+1);
      if (hasewgt)
        fprintf(fpout, " %"PRIDX, adjwgt[j]);
    }
  }
  fprintf(fpout, "\n");

  if (!FinishOutputFile(fpout, &tempname, filename)) {
    saved_errno = errno;
    status = OutputErrorStatus();
    return ReportError(status, saved_errno,
        "Failed to write output file %s: %s\n",
        filename, strerror(saved_errno));
  }
  return METIS_OK;
}
