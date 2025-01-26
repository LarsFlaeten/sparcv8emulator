/* Interface for accessing Gaisler AMBA Plug&Play Bus
 *
 * (C) Copyright 2011 Frontgrade Gaisler
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
 */

#ifndef __AMBAPP_H__
#define __AMBAPP_H__

#include "common.h"
#include "prom_no.h"
#include "ambapp_ids.h"

/* AMBA Nodes */
extern struct node *pnodes_amba;

/* Initialize AMBA Structures but not bus frequency, pair devices into cores */
extern void ambapp_init(void);

/* Find drivers for AMBA devices and initialize drivers in 5 stages */
extern void ambapp_drivers_init(void);

/* Create Device Nodes */
extern void ambapp_nodes_init(struct node *n);

/* Register the Frequency of an AHB bus. It will be used to calculate the
 * frequency of all other AHB/APB buses.
 */
extern void ambapp_freq_init(int ahb_bus_idx, unsigned long freq_hz);

#define AMBA_DEFAULT_IOAREA 0xfff00000
#define AMBA_CONF_AREA 0xff000
#define AMBA_AHBMST_AREA 0x000
#define AMBA_AHBSLV_AREA 0x800
#define AMBA_ID_AREA (AMBA_CONF_AREA | 0xff0)
#define AMBA_APB_SLAVES 16

/* Configuration Options */

#ifndef CONFIG_AMBA_IO_AREA
#define CONFIG_AMBA_IO_AREA AMBA_DEFAULT_IOAREA
#endif

#ifndef CONFIG_AMBA_AHB_MAX
#define AMBA_AHB_MAX 6
#endif

#ifndef CONFIG_AMBA_APB_MAX
#define AMBA_APB_MAX 6
#endif

/* Structures used to access Plug&Play information directly */
struct ambapp_pnp_axb {
	unsigned int id;
};

struct ambapp_pnp_ahb {
	unsigned int	id;		/* VENDOR, DEVICE, VER, IRQ, */
	unsigned int	custom[3];
	unsigned int	mbar[4];	/* MASK, ADDRESS, TYPE,
						 * CACHABLE/PREFETCHABLE */
};

struct ambapp_pnp_apb {
	unsigned int	id;		/* VENDOR, DEVICE, VER, IRQ, */
	unsigned int	iobar;		/* MASK, ADDRESS, TYPE,
						 * CACHABLE/PREFETCHABLE */
};

/* AMBA Plug&Play AHB Masters & Slaves information locations
 * Max devices is 64 supported by HW, however often only 16
 * are used.
 */
struct ambapp_pnp_info {
	struct ambapp_pnp_ahb	masters[64];
	struct ambapp_pnp_ahb	slaves[63];
	const unsigned int	unused[4];
	const unsigned int	systemid[4];
};

/* AMBA PnP information of a APB Device */
typedef struct {
	unsigned int vendor;
	unsigned int device;
	unsigned char irq[32];
	unsigned int extra;
	unsigned char ver;
	unsigned char bus_idx;
	unsigned char dev_type;
	unsigned int address;
	unsigned int mask;
} ambapp_apbdev;

/* AMBA PnP information of a AHB Device */
typedef struct {
	unsigned int vendor;
	unsigned int device;
	unsigned char irq[32];
	unsigned int extra;
	unsigned char ver;
	unsigned char bus_idx;
	unsigned char dev_type;
	unsigned int userdef[3];
	unsigned int address[4];
	unsigned int mask[4];
	unsigned char type[4];
} ambapp_ahbdev;

/* Common fields of APB Slaves, AHB Slaves and AHB Masters */
typedef struct {
	unsigned int vendor;
	unsigned int device;
	unsigned char irq[32];
	unsigned int extra;
	unsigned char ver;
	unsigned char bus_idx;
	unsigned char dev_type;
} ambapp_dev;

typedef struct ambapp_core_s ambapp_core;

struct ambapp_core_s {
	ambapp_core *next;
	int index; /* Global index of core among all cores */
#ifdef CONFIG_DEBUG
	unsigned int vendor;
	unsigned int device;
#endif
#ifdef HASXML
	/*
	 * Index within set of cores with same vendor and device IDs. Only set
	 * up when using XML.
	 */
	int subindex_for_xml;
#endif

	/* Device Information */
	ambapp_ahbdev *ahbmst;
	ambapp_ahbdev *ahbslv;
	ambapp_apbdev *apbslv;

	ambapp_core *next_in_drv;

	void *fixup_priv; /* For carrying fixup data */
	int fixup_versions;
};

