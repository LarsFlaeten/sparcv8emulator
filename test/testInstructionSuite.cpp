#include "../src/sparcv8/CPU.h"
#include "../src/sparcv8/MMU.h"
#include "../src/readelf.cpp"



#include <gtest/gtest.h>

#include <cmath>

std::string path = "../test/asm/";


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
};



INSTRTest::INSTRTest()
    : cpu()

{  
    cpu.SetVerbose(true);
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

    // Read the ELF and get the entry point, then reset
    u32 entry_va = 0x0; 
    u32 word_count = ReadElf(path + "add.aout", cpu, entry_va); 
    ASSERT_GT(word_count, 0);
    cpu.Reset(entry_va);
    
    RunSummary rs;
    cpu.Run(0, &rs); 
} 
