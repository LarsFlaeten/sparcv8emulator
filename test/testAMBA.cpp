#include "../src/sparcv8/CPU.h"
#include "../src/sparcv8/MMU.h"
#include "../src/peripherals/Amba.h"
#include "../src/peripherals/gaisler/ambapp.h"




#include <gtest/gtest.h>

#include <cmath>

#define GR712RC_SYSTEMID 0x7120e70
#define GR740_REV0_SYSTEMID 0x07401038
#define GR740_REV1_SYSTEMID 0x07401039


class AMBATest : public ::testing::Test {

protected:
    AMBATest();

    virtual ~AMBATest();

    // Code here will be called immediately after the constructor (right
    // before each test).
    virtual void SetUp();

    // Code here will be called immediately after each test (right
    // before the destructor).
    virtual void TearDown();

	MCtrl mctrl;
    MMU mmu;

};



AMBATest::AMBATest() : mmu(mctrl)
{  
   	


}

AMBATest::~AMBATest()
{

}

void AMBATest::SetUp()
{


}

void AMBATest::TearDown()
{
}

TEST_F(AMBATest, AHB_setup)
{
	mctrl.attach_bank<RomBank<64 * 1024>>(0xffff0000);
    mctrl.attach_bank<RomBank<4 * 1024>>(0x800ff000);

    amba_ahb_pnp_setup(mctrl);
    amba_apb_pnp_setup(mctrl);

    // Find the system version
    ASSERT_EQ(mmu.MemAccessBypassRead4(0xfffffff0), 0x0); 



    // mst0 Find the LEON processor
	u32 ahb_id = mmu.MemAccessBypassRead4(0xfffff000);
    ASSERT_EQ(ambapp_pnp_vendor(ahb_id), VENDOR_GAISLER);
 	ASSERT_EQ(ambapp_pnp_device(ahb_id), GAISLER_LEON3);
 	ASSERT_EQ(ambapp_pnp_ver(ahb_id), 0x0);
 	ASSERT_EQ(ambapp_pnp_irq(ahb_id), 0x0);

   
    // slv0 Find the Memory Controller
	ambapp_pnp_ahb ahbpp;
	ahbpp.id = mctrl.read32(0xfffff820);
	ahbpp.mbar[0] = mctrl.read32(0xfffff830);
	ahbpp.mbar[1] = mctrl.read32(0xfffff834);
	ahbpp.mbar[2] = mctrl.read32(0xfffff838);
    
	auto ahb = &ahbpp; 
 	ASSERT_EQ(ambapp_pnp_vendor(ahb->id), VENDOR_ESA);
 	ASSERT_EQ(ambapp_pnp_device(ahb->id), ESA_MCTRL);
 	ASSERT_EQ(ambapp_pnp_ver(ahb->id), 0x0);
 	ASSERT_EQ(ambapp_pnp_irq(ahb->id), 0x0);
	ASSERT_EQ(ambapp_pnp_mbar_start(ahb->mbar[0]), 0x00000000);
	ASSERT_EQ(ambapp_pnp_mbar_mask(ahb->mbar[0]), 0xe00);
    ASSERT_EQ(ambapp_pnp_mbar_type(ahb->mbar[0]), AMBA_TYPE_MEM);
	ASSERT_EQ(ambapp_pnp_mbar_start(ahb->mbar[1]), 0x20000000);
	ASSERT_EQ(ambapp_pnp_mbar_mask(ahb->mbar[1]), 0xe00);
    ASSERT_EQ(ambapp_pnp_mbar_type(ahb->mbar[1]), AMBA_TYPE_MEM);
	ASSERT_EQ(ambapp_pnp_mbar_start(ahb->mbar[2]), 0x40000000);
	ASSERT_EQ(ambapp_pnp_mbar_mask(ahb->mbar[2]), 0xc00);
    ASSERT_EQ(ambapp_pnp_mbar_type(ahb->mbar[2]), AMBA_TYPE_MEM);



    // slv1 Find the APB Bridge @ 0xfffff800
    ahbpp.id = mctrl.read32(0xfffff800);
	ahbpp.mbar[0] = mctrl.read32(0xfffff810);
	ahbpp.mbar[1] = mctrl.read32(0xfffff814);
	ahbpp.mbar[2] = mctrl.read32(0xfffff818);

	ASSERT_EQ(ambapp_pnp_vendor(ahb->id), VENDOR_GAISLER);
 	ASSERT_EQ(ambapp_pnp_device(ahb->id), GAISLER_APBMST);
 	ASSERT_EQ(ambapp_pnp_ver(ahb->id), 0x0);
 	ASSERT_EQ(ambapp_pnp_irq(ahb->id), 0x0);
	ASSERT_EQ(ambapp_pnp_mbar_start(ahb->mbar[0]), 0x80000000);
	ASSERT_EQ(ambapp_pnp_mbar_mask(ahb->mbar[0]), 0xfff);
	ASSERT_EQ(ambapp_pnp_mbar_type(ahb->mbar[0]), AMBA_TYPE_MEM); //AMBA_TYPE_APBIO);

    // slv1 Find the slaves under the APB Bridge
	ambapp_pnp_apb apbpp;
	apbpp.id = mctrl.read32(0x800ff000);
	apbpp.iobar = mctrl.read32(0x800ff004);
    ambapp_pnp_apb* apb = &apbpp; 
 	ASSERT_EQ(ambapp_pnp_vendor(apb->id), VENDOR_ESA);
 	ASSERT_EQ(ambapp_pnp_device(apb->id), ESA_MCTRL);
 	ASSERT_EQ(ambapp_pnp_ver(apb->id), 0x0);
 	ASSERT_EQ(ambapp_pnp_irq(apb->id), 0x0);
	ASSERT_EQ(ambapp_pnp_apb_start(apb->iobar, 0x80000000), 0x80000000);
	//ASSERT_EQ(ambapp_pnp_apb_mask(apb->iobar), 0xff);
	ASSERT_EQ(ambapp_pnp_mbar_type(apb->iobar), AMBA_TYPE_APBIO);

    // slv2 
	apbpp.id = mctrl.read32(0x800ff008);
	apbpp.iobar = mctrl.read32(0x800ff00c);
 	ASSERT_EQ(ambapp_pnp_vendor(apb->id), VENDOR_GAISLER);
 	ASSERT_EQ(ambapp_pnp_device(apb->id), GAISLER_APBUART);
 	ASSERT_EQ(ambapp_pnp_ver(apb->id), 0x1);
 	ASSERT_EQ(ambapp_pnp_irq(apb->id), 0x2);
	ASSERT_EQ(ambapp_pnp_apb_start(apb->iobar, 0x80000100), 0x80000100);
	//ASSERT_EQ(ambapp_pnp_apb_mask(apb->iobar), 0xff);
	ASSERT_EQ(ambapp_pnp_mbar_type(apb->iobar), AMBA_TYPE_APBIO);

    // slv3 
 	apbpp.id = mctrl.read32(0x800ff010);
	apbpp.iobar = mctrl.read32(0x800ff014);
 	ASSERT_EQ(ambapp_pnp_vendor(apb->id), VENDOR_GAISLER);
 	ASSERT_EQ(ambapp_pnp_device(apb->id), GAISLER_IRQMP);
 	ASSERT_EQ(ambapp_pnp_ver(apb->id), 0x0);
 	ASSERT_EQ(ambapp_pnp_irq(apb->id), 0x0);
	ASSERT_EQ(ambapp_pnp_apb_start(apb->iobar, 0x80000200), 0x80000200);
	//ASSERT_EQ(ambapp_pnp_apb_mask(apb->iobar), 0xff);
	ASSERT_EQ(ambapp_pnp_mbar_type(apb->iobar), AMBA_TYPE_APBIO);

    // slv4 
 	apbpp.id = mctrl.read32(0x800ff018);
	apbpp.iobar = mctrl.read32(0x800ff01c);
 	ASSERT_EQ(ambapp_pnp_vendor(apb->id), VENDOR_GAISLER);
 	ASSERT_EQ(ambapp_pnp_device(apb->id), GAISLER_GPTIMER);
 	ASSERT_EQ(ambapp_pnp_ver(apb->id), 0x0);
 	ASSERT_EQ(ambapp_pnp_irq(apb->id), 0x8);
	ASSERT_EQ(ambapp_pnp_apb_start(apb->iobar, 0x80000300), 0x80000300);
	//ASSERT_EQ(ambapp_pnp_apb_mask(apb->iobar), 0xff);
	ASSERT_EQ(ambapp_pnp_mbar_type(apb->iobar), AMBA_TYPE_APBIO);

    // slv5 
 	apbpp.id = mctrl.read32(0x800ff020);
	apbpp.iobar = mctrl.read32(0x800ff024);
 	ASSERT_EQ(ambapp_pnp_vendor(apb->id), VENDOR_GAISLER);
 	ASSERT_EQ(ambapp_pnp_device(apb->id), GAISLER_SVGACTRL);
	ASSERT_EQ(ambapp_pnp_ver(apb->id), 0x0);
 	ASSERT_EQ(ambapp_pnp_irq(apb->id), 0x9); // TODO : Do we need IRQ=9?
	ASSERT_EQ(ambapp_pnp_apb_start(apb->iobar, 0x80000400), 0x80000400);
	ASSERT_EQ(ambapp_pnp_mbar_type(apb->iobar), AMBA_TYPE_APBIO);





    // slv6 
 	apbpp.id = mctrl.read32(0x800ff028);
	apbpp.iobar = mctrl.read32(0x800ff02C);
 	ASSERT_EQ(ambapp_pnp_vendor(apb->id), VENDOR_GAISLER);
 	ASSERT_EQ(ambapp_pnp_device(apb->id), GAISLER_APBUART);
 	ASSERT_EQ(ambapp_pnp_ver(apb->id), 0x1);
 	ASSERT_EQ(ambapp_pnp_irq(apb->id), 0x3);
	ASSERT_EQ(ambapp_pnp_apb_start(apb->iobar, 0x80000900), 0x80000900);
	//ASSERT_EQ(ambapp_pnp_apb_mask(apb->iobar), 0xff);
	ASSERT_EQ(ambapp_pnp_mbar_type(apb->iobar), AMBA_TYPE_APBIO);






   
}
