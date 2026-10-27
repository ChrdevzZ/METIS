/* A standalone diagnostic control must fail at the requested memory access. */
#include <stdio.h>
#include <stdlib.h>

#ifndef __has_feature
#define __has_feature(x) 0
#endif
#if !defined(__SANITIZE_ADDRESS__) && !__has_feature(address_sanitizer)
#error AddressSanitizer instrumentation is not enabled
#endif


int main(int argc, char **argv)
{
  volatile size_t index = (size_t)argc + 7;
  volatile unsigned char *buffer = (volatile unsigned char *)malloc(8);

  if (buffer == NULL)
    return 2;
  puts("ASAN_FIXTURE_STARTED");
  fflush(stdout);
  buffer[index] = (unsigned char)(argv[0] != NULL);
  free((void *)buffer);
  return 0;
}
