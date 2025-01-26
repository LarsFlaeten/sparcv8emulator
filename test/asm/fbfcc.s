! ========================================
!  $Id: bicc.s,v 1.1 2013/06/25 18:30:59 simon Exp $
!  $Source: /home/simon/CVS/src/cpu/sparc/test/bicc.s,v $
! ========================================
        .file "bicc.s"
        .section ".text"

! ----------------------------------------
#define GOOD    0x900d
#define BAD     0xbad

#define CC_MASK  0x00f00000
#define ZV_MASK1 0x00400000

#define MOVW(_NUM, _R1) \
        sethi   %hi(_NUM), _R1; \
        or      _R1, %lo(_NUM), _R1;

#define SetCC(_CC,_R1,_R2,_R3) \
        sethi   %hi(CC_MASK), _R1; \
        or      _R1, %lo(CC_MASK), _R1; \
        sethi   %hi(_CC<<20), _R2; \
        or      _R2, %lo(_CC<<20), _R2; \
        rd      %psr, _R3; \
        andn    _R3, _R1, _R3; \
        or      _R3, _R2, _R3; \
        wr      _R3, %psr
#define SetFCC(_label,_R1) \
        sethi   %hi(_label), _R1; \
        or      _R1, %lo(_label), _R1; \
        ld      [_R1], %fsr
        
        .global main
        .align 4

main:

        save    %sp, -96, %sp 
        mov     0, %g7

        !!!!!!! fBA !!!!!!!
        mov     0, %g6
        fba      .LBRANCH1
        inc     %g6

.LRETURN1:
        cmp     %g6,2
        bne     .LFAIL
        nop

        mov     0, %g6
        fba,a    .LBRANCH2
        inc     %g6
.LRETURN2:
        cmp     %g6,1
        bne     .LFAIL
        nop


        !!!!!!! fBN !!!!!!!
        mov     0, %g6
        fbn      .LBRANCH1
        inc     %g6

        cmp     %g6, 1
        bne     .LFAIL
        nop

        fbn,a    .LBRANCH1
        inc     %g6

        cmp     %g6, 1
        bne     .LFAIL
        nop

        !!!!!!! FBU !!!!!!!
        mov     0, %g6
        SetFCC  (.LTESTDATA_E, %l5)  
        fbu     .LFAIL
        add     %g6, 10, %g6

        cmp     %g6, 10
        bne     .LFAIL
        nop

        mov     0, %g6
        SetFCC  (.LTESTDATA_E, %l5)  

        fbu,a   .LFAIL
        add     %g6, 10, %g6

        cmp     %g6, 0
        bne     .LFAIL
        nop

        mov     0, %g6
        SetFCC  (.LTESTDATA_U, %l5)  

        fbu     .LBRANCH3
        add     %g6, 10, %g6

.LRETURN3:
        cmp     %g6, 11
        bne     .LFAIL
        nop

        mov     0, %g6
        SetFCC  (.LTESTDATA_U, %l5)  

        fbu,a   .LBRANCH4
        add     %g6, 10, %g6

.LRETURN4:
        cmp     %g6, 11
        bne     .LFAIL
        nop

        !!!!!!! FBG !!!!!!!
        mov     0, %g6
        SetFCC  (.LTESTDATA_L, %l5)  

        fbg      .LFAIL
        add     %g6, 10, %g6

        cmp     %g6, 10
        bne     .LFAIL
        nop

        mov     0, %g6
        SetFCC  (.LTESTDATA_E, %l5)  

        fbg,a    .LFAIL
        add     %g6, 10, %g6

        cmp     %g6, 0
        bne     .LFAIL
        nop

        mov     0, %g6
        SetFCC  (.LTESTDATA_G, %l5)  

        fbg      .LBRANCH5
        add     %g6, 10, %g6

.LRETURN5:
        cmp     %g6, 11
        bne     .LFAIL
        nop

        mov     0, %g6
        SetFCC  (.LTESTDATA_G, %l5)  

        fbg,a    .LBRANCH6
        add     %g6, 10, %g6

.LRETURN6:
        cmp     %g6, 11
        bne     .LFAIL
        nop

        !!!!!!! FBUG !!!!!!!
        mov     0, %g6
        SetFCC  (.LTESTDATA_E, %l5)  

        fbug    .LFAIL
        add     %g6, 10, %g6

        cmp     %g6, 10
        bne     .LFAIL
        nop

        mov     0, %g6
        SetFCC  (.LTESTDATA_L, %l5)  

        fbug,a  .LFAIL
        add     %g6, 10, %g6

        cmp     %g6, 0
        bne     .LFAIL
        nop

        mov     0, %g6
        SetFCC  (.LTESTDATA_G, %l5)  

        fbug    .LBRANCH7
        add     %g6, 10, %g6

