#include "../src/sparcv8/CPU.h"
#include "../src/sparcv8/MMU.h"


#include <gtest/gtest.h>

#include <cmath>

/* MMU Tables provided by the Linker script */
u32 _mmu_ctx_table[256];
u32 _mmu_ctx0_level1[256];
u32 _mmu_ctx0_fc_level2[64];
u32 _mmu_ctx0_ffd_level3[64];



struct AMBA_mock {
        
    u32 Read(u32 va) {
         return m[va];

    }

    void Write(u32 va, u32 value) {
        m[va] = value;

    }

    std::map<u32, u32> m;
};


class MMUTest : public ::testing::Test {

protected:
    MMUTest();

    virtual ~MMUTest();

    // Code here will be called immediately after the constructor (right
    // before each test).
    virtual void SetUp();

    // Code here will be called immediately after each test (right
    // before the destructor).
    virtual void TearDown();
    IRQMP intc;
    MCtrl mctrl;
    CPU cpu;
    
    void do_LDA_instr(u32 rs1, u32 rs2, u32 rd, u32 asi) {
        u32 op3 = 0b010000; // LDA
        do_op3_instr(3, op3, rs1, rs2, rd, asi);
    }
 
    void do_LD_instr(u32 rs1, u32 rs2, u32 rd) {
        u32 op3 = 0b000000;
        do_op3_instr(3, op3, rs1, rs2, rd, 0);
    }
 
    void do_STA_instr(u32 rs1, u32 rs2, u32 rd, u32 asi) {
        u32 op3 = 0b010100; // STA
        do_op3_instr(3, op3, rs1, rs2, rd, asi);
    }
 
    void do_ST_instr(u32 rs1, u32 rs2, u32 rd) {
        u32 op3 = 0b000100; // STA
        do_op3_instr(3, op3, rs1, rs2, rd, 0);
    }
 
    void do_op3_instr(u32 op, u32 op3, u32 rs1, u32 rs2, u32 rd, u32 asi) {
        DecodeStruct d;
        d.opcode = ((op & LOBITS2) << FMTSTARTBIT) 
            | ((rd & LOBITS5) << RDSTARTBIT)
            | ((op3 & LOBITS6) << OP3STARTBIT)
            | ((rs1) << RS1STARTBIT)
            | (0x0 << ISTARTBIT)
            | (asi << ASISTARTBIT)
            | ((rs2)<< RS2STARTBIT); 
    
        d.psr = cpu.get_psr(); 
        d.p = (pPSR_t)&(d.psr);
       
        cpu.decode(&d);
        
        (cpu.*d.function)(&d);
        cpu.write_back(&d); 
    }


};



MMUTest::MMUTest() : intc(1), cpu(mctrl, intc, 0)
{  
   	


}

MMUTest::~MMUTest()
{

}

void MMUTest::SetUp()
{
    mctrl.clear_banks();

    mctrl.attach_bank<RamBank>(0x60000000, 0x01000000);
    mctrl.attach_bank<RamBank>(0x00000000, 1024 * 1024);
    
    
    // Read the ELF and get the entry point, then reset
    u32 entry_va = 0x0; 
    cpu.reset(entry_va);

    
     
}

void MMUTest::TearDown()
{
}

TEST_F(MMUTest, TestBitsAndBytes)
{    
}

TEST_F(MMUTest, PTEvailidity)
{    
    u32 pte = 0;
    ASSERT_FALSE(TLB::is_valid(pte));

    pte = pte | 0x1;
    ASSERT_FALSE(TLB::is_valid(pte));

    pte = 0x2;
    ASSERT_TRUE(TLB::is_valid(pte));

    pte = 0b1100100100100010;
    ASSERT_TRUE(TLB::is_valid(pte));

    pte = 0b1100100100100011;
    ASSERT_FALSE(TLB::is_valid(pte));

    pte = 0b1100100100100001;
    ASSERT_FALSE(TLB::is_valid(pte));

    pte = 0b1100100100100000;
    ASSERT_FALSE(TLB::is_valid(pte));

}

TEST_F(MMUTest, TestTLBAddressMasks)
{    
    u8 level = 0;
    ASSERT_THROW(MMU::get_addr_level_mask(level), std::logic_error);

    level = 1;
    auto mask = MMU::get_addr_level_mask(level);
    ASSERT_EQ(mask, 0xff << 24); // Mask should be 8 bits starting from bit 24
    auto page_sz = ~mask + 1;
    ASSERT_EQ(page_sz, 16 * 1024 * 1024); // page size on level 1 is 16 MB

    level = 2;
    mask = MMU::get_addr_level_mask(level);
    ASSERT_EQ(mask, 0x3fff << 18); // Mask should be 8 + 6 bits starting from bit 18
    page_sz = ~mask + 1;
    ASSERT_EQ(page_sz, 256 * 1024); // Page size on level 2 is 256 kb

    level = 3;
    mask = MMU::get_addr_level_mask(level);
    ASSERT_EQ(mask, 0xfffff << 12); // Mask should be 8 + 6 + 6 bits starting from bit 12
    page_sz = ~mask + 1;
    ASSERT_EQ(page_sz, 4 * 1024); // Page size on level 4 is 4 kb


}

TEST_F(MMUTest, MMUInitAndChangeReg)
{   
    auto& mmu = cpu.get_mmu();
/*    SDRAM2 RAM(0x001000000);  // IO: 0x0, 16 MB of RAM


    // Set up IO mapping
    // TODO: Move this MMU functions?
    for(unsigned a = 0x0; a < 0x100; ++a)
        mmu.IOmap[a] = { [&](u32 i)          { return RAM.Read(i/4); },
                         [&](u32 i, u32 v)   { RAM.Write(i/4, v);    } };

    // Read the ELF and get the entry point, then reset
    u32 entry_va = 0x0; 
    cpu.Reset(entry_va);
 */
   
    EXPECT_FALSE(mmu.GetEnabled());
 

    // adress to indicate MMU op is taken from LOCALREG4
    cpu.write_reg(0x00011, LOCALREG0); //0x0000 is VA/adress in MMU regs
    cpu.write_reg(0x00000, LOCALREG4); 
    cpu.write_reg(0x00000, LOCALREG5); // [L4] + [l5] is value to write


    do_STA_instr(LOCALREG4, LOCALREG5, LOCALREG0, 0x19);

    // MMU shuld now have set new register value:
    EXPECT_EQ(mmu.GetControlReg(), 0x0011);


}

TEST_F(MMUTest, MMUEnableDisable)
{     
    auto& mmu = cpu.get_mmu();

    EXPECT_FALSE(mmu.GetEnabled());
 
    // adress to indicate MMU op is taken from LOCALREG4
    cpu.write_reg(0x00011, LOCALREG0); //0x0000 is VA/adress in MMU regs
    cpu.write_reg(0x00000, LOCALREG4); 
    cpu.write_reg(0x00000, LOCALREG5); // [L4] + [l5] is value to write
 
    do_STA_instr(LOCALREG4, LOCALREG5, LOCALREG0, 0x19);

    // MMU shuld now have set enabled bit to 1:
    EXPECT_TRUE(mmu.GetEnabled());

    cpu.write_reg(0x00010, LOCALREG0); //0x0000 is VA/adress in MMU regs
 
    do_STA_instr(LOCALREG4, LOCALREG5, LOCALREG0, 0x19);
    // MMU shuld now have set enabled bit to 0:
    EXPECT_FALSE(mmu.GetEnabled());


}


TEST_F(MMUTest, MMUInitCtxTblPtr)
{
    auto& mmu = cpu.get_mmu();

    mmu.reset();

    // MMU shuld now have set new register value:
    EXPECT_EQ(mmu.GetCtxTblPtr(), 0x0);

    // adress to indicate MMU op is taken from LOCALREG4
    cpu.write_reg(0x4000200, LOCALREG0); //0x...... is value to write
    cpu.write_reg(0x00100, LOCALREG4); 
    cpu.write_reg(0x00000, LOCALREG5); // [L4] + [l5] is va adress to MMU regs
 
    do_STA_instr(LOCALREG4, LOCALREG5, LOCALREG0, 0x19);

    // MMU shuld now have set new register value:
    EXPECT_EQ(mmu.GetCtxTblPtr(), 0x4000200);

}

TEST_F(MMUTest, MMUInitSetContext)
{  
    auto& mmu = cpu.get_mmu();

    mmu.reset();
    EXPECT_EQ(mmu.GetCtxNumber(), 0x0);
 
    // adress to indicate MMU op is taken from LOCALREG4
    cpu.write_reg(0x42, LOCALREG0); //0x0042 is value to write
    cpu.write_reg(0x00100, LOCALREG4); 
    cpu.write_reg(0x00100, LOCALREG5); // [L4] + [l5] is va (MMU regs)
 
    do_STA_instr(LOCALREG4, LOCALREG5, LOCALREG0, 0x19);

    // MMU shuld now ave new context:
    EXPECT_EQ(mmu.GetCtxNumber(), 0x42);

}

