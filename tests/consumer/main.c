#include <metis.h>

int main(void)
{
  idx_t options[METIS_NOPTIONS];

  return METIS_SetDefaultOptions(options) == METIS_OK ? 0 : 1;
}