.LRETURN7:
        cmp     %g6, 11
        bne     .LFAIL
        nop

        mov     0, %g6
        SetFCC  (.LTESTDATA_U, %l5)  

        fbug,a   .LBRANCH8
        add     %g6, 10, %g6

.LRETURN8:
        cmp     %g6, 11
        bne     .LFAIL
        nop

        !!!!!!! FBL !!!!!!!
        mov     0, %g6
        SetFCC  (.LTESTDATA_U, %l5)  

        fbl     .LFAIL
        add     %g6, 10, %g6

        cmp     %g6, 10
        bne     .LFAIL
        nop

        mov     0, %g6
        SetFCC  (.LTESTDATA_G, %l5)  

        fbl,a   .LFAIL
        add     %g6, 10, %g6

        cmp     %g6, 0
        bne     .LFAIL
        nop

        mov     0, %g6
        SetFCC  (.LTESTDATA_L, %l5)  

        fbl     .LBRANCH9
        add     %g6, 10, %g6

.LRETURN9:
        cmp     %g6, 11
        bne     .LFAIL
        nop

        mov     0, %g6
        SetFCC  (.LTESTDATA_L, %l5)  

        fbl,a   .LBRANCH10
        add     %g6, 10, %g6

.LRETURN10:
        cmp     %g6, 11
        bne     .LFAIL
        nop

        !!!!!!! FBUL !!!!!!!
        mov     0, %g6
        SetFCC  (.LTESTDATA_E, %l5)  

        fbul     .LFAIL
        add     %g6, 10, %g6

        cmp     %g6, 10
        bne     .LFAIL
        nop

        mov     0, %g6
        SetFCC  (.LTESTDATA_G, %l5)  

        fbul,a   .LFAIL
        add     %g6, 10, %g6

        cmp     %g6, 0
        bne     .LFAIL
        nop

        mov     0, %g6
        SetFCC  (.LTESTDATA_U, %l5)  

        fbul     .LBRANCH11
        add     %g6, 10, %g6

.LRETURN11:
        cmp     %g6, 11
        bne     .LFAIL
        nop

        mov     0, %g6
        SetFCC  (.LTESTDATA_L, %l5)  

        fbul,a   .LBRANCH12
        add     %g6, 10, %g6

.LRETURN12:
        cmp     %g6, 11
        bne     .LFAIL
        nop

        !!!!!!! FBLG !!!!!!!
        mov     0, %g6
        SetFCC  (.LTESTDATA_E, %l5)  

        fblg     .LFAIL
        add     %g6, 10, %g6

        cmp     %g6, 10
        bne     .LFAIL
        nop

        mov     0, %g6
        SetFCC  (.LTESTDATA_U, %l5)  

        fblg,a   .LFAIL
        add     %g6, 10, %g6

        cmp     %g6, 0
        bne     .LFAIL
        nop

        mov     0, %g6
        SetFCC  (.LTESTDATA_L, %l5)  

        fblg     .LBRANCH13
        add     %g6, 10, %g6

.LRETURN13:
        cmp     %g6, 11
        bne     .LFAIL
        nop

        mov     0, %g6
        SetFCC  (.LTESTDATA_G, %l5)  

        fblg,a   .LBRANCH14
        add     %g6, 10, %g6

.LRETURN14:
        cmp     %g6, 11
        bne     .LFAIL
        nop

        !!!!!!! FBNE !!!!!!!
        mov     0, %g6
        SetFCC  (.LTESTDATA_E, %l5)  

        fbne    .LFAIL
        add     %g6, 10, %g6

        cmp     %g6, 10
        bne     .LFAIL
        nop

        mov     0, %g6
        SetFCC  (.LTESTDATA_E, %l5)  

        fbne,a  .LFAIL
        add     %g6, 10, %g6

        cmp     %g6, 0
        bne     .LFAIL
        nop

        mov     0, %g6
        SetFCC  (.LTESTDATA_L, %l5)  

        fbne    .LBRANCH15
        add     %g6, 10, %g6

.LRETURN15:
        cmp     %g6, 11
        bne     .LFAIL
        nop

        mov     0, %g6
        SetFCC  (.LTESTDATA_G, %l5)  

        fbne,a  .LTEMP1
        add     %g6, 10, %g6
        add     %g6, 10, %g6


.LTEMP1:
        SetFCC  (.LTESTDATA_U, %l5)  

        fbne,a  .LBRANCH16
        add     %g6, 10, %g6




