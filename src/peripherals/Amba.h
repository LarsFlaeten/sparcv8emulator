#ifndef _AMBA_H_
#define _AMBA_H_

#include "../sparcv8/MMU.h"


// These functions merely sets up the PNP structs
// Pripheral layout and bank adresses ae hard-coded
// and must correspons with the layout in e.g. the apb master
void amba_ahb_setup(SDRAM2& io);
void amba_apb_setup(SDRAM2& io, u32 base);



#endif
