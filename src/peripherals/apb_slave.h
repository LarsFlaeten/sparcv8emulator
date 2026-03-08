// SPDX-License-Identifier: MIT
#ifndef _APB_SLAVE_H_
#define _APB_SLAVE_H_

#include "../common.h"

class apb_slave {
public:
    apb_slave() {};
    virtual ~apb_slave() {};
    // I/O
    virtual u32 read(u32 offset) const = 0;
    virtual void write(u32 offset, u32 value) = 0;
    
    // We expose reset as public mainly for testing reasons. The system will
    // usually write to some register to trigger a device reset.
    virtual void reset() = 0;
    
    // Standard vedor and device id values, mapped into pnp mem area
    // and used by the pnp routines at boot
    virtual u32 vendor_id() const = 0;
    virtual u32 device_id() const = 0;

    virtual void tick() {;}
};


#endif