.LRETURN16:
        cmp     %g6, 21
        bne     .LFAIL
        nop

        !!!!!!! FBE !!!!!!!
        mov     0, %g6
        SetFCC  (.LTESTDATA_U, %l5)  

        fbe     .LFAIL
        add     %g6, 10, %g6

        cmp     %g6, 10
        bne     .LFAIL
        nop

        mov     0, %g6
        SetFCC  (.LTESTDATA_L, %l5)  

        fbe,a  .LFAIL
        add     %g6, 10, %g6

        cmp     %g6, 0
        bne     .LFAIL
        nop

        mov     0, %g6
        SetFCC  (.LTESTDATA_E, %l5)  

        fbe     .LBRANCH17
        add     %g6, 10, %g6

.LRETURN17:
        cmp     %g6, 11
        bne     .LFAIL
        nop

        mov     0, %g6
        SetFCC  (.LTESTDATA_E, %l5)  

        fbe,a  .LBRANCH18
        add     %g6, 10, %g6

.LRETURN18:
        cmp     %g6, 11
        bne     .LFAIL
        nop

        !!!!!!! FBUE !!!!!!!
        mov     0, %g6
        SetFCC  (.LTESTDATA_L, %l5)  

        fbue    .LFAIL
        add     %g6, 10, %g6

        cmp     %g6, 10
        bne     .LFAIL
        nop

        mov     0, %g6
        SetFCC  (.LTESTDATA_G, %l5)  

        fbue,a  .LFAIL
        add     %g6, 10, %g6

        cmp     %g6, 0
        bne     .LFAIL
        nop

        mov     0, %g6
        SetFCC  (.LTESTDATA_U, %l5)  

        fbue    .LBRANCH19
        add     %g6, 10, %g6

.LRETURN19:
        cmp     %g6, 11
        bne     .LFAIL
        nop

        mov     0, %g6
        SetFCC  (.LTESTDATA_E, %l5)  

        fbue,a  .LBRANCH20
        add     %g6, 10, %g6

.LRETURN20:
        cmp     %g6, 11
        bne     .LFAIL
        nop

        !!!!!!! FBGE !!!!!!!
        mov     0, %g6
        SetFCC  (.LTESTDATA_L, %l5)  

        fbge     .LFAIL
        add     %g6, 10, %g6

        cmp     %g6, 10
        bne     .LFAIL
        nop

        mov     0, %g6
        SetFCC  (.LTESTDATA_U, %l5)  

        fbge,a  .LFAIL
        add     %g6, 10, %g6

        cmp     %g6, 0
        bne     .LFAIL
        nop

        mov     0, %g6
        SetFCC  (.LTESTDATA_G, %l5)  

        fbge    .LBRANCH21
        add     %g6, 10, %g6

.LRETURN21:
        cmp     %g6, 11
        bne     .LFAIL
        nop

        mov     0, %g6
        SetFCC  (.LTESTDATA_E, %l5)  

        fbge,a  .LBRANCH22
        add     %g6, 10, %g6

.LRETURN22:
        cmp     %g6, 11
        bne     .LFAIL
        nop

        !!!!!!! FBUGE !!!!!!!
        mov     0, %g6
        SetFCC  (.LTESTDATA_L, %l5)  

        fbuge   .LFAIL
        add     %g6, 10, %g6

        cmp     %g6, 10
        bne     .LFAIL
        nop

        mov     0, %g6
        SetFCC  (.LTESTDATA_L, %l5)  

        fbuge,a .LFAIL
        add     %g6, 10, %g6

        cmp     %g6, 0
        bne     .LFAIL
        nop

        mov     0, %g6
        SetFCC  (.LTESTDATA_U, %l5)  

        fbuge   .LBRANCH23
        add     %g6, 10, %g6

.LRETURN23:
        cmp     %g6, 11
        bne     .LFAIL
        nop

        mov     0, %g6
        SetFCC  (.LTESTDATA_G, %l5)  

        fbuge,a .LTEMP2
        add     %g6, 10, %g6
        add     %g6, 10, %g6
.LTEMP2:
        SetFCC  (.LTESTDATA_E, %l5)  

        fbuge,a .LBRANCH24
        add     %g6, 10, %g6

.LRETURN24:
        cmp     %g6, 21
        bne     .LFAIL
        nop

        !!!!!!! FBLE !!!!!!!
        mov     0, %g6
        SetFCC  (.LTESTDATA_G, %l5)  

        fble    .LFAIL
        add     %g6, 10, %g6

        cmp     %g6, 10
        bne     .LFAIL
        nop

        mov     0, %g6
        SetFCC  (.LTESTDATA_U, %l5)  

        fble,a  .LFAIL
        add     %g6, 10, %g6

        cmp     %g6, 0
        bne     .LFAIL
        nop

        mov     0, %g6
        SetFCC  (.LTESTDATA_L, %l5)  

        fble    .LBRANCH25
        add     %g6, 10, %g6

.LRETURN25:
        cmp     %g6, 11
        bne     .LFAIL
        nop

        mov     0, %g6
        SetFCC  (.LTESTDATA_E, %l5)  

        fble,a  .LBRANCH26
        add     %g6, 10, %g6

