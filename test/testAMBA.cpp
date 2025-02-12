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


};



AMBATest::AMBATest()
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
    SDRAM2 amba_ahb(0x100000); // AMBA resides from 0xfff00000 -> 0xfffffff0 (+ u32)
    SDRAM2 amba_apb(0x100000);

    // IO mapping for AMBA AHB IO AREA
    u32 base_amba_ahb_io = 0xfff00000;
    u32 end_amba_ahb_io =  0xffffffff;
    u32 start = base_amba_ahb_io/0x10000;
    u32 end =    end_amba_ahb_io/0x10000;
    
    for(unsigned a = start; a <= end; ++a) {
        std::cout << "Mapping 0x" << std::hex << a << "0000 to 0x" << a << "ffff\n";
        MMU::IOmap[a] = { [&amba_ahb](u32 i)          { return amba_ahb.Read((i-0xfff00000)/4); } ,
                          [&amba_ahb](u32 i, u32 v)   { amba_ahb.Write((i-0xfff00000)/4, v);    } };
    }

    // IO mapping for AMBA APB Pnp IO AREA
    u32 base_amba_apb_io = 0x800f0000;
    u32 end_amba_apb_io =  0x800fffff;
    start = base_amba_apb_io/0x10000;
    end =    end_amba_apb_io/0x10000;
    
    for(unsigned a = start; a <= end; ++a) {
        std::cout << "Mapping 0x" << std::hex << a << "0000 to 0x" << a << "ffff\n";
        MMU::IOmap[a] = { [&amba_apb](u32 i)          { return amba_apb.Read((i-0x800f0000)/4); } ,
                          [&amba_apb](u32 i, u32 v)   { amba_apb.Write((i-0x800f0000)/4, v);    } };
    }

    amba_ahb_setup(amba_ahb);
    amba_apb_setup(amba_apb, base_amba_apb_io);

    // Find the system version
    //ASSERT_EQ(MMU::MemAccessBypassRead4(0xfffffff0, CROSS_ENDIAN), GR740_REV1_SYSTEMID);  
    ASSERT_EQ(MMU::MemAccessBypassRead4(0xfffffff0, CROSS_ENDIAN), 0x0); 



    // mst0 Find the LEON processor
    auto p = amba_ahb.getPtr(0x000ff000/4);
    ambapp_pnp_ahb* ahb = reinterpret_cast<ambapp_pnp_ahb*>(p); 
 	ASSERT_EQ(ambapp_pnp_vendor(ahb->id), VENDOR_GAISLER);
 	ASSERT_EQ(ambapp_pnp_device(ahb->id), GAISLER_LEON3);
 	ASSERT_EQ(ambapp_pnp_ver(ahb->id), 0x0);
 	ASSERT_EQ(ambapp_pnp_irq(ahb->id), 0x0);

    // mst1 Find the AHB UART
