#include "../src/sparcv8/CPU.h"
#include "../src/sparcv8/MMU.h"


#include <gtest/gtest.h>

#include <cmath>


class CPUInstructionsTest : public ::testing::Test {

protected:
    CPUInstructionsTest();

    virtual ~CPUInstructionsTest();

    // Code here will be called immediately after the constructor (right
    // before each test).
    virtual void SetUp();

    // Code here will be called immediately after each test (right
    // before the destructor).
    virtual void TearDown();
    MCtrl mctrl;
    MMU mmu;
    IRQMP intc;
    CPU cpu;
 
    void do_SAVE_instr(u32 rs1, u32 rs2, u32 rd) {
        u32 op3 = 0b111100; // SAVE
        do_op3_instr(2, op3, rs1, rs2, rd);
    }
 
    void do_RESTORE_instr(u32 rs1, u32 rs2, u32 rd) {
        u32 op3 = 0b111101; // RESTORE
        do_op3_instr(2, op3, rs1, rs2, rd);
    }
 
    void do_op3_instr(u32 op, u32 op3, u32 rs1, u32 rs2, u32 rd) {
        DecodeStruct d;
        d.opcode = ((op & LOBITS2) << FMTSTARTBIT) 
            | ((rd & LOBITS5) << RDSTARTBIT)
            | ((op3 & LOBITS6) << OP3STARTBIT)
            | ((rs1) << RS1STARTBIT)
            | (0x0 << ISTARTBIT)
            | ((rs2)<< RS2STARTBIT); 
     
        d.p = (pPSR_t)&(d.psr);
        d.p->s = 1; // Set supervisor mode 
        d.p->et = 1; // Enable traps 
       
        cpu.decode(&d);
        
        d.function(&cpu, &d);
        cpu.write_back(&d); 
    }


};



CPUInstructionsTest::CPUInstructionsTest() : mmu(mctrl), cpu(mmu, intc)
{  
   	


}

CPUInstructionsTest::~CPUInstructionsTest()
{

}

void CPUInstructionsTest::SetUp()
{
    mctrl.attach_bank<RamBank>(0x0, 1*1024*1024); // 1 MB @ 0x0
    
    
    // Read the ELF and get the entry point, then reset
    u32 entry_va = 0x0; 
    cpu.reset(entry_va);
 
}

void CPUInstructionsTest::TearDown()
{
}

TEST_F(CPUInstructionsTest, WRWIM_allOnes)
{    
    // adress to read is taken from LOCALREG4
    cpu.write_reg((u32)0-1, LOCALREG4); // write all ones to L4
    cpu.write_reg(0x0,  LOCALREG5); 
     
    u32 op3 = 0b110010; // WRWIM
    do_op3_instr(2, op3, LOCALREG4, LOCALREG5, LOCALREG0);

    
    // WIM should only have bits set up the number of windows:
    // Sparc reference:
    // WRWIM with all
    // bits set to 1, followed by a RDWIM, yields a bit vector in which the imple-
    // mented windows (and only the implemented windows) are indicated by 1’s.
    ASSERT_EQ(cpu.get_wim(), (0x1 << (NWINDOWS))-1); // E.G for NWINDOWS = 8, WIM shall read 0b11111111
    ASSERT_EQ(cpu.get_wim(), 0b11111111); // This will brea if we change NWINDOWS

    // Check if instruction RDWIM yields the same
    op3 = 0b101010; // RDWIM
    do_op3_instr(2, op3, LOCALREG4, LOCALREG5, LOCALREG0);

    // WIM is now in rd (L0)    
    u32 l0;
    cpu.read_reg(LOCALREG0, &l0);
 
    ASSERT_EQ(cpu.get_wim(), l0);
}

TEST_F(CPUInstructionsTest, LDSTUB_noMMU)
{   

    //for(int i = 0; i <= 8; ++i) 
    //{
        // TEst swapping the values from $o0 and a value in memory, pointed by $g2 
        
        u32 value = 0xab;
        mmu.MemAccess<intent_store, 1>(0x100, value, CROSS_ENDIAN); 
        cpu.write_reg(0x100, GLOBALREG2); 
        cpu.write_reg(0x000, LOCALREG0); 


        u32 op3 = 0b001101; // LDSTUB
        do_op3_instr(3, op3, GLOBALREG2, LOCALREG0, OUTREG0);

        
        u32 val1; cpu.read_reg(OUTREG0, &val1);
        u32 val2; mmu.MemAccess<intent_load, 1>(0x100, val2, CROSS_ENDIAN); 
     


        EXPECT_EQ(val1, 0xab);
        EXPECT_EQ(val2, 0xff); 

        do_SAVE_instr(LOCALREG0, LOCALREG1, LOCALREG2); 
    //}

}

