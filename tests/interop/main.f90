program metis_interop_main
  use, intrinsic :: iso_c_binding, only: c_int
  use metis_fortran_interop, only: interop_fortran_test
  implicit none

  integer(c_int) :: status

  status = interop_fortran_test()
  if (status /= 0_c_int) then
    write (*, '(a, i0)') "Fortran interop check failed: ", status
    error stop 1
  end if
end program metis_interop_main
