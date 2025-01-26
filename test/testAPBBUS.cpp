#include "../src/sparcv8/CPU.h"
#include "../src/sparcv8/MMU.h"
#include "../src/peripherals/APBMST.h"




#include <gtest/gtest.h>

#include <cmath>

#define GR712RC_SYSTEMID 0x7120e70
#define GR740_REV0_SYSTEMID 0x07401038
#define GR740_REV1_SYSTEMID 0x07401039


class APBBUSTest : public ::testing::Test {

protected:
    APBBUSTest();

    virtual ~APBBUSTest();

    // Code here will be called immediately after the constructor (right
    // before each test).
    virtual void SetUp();

    // Code here will be called immediately after each test (right
    // before the destructor).
    virtual void TearDown();


};



APBBUSTest::APBBUSTest()
{  
   	


}

APBBUSTest::~APBBUSTest()
{

}

void APBBUSTest::SetUp()
{


}

void APBBUSTest::TearDown()
{
}

TEST_F(APBBUSTest, AHB_setup)
{
    APBCTRL apbctrl;


   
}