struct ahb_bus {
	unsigned long ioarea;
	unsigned char bus_idx;
	unsigned char parent_bus_idx;
	unsigned char slv_cnt;
	unsigned char mst_cnt;
	unsigned char ffact;
	unsigned char isiommu;
	unsigned char dir; /* frequency direction relative to parent bus */
	unsigned long freq; /* HZ */
	ambapp_ahbdev *msts;
	ambapp_ahbdev *slvs;
};

struct apb_bus {
	unsigned long ioarea;
	unsigned char bus_idx;
	unsigned char ahb_bus_idx;
	unsigned char parent_bus_idx;
	unsigned char slv_cnt;
	ambapp_apbdev *slvs;
};

/* Describes a AMBA PnP bus */
struct ambapp_bus {
	int		ahb_buses;		/* Number of AHB buses */
	int		apb_buses;		/* Number of APB Buses */
	struct ahb_bus	ahbs[AMBA_AHB_MAX];	/* PnP I/O AREAs of AHB buses */
	struct apb_bus	apbs[AMBA_APB_MAX];	/* PnP I/O AREAs of APB buses */

	/* Devices paired based in VENDOR:DEVICE:VERSION in cores */
	ambapp_core	*cores;			/* Linked list of cores */

	struct node *ahbsn[AMBA_AHB_MAX];	/* per bus node */
	struct node **ahbsnn[AMBA_AHB_MAX];	/* bus node child-chain next pointer  */
	
	int core_cnt;
};

/* Processor Local AMBA bus */
extern struct ambapp_bus ambapp_plb;

typedef int (*ambapp_dev_f)(struct ambapp_bus *ainfo, ambapp_dev *dev,
								void *arg);

/* Call 'func' for every AMBA device (device type determined by 'type') present
 * in the 'ainfo' AMBA information. If 'func' returns non-zero the search is
 * stopped and the value is returned. When all devices has been processed zero
 * is returned.
 *
 * ainfo  AMBA information to process
 * type   Bit-mask: For each 0x1="AHB MSTs", 0x2="AHB SLVs", 0x4="APB SLVs"
 * func   Called for each device found.
 * arg    Custom argument passed to 'func'
 */
extern int ambapp_for_each_dev(struct ambapp_bus *ainfo, int type,
				ambapp_dev_f func, void *arg);

extern unsigned long ambapp_dev_freq(ambapp_dev *dev);

/* initialize a AHB device */
extern void ambapp_dev_init_ahb(struct ahb_bus *bus,
				struct ambapp_pnp_ahb *ahb,
				ambapp_ahbdev *dev,
				int got_version);

#define DEV_NONE	0
#define DEV_AHB_MST	1
#define DEV_AHB_SLV	2
#define DEV_APB_SLV	3

#define DEV_VER_NONE    0xff

#define AMBA_TYPE_APBIO 0x1
#define AMBA_TYPE_MEM 0x2
#define AMBA_TYPE_AHBIO 0x3

/* ID layout for APB and AHB devices */
#define AMBA_PNP_ID(vendor, device) (((vendor)<<24) | ((device)<<12))

/* APB Slave PnP layout definitions */
#define AMBA_APB_ID_OFS		(0*4)
#define AMBA_APB_IOBAR_OFS	(1*4)
#define AMBA_APB_CONF_LENGH	(2*4)

/* AHB Master/Slave layout PnP definitions */
#define AMBA_AHB_ID_OFS		(0*4)
#define AMBA_AHB_CUSTOM0_OFS	(1*4)
#define AMBA_AHB_CUSTOM1_OFS	(2*4)
#define AMBA_AHB_CUSTOM2_OFS	(3*4)
#define AMBA_AHB_MBAR0_OFS	(4*4)
#define AMBA_AHB_MBAR1_OFS	(5*4)
#define AMBA_AHB_MBAR2_OFS	(6*4)
#define AMBA_AHB_MBAR3_OFS	(7*4)
#define AMBA_AHB_CONF_LENGH	(8*4)

/* Bridge PnP definitions */
#define AMBAPP_FLAG_FFACT_DIR	0x100	/* Frequency factor direction, 0=down, 1=up */
#define AMBAPP_FLAG_FFACT	0x0f0	/* Frequency factor against top bus */
#define AMBAPP_FLAG_MBUS	0x00c	/* Master bus number mask */
#define AMBAPP_FLAG_SBUS	0x003	/* Slave bus number mask */

/* Macros for extracting information from AMBA PnP information
 * registers.
 */

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

#endif
