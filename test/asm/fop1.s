! ========================================
!  Floating ops1 tests
! ========================================
.file "fop1.s"
.section ".text"

! ----------------------------------------
#define GOOD    0x900d
#define BAD     0xbad


.global main
.align 4

main:

save    %sp, -96, %sp 

mov     0, %g7

! LDF / STF

set .FLOAT1, %l0
ld [%l0], %f0

set .FLOAT2, %l0
ld [%l0], %f1

set 0x000f0000, %g3

set 0x000f0004, %g4


st %f0, [%g3]
st %f1, [%g4]

set .FLOAT1, %l0
ld [%l0], %g1

set .FLOAT2, %l0
ld [%l0], %g2



ld [%g3], %g3
ld [%g4], %g4

cmp     %g3, %g1
bne     .LFAIL
nop

cmp     %g4, %g2
bne     .LFAIL
nop

! Check expected bytes values of some common floats
set .TEST1, %l0
ld [%l0], %f0

set 0x000f0008, %l0
st %f0, [%l0]

ld [%l0], %g4 ! Hex version of float (still in %l0 from _STORE) now in %g4

set .WTEST1, %l0
ld [%l0], %g5


cmp     %g4, %g5
bne     .LFAIL
nop

set .TEST2, %l0
ld [%l0], %f0


set 0x000f0008, %l0
st %f0, [%l0]

ld [%l0], %g4 ! Hex version of float now in %g4

set .WTEST2, %l0
ld [%l0], %g3

cmp     %g4, %g3
bne     .LFAIL
nop

set .TEST3, %l0
ld [%l0], %f0

set 0x000f0008, %l0
st %f0, [%l0]
ld [%l0], %g4 ! Hex version of float now in %g4

set .WTEST3, %l0
ld [%l0], %g3

cmp     %g4, %g3
bne     .LFAIL
nop

set .TEST4, %l0
ld [%l0], %f0

set 0x000f0008, %l0
st %f0, [%l0]
ld [%l0], %g4 ! Hex version of float now in %g4

set .WTEST4, %l0
ld [%l0], %g3

cmp     %g4, %g3
bne     .LFAIL
nop



!!!!!!!!!!!!!!!! fadd  !!!!!!!!!!!!!!!!!!!!!!!!!

set .FLOAT1, %l0
ld [%l0], %f0    

set .FLOAT2, %l0
ld [%l0], %f1

fadds %f0, %f1, %f2

set 0x000f0000, %g3

st %f2, [%g3]
ld [%g3], %g3 ! Hex version of sum now in %g3

set .ADD12, %l0
ld [%l0], %g4 ! Hex version of expected result now in %g4

cmp     %g3, %g4
bne     .LFAIL
nop

set .TEST3, %l0
ld [%l0], %f0    ! 0.25

set .TEST2, %l0
ld [%l0], %f1   ! 1.0

fadds %f0, %f0, %f2 ! 0.5 ?
fadds %f2, %f0, %f2 ! 0.75 ?
fadds %f2, %f0, %f2 ! 1.0 ?

st %f2, [%l2]
ld [%l2], %l2 ! Hex version of sum now in %l2

st %f1, [%l1]
ld [%l1], %l1 ! Expected Hex version of sum now in %l1

cmp     %l2, %l1
bne     .LFAIL
nop

!!!!!!!!!!!!!!!! fsub  !!!!!!!!!!!!!!!!!!!!!!!!!

set .FLOAT1, %l0
ld [%l0], %f0    

set .FLOAT2, %l0
ld [%l0], %f1

fsubs %f0, %f1, %f2

set 0x000f0000, %g3

st %f2, [%g3]
ld [%g3], %g3 ! Hex version of sum now in %g3

set .SUB12, %l0
ld [%l0], %g4 ! Hex version of expected result now in %g4

cmp     %g3, %g4
bne     .LFAIL
nop


!!!!!!!!!!!!!! fMULs !!!!!!!!!!!!!!!


set .TEST3, %l0
ld [%l0], %f0    

set .TEST5, %l0
ld [%l0], %f1

fmuls %f0, %f1, %f2 ! 4.0 * 0.25 = 1.0

set 0x000f0000, %g3

st %f2, [%g3]
ld [%g3], %g3 ! Hex version of product now in %g3

set .TEST2, %l0
ld [%l0], %g4 ! Hex version of expected result now in %g4

cmp     %g3, %g4
bne     .LFAIL
nop

set .TEST6, %l0
ld [%l0], %f0    

set .TEST6, %l0
ld [%l0], %f1