/*    p = amba_ahb.getPtr(0x000ff020/4);
    ahb = reinterpret_cast<ambapp_pnp_ahb*>(p); 
 	ASSERT_EQ(ambapp_pnp_vendor(ahb->id), VENDOR_GAISLER);
 	ASSERT_EQ(ambapp_pnp_device(ahb->id), GAISLER_AHBUART);
 	ASSERT_EQ(ambapp_pnp_ver(ahb->id), 0x1);
 	ASSERT_EQ(ambapp_pnp_irq(ahb->id), 0x0);
*/
    // slv0 Find the Memory Controller
    p = amba_ahb.getPtr(0x000ff820/4);

	ahb = reinterpret_cast<ambapp_pnp_ahb*>(p); 
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



    // slv1 Find the APB Bridge
    p = amba_ahb.getPtr(0x000ff800/4);

	ahb = reinterpret_cast<ambapp_pnp_ahb*>(p); 
 	ASSERT_EQ(ambapp_pnp_vendor(ahb->id), VENDOR_GAISLER);
 	ASSERT_EQ(ambapp_pnp_device(ahb->id), GAISLER_APBMST);
 	ASSERT_EQ(ambapp_pnp_ver(ahb->id), 0x0);
 	ASSERT_EQ(ambapp_pnp_irq(ahb->id), 0x0);
	ASSERT_EQ(ambapp_pnp_mbar_start(ahb->mbar[0]), 0x80000000);
	ASSERT_EQ(ambapp_pnp_mbar_mask(ahb->mbar[0]), 0xfff);
	ASSERT_EQ(ambapp_pnp_mbar_type(ahb->mbar[0]), AMBA_TYPE_MEM); //AMBA_TYPE_APBIO);

    // slv1 Find the slaves under the APB Bridge
    p = amba_apb.getPtr((0x800ff000-base_amba_apb_io)/4);
	ambapp_pnp_apb* apb = reinterpret_cast<ambapp_pnp_apb*>(p); 
 	ASSERT_EQ(ambapp_pnp_vendor(apb->id), VENDOR_ESA);
 	ASSERT_EQ(ambapp_pnp_device(apb->id), ESA_MCTRL);
 	ASSERT_EQ(ambapp_pnp_ver(apb->id), 0x0);
 	ASSERT_EQ(ambapp_pnp_irq(apb->id), 0x0);
	ASSERT_EQ(ambapp_pnp_apb_start(apb->iobar, 0x80000000), 0x80000000);
	//ASSERT_EQ(ambapp_pnp_apb_mask(apb->iobar), 0xff);
	ASSERT_EQ(ambapp_pnp_mbar_type(apb->iobar), AMBA_TYPE_APBIO);

    // slv2 
    p = amba_apb.getPtr((0x800ff008-base_amba_apb_io)/4);
	apb = reinterpret_cast<ambapp_pnp_apb*>(p); 
 	ASSERT_EQ(ambapp_pnp_vendor(apb->id), VENDOR_GAISLER);
 	ASSERT_EQ(ambapp_pnp_device(apb->id), GAISLER_APBUART);
 	ASSERT_EQ(ambapp_pnp_ver(apb->id), 0x1);
 	ASSERT_EQ(ambapp_pnp_irq(apb->id), 0x2);
	ASSERT_EQ(ambapp_pnp_apb_start(apb->iobar, 0x80000100), 0x80000100);
	//ASSERT_EQ(ambapp_pnp_apb_mask(apb->iobar), 0xff);
	ASSERT_EQ(ambapp_pnp_mbar_type(apb->iobar), AMBA_TYPE_APBIO);

    // slv3 
    p = amba_apb.getPtr((0x800ff010-base_amba_apb_io)/4);
	apb = reinterpret_cast<ambapp_pnp_apb*>(p); 
 	ASSERT_EQ(ambapp_pnp_vendor(apb->id), VENDOR_GAISLER);
 	ASSERT_EQ(ambapp_pnp_device(apb->id), GAISLER_IRQMP);
 	ASSERT_EQ(ambapp_pnp_ver(apb->id), 0x0);
 	ASSERT_EQ(ambapp_pnp_irq(apb->id), 0x0);
	ASSERT_EQ(ambapp_pnp_apb_start(apb->iobar, 0x80000200), 0x80000200);
	//ASSERT_EQ(ambapp_pnp_apb_mask(apb->iobar), 0xff);
	ASSERT_EQ(ambapp_pnp_mbar_type(apb->iobar), AMBA_TYPE_APBIO);

    // slv4 
    p = amba_apb.getPtr((0x800ff018-base_amba_apb_io)/4);
	apb = reinterpret_cast<ambapp_pnp_apb*>(p); 
 	ASSERT_EQ(ambapp_pnp_vendor(apb->id), VENDOR_GAISLER);
 	ASSERT_EQ(ambapp_pnp_device(apb->id), GAISLER_GPTIMER);
 	ASSERT_EQ(ambapp_pnp_ver(apb->id), 0x0);
 	ASSERT_EQ(ambapp_pnp_irq(apb->id), 0x8);
	ASSERT_EQ(ambapp_pnp_apb_start(apb->iobar, 0x80000300), 0x80000300);
	//ASSERT_EQ(ambapp_pnp_apb_mask(apb->iobar), 0xff);
	ASSERT_EQ(ambapp_pnp_mbar_type(apb->iobar), AMBA_TYPE_APBIO);

    // slv5 
    p = amba_apb.getPtr((0x800ff020-base_amba_apb_io)/4);
	apb = reinterpret_cast<ambapp_pnp_apb*>(p); 
 	ASSERT_EQ(ambapp_pnp_vendor(apb->id), VENDOR_GAISLER);
 	ASSERT_EQ(ambapp_pnp_device(apb->id), GAISLER_AHBUART);
 	ASSERT_EQ(ambapp_pnp_ver(apb->id), 0x1);
 	ASSERT_EQ(ambapp_pnp_irq(apb->id), 0x3);
	ASSERT_EQ(ambapp_pnp_apb_start(apb->iobar, 0x80000900), 0x80000900);
	//ASSERT_EQ(ambapp_pnp_apb_mask(apb->iobar), 0xff);
	ASSERT_EQ(ambapp_pnp_mbar_type(apb->iobar), AMBA_TYPE_APBIO);






   
}
/*
#define ambapp_pnp_vendor(id) (((id) >> 24) & 0xff)
#define ambapp_pnp_device(id) (((id) >> 12) & 0xfff)
#define ambapp_pnp_ver(id) (((id)>>5) & 0x1f)
#define ambapp_pnp_irq(id) ((id) & 0x1f)

#define ambapp_pnp_mbar_start(mbar) \
	(((mbar) & 0xfff00000) & (((mbar) & 0xfff0) << 16))
#define ambapp_pnp_mbar_mask(mbar) (((mbar)>>4) & 0xfff)
#define ambapp_pnp_mbar_type(mbar) ((mbar) & 0xf)
#define ambapp_pnp_ahbio_adr(addr, base_ioarea) \
	((unsigned int)(base_ioarea) | ((addr) >> 12))

#define ambapp_pnp_apb_start(iobar, base) \
	((base) | ((((iobar) & 0xfff00000)>>12) & (((iobar) & 0xfff0)<<4)))
#define ambapp_pnp_apb_mask(iobar) \
	((~(ambapp_pnp_mbar_mask(iobar)<<8) & 0x000fffff) + 1)
*/

TEST_F(AMBATest, LEON_SOC_Device_enumeration) {
    // Set up CPU
    CPU cpu(std::cout);
    cpu.SetVerbose(false);
    cpu.SetId(0);
 
    SDRAM<0x00100000> RAM2;  // IO: 0xffd03000, 1 MB of RAM

    // Set up amba IO area:
    SDRAM2 amba_ahb(0x100000); // AMBA resides from 0xfff00000 -> 0xfffffff0 (+ u32)
    amba_ahb_setup(amba_ahb);
    SDRAM2 amba_apb(0x010000); // AMBA resides from 0x80000000 -> 0x800ffff0 (+ u32)
    amba_apb_setup(amba_apb, 0x800f0000);





}
