/* Common declaration, includes the current configuration
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

#ifndef __COMMON_H__
#define __COMMON_H__

#include "config.h"

struct common_data {
	/* Arguments from Boot Loader */
	int arg0;
	int arg1;
	int arg2;

	unsigned long end_of_mem;
	unsigned long end_of_mem_linux_pa;
	unsigned long linux__bss_start_va;
	unsigned long linux__bss_stop_va;
	int boot_cpu;

	/* Kernel Arguments */
	int karg0;
	int karg1;
};

#ifndef NULL
#define NULL ((void *)0)
#endif

#define KERNEL_VER(major, minor, rev) (((major) << 16) | ((minor) << 8) | (rev))

#ifdef __FIX_LEON3FT_B2BST
#define B2B_INLINE_SINGLE_NOP "nop\n\t"
#define B2B_INLINE_DOUBLE_NOP "nop\n\tnop\n\t"
#else
#define B2B_INLINE_SINGLE_NOP ""
#define B2B_INLINE_DOUBLE_NOP ""
#endif

/* do a physical address bypass load, i.e. for 0x80000000 */
static inline unsigned long load_reg(unsigned long paddr)
{
	unsigned long retval;
	__asm__ __volatile__("lda [%1] %2, %0\n\t" :
			     "=r"(retval) : "r"(paddr), "i"(ASI_LEON_BYPASS));
	return retval;
}

#define BYPASS_LOAD_PA(x) (load_reg((unsigned long)(x)))

/* do a physical address bypass write, i.e. for 0x80000000 */
static inline void store_reg(unsigned long paddr, unsigned long value)
{
	__asm__ __volatile__(B2B_INLINE_DOUBLE_NOP
			     "sta %0, [%1] %2\n\t"
			     B2B_INLINE_DOUBLE_NOP
			     : : "r"(value), "r"(paddr),
			     "i"(ASI_LEON_BYPASS) : "memory");
}

#define BYPASS_STORE_PA(x, v) \
			(store_reg((unsigned long)(x), (unsigned long)(v)))


#endif
