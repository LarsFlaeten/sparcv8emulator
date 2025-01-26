! ========================================
!  $Id: bicc.s,v 1.1 2013/06/25 18:30:59 simon Exp $
!  $Source: /home/simon/CVS/src/cpu/sparc/test/bicc.s,v $
! ========================================
        .file "fld.s"
        .section ".text"

! ----------------------------------------
#define GOOD    0x900d
#define BAD     0xbad


#define SetFCC(_label,_R1) \
        sethi   %hi(_label), _R1; \
        or      _R1, %lo(_label), _R1; \
        ld      [_R1], %fsr
        
        .global main
        .align 4

#define Set(_label, _R1) \
        sethi   %hi(_label), _R1; \
        or      _R1, %lo(_label), _R1
        

main:

        save    %sp, -96, %sp 
        mov     0, %g7

        !sethi   %hi(.LFSMALL), %l0
        !or      %l0, %lo(.LFSMALL), %l0

        !sethi   %hi(.LFBIG), %l1
        !or      %l1, %lo(.LFBIG), %l1
        Set(.LFSMALL, %l0) 
        Set(.LFBIG, %l1)

        ld      [%l0], %f1
        ld      [%l1], %f2
        
         


        mov     0, %g6
        fba      .LBRANCH1
        inc     %g6

.LRETURN1:
        cmp     %g6,2
        bne     .LFAIL
        nop


.LPASS:
        sethi   %hi(GOOD), %g7
        or      %g7, %lo(GOOD), %g7
        ba      .LFINISH
        nop

.LFAIL:
        mov     BAD, %g7
.LFINISH:
        ret
        restore 

.LBRANCH1:
        ba      .LRETURN1
        inc     %g6


        .type   main, #function
        .size   main, (.-main)
.LTESTDATA_E:
        .word 0x00000000
.LTESTDATA_L:
        .word 0x00000400
.LTESTDATA_G:
        .word 0x00000800
.LTESTDATA_U:
        .word 0x00000C00
.LFSMALL:
        .single 0r1.2001
.LFBIG:
        .single 0r10034.67
.LFZERO:
        .single 0r0.0
