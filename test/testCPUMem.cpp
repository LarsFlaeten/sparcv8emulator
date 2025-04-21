#include "../src/sparcv8/CPU.h"
#include "../src/sparcv8/MMU.h"


#include <gtest/gtest.h>

#include <cmath>


class CPUMemTest : public ::testing::Test {

protected:
    CPUMemTest();

    virtual ~CPUMemTest();

    // Code here will be called immediately after the constructor (right
    // before each test).
    virtual void SetUp();

    // Code here will be called immediately after each test (right
    // before the destructor).
    virtual void TearDown();

    MMU mmu;
    CPU cpu;
    SDRAM<0x01000000> RAM;  // IO: 0x0, 16 MB of RAM

};



CPUMemTest::CPUMemTest() : cpu(mmu)
{  
   	


}

CPUMemTest::~CPUMemTest()
{

}

void CPUMemTest::SetUp()
{
    // Set up IO mapping
    // TODO: Move this MMU functions?
    for(unsigned a = 0x0; a < 0x100; ++a)
        mmu.IOmap[a] = { [&](u32 i)          { return RAM.Read(i/4); },
                         [&](u32 i, u32 v)   { RAM.Write(i/4, v);    } };

    // Read the ELF and get the entry point, then reset
    u32 entry_va = 0x0; 
    cpu.reset(entry_va);
 
}

void CPUMemTest::TearDown()
{
}

TEST_F(CPUMemTest, TestResetState)
{
    EXPECT_EQ(cpu.get_pc(), 0);
    EXPECT_EQ(cpu.get_npc(), 4);

    u32 psr = cpu.get_psr(); 
    
    // Current window pointer should be 0
    EXPECT_EQ(psr & LOBITS5 , 0);
    
    // Supervisor, enable traos and enable fp should all be 1
    EXPECT_EQ((psr >> PSR_SUPER_MODE) & 0x1 , 1);
    EXPECT_EQ((psr >> PSR_ENABLE_TRAPS) & 0x1 , 1);
    EXPECT_EQ((psr >> PSR_ENABLE_FLOATING_POINT) & 0x1, 1);

    // reset to another memory location:
    cpu.reset(0x1180);
    EXPECT_EQ(cpu.get_pc(), 0x1180);
    EXPECT_EQ(cpu.get_npc(), 0x1184);



}

/*TEST_F(CPUMemTest, CheckRegBase)
{
    std::cout << "RegNo -> RegBase\n";

    // Expect current window pointer to be 0 
    u32 PSR = cpu.GetPSR(); 
    EXPECT_EQ(((pPSR_t)&PSR)->cwp , 0);;
    
    for(u32 r = 0; r < 32; ++r)
    {
        if( r < 0x8 ) { EXPECT_EQ(cpu.GetRegBase(r), r + 0x100); continue; }
        if( r < 0x10 ) { EXPECT_EQ(cpu.GetRegBase(r), r - 0x8); continue; }
        if( r < 0x18 ) { EXPECT_EQ(cpu.GetRegBase(r), r + 0x70); continue; }
        if( r < 0x20 ) { EXPECT_EQ(cpu.GetRegBase(r), r - 0x10); continue; }
    }        

    
}
*/
TEST_F(CPUMemTest, MemAccess_ReadWrite)
{

    // Checl WORD sized loads and stores, and replace individual bytes
    u32 value = 0xff00ffff;
    mmu.MemAccess<intent_store, 4>(0x100, value, CROSS_ENDIAN); 

    u32 value2;
    mmu.MemAccess<intent_load, 4>(0x100, value2, CROSS_ENDIAN);

    EXPECT_EQ(value, value2);

    u32 bv1, bv2, bv3, bv4;
    mmu.MemAccess<intent_load, 1>(0x100, bv1, CROSS_ENDIAN);
    mmu.MemAccess<intent_load, 1>(0x101, bv2, CROSS_ENDIAN);
    mmu.MemAccess<intent_load, 1>(0x102, bv3, CROSS_ENDIAN);
    mmu.MemAccess<intent_load, 1>(0x103, bv4, CROSS_ENDIAN);

    EXPECT_EQ(bv1, 0xff);
    EXPECT_EQ(bv2, 0x00);
    EXPECT_EQ(bv3, 0xff);
    EXPECT_EQ(bv4, 0xff);
    mmu.MemAccess<intent_load,4>(0x100, value, CROSS_ENDIAN);
    EXPECT_EQ(value, 0xff00ffff);
 
    mmu.MemAccess<intent_store,1>(0x101, bv1, CROSS_ENDIAN);
    mmu.MemAccess<intent_load,4>(0x100, value, CROSS_ENDIAN);
    EXPECT_EQ(value, 0xffffffff);
   
    u32 v3 = 0xcafebabe;
    u32 v4 = 0xbadbadba;

    mmu.MemAccess<intent_store,4>(0x200, v3, CROSS_ENDIAN);
    mmu.MemAccess<intent_store,4>(0x204, v4, CROSS_ENDIAN);
    
    
    mmu.MemAccess<intent_load,4>(0x200, value, CROSS_ENDIAN);
    mmu.MemAccess<intent_load,4>(0x204, value2, CROSS_ENDIAN);
    EXPECT_EQ(value, 0xcafebabe);
    EXPECT_EQ(value2, 0xbadbadba);
    
    u64 result = (u64)value2 + ((u64)value << 32);
    EXPECT_EQ(result, 0xcafebabebadbadba);
   
}

