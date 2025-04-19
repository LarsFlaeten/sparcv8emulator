! ========================================
! Code to check that bytes are stored correct
! even though the host may have different
! endianess
! ========================================
        .file "endian.s"
        .section ".text"

! ----------------------------------------
#define GOOD    0x900d
#define BAD     0xbad

#define VALUE8   0x12345678
#define RESULT1  0x00000012
#define RESULT2  0x00000034
#define RESULT3  0x00000056
#define RESULT4  0x00000078

#define CC_MASK  0x00f00000
#define ZV_MASK1 0x00400000

        .global main
        .align 4

main:

        save    %sp, -96, %sp 

        mov     0, %g7

        !!!!!!! ST !!!!!!!!

        sethi   %hi(VALUE8), %l1
        or      %l1, %lo(VALUE8), %l1
        sethi   %hi(.LTESTDATA), %l0
        or      %l0, %lo(.LTESTDATA), %l0
        mov     0, %l2
        flush   %l0

        st      %l1, [%l0]

        ! Check result
        ld      [%l0], %l2
        cmp     %l1, %l2
        bne     .LFAIL
        stbar


        ! Read single bytes from .LTESTDATA in the order exoected by an big endian byte order

        sethi   %hi(RESULT1), %l1
        or      %l1, %lo(RESULT1), %l1
        sethi   %hi(.LTESTDATA), %l0
        or      %l0, %lo(.LTESTDATA), %l0
        mov     0, %l2

        ldub    [%l0], %l2

        cmp     %l1, %l2
        bne     .LFAIL
        nop

        sethi   %hi(RESULT2), %l1
        or      %l1, %lo(RESULT2), %l1
        sethi   %hi(.LTESTDATA), %l0
        or      %l0, %lo(.LTESTDATA), %l0
        add     %l0, 1, %l0 
        mov     0, %l2

        ldub    [%l0], %l2

        cmp     %l1, %l2
        bne     .LFAIL
        nop


        sethi   %hi(RESULT3), %l1
        or      %l1, %lo(RESULT3), %l1
        sethi   %hi(.LTESTDATA), %l0
        or      %l0, %lo(.LTESTDATA), %l0
        add     %l0, 2, %l0 
        mov     0, %l2

        ldub    [%l0], %l2

        cmp     %l1, %l2
        bne     .LFAIL
        nop


        sethi   %hi(RESULT4), %l1
        or      %l1, %lo(RESULT4), %l1
        sethi   %hi(.LTESTDATA), %l0
        or      %l0, %lo(.LTESTDATA), %l0
        add     %l0, 3, %l0 
        mov     0, %l2

        ldub    [%l0], %l2

        cmp     %l1, %l2
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

        .type   main, #function
        .size   main, (.-main)

! ----------------------------------------
        .section ".data", #write
        .align  8

.LTESTDATA:
        .word 0x00000000
        .word 0x00000000

