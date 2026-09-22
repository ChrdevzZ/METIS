#include "metislib.h"


int main(void)
{
  idx_t eptr1[2] = {1, 3};
  idx_t eind1[2] = {1, 2};
  idx_t eptr2[2] = {1, 3};
  idx_t eind2[2] = {1, 2};

  ChangeMesh2CNumbering(1, eptr1, eind1);
  ChangeMesh2FNumbering(1, eptr1, eind1, 2, NULL, NULL);
  if (eptr1[0] != 1 || eptr1[1] != 3 ||
      eind1[0] != 1 || eind1[1] != 2)
    return 1;

  ChangeMesh2CNumbering(1, eptr2, eind2);
  ChangeMesh2FNumbering2(1, 2, eptr2, eind2, NULL, NULL);
  if (eptr2[0] != 1 || eptr2[1] != 3 ||
      eind2[0] != 1 || eind2[1] != 2)
    return 2;

  return 0;
}
