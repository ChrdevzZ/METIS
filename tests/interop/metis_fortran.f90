module metis_fortran_interop
  use, intrinsic :: iso_c_binding, only: c_associated, c_f_pointer, c_int, &
      c_null_ptr, c_ptr
  use metis_kinds, only: metis_idx_kind, metis_idx_width, metis_real_kind, &
      metis_real_width
  implicit none
  private

  public :: interop_fortran_test

  interface
    integer(c_int) function metis_set_default_options(options) &
        bind(C, name="METIS_SetDefaultOptions")
      import :: c_int, metis_idx_kind
      integer(metis_idx_kind) :: options(*)
    end function metis_set_default_options

    integer(c_int) function metis_legacy_options(options) &
        bind(C, name="metis_setdefaultoptions_")
      import :: c_int, metis_idx_kind
      integer(metis_idx_kind) :: options(*)
    end function metis_legacy_options

    integer(c_int) function metis_part_graph_kway(nvtxs, ncon, xadj, &
        adjncy, vwgt, vsize, adjwgt, nparts, tpwgts, ubvec, options, &
        edgecut, part) bind(C, name="METIS_PartGraphKway")
      import :: c_int, metis_idx_kind, metis_real_kind
      integer(metis_idx_kind) :: nvtxs, ncon, nparts, edgecut
      integer(metis_idx_kind) :: xadj(*), adjncy(*), vwgt(*), vsize(*)
      integer(metis_idx_kind) :: adjwgt(*), options(*), part(*)
      real(metis_real_kind) :: tpwgts(*), ubvec(*)
    end function metis_part_graph_kway

    integer(c_int) function metis_mesh_to_nodal(ne, nn, eptr, eind, &
        numflag, mesh_xadj, mesh_adjncy) bind(C, name="METIS_MeshToNodal")
      import :: c_int, c_ptr, metis_idx_kind
      integer(metis_idx_kind) :: ne, nn, numflag
      integer(metis_idx_kind) :: eptr(*), eind(*)
      type(c_ptr) :: mesh_xadj, mesh_adjncy
    end function metis_mesh_to_nodal

    integer(c_int) function metis_node_nd(nvtxs, xadj, adjncy, vwgt, &
        options, perm, iperm) bind(C, name="METIS_NodeND")
      import :: c_int, metis_idx_kind
      integer(metis_idx_kind) :: nvtxs
      integer(metis_idx_kind) :: xadj(*), adjncy(*), vwgt(*)
      integer(metis_idx_kind) :: options(*), perm(*), iperm(*)
    end function metis_node_nd

    integer(c_int) function metis_legacy_node_nd(nvtxs, xadj, adjncy, &
        vwgt, options, perm, iperm) bind(C, name="metis_nodend_")
      import :: c_int, metis_idx_kind
      integer(metis_idx_kind) :: nvtxs
      integer(metis_idx_kind) :: xadj(*), adjncy(*), vwgt(*)
      integer(metis_idx_kind) :: options(*), perm(*), iperm(*)
    end function metis_legacy_node_nd

    integer(c_int) function metis_free(memory) bind(C, name="METIS_Free")
      import :: c_int, c_ptr
      type(c_ptr), value :: memory
    end function metis_free
  end interface

