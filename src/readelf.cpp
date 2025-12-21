#include "readelf.h"

#include <fstream>
#include <iostream>

#include <elf.h>


#include "dis.h"

typedef Elf32_Ehdr* pElf32_Ehdr;
typedef Elf32_Phdr* pElf32_Phdr;

#define ELF_IDENT       "\177ELF"

#define BASE_MEM    0x0000000

static unsigned SwapBytes(unsigned value, unsigned size)
{
        if(size >= 2) value = ((value & 0xFF00FF00u) >> 8)
                                    | ((value & 0x00FF00FFu) << 8);
            if(size >= 4) value = (value >> 16) | (value << 16);
                return value;
}

u32 ReadElf(const std::string& filename, MCtrl& mctrl, u32& entry_va, bool verbose, std::ostream& os) {

    unsigned int i;
    int  c;
    u32 pcount, bytecount = 0;
    u32 word;
    pElf32_Ehdr h;
    pElf32_Phdr *h2;
    char buf[sizeof(Elf32_Ehdr)];
    char buf2[sizeof(Elf32_Phdr)*4];
    u32 text_count = 0;


    std::ifstream s(filename, std::ios::binary);
    if(!s.is_open()) {
        std::cerr << "*** ReadElf(): Unable to open file " <<  filename << " for reading\n";
        exit(1);
    }
    
    if(verbose)
        os << "Reading ELF file \"" << filename << "\"..\n";

    // Read elf header
    h = (pElf32_Ehdr) buf;
    for (i = 0; i < sizeof(Elf32_Ehdr); i++) {
        buf[i] = s.get();
        bytecount++;
        if (s.eof()) {
            std::cerr << "*** ReadElf(): unexpected EOF\n";
            exit(1);
        } 
    }
    if(verbose)
	    os << "ELF Header:\n"
		<< "e_type:      " << std::dec << SwapBytes(h->e_type, 2) << "\n"
		<< "e_machine:   " << std::dec << SwapBytes(h->e_machine, 2) << "\n"
		<< "e_entry:     0x" << std::hex << SwapBytes(h->e_entry, 4) << "\n"
		<< "e_phoff:     0x" << std::hex << SwapBytes(h->e_phoff, 4) << "\n"
		<< "e_shoff:     0x" << std::hex << SwapBytes(h->e_shoff, 4) << "\n"
		<< "e_flags:     " << std::dec << SwapBytes(h->e_flags, 4) << "\n"
		<< "e_ehsize:    " << std::dec << SwapBytes(h->e_ehsize, 2) << "\n"
		<< "e_phentsize: " << std::dec << SwapBytes(h->e_phentsize, 2) << "\n"
		<< "e_phnum:     " << std::dec << SwapBytes(h->e_phnum, 2) << "\n"
		<< "e_shentsize: " << std::dec << SwapBytes(h->e_shentsize, 2) << "\n"
		<< "e_shnum:     " << std::dec << SwapBytes(h->e_shnum, 2) << "\n"
		<< "e_shstrndx:  " << std::dec << SwapBytes(h->e_shstrndx, 2) << "\n";


    // Check some things
    //ptr = ELF_IDENT;
    std::string id = ELF_IDENT;
    for (i = 0; i < 4; i++) {
        if (h->e_ident[i] != id[i]) {
            std::cerr << "*** ReadElf(): not an ELF file\n";
            exit(1);
        }
    }

    if (SwapBytes(h->e_type, 2) != ET_EXEC) {
        std::cerr << "*** ReadElf(): not an executable ELF file\n";
        exit(1);
    }

    if (SwapBytes(h->e_machine, 2) != EM_SPARC) {
        std::cerr << "*** ReadElf(): not a SPARC ELF file\n";
        exit(1);
    }


    // Read program headers
    for (pcount=0 ; pcount < SwapBytes(h->e_phnum, 2); pcount++) {
        for (i = 0; i < sizeof(Elf32_Phdr); i++) {
            buf2[i+(pcount * sizeof(Elf32_Phdr))] = s.get();
            bytecount++;
            if (s.eof()) {
                std::cerr << "*** ReadElf(): unexpected EOF\n";
                exit(1);
            } 
        }
    }

    // Allocate some space for the program headers
    // TODO C++ alloc
    if ((h2 = (pElf32_Phdr *)malloc(SwapBytes(h->e_phnum, 2)*sizeof(Elf32_Phdr))) == NULL) {
        std::cerr << "*** ReadElf(): memory allocation failure\n";
        exit(1);
    }
    
    // Load text/data segments
    for (pcount=0 ; pcount < SwapBytes(h->e_phnum, 2); pcount++) {
        h2[pcount] = (pElf32_Phdr) &buf2[pcount * sizeof(Elf32_Phdr)];

        // Gobble bytes until section start
        for (; bytecount < SwapBytes(h2[pcount]->p_offset, 4); bytecount++) {
            c = s.get();
            if (s.eof()) {
                std::cerr << "*** ReadElf(): unexpected EOF\n";
                exit(1);
            }
        }

        // Check we can load the segment to memory
        u32 va = SwapBytes(h2[pcount]->p_vaddr, 4);
        u32 pa = SwapBytes(h2[pcount]->p_paddr, 4);
        u32 fsz = SwapBytes(h2[pcount]->p_filesz, 4);
        u32 msz = SwapBytes(h2[pcount]->p_memsz, 4);
        if(verbose)
            os << "ELF: Reading program segment: " << std::dec << pcount << ", va: 0x" << std::hex << va << ", pa: 0x" << pa << ", fsize: " << std::dec << fsz << ", msize: " << msz << "\n"; 
/*        if ((va + sz - base_mem) >= memsize) {
            std::cerr << "*** ReadElf(): segment memory footprint outside of internal memory range\n" << "  va: " << std::hex << va << ", size: " << sz << ", base: " << base_mem << ", memsize: " << memsize << "\n";
            exit(1);
        }
*/
        // For p_filesz bytes ...
        i = 0;
        word = 0;
        for (; bytecount < SwapBytes(h2[pcount]->p_offset, 4) + SwapBytes(h2[pcount]->p_filesz, 4); bytecount++) {
            c = s.get();
            if (s.eof()) {
                std::cerr << "*** ReadElf(): unexpected EOF\n";
                exit(1);
            }

            word |= (c << ((3-(bytecount&3)) * 8));
            if ((bytecount&3) == 3) {
                mctrl.write32(pa + i, word);
                i+=4;
                word = 0;
                
            }
        }
        if(msz > fsz) {
            if(verbose)
                os << "ELF: Writing empty memory segment: " << std::dec << pcount << ", va: 0x" << std::hex << va << ", pa: 0x" << pa << ", fsize: " << std::dec << fsz << ", msize: " << msz << "\n  -> Starting at 0x" << std::hex << pa + i << ", remaining size: " << std::dec << msz - fsz << " (" << (msz-fsz) / 4 << " words, " << (msz-fsz)%4 << " remainder..\n"; 
           // Implement writing zeros to mem here::
           u32 wsize = (msz - fsz) / 4;
           u32 zero = 0;
           for(u32 j = 0; j < wsize; j = j+4)
                 try {
                    mctrl.write32(pa + i + j, zero);
                 } catch (...) {
                     
                     /*
                      * This is to catch when we have BSS sections,
                      *  which do not map to physical memory, like in gaisler 
                      *  buildroot images (0x170 bytes @ 0xffd03000) */
                 }
                
           if((msz-fsz)%4 != 0)
                throw std::runtime_error("Not implemented");

        }

        if (pcount == 0)
            text_count = i;
    }
    free(h2);

    entry_va = SwapBytes(h->e_entry, 4);

    return text_count/4;
}




