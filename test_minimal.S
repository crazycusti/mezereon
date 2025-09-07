        .text
        .globl  _start
        .type   _start, #function

_start:
        ! Absolute minimal SPARC program
        mov     1, %g1
        ta      0                       ! trap to supervisor
        
        ! Loop forever
1:      ba      1b
        nop

        .size   _start, .-_start
