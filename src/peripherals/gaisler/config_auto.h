/* Defaults for all settings available for mklinuximg utility. This file
 * is overrided by the include path to the user specified config_auto.h.
 */

/* set RAM Base Address where the image will be loaded */
#define CONFIG_RAM_START 0x40000000

/* Hardcoded Frequency of AHB Bus 0 (system frequency). [Hz] */
#define CONFIG_BOOTLOADER_FREQ 40000000

/* AMBA Plug and Play Base address for first bus (scanning starts here) */
#define CONFIG_AMBA_IO_AREA 0xfff00000

/* SMP only: IRQ number used by Linux to generate IPIs between CPUs. Zero
 *           lets Linux use its default IRQ value. [1..14]
 */
#define CONFIG_IPI_NUM 0

/* Enable SMP support by bootloader */
/*#define CONFIG_SMP*/

/* SMP only: Maximum number of CPUs used in the SMP system */
/*#define CONFIG_MAX_CPUS 8*/

/* If CONFIG_MAX_CPUS=N, N!=8, then mklinuximg wake CPU N+1..8 to other OS */
#define CONFIG_WAKE_DISABLED_CPUS 0

/* Ethernet MAC address for first MAC. 6byte HEX digit 00:7c:cc:01:45 */
#define CONFIG_ETHMAC 0x000000007ccc0145ULL

/* Create optional "ampopt" AMBA node properties for selected nodes based
 * on the AMBA core index in the system. The AMP options can be used an extra
 * "AMP argument" for drivers that support it, to ignore a core or use parts
 * of the core. This is typically used by AMP systems to help with device
 * resource sharing.
 *
 * If this is defined include/config_ampopts.h must include an array of the
 * AMBA options.
 */
/*#define CONFIG_AMPOPTS*/

/* Linux Kernel command line. */
#define CONFIG_LINUX_CMDLINE "console=ttyS0,38400"

/* Linux Kernel Verion. Different kernels may need sligtly different
 * initialization.
 */
/*#define CONFIG_LINUX_VERSION_CODE 0x00020624*/
#define CONFIG_LINUX_VERSION_CODE 0x00020626

/* Linux kernel BSS area in Virtual addresses. This area is cleared during
 * startup by mklinuximg. This is different for every Linux kernel.
 * (MUST BE SET CORRECTLY OTHERWISE LINUX WILL CRASH/HANG DURING STARTUP)
 */
#define CONFIG_LINUX_BSS_START 0xf099f000
#define CONFIG_LINUX_BSS_SIZE  0x0001d0d8

/* Select which UART will be PROM system console, by index (0=first UART,
 * 1=second UART, and so on)
 */
#define CONFIG_UART_INDEX 0

/* Enable Debug Options */
#define CONFIG_DEBUG

/* Build mklinux but ignore Linux kernel to make kernel image smaller when
 * debugging mklinuximg */
/*#define CONFIG_NO_LINUX*/

/* Let user define custom AMBA nodes [NOT IMLPEMENTED] */
/*#define CONFIG_CUSTOM_NODES*/

/* Debug: Use this to create nodes for APBUART/IRQMP/GPTIMER only. That is the
 *        minimal requirement for booting linux.
 */
/*#define CONFIG_MINIMAL_CORES*/
/*#define CONFIG_APBUART_LINUX_MAX 1*/

/* Standard register addresses for basic peripherals needed to boot Linux
 * later overridden by AMBA Plug&Play routines when found
 */
#define CONFIG_DEBUG_APBUART 0x80000100
#define CONFIG_DEBUG_IRQMP 0x80000200
#define CONFIG_DEBUG_GPTIMER 0x80000300

#define CONFIG_GRGPIO_PROBE 1
#define CONFIG_GRGPIO_NBITS
#define CONFIG_GRGPIO_IMASKGEN
#define CONFIG_GRGPIO_IRQGEN
