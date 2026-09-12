/* Check the four legacy Fortran naming conventions through their C ABI. */
#include <metis.h>

#if defined(_WIN32)
#define METIS_TEST_CALL __cdecl
#else
#define METIS_TEST_CALL
#endif

#define DECLARE(name) \
  extern METIS_API(int) name(idx_t *, idx_t *, idx_t *, idx_t *, idx_t *, idx_t *, idx_t *)
DECLARE(METIS_NODEND);
DECLARE(metis_nodend);
DECLARE(metis_nodend_);
DECLARE(metis_nodend__);

int main(void)
{
  int (METIS_TEST_CALL *functions[])(idx_t *, idx_t *, idx_t *, idx_t *, idx_t *, idx_t *, idx_t *) = {
    METIS_NODEND, metis_nodend, metis_nodend_, metis_nodend__};
  idx_t n = 3, xadj[] = {0, 2, 4, 6}, adjncy[] = {1, 2, 0, 2, 0, 1};
  idx_t options[METIS_NOPTIONS], perm[3], iperm[3], i, j;
  for (i=0; i<4; i++) {
    METIS_SetDefaultOptions(options);
    if (functions[i](&n, xadj, adjncy, 0, options, perm, iperm) != METIS_OK)
      return 1;
    for (j=0; j<n; j++) {
      if (perm[j] < 0 || perm[j] >= n || iperm[perm[j]] != j)
        return 2;
    }
  }
  return 0;
}