contains

  integer(c_int) function interop_fortran_test() &
      bind(C, name="interop_fortran_test")
    integer(metis_idx_kind) :: nvtxs, ncon, nparts, edgecut, i
    integer(metis_idx_kind) :: xadj(5), adjncy(8), vwgt(4), vsize(4)
    integer(metis_idx_kind) :: adjwgt(8), part(4), partition_weights(2)
    integer(metis_idx_kind) :: perm(4), iperm(4), options(40)
    real(metis_real_kind) :: tpwgts(2), ubvec(1)
    integer(metis_idx_kind) :: ne, nn, numflag
    integer(metis_idx_kind) :: eptr(3), eind(6)
    integer(metis_idx_kind), pointer :: mesh_xadj_values(:)
    type(c_ptr) :: mesh_xadj, mesh_adjncy
    integer(c_int) :: status, xadj_status, adjncy_status, wrapper

    interop_fortran_test = 0_c_int
    if (storage_size(nvtxs) /= metis_idx_width .or. &
        storage_size(tpwgts(1)) /= metis_real_width) then
      interop_fortran_test = 11_c_int
      return
    end if

    nvtxs = 4
    ncon = 1
    nparts = 2
    xadj = [0, 2, 4, 6, 8]
    adjncy = [1, 3, 0, 2, 1, 3, 0, 2]
    vwgt = 1
    vsize = 1
    adjwgt = 1
    tpwgts = [0.25_metis_real_kind, 0.75_metis_real_kind]
    ubvec = [1.05_metis_real_kind]

    status = metis_set_default_options(options)
    if (status /= 1_c_int) then
      interop_fortran_test = 12_c_int
      return
    end if
    status = metis_part_graph_kway(nvtxs, ncon, xadj, adjncy, vwgt, &
        vsize, adjwgt, nparts, tpwgts, ubvec, options, edgecut, part)
    if (status /= 1_c_int) then
      interop_fortran_test = 13_c_int
      return
    end if
    partition_weights = 0
    do i = 1, nvtxs
      if (part(i) < 0 .or. part(i) >= nparts) then
        interop_fortran_test = 14_c_int
        return
      end if
      partition_weights(part(i) + 1) = &
          partition_weights(part(i) + 1) + vwgt(i)
    end do
    if (any(partition_weights /= [1, 3])) then
      interop_fortran_test = 15_c_int
      return
    end if

    ! Exercise the C entry points and the original underscore wrappers with
    ! the installed index kind, including 64-bit packages.
    do wrapper = 0, 1
      if (wrapper == 0) then
        status = metis_set_default_options(options)
      else
        status = metis_legacy_options(options)
      end if
      if (status /= 1_c_int) then
        interop_fortran_test = 12_c_int
        return
      end if
      if (wrapper == 0) then
        status = metis_node_nd(nvtxs, xadj, adjncy, vwgt, options, perm, iperm)
      else
        status = metis_legacy_node_nd(nvtxs, xadj, adjncy, vwgt, &
            options, perm, iperm)
      end if
      if (status /= 1_c_int) then
        interop_fortran_test = 16_c_int
        return
      end if
      do i = 1, nvtxs
        ! Fortran does not require short-circuit evaluation of .or.; validate
        ! the range before using a returned permutation as an array index.
        if (perm(i) < 0 .or. perm(i) >= nvtxs) then
          interop_fortran_test = 17_c_int
          return
        end if
        if (iperm(perm(i) + 1) /= i - 1) then
          interop_fortran_test = 17_c_int
          return
        end if
      end do
    end do

    ne = 2
    nn = 4
    numflag = 0
    eptr = [0, 3, 6]
    eind = [0, 1, 2, 1, 2, 3]
    mesh_xadj = c_null_ptr
    mesh_adjncy = c_null_ptr
    status = metis_mesh_to_nodal(ne, nn, eptr, eind, numflag, &
        mesh_xadj, mesh_adjncy)
    if (status /= 1_c_int) then
      interop_fortran_test = 18_c_int
      return
    end if
    if (.not. c_associated(mesh_xadj) .or. &
        .not. c_associated(mesh_adjncy)) then
      if (c_associated(mesh_xadj)) status = metis_free(mesh_xadj)
      if (c_associated(mesh_adjncy)) status = metis_free(mesh_adjncy)
      interop_fortran_test = 19_c_int
      return
    end if

    call c_f_pointer(mesh_xadj, mesh_xadj_values, [int(nn + 1)])
    if (mesh_xadj_values(1) /= 0 .or. mesh_xadj_values(nn + 1) /= 10) &
      interop_fortran_test = 20_c_int
    xadj_status = metis_free(mesh_xadj)
    adjncy_status = metis_free(mesh_adjncy)
    if (xadj_status /= 1_c_int .or. adjncy_status /= 1_c_int) &
      interop_fortran_test = 21_c_int
  end function interop_fortran_test
end module metis_fortran_interop
