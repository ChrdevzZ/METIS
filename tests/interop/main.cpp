#include <metis.h>

#include <array>
#include <iostream>

#ifdef INTEROP_WITH_FORTRAN
extern "C" int interop_fortran_test(void);
#endif

static int test_cpp_api()
{
  idx_t nvtxs = 4, ncon = 1, nparts = 2, edgecut;
  std::array<idx_t, 5> xadj{{0, 2, 4, 6, 8}};
  std::array<idx_t, 8> adjncy{{1, 3, 0, 2, 1, 3, 0, 2}};
  std::array<idx_t, 4> vwgt{{1, 1, 1, 1}};
  std::array<idx_t, 8> adjwgt{{1, 1, 1, 1, 1, 1, 1, 1}};
  std::array<idx_t, 4> part;
  std::array<idx_t, 2> partition_weights{{0, 0}};
  std::array<idx_t, 4> perm, iperm;
  std::array<idx_t, METIS_NOPTIONS> options;
  std::array<real_t, 2> tpwgts{{real_t(0.25), real_t(0.75)}};
  std::array<real_t, 1> ubvec{{real_t(1.05)}};
  idx_t ne = 2, nn = 4, numflag = 0;
  std::array<idx_t, 3> eptr{{0, 3, 6}};
  std::array<idx_t, 6> eind{{0, 1, 2, 1, 2, 3}};
  idx_t *mesh_xadj = nullptr, *mesh_adjncy = nullptr;

  if (sizeof(idx_t)*8 != IDXTYPEWIDTH || sizeof(real_t)*8 != REALTYPEWIDTH)
    return 1;
  if (METIS_SetDefaultOptions(options.data()) != METIS_OK)
    return 2;
  if (METIS_PartGraphKway(&nvtxs, &ncon, xadj.data(), adjncy.data(),
      vwgt.data(), nullptr, adjwgt.data(), &nparts, tpwgts.data(),
      ubvec.data(), options.data(), &edgecut, part.data()) != METIS_OK)
    return 3;
  for (std::size_t i = 0; i < part.size(); ++i) {
    const idx_t value = part[i];
    if (value < 0 || value >= nparts)
      return 4;
    partition_weights[value] += vwgt[i];
  }
  if (partition_weights[0] != 1 || partition_weights[1] != 3)
    return 5;
  if (METIS_NodeND(&nvtxs, xadj.data(), adjncy.data(), vwgt.data(),
      options.data(), perm.data(), iperm.data()) != METIS_OK)
    return 6;
  for (std::size_t i = 0; i < perm.size(); ++i) {
    if (perm[i] < 0 || perm[i] >= nvtxs || iperm[perm[i]] != idx_t(i))
      return 7;
  }

  if (METIS_MeshToNodal(&ne, &nn, eptr.data(), eind.data(), &numflag,
      &mesh_xadj, &mesh_adjncy) != METIS_OK)
    return 8;
  if (mesh_xadj == nullptr || mesh_adjncy == nullptr || mesh_xadj[nn] != 10) {
    if (mesh_xadj != nullptr)
      METIS_Free(mesh_xadj);
    if (mesh_adjncy != nullptr)
      METIS_Free(mesh_adjncy);
    return 9;
  }
  const int xadj_status = METIS_Free(mesh_xadj);
  const int adjncy_status = METIS_Free(mesh_adjncy);
  if (xadj_status != METIS_OK || adjncy_status != METIS_OK)
    return 10;

  return 0;
}

int main()
{
  int status = test_cpp_api();
  if (status != 0) {
    std::cerr << "C++ interop check failed: " << status << '\n';
    return status;
  }
#ifdef INTEROP_WITH_FORTRAN
  status = interop_fortran_test();
  if (status != 0) {
    std::cerr << "Fortran interop check failed: " << status << '\n';
    return status;
  }
#endif
  return 0;
}