TEST_F(MMUTest, MMUAMBAMock)
{   
    AMBA_mock amba;

    amba.Write(0xfffffff0, 0x07401039);

    auto v = amba.Read(0xfffffff0);

    ASSERT_EQ(v, 0x07401039);

    // Non existent key shoul return 0:
    auto v2 = amba.Read(0xffffff00);
    ASSERT_EQ(v2, 0x0);

} 
TEST_F(MMUTest, MMUBypassRAMReadWrite)
{   
    auto& mmu = cpu.get_mmu();

    auto& b = mctrl.attach_bank<RamBank>(0xfff00000, 0x100000);
    
    fprintf(stderr, "bank base=%08x size=%08x limit64=%lx end64=%llx\n",
        b.get_base(),
        b.get_size(),
        b.get_end_exclusive(),               // if you still have it
        (unsigned long long)(uint64_t(b.get_base()) + uint64_t(b.get_size())));
    
    mctrl.write32(0xfffffff0, 0x07401039);

    auto v = mmu.MemAccessBypassRead4(0xfffffff0);
    ASSERT_EQ(v, 0x07401039);
    ASSERT_TRUE(CROSS_ENDIAN); 

    for(u32 a = 0xfff00000; a < 0xfffffff0; a += 0x10) {
        mmu.MemAccessBypassWrite4(a, a);
    }

    for(u32 a = 0xfff00000; a < 0xfffffff0; a += 0x10) {
        auto v = mmu.MemAccessBypassRead4(a);
    
        ASSERT_EQ(v, a);
    }



} 

TEST_F(MMUTest, MMUBypassRAMReadWrite2)
{   
    auto& mmu = cpu.get_mmu();

    auto& b = mctrl.attach_bank<RamBank>(0xfff00000, 0x100000);
    fprintf(stderr, "bank base=%08x size=%08x limit64=%lx end64=%llx\n",
        b.get_base(),
        b.get_size(),
        b.get_end_exclusive(),               // if you still have it
        (unsigned long long)(uint64_t(b.get_base()) + uint64_t(b.get_size())));
    
    // MMU shuld be off for this test, as we mix bypass
    // and normal MMU ops (without virtual mapping):
    EXPECT_FALSE(mmu.GetEnabled());


    
    mctrl.write32(0xfffffff0, 0x07401039);

    u32 v; mmu.MemAccess<intent_load>(0xfffffff0, v, true);
    ASSERT_EQ(v, 0x07401039);
    ASSERT_TRUE(CROSS_ENDIAN); 

    for(u32 a = 0xfff00000; a < 0xfffffff0; a += 0x10) {
        mmu.MemAccessBypassWrite4(a, a);
    }

    for(u32 a = 0xfff00000; a < 0xfffffff0; a += 0x10) {
        u32 v; mmu.MemAccess<intent_load>(a, v, true);
    
        ASSERT_EQ(v, a);
    }



}

TEST_F(MMUTest, CacheControlregs)
{
    auto& mmu = cpu.get_mmu();

    u32 ccr = mmu.GetCCR();
    u32 iccr = mmu.GetICCR();
    u32 dccr = mmu.GetDCCR();

    ASSERT_EQ(ccr, 0);
    ASSERT_EQ(iccr, 0);
    ASSERT_EQ(dccr, 0);

    // FROM TSIM:
    // Register     Register description              Value
    // --------     --------------------              -----
    // cctrl        Cache control register            0x00020000
    // icfg         Icache config register            0x10220008
    // dcfg         Dcache config register            0x18220008
    // asr17        Processor config register         0x00000d07

    mmu.SetCCR(0x00020000);
    mmu.SetICCR(0x10220008);
    mmu.SetDCCR(0x18220008);


    ccr = mmu.GetCCR();
    iccr = mmu.GetICCR();
    dccr = mmu.GetDCCR();

    ASSERT_EQ(ccr, 0x00020000);
    ASSERT_EQ(iccr, 0x10220008);
    ASSERT_EQ(dccr, 0x18220008);





}

// Check TLB on 4Kb page sizes (lvl 3) with different intents, one context
TEST_F(MMUTest, MMUTLBCacheLvl3)
{
    auto& mmu = cpu.get_mmu();

    auto ctx = mmu.GetCtxNumber();
    ASSERT_EQ(ctx, 0);
	u32 pte = 0;
    u8 level = 0;
	u32 va = 0x60000d90;
	u32 PTE_i = 0x600001e;
	u32 PTE_d = 0x800001e;

    // Instruction TLB should miss
	auto found = mmu.get_itlb().lookup(ctx, va, pte, level);
	ASSERT_FALSE(found);
    ASSERT_EQ(pte, 0);
	
	// Cache the pa:
	mmu.get_itlb().insert(ctx, va, 3, PTE_i);

    // Instruction TLB should not miss
	found = mmu.get_itlb().lookup(ctx, va, pte, level);
	ASSERT_TRUE(found);
	ASSERT_EQ(pte, PTE_i);
    ASSERT_EQ(level, 3);
	
	// R/W tlb, same context
	found = mmu.get_dtlb().lookup(ctx, va, pte, level);
	ASSERT_FALSE(found);
    ASSERT_EQ(pte, 0);
	
    mmu.get_dtlb().insert(ctx, va, 3, PTE_d);
	found = mmu.get_dtlb().lookup(ctx, va, pte, level);
	ASSERT_TRUE(found);
	ASSERT_EQ(pte, PTE_d);
    ASSERT_EQ(level, 3);
	
	// Go through a whole memory page, should all get back TLB on the same page
	va = 0x60000000;
	u32 pte_out_i = 0;
    u32 pte_out_d = 0;
    u8 level_out_i = 0;
    u8 level_out_d = 0; 	
    for(int i = 0; i < 0xfff/4; i++) {
        auto reti = mmu.get_itlb().lookup(ctx, va, pte_out_i, level_out_i);
	    auto retd = mmu.get_dtlb().lookup(ctx, va, pte_out_d, level_out_d);
        ASSERT_TRUE(reti);
        ASSERT_TRUE(retd);
        ASSERT_EQ(level_out_i, 3);
		ASSERT_EQ(level_out_d, 3);
        ASSERT_EQ(pte_out_i, PTE_i);
		ASSERT_EQ(pte_out_d, PTE_d);
	    
        va += 4;
    }

    
	va += 4; // This should tip over to next page, and result in a miss
	found = mmu.get_itlb().lookup(ctx, va, pte_out_i, level_out_i);
    ASSERT_FALSE(found);
    ASSERT_EQ(pte_out_i, 0);
    
	// Cache a few other pages:
	mmu.get_dtlb().insert(0, 0x60001000, 3, 0x600011e);
	mmu.get_dtlb().insert(0, 0x60002000, 3, 0x600021e);
	mmu.get_dtlb().insert(0, 0x60003000, 3, 0x600031e);

	found = mmu.get_dtlb().lookup(0, 0x60001ff0, pte_out_d, level_out_d);
	ASSERT_EQ(pte_out_d,  0x600011e);
	ASSERT_EQ(level_out_d,  3);
	ASSERT_TRUE(found);
	
    found = mmu.get_dtlb().lookup(0, 0x60002ff0, pte_out_d, level_out_d);
	ASSERT_EQ(pte_out_d,  0x600021e);
	ASSERT_EQ(level_out_d,  3);
	ASSERT_TRUE(found);

    found = mmu.get_dtlb().lookup(0, 0x60003ff0, pte_out_d, level_out_d);
	ASSERT_EQ(pte_out_d,  0x600031e);
	ASSERT_EQ(level_out_d,  3);
	ASSERT_TRUE(found);
	
    // Innsert 12 more to invalidate the first one
    mmu.get_dtlb().insert(0, 0x60004000, 3, 0x600041E);
	mmu.get_dtlb().insert(0, 0x60005000, 3, 0x600051E);
	mmu.get_dtlb().insert(0, 0x60006000, 3, 0x600061E);
    mmu.get_dtlb().insert(0, 0x60007000, 3, 0x600071E);
	mmu.get_dtlb().insert(0, 0x60008000, 3, 0x600081E);
	mmu.get_dtlb().insert(0, 0x60009000, 3, 0x600091E);
    mmu.get_dtlb().insert(0, 0x6000A000, 3, 0x6000A1E);
	mmu.get_dtlb().insert(0, 0x6000B000, 3, 0x6000B1E);
	mmu.get_dtlb().insert(0, 0x6000C000, 3, 0x6000C1E);
    mmu.get_dtlb().insert(0, 0x6000D000, 3, 0x6000D1E);
	mmu.get_dtlb().insert(0, 0x6000E000, 3, 0x6000E1E);
	mmu.get_dtlb().insert(0, 0x6000F000, 3, 0x6000F1E);
    mmu.get_dtlb().insert(0, 0x60010000, 3, 0x600101E);

    // The first page shuold now be out of the cache
    //mmu.get_dtlb().debug_dump("DTLB");
	found = mmu.get_dtlb().lookup(0, 0x60000d90, pte_out_d, level_out_d);
	EXPECT_EQ(pte_out_d,  0x0);
	ASSERT_FALSE(found);
    
    
	// Cache on another intent, check that intent_load is unaffected:
	// Cache a few other pages:
	mmu.get_itlb().insert(0, 0x60007000, 3, 0xf00071e);
	mmu.get_itlb().insert(0, 0x60008000, 3, 0xf00081e);
	mmu.get_itlb().insert(0, 0x60009000, 3, 0xf00091e);

    found = mmu.get_dtlb().lookup(0, 0x60001ff0, pte_out_d, level_out_d);
	ASSERT_EQ(pte_out_d,  0x600011e);
	ASSERT_EQ(level_out_d,  3);
	ASSERT_TRUE(found);

    found = mmu.get_dtlb().lookup(0, 0x60002ff0, pte_out_d, level_out_d);
	ASSERT_EQ(pte_out_d,  0x600021e);
	ASSERT_EQ(level_out_d,  3);
	ASSERT_TRUE(found);

    found = mmu.get_dtlb().lookup(0, 0x60003ff0, pte_out_d, level_out_d);
	ASSERT_EQ(pte_out_d,  0x600031e);
	ASSERT_EQ(level_out_d,  3);
	ASSERT_TRUE(found);
	
    found = mmu.get_dtlb().lookup(0, 0x60000d90, pte_out_d, level_out_d);
	ASSERT_EQ(pte_out_d,  0);
	ASSERT_EQ(level_out_d,  0);
	ASSERT_FALSE(found);

    found = mmu.get_dtlb().lookup(0, 0x60007ff0, pte_out_d, level_out_d);
	ASSERT_EQ(pte_out_d,  0x600071e);
	ASSERT_EQ(level_out_d,  3);
	ASSERT_TRUE(found);

	// Checl that the instruction PTEs are cached
	found = mmu.get_itlb().lookup(0, 0x600070aa, pte_out_i, level_out_i);
	ASSERT_EQ(pte_out_i,  0xf00071e);
	ASSERT_TRUE(found);

    found = mmu.get_itlb().lookup(0, 0x60008fff, pte_out_i, level_out_i);
	ASSERT_EQ(pte_out_i,  0xf00081e);
	ASSERT_TRUE(found);

    found = mmu.get_itlb().lookup(0, 0x60009001, pte_out_i, level_out_i);
	ASSERT_EQ(pte_out_i,  0xf00091e);
	ASSERT_TRUE(found);
	

}

