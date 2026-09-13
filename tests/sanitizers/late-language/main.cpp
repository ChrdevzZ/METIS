#include <metis.h>

extern "C" const char asan_empty[] = "";
extern "C" const char asan_text[] = "late-language";

int main()
{
  idx_t options[METIS_NOPTIONS];
  volatile const char *empty = asan_empty;
  volatile const char *text = asan_text;

  METIS_SetDefaultOptions(options);
  return empty[0] != '\0' || text[0] != 'l';
}
