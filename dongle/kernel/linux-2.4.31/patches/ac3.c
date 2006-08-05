
/*
 * Extended Audio ioctl for AC3 SPDIF output on Hauppauge MVP Hardware
 * May 2004
 * Bob The Builder
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/ioctl.h>
#include <linux/unistd.h>
#include <asm/uaccess.h>

#include <asm/io.h>
#include "ppc_40x.h"

MODULE_AUTHOR("Bob The Builder");
MODULE_DESCRIPTION("ac3_ioctl extension");

extern void *sys_call_table[];
static int (*real_ioctl)(unsigned int, unsigned int, unsigned long);

static asmlinkage long my_ioctl(unsigned int fd, unsigned int cmd, unsigned long arg)
{
  unsigned long ret=0;

  if (cmd==0x80046108) //the ioctl we're adding/replacing
    {
      switch (arg)
        {
	case 0x00:  //AC3 Mode
	  {
	    printk("set_spdif_bypass:AC3\n");
	    mtdcr(aud0_strmid, 0xffbd);
	    mtdcr(aud0_ctrl2, 0x800);
	    mtdcr(aud0_cmd, 1);
	  }
	  break;
	case 0x01:  //Mpeg Mode (PES)
	  {
	    mtdcr(aud0_strmid, 0xe0c0);
	    mtdcr(aud0_ctrl2, ((mfdcr(aud0_ctrl2)&(~0x800))|1));
	    mtdcr(aud0_cmd, 1);
	    printk("set_spdif_bypass:MPEG(PES)\n");
	  }
	  break;
	case 0x02:  //Mpeg Mode (ES) - untested
	  {
	    mtdcr(aud0_strmid, 0x0);
	    mtdcr(aud0_ctrl2, ((mfdcr(aud0_ctrl2)&(~0x800))|1));
	    mtdcr(aud0_cmd, 1);
	    printk("set_spdif_bypass:MPEG(ES)\n");
	  }
	  break;
	case 0x03:  //DTS Mode - Untested - I don't have a DTS capable amp.
	  {
	    printk("set_spdif_bypass:DTS\n");
	    mtdcr(aud0_strmid, 0xffbd);
	    mtdcr(aud0_ctrl2, 0x802);
	    mtdcr(aud0_cmd, 1);
	  }
	  break;
	default:
	  ret=-EINVAL;
        }
    }
  else ret = real_ioctl(fd, cmd, arg);

  return ret;
}

static int __init ac3_init_module(void)
{
  printk("load ac3_ioctl module\n");
  real_ioctl = sys_call_table[__NR_ioctl];
  sys_call_table[__NR_ioctl] = my_ioctl;
  return 0;
}

static void __exit ac3_cleanup_module(void)
{
  sys_call_table[__NR_ioctl] = real_ioctl;
}

module_init(ac3_init_module);
module_exit(ac3_cleanup_module); 
