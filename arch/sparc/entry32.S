! SPARC V8 entry skeleton (sun4m), not built yet.
! Demonstrates calling the common C entry kentry(boot_info*).

! .globl _start
! .extern kentry
! _start:
!     ! %o0..%o5 may carry OBP client interface or args depending on loader
!     set 0, %o0         ! boot_info* = NULL (placeholder)
!     call kentry
!     nop
! 1:  ba 1b
!     nop