// Check TLB on all lvls, without direct mapping va to phys 
TEST_F(MMUTest, MMUTLBCacheAllLvls)
{
    auto& mmu = cpu.get_mmu();

    ASSERT_EQ(mmu.GetCtxNumber(), 0);
	
	u32 va = 0x60000d90;
	u32 PTE = 0x200001e;
    u32 pte_out_i = 0;
    u8 level_out_i = 0;

    bool found = mmu.get_itlb().lookup(0, va, pte_out_i, level_out_i);
	ASSERT_EQ(pte_out_i,  0);
	ASSERT_FALSE(found);
	
	// Cache the PTE on level 1:
	mmu.get_itlb().insert(0, va, 1, PTE);
 
    // we should now be bale to look up across 16 MB of linear memory without a TLB miss
	found = mmu.get_itlb().lookup(0, va, pte_out_i, level_out_i);
	ASSERT_EQ(pte_out_i, PTE);
	ASSERT_TRUE(found);

    found = mmu.get_itlb().lookup(0, 0x60ffffff, pte_out_i, level_out_i);
	ASSERT_EQ(pte_out_i, PTE);
	ASSERT_TRUE(found);

    found = mmu.get_itlb().lookup(0, 0x61000000, pte_out_i, level_out_i);
	ASSERT_EQ(pte_out_i, 0);
	ASSERT_FALSE(found);

	// Cache an new PTE on level 2, 0x61000000 - 0x6103FFFF, 256 KB range
    PTE = 0x110001e;
    mmu.get_itlb().insert(0, 0x61000000, 2, PTE);
    
    // we should now be able to look up across 256 kb of linear memory without a TLB miss
    found = mmu.get_itlb().lookup(0, 0x61000000, pte_out_i, level_out_i);
	ASSERT_EQ(pte_out_i, PTE);
	ASSERT_TRUE(found);

    found = mmu.get_itlb().lookup(0, 0x6103ffff, pte_out_i, level_out_i);
	ASSERT_EQ(pte_out_i, PTE);
	ASSERT_TRUE(found);
    
    found = mmu.get_itlb().lookup(0, 0x61040000, pte_out_i, level_out_i);
	ASSERT_EQ(pte_out_i, 0);
	ASSERT_FALSE(found);
    
	// Cache an new PTE on level 3, 0x62000000 - 0x62000FFF, 4 KB range
    PTE = 0xf00001e;
    mmu.get_itlb().insert(0, 0x62000000, 3, PTE);
    
    // we should now be able to look up across 4 kb of linear memory without a TLB miss
    found = mmu.get_itlb().lookup(0, 0x62000000, pte_out_i, level_out_i);
	ASSERT_EQ(pte_out_i, PTE);
	ASSERT_TRUE(found);

    found = mmu.get_itlb().lookup(0, 0x62000fff, pte_out_i, level_out_i);
	ASSERT_EQ(pte_out_i, PTE);
	ASSERT_TRUE(found);
    
    found = mmu.get_itlb().lookup(0, 0x62001000, pte_out_i, level_out_i);
	ASSERT_EQ(pte_out_i, 0);
	ASSERT_FALSE(found);
}


// Check TLB on lvls 1-2 with context switch
TEST_F(MMUTest, MMUTLBCacheLvl12_ctx_switch)
{
    auto& mmu = cpu.get_mmu();

    ASSERT_EQ(mmu.GetCtxNumber(), 0);
	
	u32 va = 0x60000d90;
	u32 PTE = 0x600001e;
    u32 pte_out_i = 0;
    u8 level_out_i = 0;

	bool found = mmu.get_itlb().lookup(0, va, pte_out_i, level_out_i);
	ASSERT_EQ(pte_out_i,  0);
	ASSERT_FALSE(found);

	// Cache the PTE on lvel 1:
	mmu.get_itlb().insert(0, va, 1, PTE);
 
    // we should now be bale to look up across 16 MB of linear memory without a TLB miss
	found = mmu.get_itlb().lookup(0, va, pte_out_i, level_out_i);
	ASSERT_EQ(pte_out_i, PTE);
	ASSERT_TRUE(found);

    found = mmu.get_itlb().lookup(0, 0x60ffffff, pte_out_i, level_out_i);
	ASSERT_EQ(pte_out_i, PTE);
	ASSERT_TRUE(found);

    found = mmu.get_itlb().lookup(0, 0x61000000, pte_out_i, level_out_i);
	ASSERT_EQ(pte_out_i, 0);
	ASSERT_FALSE(found);

	// Cache an new PTE on level 2, 0x61000000 - 0x61003FFF, 256 KB range
    // Cache an new PTE on level 2, 0x61000000 - 0x61003FFF, 256 KB range
    PTE = 0x110001e;
    mmu.get_itlb().insert(0, 0x61000000, 2, PTE);
    
    // we should now be able to look up across 256 kb of linear memory without a TLB miss
    found = mmu.get_itlb().lookup(0, 0x61000000, pte_out_i, level_out_i);
	ASSERT_EQ(pte_out_i, PTE);
	ASSERT_TRUE(found);

    found = mmu.get_itlb().lookup(0, 0x6103ffff, pte_out_i, level_out_i);
	ASSERT_EQ(pte_out_i, PTE);
	ASSERT_TRUE(found);
    
    found = mmu.get_itlb().lookup(0, 0x61040000, pte_out_i, level_out_i);
	ASSERT_EQ(pte_out_i, 0);
	ASSERT_FALSE(found);

    // Switch context, all TLBS should now be invalid
    mmu.SetCtxNumber(42);
    auto ctx = mmu.GetCtxNumber();
	ASSERT_EQ(ctx, 42);
	
    found = mmu.get_itlb().lookup(ctx, va, pte_out_i, level_out_i);
	ASSERT_EQ(pte_out_i, 0);
	ASSERT_FALSE(found);

    found = mmu.get_itlb().lookup(ctx, 0x60ffffff, pte_out_i, level_out_i);
	ASSERT_EQ(pte_out_i, 0);
	ASSERT_FALSE(found);

    found = mmu.get_itlb().lookup(ctx, 0x61000000, pte_out_i, level_out_i);
	ASSERT_EQ(pte_out_i, 0);
	ASSERT_FALSE(found);

    found = mmu.get_itlb().lookup(ctx, 0x61000000, pte_out_i, level_out_i);
	ASSERT_EQ(pte_out_i, 0);
	ASSERT_FALSE(found);

    found = mmu.get_itlb().lookup(ctx, 0x6103ffff, pte_out_i, level_out_i);
	ASSERT_EQ(pte_out_i, 0);
	ASSERT_FALSE(found);

    found = mmu.get_itlb().lookup(ctx, 0x61040000, pte_out_i, level_out_i);
	ASSERT_EQ(pte_out_i, 0);
	ASSERT_FALSE(found);
}

