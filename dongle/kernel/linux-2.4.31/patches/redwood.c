/*
 * redwood.c - mapper for IBM Redwood-4/5/6 board.
 *
 *
 * Copyright 2001 - 2002 MontaVista Softare Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR   IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT,  INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 *  USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *  History: 12/17/2001 - Armin
 *  		migrated to use do_map_probe
 *
 *  		: 07/11/02 - Armin
 *  		added redwood 6 support
 *
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>

#include <asm/io.h>
#define STBXX_VPD_SZ	0x10000
#define STBXX_OB_SZ	0x20000
#define STBXX_4MB	0x4000000

#if !defined (CONFIG_REDWOOD_6)

#define WINDOW_ADDR 0xffc00000
#define WINDOW_SIZE 0x00400000

#define RW_PART0_OF	0
#define RW_PART0_SZ	0x10000
#define RW_PART1_OF	RW_PART0_SZ	
#define RW_PART1_SZ	0x200000 - 0x10000
#define RW_PART2_OF	0x200000	
#define RW_PART2_SZ	0x10000
#define RW_PART3_OF	0x210000	
#define RW_PART3_SZ	0x200000 - (0x10000 + 0x20000)	
#define RW_PART4_OF	0x3e0000
#define RW_PART4_SZ	0x20000	
static struct mtd_partition redwood_flash_partitions[] = {
	{
		name: "Redwood OpenBIOS Vital Product Data",
		offset: RW_PART0_OF,
		size: RW_PART0_SZ,
		mask_flags: MTD_WRITEABLE	/* force read-only */
	},
	{
		name: "Redwood kernel",
		offset: RW_PART1_OF,
		size: RW_PART1_SZ
	},
	{
		name: "Redwood OpenBIOS non-volatile storage",
		offset: RW_PART2_OF,
		size: RW_PART2_SZ,
		mask_flags: MTD_WRITEABLE	/* force read-only */
	},
	{
		name: "Redwood filesystem",
		offset: RW_PART3_OF,
		size: RW_PART3_SZ
	},
	{
		name: "Redwood OpenBIOS",
		offset: RW_PART4_OF,
		size: RW_PART4_SZ,
		mask_flags: MTD_WRITEABLE	/* force read-only */
	}
};

#else

/* FIXME: the window is bigger - armin */

/*
 * Change4VideoDongle - H. Lin
 * Warnning: What effects would be if not end on erase block size??
 * Thus combine VPD,NVRAM,into one 64KB partition, named VPD
 */
#if 1	//hcw

/* Supported Devices on HCW MediaMVP */
#define AMD2048X16K
#define AMD512X16K

#ifdef AMD2048X16K

#define MB4_WINDOW_ADDR 0xffc00000				/* 4MB part */
#define MB4_WINDOW_SIZE 0x00400000				/* to hook GPIO Pin26 (BI_ADDR10) to A20 */

#define MB4_RW_PART0_OF	0x000000				
#define MB4_RW_PART0_SZ	0x2c0000				
/*
#define MB4_RW_PART0_SZ	0x2c0000				

#define MB4_RW_PART5_OF	MB4_RW_PART0_OF + MB4_RW_PART0_SZ
#define MB4_RW_PART5_SZ	0x200000					

#define MB4_RW_PART4_OF	MB4_RW_PART5_OF + MB4_RW_PART5_SZ
*/
#define MB4_RW_PART4_OF	MB4_RW_PART0_OF + MB4_RW_PART0_SZ
#define MB4_RW_PART4_SZ	0x100000					

#define MB4_RW_PART1_OF	MB4_RW_PART4_OF + MB4_RW_PART4_SZ
#define MB4_RW_PART1_SZ	0x010000					/* 64KB VPD = 4KB VPD + 4KB NVRAM */

#define MB4_RW_PART2_OF	MB4_RW_PART1_OF + MB4_RW_PART1_SZ
#define MB4_RW_PART2_SZ	0x010000					/* 64KB LOGO = 56KB LOGO + 8KB XXX */

#define MB4_RW_PART3_OF	MB4_RW_PART2_OF + MB4_RW_PART2_SZ	/* 128KB Bootloader(Open BIOS) */
#define MB4_RW_PART3_SZ	0x020000