fmuls %f0, %f1, %f2 ! 2.0 * 2.0 = 4.0

set 0x000f0000, %g3

st %f2, [%g3]
ld [%g3], %g3 ! Hex version of product now in %g3

set .TEST5, %l0
ld [%l0], %g4 ! Hex version of expected result now in %g4

cmp     %g3, %g4
bne     .LFAIL
nop

!!!!!!!!!!!!!! fMULs !!!!!!!!!!!!!!!


set .TEST2, %l0
ld [%l0], %f0    

set .TEST3, %l0
ld [%l0], %f1

fdivs %f0, %f1, %f2 ! 1.0 / 0.25 = 4.0

set 0x000f0000, %g3

st %f2, [%g3]
ld [%g3], %g3 ! Hex version of div now in %g3

set .TEST5, %l0
ld [%l0], %g4 ! Hex version of expected result now in %g4

cmp     %g3, %g4
bne     .LFAIL
nop

set .TEST6, %l0
ld [%l0], %f0    

set .TEST6, %l0
ld [%l0], %f1

fdivs %f0, %f1, %f2 ! 2.0 / 2.0 = 1.0

set 0x000f0000, %g3

st %f2, [%g3]
ld [%g3], %g3 ! Hex version of div now in %g3

set .TEST2, %l0
ld [%l0], %g4 ! Hex version of expected result now in %g4

cmp     %g3, %g4
bne     .LFAIL
nop

! Load test values
set     0x3f800000, %g1        ! 1.0f in IEEE-754
set     0xbf800000, %g2        ! -1.0f
set     0x40000000, %g3        ! 2.0f
set     0x3fc00000, %g4        ! 1.5f
set     0x3f000000, %g5        ! 0.5f

st      %g1, [%sp - 4]
ld     [%sp - 4], %f0          ! f0 = 1.0
st      %g2, [%sp - 8]
ld     [%sp - 8], %f1          ! f1 = -1.0
st      %g3, [%sp - 12]
ld     [%sp - 12], %f2         ! f2 = 2.0

set     epsilon, %l6
ld     [%l6], %f30           ! f30 = epsilon

! Test fabs: fabs(-1.0) == 1.0
fabss    %f1, %f3
fcmps   %f3, %f0
fbne    .LFAIL
nop

! Test fneg: fneg(1.0) == -1.0
fnegs    %f0, %f4
fcmps   %f4, %f1
fbne    .LFAIL
nop

! Test fmov: fmov(1.0) == 1.0
fmovs   %f0, %f5
fcmps   %f5, %f0
fbne    .LFAIL
nop

! Test fsqrt: sqrt(1.0) == 1.0
fsqrts  %f0, %f6
fcmps   %f6, %f0
fbne    .LFAIL
nop

! Test fsqrt: sqrt(2.0) ~= 1.4142… so we square it and compare to 2.0
fsqrts  %f2, %f7
fmuls   %f7, %f7, %f8
fmovs   %f8, %f10  ! Result
fmovs   %f2, %f11  ! Expected
call check_close
nop

ba  .LPASS
nop


! -----------------------------------------------------
! Subroutine: check_close
! Inputs:
!   %f10 = result
!   %f11 = expected
!   %f30 = epsilon
! Compares abs(result - expected) < epsilon
! -----------------------------------------------------
check_close:
    fsubs   %f10, %f11, %f12
    fabss    %f12, %f12
    fcmps   %f12, %f30
    fbl    .RET_OK
    nop
    ba      .LFAIL
    nop
.RET_OK:
    retl
    nop

.LPASS:
set GOOD, %g7
ba      .LFINISH
nop

.LFAIL:
mov     BAD, %g7
.LFINISH:
ret
restore 

.type   main,#function
.size   main,(.-main)

! ----------------------------------------
.section ".data"
.align 4               
.FLOAT1: .single 3.14
.FLOAT2: .single 1.01
.ADD12: .single 4.15
.SUB12: .single 2.13
.TEST1: .single 12.375
.TEST2: .single 1.0
.TEST3: .single 0.25
.TEST4: .single 0.375
.TEST5: .single 4.0
.TEST6: .single 2.0
.WTEST1: .word 0x41460000
.WTEST2: .word 0x3f800000
.WTEST3: .word 0x3e800000
.WTEST4: .word 0x3ec00000
float_1:        .single 1.0
float_n1:       .single -1.0
float_2:        .single 2.0
float_05:       .single 0.5
float_15:       .single 1.5
float_3:        .single 3.0
epsilon:        .single 0.0001