// Check that two contexts can use same va, but point to different pa
TEST_F(MMUTest, MMUTLBCacheLvl3_ctx_switch)
{
    auto& mmu = cpu.get_mmu();

    u32 pte_out_i;
    u8 level_out_i;

	// Cache an new PTE on level 3, 0x60000000 - 0x60003FFF, 4 KB range, 
    // points to 0xf0000000 physical memory
    u32 PTE = 0xf00001e;
    mmu.get_itlb().insert(0, 0x60000000, 3, PTE);
 
    PTE = 0xb00001e;
    mmu.get_itlb().insert(9, 0x60000000, 3, PTE);
 
    // we should now be able to look up across 4 kb of linear memory without a TLB miss
    bool found = mmu.get_itlb().lookup(0, 0x60000000, pte_out_i, level_out_i);
	ASSERT_EQ(pte_out_i, 0xf00001e);
	ASSERT_TRUE(found);

    found = mmu.get_itlb().lookup(0, 0x60000fff, pte_out_i, level_out_i);
	ASSERT_EQ(pte_out_i, 0xf00001e);
	ASSERT_TRUE(found);
    
    found = mmu.get_itlb().lookup(0, 0x60001000, pte_out_i, level_out_i);
	ASSERT_EQ(pte_out_i, 0);
	ASSERT_FALSE(found);

    // And get another physical mapping with the other context, but with same va
    found = mmu.get_itlb().lookup(9, 0x60000000, pte_out_i, level_out_i);
	ASSERT_EQ(pte_out_i, 0xb00001e);
	ASSERT_TRUE(found);

    found = mmu.get_itlb().lookup(9, 0x60000fff, pte_out_i, level_out_i);
	ASSERT_EQ(pte_out_i, 0xb00001e);
	ASSERT_TRUE(found);
    
    found = mmu.get_itlb().lookup(9, 0x60001000, pte_out_i, level_out_i);
	ASSERT_EQ(pte_out_i, 0);
	ASSERT_FALSE(found);

    mmu.get_dtlb().debug_dump("dTLB");
    mmu.get_itlb().debug_dump("iTLB");
}

TEST_F(MMUTest, MMUTLBCacheFlush)
{
    auto& mmu = cpu.get_mmu();

    ASSERT_EQ(mmu.GetCtxNumber(), 0);
	
	u32 va = 0x60000d90;
	u32 PTE = 0x600001e;
    u32 pte_out_i = 0;
    u8 level_out_i = 0;

	bool found = mmu.get_itlb().lookup(0, va, pte_out_i, level_out_i);
	ASSERT_EQ(pte_out_i,  0);
	ASSERT_FALSE(found);

	// Cache the PTE on lvel 1:
	mmu.get_itlb().insert(0, va, 1, PTE);
 
    // we should now be bale to look up across 16 MB of linear memory without a TLB miss
	found = mmu.get_itlb().lookup(0, va, pte_out_i, level_out_i);
	ASSERT_EQ(pte_out_i, PTE);
	ASSERT_TRUE(found);

    found = mmu.get_itlb().lookup(0, 0x60ffffff, pte_out_i, level_out_i);
	ASSERT_EQ(pte_out_i, PTE);
	ASSERT_TRUE(found);

    found = mmu.get_itlb().lookup(0, 0x61000000, pte_out_i, level_out_i);
	ASSERT_EQ(pte_out_i, 0);
	ASSERT_FALSE(found);

	// Cache an new PTE on level 2, 0x61000000 - 0x61003FFF, 256 KB range
    // Cache an new PTE on level 2, 0x61000000 - 0x61003FFF, 256 KB range
    PTE = 0x110001e;
    mmu.get_itlb().insert(0, 0x61000000, 2, PTE);
    
    // we should now be able to look up across 256 kb of linear memory without a TLB miss
    found = mmu.get_itlb().lookup(0, 0x61000000, pte_out_i, level_out_i);
	ASSERT_EQ(pte_out_i, PTE);
	ASSERT_TRUE(found);

    found = mmu.get_itlb().lookup(0, 0x6103ffff, pte_out_i, level_out_i);
	ASSERT_EQ(pte_out_i, PTE);
	ASSERT_TRUE(found);
    
    found = mmu.get_itlb().lookup(0, 0x61040000, pte_out_i, level_out_i);
	ASSERT_EQ(pte_out_i, 0);
	ASSERT_FALSE(found);
    // Flush Cache, all TLBS should now be invalid
    mmu.flush();
	
    found = mmu.get_itlb().lookup(0, 0x60000000, pte_out_i, level_out_i);
	ASSERT_EQ(pte_out_i, 0);
	ASSERT_FALSE(found);

    found = mmu.get_itlb().lookup(0, 0x60000d90, pte_out_i, level_out_i);
	ASSERT_EQ(pte_out_i, 0);
	ASSERT_FALSE(found);

    found = mmu.get_itlb().lookup(0, 0x60ffffff, pte_out_i, level_out_i);
	ASSERT_EQ(pte_out_i, 0);
	ASSERT_FALSE(found);

    found = mmu.get_itlb().lookup(0, 0x61000000, pte_out_i, level_out_i);
	ASSERT_EQ(pte_out_i, 0);
	ASSERT_FALSE(found);

    found = mmu.get_itlb().lookup(0, 0x61000000, pte_out_i, level_out_i);
	ASSERT_EQ(pte_out_i, 0);
	ASSERT_FALSE(found);

    found = mmu.get_itlb().lookup(0, 0x6103ffff, pte_out_i, level_out_i);
	ASSERT_EQ(pte_out_i, 0);
	ASSERT_FALSE(found);

    found = mmu.get_itlb().lookup(0, 0x61040000, pte_out_i, level_out_i);
	ASSERT_EQ(pte_out_i, 0);
	ASSERT_FALSE(found);



}




void mmu_table_init(MCtrl& mctrl, u32 end_of_mem);
void mmu_init(MMU& mmu);

#define SRMMU_PGDIR_MASK (~(SRMMU_PGDIR_SIZE-1))
#define SRMMU_PGDIR_SIZE (1UL << SRMMU_PGDIR_SHIFT)
#define SRMMU_PGDIR_SHIFT 24
#define SRMMU_PRIV 0x1c
#define SRMMU_VALID 0x02
#define SRMMU_CACHE 0x80
#define SRMMU_INVALID 0x0
#define SRMMU_ET_PTD 0x1
#define SRMMU_ET_PTE 0x2
#define SRMMU_ET_MASK 0x3
#define SRMMU_ACC_S_ALL	(0x7 << 2)
#define SRMMU_ACC_U_ALL	(0x3 << 2)

/* Constants set by SUN SPARC / Linux implementation. The mappings in this
 * area is inherited by Linux to Linux final MMU tables on startup
 */
#define CONFIG_LINUX_OPPROM_BEGVM 0xffd00000
#define CONFIG_LINUX_OPPROM_ENDVM 0xfff00000
/* Use 0xFFD00000-0xFFD3FFFF Segment to map PROM into addresses */
#define CONFIG_LINUX_OPPROM_SEGMENT ((0xffd00000 - 0xff000000) / 0x40000)



//* set RAM Base Address where the image will be loaded */
#ifdef CONFIG_RAM_START
#undef CONFIG_RAM_START
#endif
#define CONFIG_RAM_START 0x60000000

#define PAGE_SHIFT 12
#define PAGE_SIZE (1UL << PAGE_SHIFT)
#define PAGE_MASK (PAGE_SIZE-1)