static struct mtd_partition mb4_redwood_flash_partitions[] = {
	{
		name: "HCW MediaMVP KERN",
		offset: MB4_RW_PART0_OF,
		size: MB4_RW_PART0_SZ,
	},
/*
	{
		name: "HCW MediaMVP FS",
		offset: MB4_RW_PART5_OF,
		size: MB4_RW_PART5_SZ,
	},
*/	
	{
		name: "HCW MediaMVP DATA",
		offset: MB4_RW_PART4_OF,
		size: MB4_RW_PART4_SZ,			
	},
	{
		name: "HCW MediaMVP VPD",
		offset: MB4_RW_PART1_OF,
		size: MB4_RW_PART1_SZ,
	},
	{
		name: "HCW MediaMVP LOGO",
		offset: MB4_RW_PART2_OF,
		size: MB4_RW_PART2_SZ,
	},
	{
		name: "HCW MediaMVP BOOT",
		offset: MB4_RW_PART3_OF,
		size: MB4_RW_PART3_SZ,
	}
};

#endif


#ifdef AMD512X16K

#define MB1_WINDOW_ADDR 0xfff00000
#define MB1_WINDOW_SIZE 0x00100000

#define MB1_RW_PART0_OF	0
#define MB1_RW_PART0_SZ	0x80000					/* 8*64 = 512KB data */

#define MB1_RW_PART1_OF	MB1_RW_PART0_OF + MB1_RW_PART0_SZ
#define MB1_RW_PART1_SZ	0x10000					/* 64KB VPD = 4KB VPD + 4KB NVRAM */

#define MB1_RW_PART2_OF	MB1_RW_PART1_OF + MB1_RW_PART1_SZ
#define MB1_RW_PART2_SZ	0x10000					/* 64KB LOGO = 56KB LOGO + 8KB XXX */

#define MB1_RW_PART3_OF	MB1_RW_PART2_OF + MB1_RW_PART2_SZ	
#define MB1_RW_PART3_SZ	0x80000-0x20000-0x20000			/* 320 - 64 = 256KB File System */

#define MB1_RW_PART4_OF	MB1_RW_PART3_OF + MB1_RW_PART3_SZ	/* 128KB Bootloader(Open BIOS) */
#define MB1_RW_PART4_SZ	0x20000

static struct mtd_partition mb1_redwood_flash_partitions[] = {
	{
		name: "HCW MediaMVP KERN",
		offset: MB1_RW_PART0_OF,
		size: MB1_RW_PART0_SZ,
	},
	{
		name: "HCW MediaMVP VPD",
		offset: MB1_RW_PART1_OF,
		size: MB1_RW_PART1_SZ,
		//mask_flags: MTD_WRITEABLE	/* force read-only */
	},
	{
		name: "HCW MediaMVP LOGO",
		offset: MB1_RW_PART2_OF,
		size: MB1_RW_PART2_SZ,
		//mask_flags: MTD_WRITEABLE	/* force read-only */
	},
	{
		name: "HCW MediaMVP FS",
		offset: MB1_RW_PART3_OF,
		size: MB1_RW_PART3_SZ,
	},
	{
		name: "HCW MediaMVP BOOT",
		offset: MB1_RW_PART4_OF,
		size: MB1_RW_PART4_SZ,
		//mask_flags: MTD_WRITEABLE	/* force read-only */
	}
};
#endif

#ifdef AMD128X16K

#define MBQ_WINDOW_ADDR 0xfffC0000
#define MBQ_WINDOW_SIZE 0x00040000

#define MBQ_RW_PART0_OF	0
#define MBQ_RW_PART0_SZ	0x10000		/* 64KB VPD (4KB VPD + 4KB NVRAM) */

#define MBQ_RW_PART1_OF	MBQ_RW_PART0_OF + MBQ_RW_PART0_SZ
#define MBQ_RW_PART1_SZ	0x10000		/* 64KB Logo */

#define MBQ_RW_PART2_OF	MBQ_RW_PART1_OF + MBQ_RW_PART1_SZ
#define MBQ_RW_PART2_SZ	0x20000		/* 128KB Bootloader(Open BIOS) */


static struct mtd_partition mbq_redwood_flash_partitions[] = {
	{
		name: "HCW MediaMVP VPD",
		offset: MBQ_RW_PART0_OF,
		size: MBQ_RW_PART0_SZ,
		//mask_flags: MTD_WRITEABLE	/* force read-only */
	},
	{
		name: "HCW MediaMVP LOGO",
		offset: MBQ_RW_PART1_OF,
		size: MBQ_RW_PART1_SZ,
		//mask_flags: MTD_WRITEABLE	/* force read-only */
	},
	{
		name: "HCW MediaMVP BOOT",
		offset: MBQ_RW_PART2_OF,
		size: MBQ_RW_PART2_SZ,
		//mask_flags: MTD_WRITEABLE	/* force read-only */
	}
};