TEST_F(CPUMemTest, Loads_LD) {

    // Put some data in memory
    //
    u32 value = 0xcafebabe;
    mmu.MemAccess<intent_store,4>(0x300, value, CROSS_ENDIAN);
   
    // adress to read is taken from LOCALREG4
    cpu.write_reg(0x300, LOCALREG4);
    
    // Construct LD opcode
    DecodeStruct d;
    u32 op = 0x000000; // LD
    d.opcode = ((3 & LOBITS2) << FMTSTARTBIT) 
        ^ ((LOCALREG0 & LOBITS5) << RDSTARTBIT)
        ^ ((op & LOBITS6) << OP3STARTBIT)
        ^ ((LOCALREG4) << RS1STARTBIT)
        ^ (0x0 << ISTARTBIT)
        ^ ((LOCALREG5)<< RS2STARTBIT); 
    cpu.decode(&d);
    
    EXPECT_EQ(d.op_2_3, 0x00000);
    EXPECT_EQ(d.rd, LOCALREG0);
    EXPECT_EQ(d.rs1, LOCALREG4);
 
    EXPECT_EQ(d.rs1_value, 0x300);
    EXPECT_EQ(d.i, 0);
    EXPECT_EQ(d.imm_disp_rs2, LOCALREG5);
    EXPECT_EQ(d.ev, 0x300);
    
    d.function(&cpu, &d);
    cpu.write_back(&d); 

    // The value from 0x300 should now be in LOCALREG0 (rd):
    u32 res; cpu.read_reg(LOCALREG0, &res);
    EXPECT_EQ(res, 0xcafebabe);

    // ----------------------------------------------------
    // Same op, but construct adress from rs1 + rs2
    
    // adress to read is taken from LOCALREG4 (rs1) + LOCALREG5 (rs2)
    cpu.write_reg(0x200, LOCALREG4);
    cpu.write_reg(0x100, LOCALREG5);
    // No change to opcode
    cpu.decode(&d);
    EXPECT_EQ(d.ev, 0x300);
    d.function(&cpu, &d);
    cpu.write_back(&d); 

    // The value from 0x300 should now be in LOCALREG0 (rd):
    u32 res2; cpu.read_reg(LOCALREG0, &res2);
    EXPECT_EQ(res2, 0xcafebabe);


 
}

