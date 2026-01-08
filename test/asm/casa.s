!
! casa.s — SPARC V8 32-bit CASA test
!
! Expectations:
!  - Runs in 32-bit address space.
!  - Uses ASI_PRIMARY (0x80) for normal memory.
!  - Branches to .LFAIL on any mismatch, .LPASS on success.
!
! Notes:
!  - CASA operates on 32-bit words and requires aligned address.
!  - CASA returns the old memory value in the destination register.
!
        .file "casa.s"
        .section ".text"

! ----------------------------------------
#define GOOD    0x900d
#define BAD     0xbad

#define NUM1    0xfedcba98
#define NUM2    0x76543210
#define NUM3    0x55555555
#define NUM4    0xAAAAAAAA

#define CC_MASK  0x00f00000
#define ZV_MASK1 0x00400000

        .global main
        .align 4

main:

        save    %sp, -96, %sp 

        mov     0, %g7

        !sethi   %hi(NUM1), %l1
        !or      %l1, %lo(NUM1), %l1
        !sethi   %hi(NUM1), %l3
        !or      %l3, %lo(NUM1), %l3
        !sethi   %hi(NUM2), %l2
        !or      %l2, %lo(NUM2), %l2
        !sethi   %hi(.LTESTDATA), %l0
        !or      %l0, %lo(.LTESTDATA), %l0

        ! %l0 = address of test word (32-bit aligned)
        set     .LTESTDATA, %l0

        ! --------------------------------------------
        ! Test 1: Successful CAS
        !   mem starts = 0x11111111
        !   expected   = 0x11111111
        !   new        = 0x22222222
        ! After CASA:
        !   return(old) == 0x11111111
        !   mem         == 0x22222222
        ! --------------------------------------------
        set     0x11111111, %l1
        st      %l1, [%l0]

        set     0x11111111, %l2      ! expected
        set     0x22222222, %l3      ! new
        stbar
        ! Test with ASI 0xb (super)
        casa    [%l0] 0xb, %l2, %l3 ! if mem==%l2 then mem=%l3; %l3=old

        ! Check returned old value == 0x11111111
        set     0x11111111, %l4
        cmp     %l3, %l4
        bne     .LFAIL
        nop

        ld      [%l0], %l5           ! verify memory updated
        set     0x22222222, %l6
        cmp     %l5, %l6
        bne     .LFAIL
        nop
        
        ! Set up new test
        set     0x11111111, %l1
        st      %l1, [%l0]

        set     0x11111111, %l2      ! expected
        set     0x22222222, %l3      ! new
        stbar
        ! Test with ASI 0xA (User)
        casa    [%l0] 0xA, %l2, %l3 ! if mem==%l2 then mem=%l3; %l3=old

        ! Check returned old value == 0x11111111
        set     0x11111111, %l4
        cmp     %l3, %l4
        bne     .LFAIL
        nop

        ld      [%l0], %l5           ! verify memory updated
        set     0x22222222, %l6
        cmp     %l5, %l6
        bne     .LFAIL
        nop

        ! --------------------------------------------
        ! Test 2: Failing CAS (no swap)
        !   mem starts = 0x33333333
        !   expected   = 0x44444444 (wrong)
        !   new        = 0x55555555
        ! After CASA:
        !   return(old) == 0x33333333
        !   mem         == 0x33333333 (unchanged)
        ! --------------------------------------------
        set     0x33333333, %l1
        st      %l1, [%l0]
        !membar  #Sync

        set     0x44444444, %l2      ! expected (wrong)
        set     0x55555555, %l3      ! new
        stbar
        casa    [%l0] 0xb, %l2, %l3 ! should NOT swap; %l3=old

        ! Check returned old value == 0x33333333
        set     0x33333333, %l4
        cmp     %l3, %l4
        bne     .LFAIL
        nop
        nop
        nop

        ld      [%l0], %l5           ! verify memory unchanged
        set     0x33333333, %l6
        cmp     %l5, %l6
        bne     .LFAIL
        nop

        ! Set up for new test with other ASI
        set     0x33333333, %l1
        st      %l1, [%l0]
        !membar  #Sync

        set     0x44444444, %l2      ! expected (wrong)
        set     0x55555555, %l3      ! new
        stbar
        casa    [%l0] 0xA, %l2, %l3 ! should NOT swap; %l3=old

        ! Check returned old value == 0x33333333
        set     0x33333333, %l4
        cmp     %l3, %l4
        bne     .LFAIL
        nop
        nop
        nop
        
        ld      [%l0], %l5           ! verify memory unchanged
        set     0x33333333, %l6
        cmp     %l5, %l6
        bne     .LFAIL
        nop

        ba      .LPASS
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
        .section ".data"
        .align  4

.LTESTDATA:
        .word 0x76543210
.LTESTDATA1:
        .word 0xAAAAAAAA

