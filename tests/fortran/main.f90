program metis_fortran_consumer
  use, intrinsic :: iso_c_binding, only: c_int
  implicit none

  interface
    integer(c_int) function metis_setdefaultoptions(options) &
        bind(C, name="metis_setdefaultoptions_")
      import c_int
      integer(c_int) :: options(*)
    end function metis_setdefaultoptions

    integer(c_int) function metis_nodend(n, xadj, adjncy, vwgt, options, &
        perm, iperm) bind(C, name="metis_nodend_")
      import c_int
      integer(c_int) :: n
      integer(c_int) :: xadj(*)
      integer(c_int) :: adjncy(*)
      integer(c_int) :: vwgt(*)
      integer(c_int) :: options(*)
      integer(c_int) :: perm(*)
      integer(c_int) :: iperm(*)
    end function metis_nodend
  end interface

  integer(c_int) :: n, status, i
  integer(c_int) :: xadj(4), adjncy(6), vwgt(3)
  integer(c_int) :: options(40), perm(3), iperm(3)

  n = 3
  xadj = [0, 2, 4, 6]
  adjncy = [1, 2, 0, 2, 0, 1]
  vwgt = 1
  status = metis_setdefaultoptions(options)
  if (status /= 1) error stop 1
  status = metis_nodend(n, xadj, adjncy, vwgt, options, perm, iperm)
  if (status /= 1) error stop 2
  do i = 1, n
    if (perm(i) < 0 .or. perm(i) >= n) error stop 3
    if (iperm(perm(i)+1) /= i-1) error stop 4
  end do
end program metis_fortran_consumer
