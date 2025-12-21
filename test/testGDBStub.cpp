#include "../src/sparcv8/CPU.h"
#include "../src/sparcv8/MMU.h"

#include "../src/gdb/gdb_stub.hpp"

#include <gtest/gtest.h>
#include <thread>

// TCP client defines:
#include <iostream>
#include <string>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

#include <chrono>
#include <thread>

#define SERVER_ADDRESS "127.0.0.1"
#define BUFFER_SIZE 1024
#define WAIT_TIME_MS 50

#define OPCODE_TA1 0x91d02001

int create_tcp_client(int debug_port) {
	int clientSocket;
    struct sockaddr_in serverAddress;

    // Create socket
    clientSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (clientSocket < 0) {
        std::cerr << "[GTEST] Error: Unable to create socket." << std::endl;
        return -1;
    }

    std::memset(&serverAddress, 0, sizeof(serverAddress));
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(debug_port);

    // Convert IP address from text to binary form
    if (inet_pton(AF_INET, SERVER_ADDRESS, &serverAddress.sin_addr) <= 0) {
        std::cerr << "[GTEST] Error: Invalid address." << std::endl;
        close(clientSocket);
        return -1;
    }

    // Connect to the server
    std::cout << "[GTEST] Connecting to the server." << std::endl;
	
    if (connect(clientSocket, (struct sockaddr*)&serverAddress, sizeof(serverAddress)) < 0) {
        std::cerr << "Error: Connection to the server failed." << std::endl;
        close(clientSocket);
        return -1;
    }

    std::cout << "[GTEST] Connected to the server." << std::endl;
	return clientSocket;


}

bool tcp_send(int client_fd, const std::string& cmd) {
	std::stringstream ss;
	unsigned char checksum = 0;
	for(char c : cmd)
		checksum += c;

	ss << std::format("${}#{:#02x}", cmd, checksum);
	auto message = ss.str();
	
	if (send(client_fd, message.c_str(), message.length(), 0) < 0) {
        std::cerr << "Error: Failed to send message." << std::endl;
        return false;
    }

	return true;	

}

std::string tcp_recv(int client_fd) {
    char buffer[BUFFER_SIZE]; 
    // Receive response from server
    std::memset(buffer, 0, BUFFER_SIZE);
    int bytesReceived = recv(client_fd, buffer, BUFFER_SIZE - 1, 0);
    if (bytesReceived < 0) {
        std::cerr << "Error: Failed to receive response." << std::endl;
        return "";
    } else if (bytesReceived == 0) {
        std::cout << "Server disconnected." << std::endl;
        return "";
    }

    return std::string(buffer);

}


std::string strip_ack(const std::string& msg) {
    // remov the ack
    std::string message;
    if(msg[0] == '+')
        message = msg.substr(1, msg.size());
    else
        message = msg;
    return message; 
}

std::string get_payload(const std::string& msg) {
    auto n1 = msg.find_first_of("#");
    auto payload = msg.substr(1, n1-1);
    return payload; 
}

std::string get_checksum(const std::string& msg) {
    auto n1 = msg.find_first_of("#");
    auto checksum = msg.substr(n1+1, std::string::npos);
    return checksum; 
}

#if 0
static std::string to_hex(const uint8_t* data, size_t len) {
    std::ostringstream oss;
    for (size_t i = 0; i < len; i++)
        oss << std::hex << std::setw(2) << std::setfill('0') << (int)data[i];
    return oss.str();
}

static std::vector<uint8_t> from_hex(const std::string& hex) {
    std::vector<uint8_t> res;
    for (size_t i = 0; i < hex.size(); i += 2)
        res.push_back(std::stoi(hex.substr(i, 2), nullptr, 16));
    return res;
}
#endif




bool validate(const std::string& msg) {
    // remov the ack
    auto message = strip_ack(msg);
    
    // Check length;
    // Shuld be at least 4 ("$#00")
    if(message.length() < 4)
        return false;

    if(message[0] != '$')
        return false;


    auto checksum = get_checksum(message);
    auto payload = get_payload(message);
    //std::cout << "paload[" << payload << "]\n";
    //std::cout << "checksum[" << checksum << "]\n";

    // calculate checksum:
    unsigned char c = 0;
    for(auto _c : payload)
        c += _c;

    char buffer[12];
    snprintf(buffer, sizeof(buffer), "%02x", c);
    std::string chsum_ss(buffer);
    if(chsum_ss != checksum)
        return false;
    
    
    
    return true;
}