TEST_F(CPUMemTest, Loads_LDD) {

    // Put some data in memory
    //
    u32 value = 0xcafebabe;
    mmu.MemAccess<intent_store,4>(0x300, value, CROSS_ENDIAN);
    u32 value2 = 0xbadbadff;
    mmu.MemAccess<intent_store,4>(0x304, value2, CROSS_ENDIAN);
   
    // adress to read is taken from LOCALREG4
    cpu.write_reg(0x300, LOCALREG4);
    
    // Construct LD opcode
    DecodeStruct d;
    u32 op = 0b000011; // LDD
    d.opcode = ((3 & LOBITS2) << FMTSTARTBIT) 
        ^ ((LOCALREG0 & LOBITS5) << RDSTARTBIT)
        ^ ((op & LOBITS6) << OP3STARTBIT)
        ^ ((LOCALREG4) << RS1STARTBIT)
        ^ (0x0 << ISTARTBIT)
        ^ ((LOCALREG5)<< RS2STARTBIT); 
    cpu.decode(&d);
    
    EXPECT_EQ(d.op_2_3, 0b000011);
    EXPECT_EQ(d.rd, LOCALREG0);
    EXPECT_EQ(d.rs1, LOCALREG4);
 
    EXPECT_EQ(d.rs1_value, 0x300);
    EXPECT_EQ(d.i, 0);
    EXPECT_EQ(d.imm_disp_rs2, LOCALREG5);
    EXPECT_EQ(d.ev, 0x300);
    
    d.function(&cpu, &d);
    cpu.write_back(&d); 

    // The value from 0x300 should now be in LOCALREG0 (rd):
    u32 res; cpu.read_reg(LOCALREG0, &res);
    EXPECT_EQ(res, 0xcafebabe);
    // The value from 0x304 should now be in LOCALREG1 (rd+1):
    u32 res2; cpu.read_reg(LOCALREG1, &res2);
    EXPECT_EQ(res2, 0xbadbadff);


    // ----------------------------------------------------
    // Same op, but construct adress from rs1 + rs2
    
    // adress to read is taken from LOCALREG4 (rs1) + LOCALREG5 (rs2)
    cpu.write_reg(0x200, LOCALREG4);
    cpu.write_reg(0x100, LOCALREG5);
    // No change to opcode
    cpu.decode(&d);
    EXPECT_EQ(d.ev, 0x300);
    d.function(&cpu, &d);
    cpu.write_back(&d); 

    // The value from 0x300 should now be in LOCALREG0 (rd):
    u32 res3; cpu.read_reg(LOCALREG0, &res3);
    EXPECT_EQ(res3, 0xcafebabe);
    // The value from 0x304 should now be in LOCALREG1 (rd+1):
    u32 res4; cpu.read_reg(LOCALREG1, &res4);
    EXPECT_EQ(res4, 0xbadbadff);



 
}

