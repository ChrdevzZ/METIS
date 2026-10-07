#include <metis.h>
#include <vector>

#define METIS_CONSUMER_NO_MAIN
#include "main.c"

int main()
{
  std::vector<idx_t> options(METIS_NOPTIONS);

  return METIS_SetDefaultOptions(options.data()) == METIS_OK &&
      metis_consumer_run() == 0 ? 0 : 1;
}