TEST_F(CPUInstructionsTest, SWAP_noMMU)
{   

    //for(int i = 0; i <= 8; ++i) 
    //{
        // TEst swapping the values from $o0 and a value in memory, pointed by $g2 
        cpu.write_reg(0x0610c041, OUTREG0); 
        
        u32 value = 0xff00ffff;
        mmu.MemAccess<intent_store, 4>(0x100, value, CROSS_ENDIAN); 
        cpu.write_reg(0x100, GLOBALREG2); 
        cpu.write_reg(0x000, LOCALREG0); 


        u32 op3 = 0b001111; // SWAP
        do_op3_instr(3, op3, GLOBALREG2, LOCALREG0, OUTREG0);

        
        u32 val1; cpu.read_reg(OUTREG0, &val1);
        u32 val2; mmu.MemAccess<intent_load, 4>(0x100, val2, CROSS_ENDIAN); 
     


        EXPECT_EQ(val1, 0xff00ffff);
        EXPECT_EQ(val2, 0x0610c041); 

        do_SAVE_instr(LOCALREG0, LOCALREG1, LOCALREG2); 
    //}

}


TEST_F(CPUInstructionsTest, SAVE)
{   
    u32 cwp = cpu.get_psr() & LOBITS5;
    ASSERT_EQ(cwp, 0);
    ASSERT_EQ(cpu.get_wim(), 0x2); // Standard value after cpu.Reset()




    cpu.write_reg(cwp*10 + 0, INREG0); 
    cpu.write_reg(cwp*10 + 1, INREG1); 
    cpu.write_reg(cwp*10 + 2, INREG2); 
    cpu.write_reg(cwp*10 + 3, INREG3); 
    cpu.write_reg(cwp*10 + 4, INREG4); 
    cpu.write_reg(cwp*10 + 5, INREG5); 
    cpu.write_reg(cwp*10 + 6, INREG6); 
    cpu.write_reg(cwp*10 + 7, INREG7); 
    cpu.write_reg(cwp*200 + 0, LOCALREG0); 
    cpu.write_reg(cwp*200 + 1, LOCALREG1); 
    cpu.write_reg(cwp*200 + 2, LOCALREG2); 
    cpu.write_reg(cwp*200 + 3, LOCALREG3); 
    cpu.write_reg(cwp*200 + 4, LOCALREG4); 
    cpu.write_reg(cwp*200 + 5, LOCALREG5); 
    cpu.write_reg(cwp*200 + 6, LOCALREG6); 
    cpu.write_reg(cwp*200 + 7, LOCALREG7); 
    cpu.write_reg(cwp*20 + 0, OUTREG0); 
    cpu.write_reg(cwp*20 + 1, OUTREG1); 
    cpu.write_reg(cwp*20 + 2, OUTREG2); 
    cpu.write_reg(cwp*20 + 3, OUTREG3); 
    cpu.write_reg(cwp*20 + 4, OUTREG4); 
    cpu.write_reg(cwp*20 + 5, OUTREG5); 
    cpu.write_reg(cwp*20 + 6, OUTREG6); 
    cpu.write_reg(cwp*20 + 7, OUTREG7); 
    cpu.write_reg(cwp*2000 + 0, GLOBALREG0); 
    cpu.write_reg(cwp*2000 + 1, GLOBALREG1); 
    cpu.write_reg(cwp*2000 + 2, GLOBALREG2); 
    cpu.write_reg(cwp*2000 + 3, GLOBALREG3); 
    cpu.write_reg(cwp*2000 + 4, GLOBALREG4); 
    cpu.write_reg(cwp*2000 + 5, GLOBALREG5); 
    cpu.write_reg(cwp*2000 + 6, GLOBALREG6); 
    cpu.write_reg(cwp*2000 + 7, GLOBALREG7); 
 
    do_SAVE_instr(LOCALREG0, LOCALREG1, LOCALREG2); 

    // Check we have xpected window pointer
    cwp = (cwp - 1) % NWINDOWS;
    ASSERT_EQ(cwp, 7);
    ASSERT_EQ(cpu.get_psr() & LOBITS5, 7);

    // The SAVE is also add, and places result in destinaiont reg in new window:
    u32 res1;
    cpu.read_reg(LOCALREG0, &res1); ASSERT_EQ(res1, 0);
    cpu.read_reg(LOCALREG1, &res1); ASSERT_EQ(res1, 0);
    cpu.read_reg(LOCALREG2, &res1); ASSERT_EQ(res1, 1); // The add from previous %l0 and %l1
    cpu.read_reg(LOCALREG3, &res1); ASSERT_EQ(res1, 0);
    cpu.read_reg(LOCALREG4, &res1); ASSERT_EQ(res1, 0);
    cpu.read_reg(LOCALREG5, &res1); ASSERT_EQ(res1, 0);
    cpu.read_reg(LOCALREG6, &res1); ASSERT_EQ(res1, 0);
    cpu.read_reg(LOCALREG7, &res1); ASSERT_EQ(res1, 0);

    // After a SAVE, the OUTS from previous register window should now be in the INS
    cpu.read_reg(INREG0, &res1); ASSERT_EQ(res1, 0);
    cpu.read_reg(INREG1, &res1); ASSERT_EQ(res1, 1);
    cpu.read_reg(INREG2, &res1); ASSERT_EQ(res1, 2);
    cpu.read_reg(INREG3, &res1); ASSERT_EQ(res1, 3);
    cpu.read_reg(INREG4, &res1); ASSERT_EQ(res1, 4);
    cpu.read_reg(INREG5, &res1); ASSERT_EQ(res1, 5);
    cpu.read_reg(INREG6, &res1); ASSERT_EQ(res1, 6);
    cpu.read_reg(INREG7, &res1); ASSERT_EQ(res1, 7);

 

    do_SAVE_instr(LOCALREG0, LOCALREG1, LOCALREG2); 
    ASSERT_EQ(cpu.get_psr() & LOBITS5, 6);

    do_SAVE_instr(LOCALREG0, LOCALREG1, LOCALREG2); 
    ASSERT_EQ(cpu.get_psr() & LOBITS5, 5);

    cwp = cpu.get_psr() & LOBITS5;
    cpu.write_reg(cwp*10 + 0, INREG0); 
    cpu.write_reg(cwp*10 + 1, INREG1); 
    cpu.write_reg(cwp*10 + 2, INREG2); 
    cpu.write_reg(cwp*10 + 3, INREG3); 
    cpu.write_reg(cwp*10 + 4, INREG4); 
    cpu.write_reg(cwp*10 + 5, INREG5); 
    cpu.write_reg(cwp*10 + 6, INREG6); 
    cpu.write_reg(cwp*10 + 7, INREG7); 
    cpu.write_reg(cwp*200 + 0, LOCALREG0); 
    cpu.write_reg(cwp*200 + 1, LOCALREG1); 
    cpu.write_reg(cwp*200 + 2, LOCALREG2); 
    cpu.write_reg(cwp*200 + 3, LOCALREG3); 
    cpu.write_reg(cwp*200 + 4, LOCALREG4); 
    cpu.write_reg(cwp*200 + 5, LOCALREG5); 
    cpu.write_reg(cwp*200 + 6, LOCALREG6); 
    cpu.write_reg(cwp*200 + 7, LOCALREG7); 
    cpu.write_reg(cwp*20 + 0, OUTREG0); 
    cpu.write_reg(cwp*20 + 1, OUTREG1); 
    cpu.write_reg(cwp*20 + 2, OUTREG2); 
    cpu.write_reg(cwp*20 + 3, OUTREG3); 
    cpu.write_reg(cwp*20 + 4, OUTREG4); 
    cpu.write_reg(cwp*20 + 5, OUTREG5); 
    cpu.write_reg(cwp*20 + 6, OUTREG6); 
    cpu.write_reg(cwp*20 + 7, OUTREG7); 
    cpu.write_reg(cwp*2000 + 0, GLOBALREG0); 
    cpu.write_reg(cwp*2000 + 1, GLOBALREG1); 
    cpu.write_reg(cwp*2000 + 2, GLOBALREG2); 
    cpu.write_reg(cwp*2000 + 3, GLOBALREG3); 
    cpu.write_reg(cwp*2000 + 4, GLOBALREG4); 
    cpu.write_reg(cwp*2000 + 5, GLOBALREG5); 
    cpu.write_reg(cwp*2000 + 6, GLOBALREG6); 
    cpu.write_reg(cwp*2000 + 7, GLOBALREG7); 
 
    do_SAVE_instr(LOCALREG0, LOCALREG1, LOCALREG2); 
    ASSERT_EQ(cpu.get_psr() & LOBITS5, 4);

    // After a SAVE, the OUTS from previous register window should now be in the INS
    cpu.read_reg(INREG0, &res1); ASSERT_EQ(res1, 100);
    cpu.read_reg(INREG1, &res1); ASSERT_EQ(res1, 101);
    cpu.read_reg(INREG2, &res1); ASSERT_EQ(res1, 102);
    cpu.read_reg(INREG3, &res1); ASSERT_EQ(res1, 103);
    cpu.read_reg(INREG4, &res1); ASSERT_EQ(res1, 104);
    cpu.read_reg(INREG5, &res1); ASSERT_EQ(res1, 105);
    cpu.read_reg(INREG6, &res1); ASSERT_EQ(res1, 106);
    cpu.read_reg(INREG7, &res1); ASSERT_EQ(res1, 107);


    do_SAVE_instr(LOCALREG0, LOCALREG1, LOCALREG2); 
    ASSERT_EQ(cpu.get_psr() & LOBITS5, 3);

    do_SAVE_instr(LOCALREG0, LOCALREG1, LOCALREG2); 
    ASSERT_EQ(cpu.get_psr() & LOBITS5, 2);

    // This one will trigger windoww overflow since WIM = 0x2:
    do_SAVE_instr(LOCALREG0, LOCALREG1, LOCALREG2); 
    ASSERT_EQ(cpu.get_psr() & LOBITS5, 2);
    ASSERT_EQ(cpu.get_trap_type(), SPARC_WINDOW_OVERFLOW);
    // Run the CPU through the trap:
    cpu.run(1, nullptr);
    // CWP should now be 1
    // In real ilfe we would do a RETT here, and end up at 2 again.
    ASSERT_EQ(cpu.get_psr() & LOBITS5, 1);
 

    do_SAVE_instr(LOCALREG0, LOCALREG1, LOCALREG2); 
    ASSERT_EQ(cpu.get_psr() & LOBITS5, 0);

    cpu.read_reg(INREG0, &res1); ASSERT_EQ(res1, 0);
    cpu.read_reg(INREG1, &res1); ASSERT_EQ(res1, 1);
    cpu.read_reg(INREG2, &res1); ASSERT_EQ(res1, 2);
    cpu.read_reg(INREG3, &res1); ASSERT_EQ(res1, 3);
    cpu.read_reg(INREG4, &res1); ASSERT_EQ(res1, 4);
    cpu.read_reg(INREG5, &res1); ASSERT_EQ(res1, 5);
    cpu.read_reg(INREG6, &res1); ASSERT_EQ(res1, 6);
    cpu.read_reg(INREG7, &res1); ASSERT_EQ(res1, 7);

    cpu.read_reg(OUTREG0, &res1); ASSERT_EQ(res1, 0);
    cpu.read_reg(OUTREG1, &res1); ASSERT_EQ(res1, 1);
    cpu.read_reg(OUTREG2, &res1); ASSERT_EQ(res1, 2);
    cpu.read_reg(OUTREG3, &res1); ASSERT_EQ(res1, 3);
    cpu.read_reg(OUTREG4, &res1); ASSERT_EQ(res1, 4);
    cpu.read_reg(OUTREG5, &res1); ASSERT_EQ(res1, 5);
    cpu.read_reg(OUTREG6, &res1); ASSERT_EQ(res1, 6);
    cpu.read_reg(OUTREG7, &res1); ASSERT_EQ(res1, 7);


 
    do_SAVE_instr(LOCALREG0, LOCALREG1, LOCALREG2); 
    ASSERT_EQ(cpu.get_psr() & LOBITS5, 7);






}