TEST_F(MMUTest, MMUTables)
{
    auto& mmu = cpu.get_mmu();

    // MMU shuld be off in the start
    ASSERT_FALSE(mmu.GetEnabled());

 	// Point the MMU table pointer to the correct location in RAM:
	// MUMU Tables base: 0x60002000 (ctx = 0)
	_mmu_ctx_table[0]   = 0x60002000;
	_mmu_ctx0_level1[0]    = 0x60002400;
    for(int i = 1; i < 256; ++i)
        _mmu_ctx0_level1[i] = _mmu_ctx0_level1[i-1] + 4;
	
    _mmu_ctx0_fc_level2[0] = 0x60002800;
	for(int i = 1; i < 64; ++i)
        _mmu_ctx0_fc_level2[i] = _mmu_ctx0_fc_level2[i-1] + 4;
	
    _mmu_ctx0_ffd_level3[0] = 0x60002900;
    for(int i = 1; i < 64; ++i)
        _mmu_ctx0_ffd_level3[i] = _mmu_ctx0_ffd_level3[i-1] + 4;
	
    // Test some values are written to correct location
    mmu.MemAccessBypassWrite4(0x60002000, 0xcafebabe);
    ASSERT_EQ(mctrl.read32(_mmu_ctx_table[0]), 0xcafebabe);

    mctrl.write32(_mmu_ctx0_level1[0], 0xbaccecaf);
    mctrl.write32(_mmu_ctx0_level1[255], 0xdeadbeef);
    u32 r1 = mmu.MemAccessBypassRead4(0x60002400);
    u32 r2 = mmu.MemAccessBypassRead4(0x600027fc);
    ASSERT_EQ(r1, 0xbaccecaf);
    ASSERT_EQ(r2, 0xdeadbeef);




	mmu_table_init(mctrl, (u32)mctrl.find_bank(0x60000000)->get_end_exclusive());
	mmu_init(mmu);


    ASSERT_TRUE(mmu.GetEnabled());

    // Fill up RAM with the values of each words physical address:
    for(u32 pa = 0x60010000; pa < 0x61000000; pa += 4)
       mmu.MemAccessBypassWrite4(pa, pa); 

    // Check values
    for(u32 pa = 0x60010000; pa < 0x61000000; pa += 4) {
        u32 val = mmu.MemAccessBypassRead4(pa);
        ASSERT_EQ(val, pa);
    }

    // Read memory through MMU translation for main range (maps 1-1 to physical)
    for(u32 va = 0x60010000; va < 0x61000000; va += 4) {
        u32 val;
        int ret = mmu.MemAccess<intent_load>(va, val, CROSS_ENDIAN);
        ASSERT_EQ(ret, 0);
        ASSERT_EQ(val, va);
    }
 
    //debug_set_active_mmu(&mmu);
    //debug_mmu_tables();

    // Read memory through MMU translation for 
    // low 192MB range ( > our entire memory)
    // I.e:
    // 0xf0000000 - 0xf0ffffff mapped to 0x60000000 - 0x60ffffff
    // 0xf1000000 - 0xfbffffff mapped to 0x61000000 ++ (but will fail, outside physical memory)
    for(u32 va = 0xF0010000; va < 0xF1000000; va += 4) {
        u32 val;
        int ret = mmu.MemAccess<intent_load>(va, val, CROSS_ENDIAN);
        //std::cout << std::hex << "0x" << va << " --> " << "0x" << val << "(" << std::dec << ret << ")\n";
       if(va >= 0xf0010000 && va < 0xf1000000) { // Same as 0x60010000 ++
            ASSERT_EQ(ret, 0);
            ASSERT_EQ(val, va - 0x90000000);
       }
    
    }
    
    // Area 0xF1000000 - 0xFBFFFFFF mapped, but no physical memory there
    u32 val;
    int ret = mmu.MemAccess<intent_load>(0xF1000000, val, CROSS_ENDIAN);
    ASSERT_EQ(ret, -5); // Bus error
    ASSERT_EQ(mmu.GetFaultAddress(), 0xF1000000);

    ret = mmu.MemAccess<intent_load>(0xFBFFFFFC, val, CROSS_ENDIAN);
    ASSERT_EQ(ret, -5);
    //ASSERT_EQ(mmu.GetFaultAddress(), 0xFBFFF000); // Corresponing page
    ASSERT_EQ(mmu.GetFaultAddress(), 0xFBFFFFFC); // We now do full VA

    // Area not mapped,  * 0xFC000000-0xFFCFFFFF: Not Mapped
    ret = mmu.MemAccess<intent_load>(0xFC000000, val, CROSS_ENDIAN);
    ASSERT_EQ(ret, -1);
    ASSERT_EQ(mmu.GetFaultAddress(), 0xFC000000);

    ret = mmu.MemAccess<intent_load>(0xFFCFFFFC, val, CROSS_ENDIAN);
    ASSERT_EQ(ret, -1);
    //ASSERT_EQ(mmu.GetFaultAddress(), 0xFFCFF000);
    ASSERT_EQ(mmu.GetFaultAddress(), 0xFFCFFFFC); // We now do full VA


    // Area not mapped,   * 0xFFD3FFFF-0xFFFFFFFF: Not Mapped
    ret = mmu.MemAccess<intent_load>(0xFFD3FFFC, val, CROSS_ENDIAN);
    ASSERT_EQ(ret, -1);
    //ASSERT_EQ(mmu.GetFaultAddress(), 0xFFD3F000);
    ASSERT_EQ(mmu.GetFaultAddress(), 0xFFD3FFFC); // We now do full VA

    ret = mmu.MemAccess<intent_load>(0xFFFFFFFC, val, CROSS_ENDIAN);
    ASSERT_EQ(ret, -1);
    //ASSERT_EQ(mmu.GetFaultAddress(), 0xFFFFF000);
    ASSERT_EQ(mmu.GetFaultAddress(), 0xFFFFFFFC); // We now do full VA

    
    // Read memory through MMU translation for 81 kb PROM (mapped to end of ram)
    for(u32 va = 0xFFD00000; va < 0xFFD14000; va += 4) {
        u32 val;
        u32 ret = mmu.MemAccess<intent_load>(va, val, CROSS_ENDIAN);
    
        ASSERT_EQ(ret, 0);        
        ASSERT_EQ(val, va - 0xffd00000 + 0x61000000 - 0x14000);
    }
  
        
    // Check a few of the mappings we see from the ELF:
    // ps_move_startup 00008820  ffd03170  60c5c8a0  
    // TODO: Revisit this test, it faild but not sure it is correct
    
    //ret = mmu.MemAccess<intent_execute>(0xffd03170, val, CROSS_ENDIAN);
    //ASSERT_EQ(val, 0x60c5c8a0); 



}

