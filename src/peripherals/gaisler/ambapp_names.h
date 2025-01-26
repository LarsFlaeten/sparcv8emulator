/*
 * AMBA Plug'n Play vendor and device ID definitions.
 *
 * COPYRIGHT (c) 2023.
 * Frontgrade Gaisler AB
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 *
 * DO NOT CHANGE ANYTHING IN THIS FILE!
 * The device and vendor information are extracted with a script from GRLIB.
 * Any changes/bug-fixes should be made in the script, then rerun the script
 * to create a new file.
 *
 * Created from GRLIB revision 4282
 */

#ifndef _ambapp_names_h_
#define _ambapp_names_h_

#include <ambapp_ids.h>

/* A struct containing a readable name of a device id */
struct ambapp_device_name {
        int device_id; /* Unique id of a device */
        char *name;    /* Human readable name of the device */
        char *desc;    /* Description of the device */
};

/* A struct containing a readable name of a vendor and the names of
 * it's devices
 */
struct ambapp_ids {
        unsigned int vendor_id;             /* Unique id of a vendor */
        char *name;                         /* Human readable name of the vendor */
        char *desc;                         /* Description of the vendor */
        struct ambapp_device_name *devices; /* Array of devices from the vendor */
};

/**
* The table that contains all the vendor and device descriptions
 */
extern struct ambapp_ids *ambapp_ids;

/**
* Get human readable vendor/device name
 */
extern const char *ambapp_vendor_id2str(struct ambapp_ids *ids, int vendor);
extern const char *ambapp_device_id2str(struct ambapp_ids *ids, int vendor, int id);

/**
* Get human readable vendor/device description
 */
extern const char *ambapp_vendor_id2desc(struct ambapp_ids *ids, int vendor);
extern const char *ambapp_device_id2desc(struct ambapp_ids *ids, int vendor, int id);

#endif /* _ambapp_names_h_ */