TEST_F(CPUInstructionsTest, RESTORE)
{   
    u32 cwp = cpu.get_psr() & LOBITS5;
    ASSERT_EQ(cwp, 0);
    ASSERT_EQ(cpu.get_wim(), 0x2); // Standard value after cpu.Reset()

    cpu.write_reg(0, LOCALREG4);
    cpu.write_reg(0, LOCALREG5); 
     
    u32 op3 = 0b110010; // WRWIM
    do_op3_instr(2, op3, LOCALREG4, LOCALREG5, LOCALREG0);
    ASSERT_EQ(cpu.get_wim(), 0x0);
    ASSERT_EQ(cpu.get_trap_type(), 0);
 
    cpu.write_reg(0x2, LOCALREG4);
    op3 = 0b110010; // WRWIM
    do_op3_instr(2, op3, LOCALREG4, LOCALREG5, LOCALREG0);
    ASSERT_EQ(cpu.get_wim(), 0x2); // Standard value after cpu.Reset()
 
 
    cwp = 1;
    cpu.write_reg(cwp*10 + 0, INREG0); 
    cpu.write_reg(cwp*10 + 1, INREG1); 
    cpu.write_reg(cwp*10 + 2, INREG2); 
    cpu.write_reg(cwp*10 + 3, INREG3); 
    cpu.write_reg(cwp*10 + 4, INREG4); 
    cpu.write_reg(cwp*10 + 5, INREG5); 
    cpu.write_reg(cwp*10 + 6, INREG6); 
    cpu.write_reg(cwp*10 + 7, INREG7); 
    cpu.write_reg(cwp*200 + 0, LOCALREG0); 
    cpu.write_reg(cwp*200 + 1, LOCALREG1); 
    cpu.write_reg(cwp*200 + 2, LOCALREG2); 
    cpu.write_reg(cwp*200 + 3, LOCALREG3); 
    cpu.write_reg(cwp*200 + 4, LOCALREG4); 
    cpu.write_reg(cwp*200 + 5, LOCALREG5); 
    cpu.write_reg(cwp*200 + 6, LOCALREG6); 
    cpu.write_reg(cwp*200 + 7, LOCALREG7); 
    cpu.write_reg(cwp*20 + 0, OUTREG0); 
    cpu.write_reg(cwp*20 + 1, OUTREG1); 
    cpu.write_reg(cwp*20 + 2, OUTREG2); 
    cpu.write_reg(cwp*20 + 3, OUTREG3); 
    cpu.write_reg(cwp*20 + 4, OUTREG4); 
    cpu.write_reg(cwp*20 + 5, OUTREG5); 
    cpu.write_reg(cwp*20 + 6, OUTREG6); 
    cpu.write_reg(cwp*20 + 7, OUTREG7); 
    cpu.write_reg(cwp*2000 + 0, GLOBALREG0); 
    cpu.write_reg(cwp*2000 + 1, GLOBALREG1); 
    cpu.write_reg(cwp*2000 + 2, GLOBALREG2); 
    cpu.write_reg(cwp*2000 + 3, GLOBALREG3); 
    cpu.write_reg(cwp*2000 + 4, GLOBALREG4); 
    cpu.write_reg(cwp*2000 + 5, GLOBALREG5); 
    cpu.write_reg(cwp*2000 + 6, GLOBALREG6); 
    cpu.write_reg(cwp*2000 + 7, GLOBALREG7); 
    cwp = 0;

    do_SAVE_instr(LOCALREG0, LOCALREG1, LOCALREG2);
    ASSERT_EQ(cpu.get_trap_type(), 0);
    ASSERT_EQ(cpu.get_psr() & LOBITS5, 7);
    u32 val; cpu.read_reg(LOCALREG2, &val); ASSERT_EQ(val, 401);
    cpu.read_reg(INREG0, &val); ASSERT_EQ(val, 20);
    cpu.read_reg(INREG1, &val); ASSERT_EQ(val, 21);
    cpu.read_reg(INREG2, &val); ASSERT_EQ(val, 22);
    cpu.read_reg(INREG3, &val); ASSERT_EQ(val, 23);
    cpu.read_reg(INREG4, &val); ASSERT_EQ(val, 24);
    cpu.read_reg(INREG5, &val); ASSERT_EQ(val, 25);
    cpu.read_reg(INREG6, &val); ASSERT_EQ(val, 26);
    cpu.read_reg(INREG7, &val); ASSERT_EQ(val, 27);









    do_SAVE_instr(LOCALREG0, LOCALREG1, LOCALREG2);
    ASSERT_EQ(cpu.get_psr() & LOBITS5, 6);
    ASSERT_EQ(cpu.get_trap_type(), 0);
    cpu.read_reg(LOCALREG2, &val); ASSERT_EQ(val, 0);

    do_RESTORE_instr(LOCALREG0, LOCALREG1, LOCALREG2); 
    ASSERT_EQ(cpu.get_psr() & LOBITS5, 7);
    cpu.read_reg(INREG0, &val); ASSERT_EQ(val, 20);
    cpu.read_reg(INREG1, &val); ASSERT_EQ(val, 21);
    cpu.read_reg(INREG2, &val); ASSERT_EQ(val, 22);
    cpu.read_reg(INREG3, &val); ASSERT_EQ(val, 23);
    cpu.read_reg(INREG4, &val); ASSERT_EQ(val, 24);
    cpu.read_reg(INREG5, &val); ASSERT_EQ(val, 25);
    cpu.read_reg(INREG6, &val); ASSERT_EQ(val, 26);
    cpu.read_reg(INREG7, &val); ASSERT_EQ(val, 27);


    do_RESTORE_instr(LOCALREG0, LOCALREG1, LOCALREG2); 
    ASSERT_EQ(cpu.get_psr() & LOBITS5, 0);
    ASSERT_EQ(cpu.get_trap_type(), 0);
 
    cpu.read_reg(LOCALREG0, &val); ASSERT_EQ(val, 200);
    cpu.read_reg(LOCALREG1, &val); ASSERT_EQ(val, 201);
    cpu.read_reg(LOCALREG2, &val); ASSERT_EQ(val, 0);
    cpu.read_reg(LOCALREG3, &val); ASSERT_EQ(val, 203);
    cpu.read_reg(LOCALREG4, &val); ASSERT_EQ(val, 204);
    cpu.read_reg(LOCALREG5, &val); ASSERT_EQ(val, 205);
    cpu.read_reg(LOCALREG6, &val); ASSERT_EQ(val, 206);
    cpu.read_reg(LOCALREG7, &val); ASSERT_EQ(val, 207);
    cpu.read_reg(OUTREG0, &val); ASSERT_EQ(val, 20);
    cpu.read_reg(OUTREG1, &val); ASSERT_EQ(val, 21);
    cpu.read_reg(OUTREG2, &val); ASSERT_EQ(val, 22);
    cpu.read_reg(OUTREG3, &val); ASSERT_EQ(val, 23);
    cpu.read_reg(OUTREG4, &val); ASSERT_EQ(val, 24);
    cpu.read_reg(OUTREG5, &val); ASSERT_EQ(val, 25);
    cpu.read_reg(OUTREG6, &val); ASSERT_EQ(val, 26);
    cpu.read_reg(OUTREG7, &val); ASSERT_EQ(val, 27);


    cpu.read_reg(INREG0, &val); ASSERT_EQ(val, 10);
    cpu.read_reg(INREG1, &val); ASSERT_EQ(val, 11);
    cpu.read_reg(INREG2, &val); ASSERT_EQ(val, 12);
    cpu.read_reg(INREG3, &val); ASSERT_EQ(val, 13);
    cpu.read_reg(INREG4, &val); ASSERT_EQ(val, 14);
    cpu.read_reg(INREG5, &val); ASSERT_EQ(val, 15);
    cpu.read_reg(INREG6, &val); ASSERT_EQ(val, 16);
    cpu.read_reg(INREG7, &val); ASSERT_EQ(val, 17);





    // This one will trigger windoww underflow since WIM = 0x2:
    // and cwp will remain 0
    do_RESTORE_instr(LOCALREG0, LOCALREG1, LOCALREG2); 
    ASSERT_EQ(cpu.get_psr() & LOBITS5, 0);
    ASSERT_EQ(cpu.get_trap_type(), SPARC_WINDOW_UNDERFLOW);
    // Run the CPU through the trap:
    cpu.run(1, nullptr);
    // CWP should now be 7 since traos decrement cwp
    // In real ilfe we would do a RETT here, and end up at 0 again.. Or something.
    ASSERT_EQ(cpu.get_psr() & LOBITS5, 7);
 




}