TEST_F(MMUTest, MMUFaults)
{
    auto& mmu = cpu.get_mmu();

    // MMU shuld be off in the start
    ASSERT_FALSE(mmu.GetEnabled());

 	// Point the MMU table pointer to the correct location in RAM:
	// MUMU Tables base: 0x60002000 (ctx = 0)
	_mmu_ctx_table[0]   = 0x60002000;
	_mmu_ctx0_level1[0]    = 0x60002400;
    for(int i = 1; i < 256; ++i) {
        _mmu_ctx0_level1[i] = _mmu_ctx0_level1[i-1] + 4;
    }
	
    _mmu_ctx0_fc_level2[0] = 0x60002800;
	for(int i = 1; i < 64; ++i) {
        _mmu_ctx0_fc_level2[i] = _mmu_ctx0_fc_level2[i-1] + 4;
    }
	
    _mmu_ctx0_ffd_level3[0] = 0x60002900;
    for(int i = 1; i < 64; ++i) {
        _mmu_ctx0_ffd_level3[i] = _mmu_ctx0_ffd_level3[i-1] + 4;
    }

	mmu_table_init(mctrl, 0x61000000);
	mmu_init(mmu);
    ASSERT_TRUE(mmu.GetEnabled());




    u32 val;
    int ret;
    // Should flag unaligned:
    // Update:
    // Should NOT flag unaligned:
    // we drop the alignment check in the MMU access. This is handled by
    // LOAD/STORE instructions themselves, and traps, not MMU faults.
    ret = mmu.MemAccess<intent_load>(0x6000000F, val, CROSS_ENDIAN);
    ASSERT_EQ(ret, 0);
    //ASSERT_EQ(ret, -3);
    //ASSERT_EQ(mmu.GetFaultAddress(), 0x60000000); // Corresponding page

    // Get a level 3 and level 1 physical address we can play with
    u32 pa_l3 = mctrl.read32(_mmu_ctx0_ffd_level3[0]) >> 8;
    u32 pa_l1 = mctrl.read32(_mmu_ctx0_level1[96]) >> 8; // Start of RAM 0x60000000


    // Create supervisor page @ va 0xffd00000, try to access with user:
    mmu.flush();
    mctrl.write32(_mmu_ctx0_ffd_level3[0], (pa_l3 << 8) | SRMMU_ACC_S_ALL | SRMMU_ET_PTE);
    ret = mmu.MemAccess<intent_load>(0xffd00000, val, CROSS_ENDIAN, false);
    
    ASSERT_LT(ret, 0);
    ASSERT_EQ(mmu.GetFaultAddress(), 0xffd00000);
    ASSERT_EQ((mmu.GetFaultStatus() >> 2) & 0x7, 3); // Privilege error

    // Lvl 3 Supervisor page read only. Execute and write should fail, load shuold be fine
    mmu.flush();
    mctrl.write32(_mmu_ctx0_ffd_level3[0], (pa_l3 << 8) | (0x0 << 2) | SRMMU_ET_PTE);
    ret = mmu.MemAccess<intent_load>(0xffd00000, val, CROSS_ENDIAN, true);
    ASSERT_EQ(ret, 0);
 
    mmu.flush();
    ret = mmu.MemAccess<intent_store>(0xffd00000, val, CROSS_ENDIAN, true);
    ASSERT_LT(ret, 0);
    ASSERT_EQ(mmu.GetFaultAddress(), 0xffd00000);
    ASSERT_EQ((mmu.GetFaultStatus() >> 2) & 0x7, 2); // Protection error

    mmu.flush();
    ret = mmu.MemAccess<intent_execute>(0xffd00000, val, CROSS_ENDIAN, true);
    ASSERT_LT(ret, 0);
    ASSERT_EQ(mmu.GetFaultAddress(), 0xffd00000);
    ASSERT_EQ((mmu.GetFaultStatus() >> 2) & 0x3, 2); // Protection error

    // Lvl 1 Supervisor page read only. Execute and write should fail, load shuold be fine
    mmu.flush();
    mctrl.write32(_mmu_ctx0_level1[96], (pa_l1 << 8) | (0x0 << 2) | SRMMU_ET_PTE);
    ret = mmu.MemAccess<intent_load>(0x60000000, val, CROSS_ENDIAN, true);
    ASSERT_EQ(ret, 0);
 
    mmu.flush();
    ret = mmu.MemAccess<intent_store>(0x60000000, val, CROSS_ENDIAN, true);
    ASSERT_LT(ret, 0);
    ASSERT_EQ(mmu.GetFaultAddress(),  0x60000000);
    ASSERT_EQ((mmu.GetFaultStatus() >> 2) & 0x7, 2); // Protection error

    mmu.flush();
    ret = mmu.MemAccess<intent_execute>(0x60000000, val, CROSS_ENDIAN, true);
    ASSERT_LT(ret, 0);
    ASSERT_EQ(mmu.GetFaultAddress(), 0x60000000);
    ASSERT_EQ((mmu.GetFaultStatus() >> 2) & 0x7, 2); // Protection error

    // All Access types
    

    // ACC 0 - User and super read only
    mctrl.write32(_mmu_ctx0_level1[96], (pa_l1 << 8) | (0x0 << 2) | SRMMU_ET_PTE);
    
    mmu.flush();
    ret = mmu.MemAccess<intent_load>(0x60000000, val, CROSS_ENDIAN, true);
    ASSERT_EQ(ret, 0);
 
    mmu.flush();
    ret = mmu.MemAccess<intent_load>(0x60000000, val, CROSS_ENDIAN, false);
    ASSERT_EQ(ret, 0);
 
    mmu.flush();
    ret = mmu.MemAccess<intent_store>(0x60000000, val, CROSS_ENDIAN, true);
    ASSERT_LT(ret, 0);
 
    mmu.flush();
    ret = mmu.MemAccess<intent_store>(0x60000000, val, CROSS_ENDIAN, false);
    ASSERT_LT(ret, 0);
 
    mmu.flush();
    ret = mmu.MemAccess<intent_execute>(0x60000000, val, CROSS_ENDIAN, true);
    ASSERT_LT(ret, 0);
 
    mmu.flush();
    ret = mmu.MemAccess<intent_execute>(0x60000000, val, CROSS_ENDIAN, false);
    ASSERT_LT(ret, 0);
 
    // ACC 1 - User and super read/write
    mctrl.write32(_mmu_ctx0_level1[96], (pa_l1 << 8) | (0x1 << 2) | SRMMU_ET_PTE);
    
    mmu.flush();
    ret = mmu.MemAccess<intent_load>(0x60000000, val, CROSS_ENDIAN, true);
    ASSERT_EQ(ret, 0);
 
    mmu.flush();
    ret = mmu.MemAccess<intent_load>(0x60000000, val, CROSS_ENDIAN, false);
    ASSERT_EQ(ret, 0);
 
    mmu.flush();
    ret = mmu.MemAccess<intent_store>(0x60000000, val, CROSS_ENDIAN, true);
    ASSERT_EQ(ret, 0);
 
    mmu.flush();
    ret = mmu.MemAccess<intent_store>(0x60000000, val, CROSS_ENDIAN, false);
    ASSERT_EQ(ret, 0);
 
    mmu.flush();
    ret = mmu.MemAccess<intent_execute>(0x60000000, val, CROSS_ENDIAN, true);
    ASSERT_LT(ret, 0);
 
    mmu.flush();
    ret = mmu.MemAccess<intent_execute>(0x60000000, val, CROSS_ENDIAN, false);
    ASSERT_LT(ret, 0);
 
    // ACC 2 - User and super read/execute
    mctrl.write32(_mmu_ctx0_level1[96], (pa_l1 << 8) | (0x2 << 2) | SRMMU_ET_PTE);
    
    mmu.flush();
    ret = mmu.MemAccess<intent_load>(0x60000000, val, CROSS_ENDIAN, true);
    ASSERT_EQ(ret, 0);
 
    mmu.flush();
    ret = mmu.MemAccess<intent_load>(0x60000000, val, CROSS_ENDIAN, false);
    ASSERT_EQ(ret, 0);
 
    mmu.flush();
    ret = mmu.MemAccess<intent_store>(0x60000000, val, CROSS_ENDIAN, true);
    ASSERT_LT(ret, 0);
 
    mmu.flush();
    ret = mmu.MemAccess<intent_store>(0x60000000, val, CROSS_ENDIAN, false);
    ASSERT_LT(ret, 0);
 
    mmu.flush();
    ret = mmu.MemAccess<intent_execute>(0x60000000, val, CROSS_ENDIAN, true);
    ASSERT_EQ(ret, 0);
 
    mmu.flush();
    ret = mmu.MemAccess<intent_execute>(0x60000000, val, CROSS_ENDIAN, false);
    ASSERT_EQ(ret, 0);
 
    // ACC 3 - User and super read/write/execute
    mctrl.write32(_mmu_ctx0_level1[96], (pa_l1 << 8) | (0x3 << 2) | SRMMU_ET_PTE);
    
    mmu.flush();
    ret = mmu.MemAccess<intent_load>(0x60000000, val, CROSS_ENDIAN, true);
    ASSERT_EQ(ret, 0);
 
    mmu.flush();
    ret = mmu.MemAccess<intent_load>(0x60000000, val, CROSS_ENDIAN, false);
    ASSERT_EQ(ret, 0);
 
    mmu.flush();
    ret = mmu.MemAccess<intent_store>(0x60000000, val, CROSS_ENDIAN, true);
    ASSERT_EQ(ret, 0);
 
    mmu.flush();
    ret = mmu.MemAccess<intent_store>(0x60000000, val, CROSS_ENDIAN, false);
    ASSERT_EQ(ret, 0);
 
    mmu.flush();
    ret = mmu.MemAccess<intent_execute>(0x60000000, val, CROSS_ENDIAN, true);
    ASSERT_EQ(ret, 0);
 
    mmu.flush();
    ret = mmu.MemAccess<intent_execute>(0x60000000, val, CROSS_ENDIAN, false);
    ASSERT_EQ(ret, 0);
 
    // ACC 4 - User and super execute only
    mctrl.write32(_mmu_ctx0_level1[96], (pa_l1 << 8) | (0x4 << 2) | SRMMU_ET_PTE);
    
    mmu.flush();
    ret = mmu.MemAccess<intent_load>(0x60000000, val, CROSS_ENDIAN, true);
    ASSERT_LT(ret, 0);
 
    mmu.flush();
    ret = mmu.MemAccess<intent_load>(0x60000000, val, CROSS_ENDIAN, false);
    ASSERT_LT(ret, 0);
 
    mmu.flush();
    ret = mmu.MemAccess<intent_store>(0x60000000, val, CROSS_ENDIAN, true);
    ASSERT_LT(ret, 0);
 
    mmu.flush();
    ret = mmu.MemAccess<intent_store>(0x60000000, val, CROSS_ENDIAN, false);
    ASSERT_LT(ret, 0);
 
    mmu.flush();
    ret = mmu.MemAccess<intent_execute>(0x60000000, val, CROSS_ENDIAN, true);
    ASSERT_EQ(ret, 0);
 
    mmu.flush();
    ret = mmu.MemAccess<intent_execute>(0x60000000, val, CROSS_ENDIAN, false);
    ASSERT_EQ(ret, 0);
 
    // ACC 5 - User read only, super read/write
    mctrl.write32(_mmu_ctx0_level1[96], (pa_l1 << 8) | (0x5 << 2) | SRMMU_ET_PTE);
    
    mmu.flush();
    ret = mmu.MemAccess<intent_load>(0x60000000, val, CROSS_ENDIAN, true);
    ASSERT_EQ(ret, 0);
 
    mmu.flush();
    ret = mmu.MemAccess<intent_load>(0x60000000, val, CROSS_ENDIAN, false);
    ASSERT_EQ(ret, 0);
 
    mmu.flush();
    ret = mmu.MemAccess<intent_store>(0x60000000, val, CROSS_ENDIAN, true);
    ASSERT_EQ(ret, 0);
 
    mmu.flush();
    ret = mmu.MemAccess<intent_store>(0x60000000, val, CROSS_ENDIAN, false);
    ASSERT_LT(ret, 0);
 
    mmu.flush();
    ret = mmu.MemAccess<intent_execute>(0x60000000, val, CROSS_ENDIAN, true);
    ASSERT_LT(ret, 0);
 
    mmu.flush();
    ret = mmu.MemAccess<intent_execute>(0x60000000, val, CROSS_ENDIAN, false);
    ASSERT_LT(ret, 0);
 
    // ACC 6 - User no access, super read/execute
    mctrl.write32(_mmu_ctx0_level1[96], (pa_l1 << 8) | (0x6 << 2) | SRMMU_ET_PTE);
    
    mmu.flush();
    ret = mmu.MemAccess<intent_load>(0x60000000, val, CROSS_ENDIAN, true);
    ASSERT_EQ(ret, 0);
 
    mmu.flush();
    ret = mmu.MemAccess<intent_load>(0x60000000, val, CROSS_ENDIAN, false);
    ASSERT_LT(ret, 0);
 
    mmu.flush();
    ret = mmu.MemAccess<intent_store>(0x60000000, val, CROSS_ENDIAN, true);
    ASSERT_LT(ret, 0);
 
    mmu.flush();
    ret = mmu.MemAccess<intent_store>(0x60000000, val, CROSS_ENDIAN, false);
    ASSERT_LT(ret, 0);
 
    mmu.flush();
    ret = mmu.MemAccess<intent_execute>(0x60000000, val, CROSS_ENDIAN, true);
    ASSERT_EQ(ret, 0);
 
    mmu.flush();
    ret = mmu.MemAccess<intent_execute>(0x60000000, val, CROSS_ENDIAN, false);
    ASSERT_LT(ret, 0);
 
    // ACC 7 - User no access, super read/write/execute
    mctrl.write32(_mmu_ctx0_level1[96], (pa_l1 << 8) | (0x7 << 2) | SRMMU_ET_PTE);
    
    mmu.flush();
    ret = mmu.MemAccess<intent_load>(0x60000000, val, CROSS_ENDIAN, true);
    ASSERT_EQ(ret, 0);
 
    mmu.flush();
    ret = mmu.MemAccess<intent_load>(0x60000000, val, CROSS_ENDIAN, false);
    ASSERT_LT(ret, 0);
 
    mmu.flush();
    ret = mmu.MemAccess<intent_store>(0x60000000, val, CROSS_ENDIAN, true);
    ASSERT_EQ(ret, 0);
 
    mmu.flush();
    ret = mmu.MemAccess<intent_store>(0x60000000, val, CROSS_ENDIAN, false);
    ASSERT_LT(ret, 0);
 
    mmu.flush();
    ret = mmu.MemAccess<intent_execute>(0x60000000, val, CROSS_ENDIAN, true);
    ASSERT_EQ(ret, 0);
 
    mmu.flush();
    ret = mmu.MemAccess<intent_execute>(0x60000000, val, CROSS_ENDIAN, false);
    ASSERT_LT(ret, 0);

    // Access outside physical memory should have MMY set FT = 5:
    mmu.flush();
    ret = mmu.MemAccess<intent_load>(0x50000000, val, CROSS_ENDIAN, true);
    ASSERT_LT(ret, 0);

    auto fsr = mmu.GetFaultStatus();
    ASSERT_EQ((fsr >> 2) & LOBITS3, 5); // FT == 5, Access Bus Error
    ASSERT_EQ((fsr >> 1) & 0x1, 0x1); // FAV == 1, Fault Address Valid
    ASSERT_EQ((fsr >> 5) & LOBITS3, 0x1); // AT == 1, Load from super data
    

    




}   