class GDBStubTest : public ::testing::Test {

protected:
    GDBStubTest();

    virtual ~GDBStubTest();

    // Code here will be called immediately after the constructor (right
    // before each test).
    virtual void SetUp();

    // Code here will be called immediately after each test (right
    // before the destructor).
    virtual void TearDown();

    IRQMP intc;
    MCtrl mctrl;
    MMU mmu;
    std::vector<std::unique_ptr<CPU>> cpus;
    GdbStub* pstub = nullptr;
    
    int debug_port;
    
    int client_socket;

};

/*
void gdb_server(int server_fd, CPU& cpu) {

	int client_fd = accept(server_fd, NULL, NULL);
       
    if (client_fd < 0) {
     	perror("Client connection failed");
       	close(server_fd);
       	exit(EXIT_FAILURE);
   	}
    
    handle_gdb_client(client_fd, cpu); 

    close(client_fd);
    close(server_fd);

}*/

// create server and start in separate thread
GDBStubTest::GDBStubTest()
    : intc(1), mmu(mctrl), debug_port(1234)
{
    cpus.emplace_back(std::make_unique<CPU>(mmu, intc));
	


}

GDBStubTest::~GDBStubTest()
{

}

void GDBStubTest::SetUp()
{
    std::cout << "[GTEST] Setting up new test\n";
    mctrl.attach_bank<RamBank>(0x60000000, 1*1024*1024); // 1 MB @ 0x0
    
   
    // Read the ELF and get the entry point, then reset
    u32 entry_va = 0x60000000; 
    cpus[0]->reset(entry_va);
 
    // Start GDB stub:
    pstub = new GdbStub(cpus, mmu);
    cpus[0]->set_gdb_stub(pstub);

    std::cout << "[GTEST] GDBStub starting.\n";
    
    pstub->start(debug_port, false);
    std::cout << "[GTEST] GDBStub started sucessfully.\n";
    
    std::this_thread::sleep_for(std::chrono::milliseconds(WAIT_TIME_MS));


    std::cout << "[GTEST] Connecting with mock client..\n";
    
    // Create client (mock GDB) in this thread
	client_socket = create_tcp_client(debug_port);


}