.LRETURN26:
        cmp     %g6, 11
        bne     .LFAIL
        nop

        !!!!!!! FBULE !!!!!!!
        mov     0, %g6
        SetFCC  (.LTESTDATA_G, %l5)  

        fbule   .LFAIL
        add     %g6, 10, %g6

        cmp     %g6, 10
        bne     .LFAIL
        nop

        mov     0, %g6
        SetFCC  (.LTESTDATA_G, %l5)  

        fbule,a .LFAIL
        add     %g6, 10, %g6

        cmp     %g6, 0
        bne     .LFAIL
        nop

        mov     0, %g6
        SetFCC  (.LTESTDATA_U, %l5)  

        fbule   .LBRANCH27
        add     %g6, 10, %g6

.LRETURN27:
        cmp     %g6, 11
        bne     .LFAIL
        nop

        mov     0, %g6
        SetFCC  (.LTESTDATA_L, %l5)  

        fbule,a .LTEMP3
        add     %g6, 10, %g6
        add     %g6, 10, %g6
.LTEMP3:
        SetFCC  (.LTESTDATA_E, %l5)  

        fbule,a .LBRANCH28
        add     %g6, 10, %g6


.LRETURN28:
        cmp     %g6, 21
        bne     .LFAIL
        nop

        !!!!!!! FBO !!!!!!!
        mov     0, %g6
        SetFCC  (.LTESTDATA_U, %l5)  

        fbo    .LFAIL
        add     %g6, 10, %g6

        cmp     %g6, 10
        bne     .LFAIL
        nop

        mov     0, %g6
        SetFCC  (.LTESTDATA_U, %l5)  

        fbo,a  .LFAIL
        add     %g6, 10, %g6

        cmp     %g6, 0
        bne     .LFAIL
        nop

        mov     0, %g6
        SetFCC  (.LTESTDATA_L, %l5)  

        fbo    .LBRANCH29
        add     %g6, 10, %g6

.LRETURN29:
        cmp     %g6, 11
        bne     .LFAIL
        nop

        mov     0, %g6
        SetFCC  (.LTESTDATA_G, %l5)  

        fbo,a  .LTEMP4
        add     %g6, 10, %g6
        add     %g6, 10, %g6
        add     %g6, 10, %g6
.LTEMP4:
        SetFCC  (.LTESTDATA_E, %l5)  

        fbo,a  .LBRANCH30
        add     %g6, 10, %g6


.LRETURN30:
        cmp     %g6, 21
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

.LBRANCH2:
        inc     %g6
        ba      .LRETURN2
        nop

.LBRANCH3:
        ba      .LRETURN3
        inc     %g6

.LBRANCH4:
        ba      .LRETURN4
        inc     %g6

.LBRANCH5:
        ba      .LRETURN5
        inc     %g6

.LBRANCH6:
        ba      .LRETURN6
        inc     %g6

.LBRANCH7:
        ba      .LRETURN7
        inc     %g6

.LBRANCH8:
        ba      .LRETURN8
        inc     %g6

.LBRANCH9:
        ba      .LRETURN9
        inc     %g6

.LBRANCH10:
        ba      .LRETURN10
        inc     %g6

.LBRANCH11:
        ba      .LRETURN11
        inc     %g6

.LBRANCH12:
        ba      .LRETURN12
        inc     %g6

.LBRANCH13:
        ba      .LRETURN13
        inc     %g6

.LBRANCH14:
        ba      .LRETURN14
        inc     %g6

.LBRANCH15:
        ba      .LRETURN15
        inc     %g6

.LBRANCH16:
        ba      .LRETURN16
        inc     %g6

.LBRANCH17:
        ba      .LRETURN17
        inc     %g6

.LBRANCH18:
        ba      .LRETURN18
        inc     %g6

.LBRANCH19:
        ba      .LRETURN19
        inc     %g6

.LBRANCH20:
        ba      .LRETURN20
        inc     %g6

.LBRANCH21:
        ba      .LRETURN21
        inc     %g6

.LBRANCH22:
        ba      .LRETURN22
        inc     %g6

.LBRANCH23:
        ba      .LRETURN23
        inc     %g6

.LBRANCH24:
        ba      .LRETURN24
        inc     %g6

.LBRANCH25:
        ba      .LRETURN25
        inc     %g6

.LBRANCH26:
        ba      .LRETURN26
        inc     %g6

.LBRANCH27:
        ba      .LRETURN27
        inc     %g6

.LBRANCH28:
        ba      .LRETURN28
        inc     %g6

.LBRANCH29:
        ba      .LRETURN29
        inc     %g6

.LBRANCH30:
        ba      .LRETURN30
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

