#include <metis.h>
#include <vector>

int main()
{
  std::vector<idx_t> options(METIS_NOPTIONS);

  return METIS_SetDefaultOptions(options.data()) == METIS_OK ? 0 : 1;
}