TEST_F(MMUTest, MMUFaults_cpuOP)
{
    auto& mmu = cpu.get_mmu();

    // MMU shuld be off in the start
    ASSERT_FALSE(mmu.GetEnabled());

 	// Point the MMU table pointer to the correct location in RAM:
	// MUMU Tables base: 0x60002000 (ctx = 0)
	_mmu_ctx_table[0]   = 0x60002000;
	_mmu_ctx0_level1[0]    = 0x60002400;
    for(int i = 1; i < 256; ++i) {
        _mmu_ctx0_level1[i] = _mmu_ctx0_level1[i-1] + 4;
    }
    _mmu_ctx0_fc_level2[0] = 0x60002800;
	for(int i = 1; i < 64; ++i) {
        _mmu_ctx0_fc_level2[i] = _mmu_ctx0_fc_level2[i-1] + 4;
    }
    _mmu_ctx0_ffd_level3[0] = 0x60002900;
    for(int i = 1; i < 64; ++i){
        _mmu_ctx0_ffd_level3[i] = _mmu_ctx0_ffd_level3[i-1] + 4;
    }
	mmu_table_init(mctrl, 0x61000000);
	mmu_init(mmu);
    ASSERT_TRUE(mmu.GetEnabled());

    // Read MMU controlreg:
    cpu.write_reg(0x000,  LOCALREG1); // MMU Control reg...
    cpu.write_reg(0x0,  LOCALREG2); 
    cpu.write_reg(0x0,  LOCALREG3); 
   
    do_LDA_instr(LOCALREG1, LOCALREG2, LOCALREG3, ASI_M_MMUREGS );
    
    u32 mmu_ctrl;
    cpu.read_reg(LOCALREG3, &mmu_ctrl);
    ASSERT_EQ(mmu_ctrl, 0x1); // MMU Enabled..

    // Place some data in memory
    u32 val = 0xcafebabe;
    u32 ret = mmu.MemAccess<intent_store>(0x60000000, val, CROSS_ENDIAN, true);
    ASSERT_EQ(ret, 0);
  
 
    // read in supervisor mode:
    cpu.write_reg(0x60000000,  LOCALREG1); // An address to read. Mapped with ACC = 7, so superviso access should be fine
    do_LD_instr(LOCALREG1, LOCALREG2, LOCALREG3);

    cpu.read_reg(LOCALREG3, &val);
    ASSERT_EQ(val, 0xcafebabe);
    ASSERT_EQ(cpu.get_trap_type(), 0);

    // Change to user mode:
    auto psr = cpu.get_psr(); 
    psr = psr & ~(1 << 7);
    cpu.set_psr(psr);
    ASSERT_EQ((cpu.get_psr() >> 7) & 0x1, 0);
    mmu.flush();
 
    // The read of address 0x60000000 should now trap:
    cpu.write_reg(0x60000000,  LOCALREG1); 
    cpu.write_reg(0x0,  LOCALREG2); 
    cpu.write_reg(0x0,  LOCALREG3); 
    do_LD_instr(LOCALREG1, LOCALREG2, LOCALREG3);

    cpu.read_reg(LOCALREG3, &val);
    ASSERT_NE(val, 0xcafebabe);
    ASSERT_EQ(cpu.get_trap_type(), 0x9); // SPARC_DATA_ACCESS_EXCEPTION
    
    // Run the CPU through the trap:
    cpu.run(1, nullptr);
 
    // We can now read fault type and address from MMUREGS:
  
    // Change to super mode:
    psr = cpu.get_psr(); 
    psr = psr | (0x1 << 7);
    cpu.set_psr(psr);
    ASSERT_EQ((cpu.get_psr() >> 7) & 0x1, 0x1);

    cpu.write_reg(0x300,  LOCALREG1); // fault status reg
    cpu.write_reg(0x0,  LOCALREG2); // fault status reg
    cpu.write_reg(0x0,  LOCALREG3); // fault status reg
    do_LDA_instr(LOCALREG1, LOCALREG2, LOCALREG3, ASI_M_MMUREGS );
    u32 fsr; cpu.read_reg(LOCALREG3, &fsr);
 
    cpu.write_reg(0x400,  LOCALREG1); // fault address reg
    do_LDA_instr(LOCALREG1, LOCALREG2, LOCALREG3, ASI_M_MMUREGS );
    u32 far; cpu.read_reg(LOCALREG3, &far);
    
    ASSERT_EQ( (fsr >> 1) & 0x1, 0x1); // FAV, i.e. fault address is available
    ASSERT_EQ( (fsr >> 2) & 0x7, 0x3); // FT = 3, Privelege violation
    ASSERT_EQ( (fsr >> 5) & 0x7, 0x0); // AT = 0, Load from user data space
    ASSERT_EQ( far, 0x60000000); // The page
    mmu.flush();


    // Ok, set nofault on the MMU. We shuld still not get the value, but the MMU should not Trap
    // Read MMU controlreg:
    cpu.write_reg(0x000,  LOCALREG1); // MMU Control reg...
    cpu.write_reg(0x0,  LOCALREG2); 
    cpu.write_reg(0x0,  LOCALREG3); 
  
    ASSERT_FALSE(mmu.GetNoFault());
        

    do_LDA_instr(LOCALREG1, LOCALREG2, LOCALREG3, ASI_M_MMUREGS );
    cpu.read_reg(LOCALREG3, &mmu_ctrl);
    mmu_ctrl = mmu_ctrl | 0x2; // Set nofault bit
    cpu.write_reg(mmu_ctrl, LOCALREG3);
    do_STA_instr(LOCALREG1, LOCALREG2, LOCALREG3, ASI_M_MMUREGS );
   
    // Check that we got the control reg right:
    u32 c = mmu.GetControlReg();
    ASSERT_EQ( c & 0x2, 0x2);
    ASSERT_TRUE(mmu.GetNoFault());

    // Change to user mode:
    psr = cpu.get_psr(); 
    psr = psr & ~(0x1 << 7);
    cpu.set_psr(psr);
    ASSERT_EQ((cpu.get_psr() >> 7) & 0x1, 0x0);


    // The read of address 0x60000000 should not trap, but we shold still have a fault:
    cpu.write_reg(0x60000000,  LOCALREG1); 
    cpu.write_reg(0x0,  LOCALREG2); 
    cpu.write_reg(0x0,  LOCALREG3); 
    do_LD_instr(LOCALREG1, LOCALREG2, LOCALREG3);

    cpu.read_reg(LOCALREG3, &val);
    ASSERT_NE(val, 0xcafebabe);
    ASSERT_EQ(cpu.get_trap_type(), 0x0);
 
}

