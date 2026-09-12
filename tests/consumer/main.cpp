#include <cstdint>
#include <vector>
#include <string>
#include <metis.h>
#include <GKlib.h>

#ifdef METIS_TEST_LIBCXX
#ifndef _LIBCPP_VERSION
#error "The CLANG64 consumer must use libc++"
#endif
#endif

int main()
{
  std::vector<idx_t> options(METIS_NOPTIONS);
  const std::string name("METIS");
  int *values = gk_imalloc(2, (char *)"C++ consumer");
  values[0] = 2;
  values[1] = 1;
  gk_isorti(2, values);
  const bool sorted = values[0] == 1 && values[1] == 2;
  gk_free((void **)&values, LTERM);
  return !sorted || name.empty() || METIS_SetDefaultOptions(options.data()) != METIS_OK;
}
