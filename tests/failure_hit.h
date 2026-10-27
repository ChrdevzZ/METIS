#ifndef TEST_FAILURE_HIT_H
#define TEST_FAILURE_HIT_H

#include <stdio.h>
#include <stdlib.h>

/* This side channel is closed before strict termination. Never use the streams
   whose silence is part of the failure contract, or accept a failed write. */
static void record_failure_hit(const char *path)
{
  FILE *stream;
  int failed;

  if (path == NULL || (stream = fopen(path, "wb")) == NULL)
    _Exit(EXIT_SUCCESS);
  failed = fputs("INJECTION_HIT\n", stream) == EOF;
  if (fclose(stream) != 0 || failed)
    _Exit(EXIT_SUCCESS);
}

#endif