#endif

#else	//org

#define WINDOW_ADDR 0xff800000
#define WINDOW_SIZE 0x00800000

#define RW_PART0_OF	0
#define RW_PART0_SZ	0x400000	/* 4 MB data */
#define RW_PART1_OF	RW_PART0_OF + RW_PART0_SZ 	
#define RW_PART1_SZ	0x10000		/* 64K VPD */
#define RW_PART2_OF	RW_PART1_OF + RW_PART1_SZ	
#define RW_PART2_SZ	0x400000 - (0x10000 + 0x20000)	
#define RW_PART3_OF	RW_PART2_OF + RW_PART2_SZ
#define RW_PART3_SZ	0x20000

static struct mtd_partition redwood_flash_partitions[] = {
	{
		name: "Redwood kernel",
		offset: RW_PART0_OF,
		size: RW_PART0_SZ
	},
	{
		name: "Redwood OpenBIOS Vital Product Data",
		offset: RW_PART1_OF,
		size: RW_PART1_SZ,
		mask_flags: MTD_WRITEABLE	/* force read-only */
	},
	{
		name: "Redwood filesystem",
		offset: RW_PART2_OF,
		size: RW_PART2_SZ
	},
	{
		name: "Redwood OpenBIOS",
		offset: RW_PART3_OF,
		size: RW_PART3_SZ,
		mask_flags: MTD_WRITEABLE	/* force read-only */
	}
};

#endif

#endif

__u8 redwood_flash_read8(struct map_info *map, unsigned long ofs)
{
	return *(__u8 *)(map->map_priv_1 + ofs);
}

__u16 redwood_flash_read16(struct map_info *map, unsigned long ofs)
{
	return *(__u16 *)(map->map_priv_1 + ofs);
}

__u32 redwood_flash_read32(struct map_info *map, unsigned long ofs)
{
	return *(volatile unsigned int *)(map->map_priv_1 + ofs);
}

void redwood_flash_copy_from(struct map_info *map, void *to,
		unsigned long from, ssize_t len)
{
	memcpy(to, (void *)(map->map_priv_1 + from), len);
}

void redwood_flash_write8(struct map_info *map, __u8 d, unsigned long adr)
{
	*(__u8 *)(map->map_priv_1 + adr) = d;
}

void redwood_flash_write16(struct map_info *map, __u16 d, unsigned long adr)
{
	*(__u16 *)(map->map_priv_1 + adr) = d;
}

void redwood_flash_write32(struct map_info *map, __u32 d, unsigned long adr)
{
	*(__u32 *)(map->map_priv_1 + adr) = d;
}

void redwood_flash_copy_to(struct map_info *map, unsigned long to,
		const void *from, ssize_t len)
{
	memcpy((void *)(map->map_priv_1 + to), from, len);
}



struct map_info redwood_flash_map = {
	name: "HCW MediaMVP",
//	size: WINDOW_SIZE,
	buswidth: 2,
	read8: redwood_flash_read8,
	read16: redwood_flash_read16,
	read32: redwood_flash_read32,
	copy_from: redwood_flash_copy_from,
	write8: redwood_flash_write8,
	write16: redwood_flash_write16,
	write32: redwood_flash_write32,
	copy_to: redwood_flash_copy_to
};

#define NUM_REDWOOD_FLASH_PARTITIONS(flash_partitions) \
	(sizeof(flash_partitions)/sizeof((flash_partitions)[0]))


static struct mtd_info *redwood_mtd;

