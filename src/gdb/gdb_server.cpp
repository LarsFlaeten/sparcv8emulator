#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "gdb_server.h"
#include "gdb_helper.h"

#define BUFFER_SIZE 1024

#include "../sparcv8/MMU.h"


int create_server_socket(int port) {
    int server_fd;
    struct sockaddr_in address;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 1) < 0) {
        perror("Listen failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    std::cout << "GDB server listening on port " << port << "\n";
    return server_fd;
}

std::string gdb_read_regs(CPU& cpu) {
	
		std::string response;
        //u32 value = 0;
        //auto hstr = u32_to_hexstr(value);

		u32  value = 0;		
		// g0 through g7
		cpu.read_reg(GLOBALREG0, &value); response += u32_to_hexstr(value);	
		cpu.read_reg(GLOBALREG1, &value); response += u32_to_hexstr(value);	
		cpu.read_reg(GLOBALREG2, &value); response += u32_to_hexstr(value);	
		cpu.read_reg(GLOBALREG3, &value); response += u32_to_hexstr(value);	
		cpu.read_reg(GLOBALREG4, &value); response += u32_to_hexstr(value);	
		cpu.read_reg(GLOBALREG5, &value); response += u32_to_hexstr(value);	
		cpu.read_reg(GLOBALREG6, &value); response += u32_to_hexstr(value);	
		cpu.read_reg(GLOBALREG7, &value); response += u32_to_hexstr(value);	
        // OUTREGS:
		cpu.read_reg(OUTREG0, &value); response += u32_to_hexstr(value);	
		cpu.read_reg(OUTREG1, &value); response += u32_to_hexstr(value);	
		cpu.read_reg(OUTREG2, &value); response += u32_to_hexstr(value);	
		cpu.read_reg(OUTREG3, &value); response += u32_to_hexstr(value);	
		cpu.read_reg(OUTREG4, &value); response += u32_to_hexstr(value);	
		cpu.read_reg(OUTREG5, &value); response += u32_to_hexstr(value);	
		cpu.read_reg(OUTREG6, &value); response += u32_to_hexstr(value);	
		cpu.read_reg(OUTREG7, &value); response += u32_to_hexstr(value);	
        // LOCALREGS:
		cpu.read_reg(LOCALREG0, &value); response += u32_to_hexstr(value);	
		cpu.read_reg(LOCALREG1, &value); response += u32_to_hexstr(value);	
		cpu.read_reg(LOCALREG2, &value); response += u32_to_hexstr(value);	
		cpu.read_reg(LOCALREG3, &value); response += u32_to_hexstr(value);	
		cpu.read_reg(LOCALREG4, &value); response += u32_to_hexstr(value);	
		cpu.read_reg(LOCALREG5, &value); response += u32_to_hexstr(value);	
		cpu.read_reg(LOCALREG6, &value); response += u32_to_hexstr(value);	
		cpu.read_reg(LOCALREG7, &value); response += u32_to_hexstr(value);	
        // INPUTREGS:
		cpu.read_reg(INREG0, &value); response += u32_to_hexstr(value);	
		cpu.read_reg(INREG1, &value); response += u32_to_hexstr(value);	
		cpu.read_reg(INREG2, &value); response += u32_to_hexstr(value);	
		cpu.read_reg(INREG3, &value); response += u32_to_hexstr(value);	
		cpu.read_reg(INREG4, &value); response += u32_to_hexstr(value);	
		cpu.read_reg(INREG5, &value); response += u32_to_hexstr(value);	
		cpu.read_reg(INREG6, &value); response += u32_to_hexstr(value);	
		cpu.read_reg(INREG7, &value); response += u32_to_hexstr(value);	
   
        // FPU regs 
        for(int i = 0; i < 32; ++i)
            response += u32_to_hexstr(0);

        // The ‘org.gnu.gdb.sparc.cp0’ feature is required for sparc32/sparc64 targets. 
		// It should describe the following registers:

		//- ‘y’, ‘psr’, ‘wim’, ‘tbr’, ‘pc’, ‘npc’, ‘fsr’, and ‘csr’ for sparc32
		response += u32_to_hexstr(cpu.get_y_reg());
		response += u32_to_hexstr(cpu.get_psr());
		response += u32_to_hexstr(cpu.get_wim());
		response += u32_to_hexstr(cpu.get_tbr());
		response += u32_to_hexstr(cpu.get_pc());
		response += u32_to_hexstr(cpu.get_npc());
		response += u32_to_hexstr(cpu.get_fsr()); // FSR
		response += u32_to_hexstr(0); // CSR
 
        return response;
}

void report_mmu_fault(u32 addr, u32 sz) {
            
    u32 fsr = MMU::GetFaultStatus();

            
    std::cerr << "Fault status: " << fsr << "\n"; 
    std::cerr << "EBE=" << ((fsr >> 10) & 0b11111111) << " L=" << ((fsr >> 8)& 0b11) << " AT=" << ((fsr>>5)&0b111) << " FT=" << ((fsr>>2)&0b111) << " FAV=" << ((fsr>>1) & 0b1) << " OW=" << (fsr&0x1) << "\n";       
            
    if(fsr & 0x2) 
        std::cerr << "Fault adress: 0x" << std::hex << MMU::GetFaultAddress() << "\n";
    else
        std::cerr << "Fult address invalid\n";

    std::cerr << "Mem: addr[" << std::hex << addr << "],sz[" << sz << "]->E14\n";



}

std::string gdb_read_mem(CPU& cpu, std::string msg) {

	// string is on the form "m[address],[size]"
	if(msg[0] != 'm')
		return "";
	
	auto n = msg.find_first_of(',');
	if( n == std::string::npos)
		return "";
	
	auto address = msg.substr(1, n-1);
	auto size = msg.substr(n+1, std::string::npos);

	u32 addr = hexstr_to_u32(address);
	u32 sz = hexstr_to_u32(size);
	u32 read = 0;

    std::string ret;


    // Read the forst  bytes if this is an unaligned read request
    if(addr % 4 != 0) {
        u32 value = 0;
        u32 fill = addr % 4; // Dmmy values to be read as part of word before our requested byte
        u32 head = 4 - fill; // The first bytes to be read
        u32 start = addr - fill;
    
        // read 4 bytes, head is the last of these, fill is discared
	    auto r = MMU::MemAccess<intent_load, 4>(start, value, CROSS_ENDIAN, true, false);	
    	
        if(r < 0) {
            report_mmu_fault(start, 4);
		    return "E14"; // errno EFAULT
	    }

        auto tmpstr = u32_to_hexstr(value);
        tmpstr = tmpstr.substr(fill*2, 2*sz);
	    ret += tmpstr;
        if(head < sz)
            sz -= head;
        else
            sz = 0;
        
        addr += head;
    }
   
     
    if(sz == 0)
        return ret;

    // read remaining words
    int j = sz/4;
    for(u32 i = 0; i < j; ++i) {
	    u32 value = 0;
	    auto r = MMU::MemAccess<intent_load, 4>(addr, value, CROSS_ENDIAN, true, false);	
	    if(r < 0) {
            report_mmu_fault(addr, 4);
		    return "E14"; // errno EFAULT
	    }

        ret += u32_to_hexstr(value);
        sz -= 4;
        addr += 4;
    }

    // Read reainder after reading head and whole words
    if(sz > 0) {
		u32 value = 0;
	    auto r = MMU::MemAccess<intent_load, 4>(addr, value, CROSS_ENDIAN, true, false);	
        if(r < 0) {
            report_mmu_fault(addr, 4);
            return "E14";
        }

        auto tmpstr = u32_to_hexstr(value);
        tmpstr = tmpstr.substr(0, sz*2);
        ret += tmpstr;
        return ret;

	}
   
    

	return ret;
}

std::string gdb_set_bp(CPU& cpu, std::string msg) {

	// string is on the form "Zn,[address],[size]"
	if(msg[0] != 'Z')
		return "E14";
	auto n = msg.find_last_of(',');
	if( n == std::string::npos)
		return "E14";
	
	auto address = msg.substr(3, n-3);
	auto size = msg.substr(n+1, std::string::npos);

	u32 addr = hexstr_to_u32(address);
	u8 sz = (u8)hexstr_to_u32(size);
	if(sz !=  4) {
        std::cerr << msg << ":\n";
		throw std::runtime_error("Size != 4 not supported for GBD set bp");
	
	}
	
    //std::cout << "GDB adding breakpoint at address 0x" << std::hex << addr << std::dec << " (msg: \"" << address << "\")\n";
 		
    cpu.add_user_breakpoint(addr);
    return "OK";
}

std::string gdb_remove_bp(CPU& cpu, std::string msg) {

	// string is on the form "zn,[address],[size]"
	if(msg[0] != 'z')
		return "E14";
	auto n = msg.find_last_of(',');
	if( n == std::string::npos)
		return "E14";
	

	auto address = msg.substr(3, n-3);
	auto size = msg.substr(n+1, std::string::npos);

	u32 addr = hexstr_to_u32(address);
	u8 sz = (u8)hexstr_to_u32(size);
	if(sz !=  4) {
        std::cerr << msg << ":\n";
		throw std::runtime_error("Size != 4 not supported for GBD remove bp");
	
	}
	
    //std::cout << "GDB removing breakpoint at address 0x" << std::hex << addr << std::dec << " (msg: \"" << address << "\")\n";
     
		
    cpu.remove_user_breakpoint(addr);
    //std::cout << "GDB added breakpoint at address 0x" << std::hex << address << std::dec << "\n";
    return "OK";
}


void send_packet(int client_fd, const char *data) {
    unsigned char checksum = 0;
    const char *ptr = data;
    char buffer[BUFFER_SIZE];

    // Calculate checksum
    while (*ptr) {
        checksum += *ptr++;
    }

    // Create packet
    snprintf(buffer, sizeof(buffer), "$%s#%02x", data, checksum);

    // Send packet
    write(client_fd, buffer, strlen(buffer));
    //std::cout << "Wrote [" << buffer << "] from data=" << data << ", (l=" << strlen(buffer) << "), checksum=" << (int)checksum << "\n";
}

bool handle_gdb_packet(int client_fd, CPU& cpu, const char *packet) {
    std::string command(packet);

    if (strcmp(packet, "g") == 0) {
        // Read registers
        auto response = gdb_read_regs(cpu);
        send_packet(client_fd, response.c_str());
    } else if (packet[0] == 'm') {
        // Read memory
        auto response = gdb_read_mem(cpu, packet);
        send_packet(client_fd, response.c_str());
    } else if (packet[0] == 'Z') {
        // Set breakpoint
        auto response = gdb_set_bp(cpu, packet);
        send_packet(client_fd, response.c_str());
    } else if (packet[0] == 'z') {
        // remove breakpoint
        auto response = gdb_remove_bp(cpu, packet);
        send_packet(client_fd, response.c_str());
    } else if (command.compare(0, 10, "qSupported") == 0) {
        send_packet(client_fd, "PacketSize=1000;vContSupported+;multiprocess+");
    } else if (strcmp(packet, "qC") == 0) {
        send_packet(client_fd, "QC0"); // assuming single thread
    } else if (strcmp(packet, "Hc-1") == 0) {
        //std::cout << "Remote wants to set thread for c and s to all threads\n";
        send_packet(client_fd, "OK");
    } else if (strcmp(packet, "Hgp0.0") == 0) {
        //std::cout << "Remote wants to set thread for g to any thread\n";
        send_packet(client_fd, "OK");
    } else if (strcmp(packet, "Hg0") == 0) {
        //std::cout << "Remote wants to set thread for g to any thread\n";
        send_packet(client_fd, "OK");
    } else if (strcmp(packet, "qSymbol::") == 0) {
        send_packet(client_fd, "OK");
    } else if (strcmp(packet, "qfThreadInfo") == 0) {
        //send_packet(client_fd, "m0");
        send_packet(client_fd, "mp01.01");
    } else if (strcmp(packet, "qsThreadInfo") == 0) {
        send_packet(client_fd, "l");
    } else if (strcmp(packet, "qOffsets") == 0) {
        //send_packet(client_fd, "Text=0;Data=0;Bss=0");
        send_packet(client_fd, "");
    } else if (strcmp(packet, "qAttached:1") == 0) {
        send_packet(client_fd, "1");
    } else if (strcmp(packet, "qAttached") == 0) {
        send_packet(client_fd, "1");
    } else if (strcmp(packet, "vCont?") == 0) {
        send_packet(client_fd, "vCont;c;C;s;S");
    } else if (command.compare(0,7, "vCont;c") == 0) {
        //send_packet(client_fd, "OK");
        
        // Here we start/continue execution until it hits a breakpoint:
        RunSummary rs;
        cpu.run(0, &rs);

        // Lets look into runsummary why it exited.
        send_packet(client_fd, "T05thread:p01.01;");
 
    } else if (strcmp(packet, "?") == 0) {
        // send SIGTRAP
        //send_packet(client_fd, "S05");
        send_packet(client_fd, "T05thread:p01.01;");
        
    } else if (strcmp(packet, "D;1") == 0) {
        // Detach
        send_packet(client_fd, "OK");
        return false;
    } else if (strcmp(packet, "vMustReplyEmpty") == 0 || strcmp(packet, "qTStatus") == 0) {
        // send SIGTRAP
        send_packet(client_fd, "");
    } else {
     std::cout << "GDB says: [" << packet << "]\n";
        send_packet(client_fd, "");
    }

    return true;
}

void handle_gdb_client(int client_fd, CPU& cpu) {
    char buffer[BUFFER_SIZE];
    while(1) {
        int bytes_read = read(client_fd, buffer, BUFFER_SIZE - 1);
        if (bytes_read <= 0) {
            std::cout << "Client disconnected or read error." << std::endl;
            break;
        }


        if(buffer[0]=='+' && bytes_read > 1) {
            --bytes_read;
            // we have two packest in te buffer, an ack(+) and a command?
            // left shift / strip the +
            for(int i = 0; i < bytes_read; ++i)
                buffer[i] =buffer[i+1];

        }

        buffer[bytes_read] = '\0';
        if (buffer[0] == '$') {
            // Extract packet
            char *end = strchr(buffer, '#');
            if (!end) continue;
            *end = '\0';
            const char *packet = buffer + 1;
            // Acknowledge packet
            write(client_fd, "+", 1);
 
            // Handle the packet
            bool cont = handle_gdb_packet(client_fd, cpu, packet);
            if (!cont)
                return;
        }
    }
}	