void GDBStubTest::TearDown()
{
	close(client_socket);

    delete pstub;
    pstub = nullptr;
}
/*
We drop these tests.
hex2str now have internal linkage and are not exposed outside gdb_stub
TEST_F(GDBStubTest, GDB_helper)
{
	u32 regval1 = 0xcafebabe;
	u32 regval2 = 0x1234babe;
	u32 regval3 = 0xcafe1234;

	auto str1 = u32_to_hexstr(regval1);
	auto str2 = u32_to_hexstr(regval2);
	auto str3 = u32_to_hexstr(regval3);

	std::string regstr1 = "cafebabe";
	std::string regstr2 = "1234babe";
	std::string regstr3 = "cafe1234";
	std::string regstr4 = "1234";
	std::string regstr5 = "1";
	std::string regstr6 = "0";
	
	ASSERT_STREQ(str1.c_str(), regstr1.c_str());		
	ASSERT_STREQ(str2.c_str(), regstr2.c_str());
	ASSERT_STREQ(str3.c_str(), regstr3.c_str());


	auto v1 = hexstr_to_u32(regstr1);
	auto v2 = hexstr_to_u32(regstr2);
	auto v3 = hexstr_to_u32(regstr3);
	auto v4 = hexstr_to_u32(regstr4);
	auto v5 = hexstr_to_u32(regstr5);
	auto v6 = hexstr_to_u32(regstr6);

	ASSERT_EQ(v1, 0xcafebabe);
	ASSERT_EQ(v2, 0x1234babe);
	ASSERT_EQ(v3, 0xcafe1234);
	ASSERT_EQ(v4, 0x1234);
	ASSERT_EQ(v5, 0x1);
	ASSERT_EQ(v6, 0x0);

}
*/
/*
TEST_F(GDBStubTest, GDB_setup)
{
    // Send something and check that the server responds correctly:
	ASSERT_TRUE(tcp_send(client_socket, "qSupported:multiprocess+;swbreak+;hwbreak+;qRelocInsn+;fork-events+;vfork-events+;exec-events+;vContSupported+;QThreadEvents+;no-resumed+;memory-tagging+"));
    
    std::this_thread::sleep_for(std::chrono::milliseconds(WAIT_TIME_MS));
   
    auto ret = tcp_recv(client_socket);
    
    //ASSERT_STREQ(get_payload(strip_ack(ret)).c_str(), "PacketSize=1000;vContSupported+;multiprocess+");
    ASSERT_STREQ(get_payload(strip_ack(ret)).c_str(), "");

    ASSERT_TRUE(validate(ret)); 
}

TEST_F(GDBStubTest, GDB_CheckStandardReplies)
{
    std::vector<std::pair<std::string, std::string>> accepted;

    // A series of standard cmds and queries we get the from GDB and the expected replies
    accepted.push_back({"vMustReplyEmpty", ""});
    accepted.push_back({"Hg0", "OK"});
    accepted.push_back({"qTStatus", ""});
    accepted.push_back({"?", "S05"});
    accepted.push_back({"qfThreadInfo", ""});
    //accepted.push_back({"qfThreadInfo", "m0"});
    //accepted.push_back({"qfThreadInfo", "mp01.01"});
    //accepted.push_back({"qsThreadInfo", "l"});
    accepted.push_back({"qL1200000000000000000", ""});
    accepted.push_back({"Hc-1", "OK"});
    accepted.push_back({"qC", ""});
    accepted.push_back({"qAttached", ""});
    accepted.push_back({"qOffsets", ""});
    accepted.push_back({"qSymbol::", ""});

    for(const auto& a : accepted) {
	    ASSERT_TRUE(tcp_send(client_socket, a.first));
    
        std::this_thread::sleep_for(std::chrono::milliseconds(WAIT_TIME_MS));
   
        auto ret = tcp_recv(client_socket);
        ASSERT_STREQ(get_payload(strip_ack(ret)).c_str(), a.second.c_str());
        ASSERT_TRUE(validate(ret));
    }

}

TEST_F(GDBStubTest, GDB_read_registers)
{
    // Send something and check that the server responds correctly:
	ASSERT_TRUE(tcp_send(client_socket, "g"));
    
    std::this_thread::sleep_for(std::chrono::milliseconds(WAIT_TIME_MS));
   
    auto ret = tcp_recv(client_socket);
    ASSERT_TRUE(validate(ret));
    auto pl = strip_ack(ret);
    pl = get_payload(pl);
    
    // 4. Manually build the expected result
    std::stringstream expected;
    expected << std::hex << std::setfill('0');

    // GPRs (32 × 4 bytes = 128 bytes = 256 hex digits)
    for (int i = 0; i < 32; ++i) {
        u32 val = 0;
        cpus[0]->read_reg(i, &val);
        expected << std::setw(8) << val;
    }
    // FPRs (32 × 8 bytes = 256 bytes = 512 hex digits)
    for (int i = 0; i < 32; ++i)
        expected << std::setw(8) << 0;

    // Specials (each 4 bytes)
    expected << std::setw(8) << cpus[0]->get_y_reg();
    expected << std::setw(8) << cpus[0]->get_psr();
    expected << std::setw(8) << cpus[0]->get_wim();
    expected << std::setw(8) << cpus[0]->get_tbr();
    expected << std::setw(8) << cpus[0]->get_pc();
    expected << std::setw(8) << cpus[0]->get_npc();
    expected << std::setw(8) << cpus[0]->get_fsr();
    expected << std::setw(8) << 0;

    std::string expected_str = expected.str();
    
    // 5. Compare
    EXPECT_EQ(pl, expected_str);


    // Set some other reg values:
    cpus[0]->set_psr(0x270f);
    cpus[0]->set_fsr(0xfff);
    // Only ones exposed....
    for(int i = 0; i < 32; ++i) {
        cpus[0]->write_reg(i*1000 -i*137 + i*3 + 7, i);
    }
    

	ASSERT_TRUE(tcp_send(client_socket, "g"));
    
    std::this_thread::sleep_for(std::chrono::milliseconds(WAIT_TIME_MS));
   
    ret = tcp_recv(client_socket);
    ASSERT_TRUE(validate(ret));
    pl = strip_ack(ret);
    pl = get_payload(pl);
    
    // 4. Manually build the expected result
    expected.str("");
    expected.clear();

    expected << std::hex << std::setfill('0');

    // GPRs (32 × 4 bytes = 128 bytes = 256 hex digits)
    for (int i = 0; i < 32; ++i) {
        u32 val = 0;
        cpus[0]->read_reg(i, &val);
        if(i == 0)
            ASSERT_EQ(val, 0); // G0 is allways zero
        else
            ASSERT_EQ(val, i*1000 -i*137 + i*3 + 7);
        expected << std::setw(8) << val;
    }
    // FPRs (32 × 8 bytes = 256 bytes = 512 hex digits)
    for (int i = 0; i < 32; ++i)
        expected << std::setw(8) << 0;

    // Specials (each 4 bytes)
    expected << std::setw(8) << cpus[0]->get_y_reg();
    expected << std::setw(8) << cpus[0]->get_psr();
    ASSERT_EQ(cpus[0]->get_psr(), 0x270f);
    expected << std::setw(8) << cpus[0]->get_wim();
    expected << std::setw(8) << cpus[0]->get_tbr();
    expected << std::setw(8) << cpus[0]->get_pc();
    expected << std::setw(8) << cpus[0]->get_npc();
    expected << std::setw(8) << cpus[0]->get_fsr();
    ASSERT_EQ(cpus[0]->get_fsr(), 0xfff);
    expected << std::setw(8) << 0;

    expected_str = expected.str();
    
    // 5. Compare
    EXPECT_EQ(pl, expected_str);

    //Regs; [" << get_payload(strip_ack(ret)) << "]\n";
}

TEST_F(GDBStubTest, GDB_read_memory)
{

	mmu.MemAccessBypassWrite4(0x60000000, 0xcafebabe);

    // Send read reuest for valid memory locations
	ASSERT_TRUE(tcp_send(client_socket, "m60000000,4"));
    
    std::this_thread::sleep_for(std::chrono::milliseconds(WAIT_TIME_MS));
   
    auto ret = tcp_recv(client_socket);
    ASSERT_TRUE(validate(ret));
 
	ASSERT_STREQ(get_payload(strip_ack(ret)).c_str(), "cafebabe");


	ASSERT_TRUE(tcp_send(client_socket, "m5ffffffc,4"));

    std::this_thread::sleep_for(std::chrono::milliseconds(WAIT_TIME_MS));
 
    ret = tcp_recv(client_socket);
    ASSERT_TRUE(validate(ret));
 
	//TODO: Figure why this does not return E14
    ASSERT_STREQ(get_payload(strip_ack(ret)).c_str(), "E14");

	//std::cout << "MEM [" << get_payload(strip_ack(ret)) << "]\n";


}

TEST_F(GDBStubTest, GDB_read_memory_long)
{

	mmu.MemAccessBypassWrite4(0x60000000, 0xcafebabe);
	mmu.MemAccessBypassWrite4(0x60000004, 0xbaccecaf);
	mmu.MemAccessBypassWrite4(0x60000008, 0x11223344);
	mmu.MemAccessBypassWrite4(0x6000000c, 0x55667788);
	mmu.MemAccessBypassWrite4(0x60000010, 0x99aabbcc);


    // Send read reuest for valid memory locations
	ASSERT_TRUE(tcp_send(client_socket, "m60000000,8"));
    
    std::this_thread::sleep_for(std::chrono::milliseconds(WAIT_TIME_MS));
   
    auto ret = tcp_recv(client_socket);
    ASSERT_TRUE(validate(ret));
 
	ASSERT_STREQ(get_payload(strip_ack(ret)).c_str(), "cafebabebaccecaf");

    // Send read reuest for valid memory locations
	ASSERT_TRUE(tcp_send(client_socket, "m60000000,14"));
    
    std::this_thread::sleep_for(std::chrono::milliseconds(WAIT_TIME_MS));
   
    ret = tcp_recv(client_socket);
    ASSERT_TRUE(validate(ret));
 
	ASSERT_STREQ(get_payload(strip_ack(ret)).c_str(), "cafebabebaccecaf112233445566778899aabbcc");

    // Send read reuest for valid memory locations
	ASSERT_TRUE(tcp_send(client_socket, "m6000000c,8"));
    
    std::this_thread::sleep_for(std::chrono::milliseconds(WAIT_TIME_MS));
   
    ret = tcp_recv(client_socket);
    ASSERT_TRUE(validate(ret));
 
	ASSERT_STREQ(get_payload(strip_ack(ret)).c_str(), "5566778899aabbcc");


	ASSERT_TRUE(tcp_send(client_socket, "m5ffffffc,4"));

    std::this_thread::sleep_for(std::chrono::milliseconds(WAIT_TIME_MS));
 
    ret = tcp_recv(client_socket);
    ASSERT_TRUE(validate(ret));
   	
    //TODO: Figure why this does not return E14
    //ASSERT_STREQ(get_payload(strip_ack(ret)).c_str(), "E14");

	//std::cout << "MEM [" << get_payload(strip_ack(ret)) << "]\n";
}


TEST_F(GDBStubTest, GDB_read_memory_short)
{

	mmu.MemAccessBypassWrite4(0x60000000, 0xcafebabe);
	mmu.MemAccessBypassWrite4(0x60000004, 0xbaccecaf);
	mmu.MemAccessBypassWrite4(0x60000008, 0x11223344);
	mmu.MemAccessBypassWrite4(0x6000000c, 0x55667788);
	mmu.MemAccessBypassWrite4(0x60000010, 0x99aabbcc);


    // Send read reuest for valid memory locations
	ASSERT_TRUE(tcp_send(client_socket, "m60000000,2"));
    
    std::this_thread::sleep_for(std::chrono::milliseconds(WAIT_TIME_MS));
   
    auto ret = tcp_recv(client_socket);
    ASSERT_TRUE(validate(ret));
 
	ASSERT_STREQ(get_payload(strip_ack(ret)).c_str(), "cafe");

    // Send read reuest for valid memory locations
	ASSERT_TRUE(tcp_send(client_socket, "m60000000,1"));
    
    std::this_thread::sleep_for(std::chrono::milliseconds(WAIT_TIME_MS));
   
    ret = tcp_recv(client_socket);
    ASSERT_TRUE(validate(ret));
 
	ASSERT_STREQ(get_payload(strip_ack(ret)).c_str(), "ca");

    // Send read reuest for valid memory locations
	ASSERT_TRUE(tcp_send(client_socket, "m60000004,2"));
    
    std::this_thread::sleep_for(std::chrono::milliseconds(WAIT_TIME_MS));
   
    ret = tcp_recv(client_socket);
    ASSERT_TRUE(validate(ret));
 
	ASSERT_STREQ(get_payload(strip_ack(ret)).c_str(), "bacc");

    // Send read reuest for valid memory locations
	ASSERT_TRUE(tcp_send(client_socket, "m60000004,1"));
    
    std::this_thread::sleep_for(std::chrono::milliseconds(WAIT_TIME_MS));
   
    ret = tcp_recv(client_socket);
    ASSERT_TRUE(validate(ret));
 
	ASSERT_STREQ(get_payload(strip_ack(ret)).c_str(), "ba");



}


TEST_F(GDBStubTest, GDB_read_memory_unaligned_1_4)
{

	mmu.MemAccessBypassWrite4(0x60000000, 0xcafebabe);
	mmu.MemAccessBypassWrite4(0x60000004, 0xbaccecaf);
	mmu.MemAccessBypassWrite4(0x60000008, 0x11223344);
	mmu.MemAccessBypassWrite4(0x6000000c, 0x55667788);
	mmu.MemAccessBypassWrite4(0x60000010, 0x99aabbcc);

    std::string ret;

    ASSERT_TRUE(tcp_send(client_socket, "m60000001,4"));
    std::this_thread::sleep_for(std::chrono::milliseconds(WAIT_TIME_MS));
    ret = tcp_recv(client_socket);
    ASSERT_TRUE(validate(ret));
	ASSERT_STREQ(get_payload(strip_ack(ret)).c_str(), "febabeba");
}

TEST_F(GDBStubTest, GDB_read_memory_unaligned_1_1)
{

	mmu.MemAccessBypassWrite4(0x60000000, 0xcafebabe);
	mmu.MemAccessBypassWrite4(0x60000004, 0xbaccecaf);
	mmu.MemAccessBypassWrite4(0x60000008, 0x11223344);
	mmu.MemAccessBypassWrite4(0x6000000c, 0x55667788);
	mmu.MemAccessBypassWrite4(0x60000010, 0x99aabbcc);

    std::string ret;

    ASSERT_TRUE(tcp_send(client_socket, "m60000001,1"));
    std::this_thread::sleep_for(std::chrono::milliseconds(WAIT_TIME_MS));
    ret = tcp_recv(client_socket);
    ASSERT_TRUE(validate(ret));
	ASSERT_STREQ(get_payload(strip_ack(ret)).c_str(), "fe");
}
 
TEST_F(GDBStubTest, GDB_read_memory_unaligned_1_2)
{

	mmu.MemAccessBypassWrite4(0x60000000, 0xcafebabe);
	mmu.MemAccessBypassWrite4(0x60000004, 0xbaccecaf);
	mmu.MemAccessBypassWrite4(0x60000008, 0x11223344);
	mmu.MemAccessBypassWrite4(0x6000000c, 0x55667788);
	mmu.MemAccessBypassWrite4(0x60000010, 0x99aabbcc);

    std::string ret;

    ASSERT_TRUE(tcp_send(client_socket, "m60000001,2"));
    std::this_thread::sleep_for(std::chrono::milliseconds(WAIT_TIME_MS));
    ret = tcp_recv(client_socket);
    ASSERT_TRUE(validate(ret));
	ASSERT_STREQ(get_payload(strip_ack(ret)).c_str(), "feba");
}

TEST_F(GDBStubTest, GDB_read_memory_unaligned_2_4)
{

	mmu.MemAccessBypassWrite4(0x60000000, 0xcafebabe);
	mmu.MemAccessBypassWrite4(0x60000004, 0xbaccecaf);
	mmu.MemAccessBypassWrite4(0x60000008, 0x11223344);
	mmu.MemAccessBypassWrite4(0x6000000c, 0x55667788);
	mmu.MemAccessBypassWrite4(0x60000010, 0x99aabbcc);

    std::string ret;



	ASSERT_TRUE(tcp_send(client_socket, "m60000002,4"));
    std::this_thread::sleep_for(std::chrono::milliseconds(WAIT_TIME_MS));
    ret = tcp_recv(client_socket);
    ASSERT_TRUE(validate(ret));
	ASSERT_STREQ(get_payload(strip_ack(ret)).c_str(), "babebacc");
}


TEST_F(GDBStubTest, GDB_read_memory_unaligned_2_8)
{

	mmu.MemAccessBypassWrite4(0x60000000, 0xcafebabe);
	mmu.MemAccessBypassWrite4(0x60000004, 0xbaccecaf);
	mmu.MemAccessBypassWrite4(0x60000008, 0x11223344);
	mmu.MemAccessBypassWrite4(0x6000000c, 0x55667788);
	mmu.MemAccessBypassWrite4(0x60000010, 0x99aabbcc);

    std::string ret;



	ASSERT_TRUE(tcp_send(client_socket, "m60000002,8"));
    std::this_thread::sleep_for(std::chrono::milliseconds(WAIT_TIME_MS));
    ret = tcp_recv(client_socket);
    ASSERT_TRUE(validate(ret));
	ASSERT_STREQ(get_payload(strip_ack(ret)).c_str(), "babebaccecaf1122");
}

TEST_F(GDBStubTest, GDB_read_memory_unaligned_2_16)
{

	mmu.MemAccessBypassWrite4(0x60000000, 0xcafebabe);
	mmu.MemAccessBypassWrite4(0x60000004, 0xbaccecaf);
	mmu.MemAccessBypassWrite4(0x60000008, 0x11223344);
	mmu.MemAccessBypassWrite4(0x6000000c, 0x55667788);
	mmu.MemAccessBypassWrite4(0x60000010, 0x99aabbcc);

    std::string ret;



	ASSERT_TRUE(tcp_send(client_socket, "m60000002,10"));
    std::this_thread::sleep_for(std::chrono::milliseconds(WAIT_TIME_MS));
    ret = tcp_recv(client_socket);
    ASSERT_TRUE(validate(ret));
	ASSERT_STREQ(get_payload(strip_ack(ret)).c_str(), "babebaccecaf112233445566778899aa");
}

TEST_F(GDBStubTest, GDB_read_memory_unaligned_1_16)
{

	mmu.MemAccessBypassWrite4(0x60000000, 0xcafebabe);
	mmu.MemAccessBypassWrite4(0x60000004, 0xbaccecaf);
	mmu.MemAccessBypassWrite4(0x60000008, 0x11223344);
	mmu.MemAccessBypassWrite4(0x6000000c, 0x55667788);
	mmu.MemAccessBypassWrite4(0x60000010, 0x99aabbcc);

    std::string ret;



	ASSERT_TRUE(tcp_send(client_socket, "m60000001,10"));
    std::this_thread::sleep_for(std::chrono::milliseconds(WAIT_TIME_MS));
    ret = tcp_recv(client_socket);
    ASSERT_TRUE(validate(ret));
	ASSERT_STREQ(get_payload(strip_ack(ret)).c_str(), "febabebaccecaf112233445566778899");
}

TEST_F(GDBStubTest, GDB_read_memory_unaligned_3_16)
{

	mmu.MemAccessBypassWrite4(0x60000000, 0xcafebabe);
	mmu.MemAccessBypassWrite4(0x60000004, 0xbaccecaf);
	mmu.MemAccessBypassWrite4(0x60000008, 0x11223344);
	mmu.MemAccessBypassWrite4(0x6000000c, 0x55667788);
	mmu.MemAccessBypassWrite4(0x60000010, 0x99aabbcc);

    std::string ret;



	ASSERT_TRUE(tcp_send(client_socket, "m60000003,10"));
    std::this_thread::sleep_for(std::chrono::milliseconds(WAIT_TIME_MS));
    ret = tcp_recv(client_socket);
    ASSERT_TRUE(validate(ret));
	ASSERT_STREQ(get_payload(strip_ack(ret)).c_str(), "bebaccecaf112233445566778899aabb");
}




TEST_F(GDBStubTest, GDB_vCont)
{
    ASSERT_TRUE(tcp_send(client_socket, "vCont?"));
    
    std::this_thread::sleep_for(std::chrono::milliseconds(WAIT_TIME_MS));
   
    auto ret = tcp_recv(client_socket);
    ASSERT_TRUE(validate(ret));
 
    ASSERT_STREQ(get_payload(strip_ack(ret)).c_str(), "");

}



TEST_F(GDBStubTest, GDB_set_breakpoint)
{


	ASSERT_TRUE(tcp_send(client_socket, "Z0,60000000,4"));
    
    std::this_thread::sleep_for(std::chrono::milliseconds(WAIT_TIME_MS));
   
    auto ret = tcp_recv(client_socket);
    ASSERT_STREQ(get_payload(strip_ack(ret)).c_str(), "OK");
    ASSERT_TRUE(validate(ret));
 

    ASSERT_NE(pstub, nullptr);
    ASSERT_TRUE(pstub->has_breakpoint(0x60000000));
    ASSERT_EQ(mmu.MemAccessBypassRead4(0x60000000), OPCODE_TA1);
    

}

TEST_F(GDBStubTest, GDB_remove_breakpoint)
{
    mmu.MemAccessBypassWrite4(0x60000000, 0xcafebabe);

	ASSERT_TRUE(tcp_send(client_socket, "Z0,60000000,4"));
    
    std::this_thread::sleep_for(std::chrono::milliseconds(WAIT_TIME_MS));
   
  

    auto ret = tcp_recv(client_socket);
    ASSERT_STREQ(get_payload(strip_ack(ret)).c_str(), "OK");
    ASSERT_TRUE(validate(ret));
 

    ASSERT_NE(pstub, nullptr);
    ASSERT_TRUE(pstub->has_breakpoint(0x60000000));
    ASSERT_EQ(mmu.MemAccessBypassRead4(0x60000000), OPCODE_TA1);
    


    ASSERT_TRUE(tcp_send(client_socket, "z0,60000000,4"));
    
    std::this_thread::sleep_for(std::chrono::milliseconds(WAIT_TIME_MS));
    
    ASSERT_FALSE(pstub->has_breakpoint(0x60000000));
    ASSERT_EQ(mmu.MemAccessBypassRead4(0x60000000), 0xcafebabe);
    

}




*/