int __init init_redwood_flash(void)
{
	int numparts;
	struct mtd_partition *redwood_flash_partitions;

#ifdef AMD512X16K
	/*
 	 * Am29LV800/200BT does NOT seem to do CFI
	 */
	redwood_flash_map.map_priv_1 =
		(unsigned long)ioremap(MB1_WINDOW_ADDR, MB1_WINDOW_SIZE);

	if (!redwood_flash_map.map_priv_1) {
		printk("init_redwood_flash: failed to ioremap\n");
		return -EIO;
	}

	printk(KERN_NOTICE "HCW MediaMVP: flash mapping: %x at %x to %lx\n",
			MB1_WINDOW_SIZE, MB1_WINDOW_ADDR, redwood_flash_map.map_priv_1);

	redwood_flash_map.size = MB1_WINDOW_SIZE;
	redwood_mtd = do_map_probe("jedec_probe",&redwood_flash_map);
	if(redwood_mtd) {
		printk("jedec_probe: found redwood_mtd size = %08x\n", redwood_mtd->size);
		if( redwood_mtd->size == 0x00100000 ) {
			redwood_flash_partitions = mb1_redwood_flash_partitions;
			numparts = NUM_REDWOOD_FLASH_PARTITIONS(mb1_redwood_flash_partitions);
			printk(KERN_NOTICE "HCW MediaMVP: jedec_probe: add [%d]parts\n", numparts);
			goto add_parts;
		} else {
			map_destroy( redwood_mtd );
		}
	}
	printk(KERN_NOTICE "HCW MediaMVP: jedec_probe: no known JEDEC part found\n");
	iounmap((void*)(redwood_flash_map.map_priv_1));

try_4mb_flash:
#endif

#ifdef AMD2048X16K

	redwood_flash_map.map_priv_1 =
		(unsigned long)ioremap(MB4_WINDOW_ADDR, MB4_WINDOW_SIZE);

	if (!redwood_flash_map.map_priv_1) {
		printk("init_redwood_flash: failed to ioremap\n");
		return -EIO;
	}

	printk(KERN_NOTICE "HCW MediaMVP: flash mapping: %x at %x to %lx\n",
			MB4_WINDOW_SIZE, MB4_WINDOW_ADDR, redwood_flash_map.map_priv_1);

#if 1 	
	/*
	 * do CFI for 320DT 
	 */
	redwood_flash_map.size = MB4_WINDOW_SIZE;
	redwood_mtd = do_map_probe("cfi_probe",&redwood_flash_map);
	if(redwood_mtd) {
		printk("cfi_probe: found redwood_mtd size = %08x\n", redwood_mtd->size);
		redwood_flash_partitions = mb4_redwood_flash_partitions;
		numparts = NUM_REDWOOD_FLASH_PARTITIONS(mb4_redwood_flash_partitions);
		printk(KERN_NOTICE "HCW MediaMVP: cfi_probe: adding [%d]parts\n", numparts);
		goto add_parts;
	}
	else {
		printk(KERN_NOTICE "HCW MediaMVP: cfi_probe: no known CFI part found\n");
		iounmap((void*)(redwood_flash_map.map_priv_1));
	}
#endif
	
#if 0 
	/*
	 * try jedec too for ST320DT???
	 */
	redwood_flash_map.map_priv_1 =
		(unsigned long)ioremap(MB4_WINDOW_ADDR, MB4_WINDOW_SIZE);

	if (!redwood_flash_map.map_priv_1) {
		printk("init_redwood_flash: failed to ioremap\n");
		return -EIO;
	}

	printk(KERN_NOTICE "HCW MediaMVP: flash mapping: %x at %x to %lx\n",
			MB4_WINDOW_SIZE, MB4_WINDOW_ADDR, redwood_flash_map.map_priv_1);

	redwood_flash_map.size = MB4_WINDOW_SIZE;
	redwood_mtd = do_map_probe("jedec_probe",&redwood_flash_map);
	if(redwood_mtd) {
		printk("jedec_probe: found redwood_mtd size = %08x\n", redwood_mtd->size);
		redwood_flash_partitions = mb4_redwood_flash_partitions;
		numparts = NUM_REDWOOD_FLASH_PARTITIONS(mb4_redwood_flash_partitions);
		printk(KERN_NOTICE "HCW MediaMVP: jedec_probe: adding [%d]parts\n", numparts);
		goto add_parts;
	}
	else {
		printk(KERN_NOTICE "HCW MediaMVP: jedec_probe: no known CFI part found\n");
		iounmap((void*)(redwood_flash_map.map_priv_1));
	}
#endif
#endif


add_parts:
	if (redwood_mtd) {
		redwood_mtd->module = THIS_MODULE;
		return add_mtd_partitions(redwood_mtd, redwood_flash_partitions, numparts);
	}

	return -ENXIO;
}

static void __exit cleanup_redwood_flash(void)
{
	if (redwood_mtd) {
		del_mtd_partitions(redwood_mtd);
		/* moved iounmap after map_destroy  -armin*/
		map_destroy(redwood_mtd);
		iounmap((void *)redwood_flash_map.map_priv_1);
	}
}

module_init(init_redwood_flash);
module_exit(cleanup_redwood_flash);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Armin Kuster <akuster@mvista.com>");
MODULE_DESCRIPTION("MTD map driver for the IBM Redwood reference boards");
