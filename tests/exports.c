/* Verify the public, Fortran, and application-facing Windows DLL exports. */
#include <windows.h>
#include <stdio.h>
#include "export_symbols.h"

int main(int argc, char **argv)
{
  HMODULE library;
  size_t i;
  char decorated[256];

  if (argc != 2 || (library = LoadLibraryA(argv[1])) == NULL)
    return 1;
  for (i=0; i<sizeof(symbols)/sizeof(symbols[0]); i++) {
    if (GetProcAddress(library, symbols[i]) != NULL)
      continue;
    if (sizeof(void *) == 4) {
      snprintf(decorated, sizeof(decorated), "_%s", symbols[i]);
      if (GetProcAddress(library, decorated) != NULL)
        continue;
    }
    fprintf(stderr, "Missing DLL export: %s\n", symbols[i]);
    FreeLibrary(library);
    return 2;
  }
  FreeLibrary(library);
  return 0;
}
