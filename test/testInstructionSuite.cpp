#include "../src/sparcv8/CPU.h"
#include "../src/sparcv8/MMU.h"
#include "../src/readelf.cpp"



#include <gtest/gtest.h>

#include <cmath>

std::string path = "../../test/asm/";


class INSTRTest : public ::testing::Test {

protected:
    INSTRTest();

    virtual ~INSTRTest();

    // Code here will be called immediately after the constructor (right
    // before each test).
    virtual void SetUp();

    // Code here will be called immediately after each test (right
    // before the destructor).
    virtual void TearDown();
        
    SDRAM<0x01000000> RAM;   // IO: 0xf0000000, 16 MB of RAM
 
    CPU cpu;
    
    
    void do_test_assertg7(const std::string& test) {
        // Read the ELF and get the entry point, then reset
        u32 entry_va = 0x0; 
        u32 word_count = ReadElf(path + test, cpu, entry_va); 
        ASSERT_GT(word_count, 0);
        cpu.Reset(entry_va);
        
        RunSummary rs;
        cpu.Run(0, &rs); 

        // Check if test hac placed GOOD (0x900d) or BAD (0xbad) in %g7
        u32 val; cpu.ReadReg(GLOBALREG7, &val);
        if(val == 0xbad)
            FAIL() << "Test failed.\n";
        
        ASSERT_EQ(val, 0x900d); 


    }
};



INSTRTest::INSTRTest()
    : cpu()

{  
    //cpu.SetVerbose(true);
    cpu.SetId(0);

    // Set up IO mapping
    // TODO: Move this MMU functions?
    u32 base_ram = 0x00000000;
    u32 size_ram = RAM.getSizeBytes();
    u32 start = base_ram/0x10000;
    u32 end = (base_ram + size_ram)/0x10000;
    for(unsigned a = start; a < end; ++a)
        MMU::IOmap[a] = { [&RAM = RAM](u32 i)          { return RAM.Read( (i-0x00000000)/4); },
                          [&RAM = RAM](u32 i, u32 v)   {        RAM.Write((i-0x00000000)/4, v);    } };
   
}

INSTRTest::~INSTRTest()
{

}

void INSTRTest::SetUp()
{


}

void INSTRTest::TearDown()
{
}


TEST_F(INSTRTest, add)
{
    do_test_assertg7("add.aout");
} 

TEST_F(INSTRTest, _and)
{
    do_test_assertg7("and.aout");
} 

TEST_F(INSTRTest, bicc)
{
    do_test_assertg7("bicc.aout");
} 

TEST_F(INSTRTest, call)
{
    do_test_assertg7("call.aout");
} 

TEST_F(INSTRTest, div)
{
    do_test_assertg7("div.aout");
} 

TEST_F(INSTRTest, jmpl)
{
    do_test_assertg7("jmpl.aout");
} 

TEST_F(INSTRTest, ld)
{
    do_test_assertg7("ld.aout");
} 

TEST_F(INSTRTest, ldstub)
{
    do_test_assertg7("ldstub.aout");
} 

TEST_F(INSTRTest, mul)
{
    do_test_assertg7("mul.aout");
} 

TEST_F(INSTRTest, mulscc)
{
    do_test_assertg7("mulscc.aout");
} 

TEST_F(INSTRTest, _or)
{
    do_test_assertg7("or.aout");
} 

TEST_F(INSTRTest, rd_wr)
{
    do_test_assertg7("rd_wr.aout");
} 

TEST_F(INSTRTest, save_rest)
{
    do_test_assertg7("save_rest.aout");
} 

TEST_F(INSTRTest, sethi)
{
    do_test_assertg7("sethi.aout");
} 

TEST_F(INSTRTest, shift)
{
    do_test_assertg7("shift.aout");
} 

TEST_F(INSTRTest, st)
{
    do_test_assertg7("st.aout");
} 

TEST_F(INSTRTest, sub)
{
    do_test_assertg7("sub.aout");
} 

TEST_F(INSTRTest, swap)
{
    do_test_assertg7("swap.aout");
} 

TEST_F(INSTRTest, tadd)
{
    do_test_assertg7("tadd.aout");
} 

TEST_F(INSTRTest, ticc)
{
    do_test_assertg7("ticc.aout");
} 

TEST_F(INSTRTest, tsub)
{
    do_test_assertg7("tsub.aout");
} 

TEST_F(INSTRTest, _xor)
{
    do_test_assertg7("xor.aout");
} 

// Floating OP tests

/*
TEST_F(INSTRTest, _fbfcc)
{
    do_test_assertg7("fbfcc.aout");
} 

TEST_F(INSTRTest, _fld)
{
    do_test_assertg7("fld.aout");
} 
*/


// C - tests
TEST_F(INSTRTest, mmu_fault_traps)
{
    do_test_assertg7("mmu_fault_traps.aout");
} 


