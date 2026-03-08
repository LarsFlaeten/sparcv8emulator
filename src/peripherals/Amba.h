// SPDX-License-Identifier: MIT
#pragma once

#include "../sparcv8/MMU.h"


// These functions merely sets up the PNP structs
// Pripheral layout and bank adresses ae hard-coded
// and must correspons with the layout in e.g. the apb master
void amba_ahb_pnp_setup(MCtrl& mctrl);
void amba_apb_pnp_setup(MCtrl& mctrl);



