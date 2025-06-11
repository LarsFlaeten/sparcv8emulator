#include "../src/sparcv8/CPU.h"
#include "../src/sparcv8/MMU.h"
#include "../src/peripherals/Display.h"
#include "../src/peripherals/gaisler/ambapp.h"




#include <gtest/gtest.h>

#include <cmath>



class DisplayTest : public ::testing::Test {

protected:
    DisplayTest();

    virtual ~DisplayTest();

    // Code here will be called immediately after the constructor (right
    // before each test).
    virtual void SetUp();

    // Code here will be called immediately after each test (right
    // before the destructor).
    virtual void TearDown();


};



DisplayTest::DisplayTest()
{  
   	


}

DisplayTest::~DisplayTest()
{

}

void DisplayTest::SetUp()
{


}

void DisplayTest::TearDown()
{
}

TEST_F(DisplayTest, TestDisplay)
{
    const int width = 640;
    const int height = 480;
    const int bpp = 32;
    const int refreshRate = 60;

    std::vector<uint32_t> buffer(width * height);
    std::srand(time(nullptr));
    for (auto& px : buffer) {
        px = 0xFF000000 | (std::rand() & 0xFFFFFF); // Random ARGB
    }

    Display screen(width, height, bpp, refreshRate, buffer.data());
    screen.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    screen.enable();

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    screen.disable(true);

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    screen.enable();

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    screen.disable(false);

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    screen.stop();
    
}