TEST_F(CPUMemTest, Loads_LDUH) {

    // Put some data in memory
    //
    u32 value = 0xcafebabe;
    mmu.MemAccess<intent_store,4>(0x300, value, CROSS_ENDIAN);
    u32 value2 = 0xbadbadff;
    mmu.MemAccess<intent_store,4>(0x304, value2, CROSS_ENDIAN);
  
    u32 tmp1, tmp2, tmp3, tmp4;
    mmu.MemAccess<intent_load,2>(0x300, tmp1, CROSS_ENDIAN);
    mmu.MemAccess<intent_load,2>(0x302, tmp2, CROSS_ENDIAN);
    mmu.MemAccess<intent_load,2>(0x304, tmp3, CROSS_ENDIAN);
    mmu.MemAccess<intent_load,2>(0x306, tmp4, CROSS_ENDIAN);
    EXPECT_EQ(tmp1, 0xcafe);
    EXPECT_EQ(tmp2, 0xbabe);
    EXPECT_EQ(tmp3, 0xbadb);
    EXPECT_EQ(tmp4, 0xadff);
 





    // adress to read is taken from LOCALREG4
    cpu.write_reg(0x300, LOCALREG4);
    
    // Construct LD opcode
    DecodeStruct d;
    u32 op = 0b000001; // LDUB
    d.opcode = ((3 & LOBITS2) << FMTSTARTBIT) 
        ^ ((LOCALREG0 & LOBITS5) << RDSTARTBIT)
        ^ ((op & LOBITS6) << OP3STARTBIT)
        ^ ((LOCALREG4) << RS1STARTBIT)
        ^ (0x0 << ISTARTBIT)
        ^ ((LOCALREG5)<< RS2STARTBIT); 
    cpu.decode(&d);
    
    EXPECT_EQ(d.op_2_3, 0b000001);
    EXPECT_EQ(d.rd, LOCALREG0);
    EXPECT_EQ(d.rs1, LOCALREG4);
 
    EXPECT_EQ(d.rs1_value, 0x300);
    EXPECT_EQ(d.i, 0);
    EXPECT_EQ(d.imm_disp_rs2, LOCALREG5);
    EXPECT_EQ(d.ev, 0x300);
    
    d.function(&cpu, &d);
    cpu.write_back(&d); 

    // The value from 0x300 should now be in LOCALREG0 (rd):
    u32 res; cpu.read_reg(LOCALREG0, &res);
    EXPECT_EQ(res, 0xca);

    // continue to read bytes in memory at other adresses:
    cpu.write_reg(0x301, LOCALREG4);
    cpu.decode(&d);
    EXPECT_EQ(d.ev, 0x301);
    d.function(&cpu, &d);
    cpu.write_back(&d); 
    cpu.read_reg(LOCALREG0, &res);
    EXPECT_EQ(res, 0xfe);

    cpu.write_reg(0x302, LOCALREG4);
    cpu.decode(&d);
    EXPECT_EQ(d.ev, 0x302);
    d.function(&cpu, &d);
    cpu.write_back(&d); 
    cpu.read_reg(LOCALREG0, &res);
    EXPECT_EQ(res, 0xba);

    cpu.write_reg(0x303, LOCALREG4);
    cpu.decode(&d);
    EXPECT_EQ(d.ev, 0x303);
    d.function(&cpu, &d);
    cpu.write_back(&d); 
    cpu.read_reg(LOCALREG0, &res);
    EXPECT_EQ(res, 0xbe);

    cpu.write_reg(0x304, LOCALREG4);
    cpu.decode(&d);
    EXPECT_EQ(d.ev, 0x304);
    d.function(&cpu, &d);
    cpu.write_back(&d); 
    cpu.read_reg(LOCALREG0, &res);
    EXPECT_EQ(res, 0xba);

    cpu.write_reg(0x305, LOCALREG4);
    cpu.decode(&d);
    EXPECT_EQ(d.ev, 0x305);
    d.function(&cpu, &d);
    cpu.write_back(&d); 
    cpu.read_reg(LOCALREG0, &res);
    EXPECT_EQ(res, 0xdb);

    cpu.write_reg(0x306, LOCALREG4);
    cpu.decode(&d);
    EXPECT_EQ(d.ev, 0x306);
    d.function(&cpu, &d);
    cpu.write_back(&d); 
    cpu.read_reg(LOCALREG0, &res);
    EXPECT_EQ(res, 0xad);

    cpu.write_reg(0x307, LOCALREG4);
    cpu.decode(&d);
    EXPECT_EQ(d.ev, 0x307);
    d.function(&cpu, &d);
    cpu.write_back(&d); 
    cpu.read_reg(LOCALREG0, &res);
    EXPECT_EQ(res, 0xff);

    // Test reads of halfwords 

    op = 0b000010; // LDUH
    cpu.write_reg(0x300, LOCALREG4);
    d.opcode = ((3 & LOBITS2) << FMTSTARTBIT) 
        ^ ((LOCALREG0 & LOBITS5) << RDSTARTBIT)
        ^ ((op & LOBITS6) << OP3STARTBIT)
        ^ ((LOCALREG4) << RS1STARTBIT)
        ^ (0x0 << ISTARTBIT)
        ^ ((LOCALREG5)<< RS2STARTBIT); 
    cpu.decode(&d);
     
    EXPECT_EQ(d.op_2_3, 0b000010);
    EXPECT_EQ(d.rd, LOCALREG0);
    EXPECT_EQ(d.rs1, LOCALREG4);
 
    EXPECT_EQ(d.rs1_value, 0x300);
    EXPECT_EQ(d.i, 0);
    EXPECT_EQ(d.imm_disp_rs2, LOCALREG5);
    EXPECT_EQ(d.ev, 0x300);
    
    d.function(&cpu, &d);
    cpu.write_back(&d); 

    // The value from 0x300 should now be in LOCALREG0 (rd):
    cpu.read_reg(LOCALREG0, &res);
    EXPECT_EQ(res, 0xcafe);

    // continue to read bytes in memory at other adresses:
    /*cpu.WriteReg(0x301, LOCALREG4);
    cpu.Decode(&d);
    EXPECT_EQ(d.ev, 0x301);
    d.function(&cpu, &d); // This shouuld throw, unaligned
    cpu.WriteBack(&d); 
    cpu.ReadReg(LOCALREG0, &res);
    EXPECT_EQ(res, 0xfeba);*/

    // continue to read bytes in memory at other adresses:
    cpu.write_reg(0x302, LOCALREG4);
    cpu.decode(&d);
    EXPECT_EQ(d.ev, 0x302);
    d.function(&cpu, &d);
    cpu.write_back(&d); 
    cpu.read_reg(LOCALREG0, &res);
    EXPECT_EQ(res, 0xbabe);

    // continue to read bytes in memory at other adresses:
    cpu.write_reg(0x304, LOCALREG4);
    cpu.decode(&d);
    EXPECT_EQ(d.ev, 0x304);
    d.function(&cpu, &d);
    cpu.write_back(&d); 
    cpu.read_reg(LOCALREG0, &res);
    EXPECT_EQ(res, 0xbadb);

    // continue to read bytes in memory at other adresses:
    cpu.write_reg(0x306, LOCALREG4);
    cpu.decode(&d);
    EXPECT_EQ(d.ev, 0x306);
    d.function(&cpu, &d);
    cpu.write_back(&d); 
    cpu.read_reg(LOCALREG0, &res);
    EXPECT_EQ(res, 0xadff);

  
}