/* The Below Code sets up the Virtual Address Space as follows:
 *
 * Only Context 0 valid. Context 0 address map using 3 levels:
 *
 * 0x00000000-0xEFFFFFFF: 1:1 mapping to physical
 * 0xF0000000-0xFBFFFFFF: Map to main memory (low 192MB memory only)
 * 0xFC000000-0xFFCFFFFF: Not Mapped
 * 0xFFD00000-0xFFD3FFFF: Mapped to STARTUP/PROM code (last 128KB of RAM)
 * 0xFFD3FFFF-0xFFFFFFFF: Not Mapped
 */
void mmu_table_init(MCtrl& mctrl, u32 end_of_mem)
{
	u32 i;
	unsigned long page_va_start, page_va_end, page_cnt;
	unsigned long page_pa_start, page_pa_end;

	// From radelf image.ram
	unsigned long _startup_start = 0xffd00000;
	unsigned long _prom_end = 0xffd14000;
	//u32 start_ffd00000_pa;

	for (i = 0; i < 256; i++) {
		if (i < 64) {
			mctrl.write32(_mmu_ctx0_fc_level2[i], SRMMU_INVALID);
			mctrl.write32(_mmu_ctx0_ffd_level3[i], SRMMU_INVALID);
		}
		mctrl.write32(_mmu_ctx_table[i], SRMMU_INVALID);
		mctrl.write32(_mmu_ctx0_level1[i], SRMMU_INVALID);
	}

	/* Setup Context Table, Context 0, point to level 1 Table */
	//_mmu_ctx_table[0] = (((unsigned long)&_mmu_ctx0_level1[0] >> 4) &
	//			~SRMMU_ET_MASK) | SRMMU_ET_PTD;
	mctrl.write32(_mmu_ctx_table[0], ((0x60002400 >> 4) &
				~SRMMU_ET_MASK) | SRMMU_ET_PTD);


	/* Setup Level1 Context0 Address space. 16MB/Entry
	 *
	 * 0x00000000-0xEFFFFFFF: 1:1 mapping (non-cachable)
	 * 0xF0000000-0xFBFFFFFF: Map to RAM memory (low memory only)
	 * 0xFC000000-0xFEFFFFFF: INVALID
	 * 0xFF000000-0xFFFFFFFF: To Level2, see below...
	 */
	for (i = 0; i < 240; i++) {
		mctrl.write32(_mmu_ctx0_level1[i], (i << 20) | SRMMU_ACC_S_ALL |
					SRMMU_ET_PTE);
	}
	for (; i < 252; i++) {
		mctrl.write32(_mmu_ctx0_level1[i], ((CONFIG_RAM_START >> 4)+((i-240)<<20)) |
					SRMMU_CACHE | SRMMU_ACC_S_ALL |
					SRMMU_ET_PTE);
	}
	for (; i < 255; i++)
		mctrl.write32(_mmu_ctx0_level1[i], SRMMU_INVALID);
	//_mmu_ctx0_level1[255] = ((unsigned long)&_mmu_ctx0_fc_level2[0] >> 4) |
	//			SRMMU_ET_PTD;
	mctrl.write32(_mmu_ctx0_level1[255], (0x60002800 >> 4) |
				SRMMU_ET_PTD);


	/* Setup Level2, Context0, 0xFF000000-0xFFFFFFFF. 256KB/Entry
	 *
	 * 0xFF000000-0xFFD00000: INVALID
	 * 0xFFD00000-0xFFD3FFFF: To Level 3, see below
	 * 0xFFD3FFFF-0xFFFFFFFF: INVALID
	 */
	for (i = 0; i < 64; i++)
		mctrl.write32(_mmu_ctx0_fc_level2[i], SRMMU_INVALID);
	//_mmu_ctx0_fc_level2[CONFIG_LINUX_OPPROM_SEGMENT] =
	//	(((unsigned long)&_mmu_ctx0_ffd_level3[0]) >> 4) | SRMMU_ET_PTD;
	mctrl.write32(_mmu_ctx0_fc_level2[CONFIG_LINUX_OPPROM_SEGMENT],
		(0x60002900 >> 4) | SRMMU_ET_PTD);


	/* Setup Level3, Context0, 0xFFD00000-0xFFD3FFFF. 4KB/Entry
	 *
	 * 0xFFD00000-0xFFD3FFFF: Mapped to STARTUP/PROM code (End of RAM)
	 * 0xFFD3FFFF-0xFFFFFFFF: INVALID
	 */
	i = 0;
	page_va_start = (unsigned long)_startup_start;
	page_va_end = (unsigned long)_prom_end;
	page_cnt = (page_va_end - page_va_start) >> PAGE_SHIFT;
	page_pa_end = (end_of_mem + (PAGE_SIZE-1)) & ~PAGE_MASK;
	page_pa_start = page_pa_end - (page_cnt << PAGE_SHIFT);
	//start_ffd00000_pa = page_pa_start;
	for (i = 0; i < page_cnt; i++) {
		mctrl.write32(_mmu_ctx0_ffd_level3[i], (page_pa_start >> 4) | SRMMU_CACHE |
						SRMMU_ACC_S_ALL | SRMMU_ET_PTE);
		page_pa_start += PAGE_SIZE;
	}
	for (; i < 64; i++)
		mctrl.write32(_mmu_ctx0_ffd_level3[i], SRMMU_INVALID);
}


void mmu_init(MMU& mmu)
{
   
	mmu.SetCtxTblPtr((0x60002000 >> 4) & 0xfffffff0);
    
    // Flush TLB
    mmu.flush();

    // Enable MMU:
    u32 creg = mmu.GetControlReg();
    creg = creg | 0x1;
    mmu.SetControlReg(creg);
 
/*
	// Setup MMU
	srmmu_set_ctable_ptr((unsigned long)&_mmu_ctx_table[0]);
	srmmu_set_context(0);
	__asm__ __volatile__("flush\n\t");

	// Flush TLB
	leon_flush_tlb_all();

	// Enable MMU
	srmmu_set_mmureg(0x00000001);

	//Flush All Cache 
	leon_flush_cache_all();
*/
}

