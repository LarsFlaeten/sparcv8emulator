#include "../src/sparcv8/CPU.h"
#include "../src/sparcv8/MMU.h"
#include "../src/readelf.h"



#include <gtest/gtest.h>

#include <cmath>

std::string fpu_test_path = "../../test/asm/";


class INSTRFPUTest : public ::testing::Test {

protected:
    INSTRFPUTest();

    virtual ~INSTRFPUTest();

    // Code here will be called immediately after the constructor (right
    // before each test).
    virtual void SetUp();

    // Code here will be called immediately after each test (right
    // before the destructor).
    virtual void TearDown();
    IRQMP intc;    
    MCtrl mctrl;
    CPU cpu;
    
    
    void do_test_assertg7(const std::string& test) {
        // Read the ELF and get the entry point, then reset
        u32 entry_va = 0x0; 
        u32 word_count = ReadElf(fpu_test_path + test, mctrl, entry_va, false, std::cout); 
        ASSERT_GT(word_count, 0);
        cpu.reset(entry_va);
        
        RunSummary rs;
        cpu.run(0, &rs); 

        // Check if test hac placed GOOD (0x900d) or BAD (0xbad) in %g7
        u32 val; cpu.read_reg(GLOBALREG7, &val);
        if(val == 0xbad)
            FAIL() << "Test failed.\n";
        
        ASSERT_EQ(val, 0x900d); 


    }
};



INSTRFPUTest::INSTRFPUTest()
    : intc(1), cpu(mctrl, intc, 0)

{  
    
    mctrl.attach_bank<RamBank>(0x0, 16*1024*1024); // 16 MB @ 0x0
}

INSTRFPUTest::~INSTRFPUTest()
{

}

void INSTRFPUTest::SetUp()
{


}

void INSTRFPUTest::TearDown()
{
}

TEST_F(INSTRFPUTest, fld)
{
    do_test_assertg7("fld.aout");
} 

TEST_F(INSTRFPUTest, fbfcc)
{
    do_test_assertg7("fbfcc.aout");
}

TEST_F(INSTRFPUTest, fop1)
{
    do_test_assertg7("fop1.aout");
} 

TEST_F(INSTRFPUTest, fop)
{
    do_test_assertg7("fop.aout");
} 

TEST_F(INSTRFPUTest, fopd)
{
    do_test_assertg7("fopd.aout");
} 