TEST_F(CPUMemTest, Loads_bytes) {

    // NOy actually needed, was done as part of verifying the internal memory model
    // after the bug in https://github.com/wyvernSemi/sparc/commit/7fc3e907f150a0b7c10cffe9be5cf7f554446490 

    // Put some data in memory
    //
    u32 value = 0xcafebabe;
    mmu.MemAccess<intent_store,4>(0x500, value, CROSS_ENDIAN);
    u32 value2 = 0xbadbadff;
    mmu.MemAccess<intent_store,4>(0x504, value2, CROSS_ENDIAN);
  
    u32 tmp[8];
    mmu.MemAccess<intent_load,1>(0x500, tmp[0], CROSS_ENDIAN);
    mmu.MemAccess<intent_load,1>(0x501, tmp[1], CROSS_ENDIAN);
    mmu.MemAccess<intent_load,1>(0x502, tmp[2], CROSS_ENDIAN);
    mmu.MemAccess<intent_load,1>(0x503, tmp[3], CROSS_ENDIAN);
    mmu.MemAccess<intent_load,1>(0x504, tmp[4], CROSS_ENDIAN);
    mmu.MemAccess<intent_load,1>(0x505, tmp[5], CROSS_ENDIAN);
    mmu.MemAccess<intent_load,1>(0x506, tmp[6], CROSS_ENDIAN);
    mmu.MemAccess<intent_load,1>(0x507, tmp[7], CROSS_ENDIAN);
    EXPECT_EQ(tmp[0], 0xca);
    EXPECT_EQ(tmp[1], 0xfe);
    EXPECT_EQ(tmp[2], 0xba);
    EXPECT_EQ(tmp[3], 0xbe);
    EXPECT_EQ(tmp[4], 0xba);
    EXPECT_EQ(tmp[5], 0xdb);
    EXPECT_EQ(tmp[6], 0xad);
    EXPECT_EQ(tmp[7], 0xff);
 


}


TEST_F(CPUMemTest, RD_ASR17) {
    u32 op = 0x89444000;
    DecodeStruct d;
    d.opcode = op;
    cpu.decode(&d);
    
 
    
    d.function(&cpu, &d);
    cpu.write_back(&d); 
    u32 res;
    cpu.read_reg(GLOBALREG4, &res);
 
    ASSERT_EQ((d.value >> 28), 0x0);
    ASSERT_EQ(res>>28, 0x0);



    cpu.set_cpu_id(3);
    cpu.decode(&d);
    d.function(&cpu, &d);
    cpu.write_back(&d); 
 
    cpu.read_reg(GLOBALREG4, &res);
 
    ASSERT_EQ((d.value >> 28), 0x03);
    ASSERT_EQ(res>>28, 0x03);





}
