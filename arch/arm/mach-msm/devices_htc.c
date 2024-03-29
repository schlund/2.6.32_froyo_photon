/* linux/arch/arm/mach-msm/devices.c
 *
 * Copyright (C) 2008 Google, Inc.
 * Copyright (C) 2007-2009 HTC Corporation.
 * Author: Thomas Tsai <thomas_tsai@htc.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/platform_device.h>

#include <linux/dma-mapping.h>
#include <mach/msm_iomap.h>
#include <mach/dma.h>
#include "gpio_chip.h"
#include "devices.h"
#include <mach/board.h>
#include <mach/board_htc.h>
#include <mach/msm_hsusb.h>
#include <linux/usb/android_composite.h>

#include <asm/mach/flash.h>
#include <asm/setup.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>
#include <linux/delay.h>
#include <linux/android_pmem.h>
#include <mach/msm_rpcrouter.h>
#include <mach/msm_iomap.h>
#include <asm/mach/mmc.h>
#include <linux/msm_kgsl.h>
#include <mach/dal_axi.h>
#include "proc_comm.h"
#include <mach/htc_acoustic_wince.h>

#ifndef CONFIG_ARCH_MSM7X30
struct platform_device *devices[] __initdata = {
	&msm_device_nand,
	&msm_device_smd,
	/* &msm_device_i2c, */
};

void __init msm_add_devices(void)
{
	platform_add_devices(devices, ARRAY_SIZE(devices));
}
#endif

static struct android_pmem_platform_data pmem_pdata = {
	.name = "pmem",
	.no_allocator = 1,
	.cached = 1,
};

static struct android_pmem_platform_data pmem_adsp_pdata = {
	.name = "pmem_adsp",
	.no_allocator = 0,
#if defined(CONFIG_ARCH_MSM7227)
	.cached = 1,
#else
	.cached = 0,
#endif
};

static struct android_pmem_platform_data pmem_camera_pdata = {
	.name = "pmem_camera",
	.no_allocator = 0,
	.cached = 0,
};

static struct platform_device pmem_device = {
	.name = "android_pmem",
	.id = 0,
	.dev = { .platform_data = &pmem_pdata },
};

static struct platform_device pmem_adsp_device = {
	.name = "android_pmem",
	.id = 1,
	.dev = { .platform_data = &pmem_adsp_pdata },
};

static struct platform_device pmem_camera_device = {
	.name = "android_pmem",
	.id = 4,
	.dev = { .platform_data = &pmem_camera_pdata },
};

static struct resource ram_console_resource[] = {
	{
		.flags	= IORESOURCE_MEM,
	}
};

static struct platform_device ram_console_device = {
	.name = "ram_console",
	.id = -1,
	.num_resources  = ARRAY_SIZE(ram_console_resource),
	.resource       = ram_console_resource,
};

#ifdef CONFIG_MSM_CAMERA_7X30
static struct resource msm_vpe_resources[] = {
       {
               .start  = 0xAD200000,
               .end    = 0xAD200000 + SZ_1M - 1,
               .flags  = IORESOURCE_MEM,
       },
       {
               .start  = INT_VPE,
               .end    = INT_VPE,
               .flags  = IORESOURCE_IRQ,
       },
};

static struct platform_device msm_vpe_device = {
       .name = "msm_vpe",
       .id   = 0,
       .num_resources = ARRAY_SIZE(msm_vpe_resources),
       .resource = msm_vpe_resources,
};
#endif

#if defined(CONFIG_MSM_HW3D)
static struct resource resources_hw3d[] = {
	{
		.start	= 0xA0000000,
		.end	= 0xA00fffff,
		.flags	= IORESOURCE_MEM,
		.name	= "regs",
	},
	{
		.flags	= IORESOURCE_MEM,
		.name	= "smi",
	},
	{
		.flags	= IORESOURCE_MEM,
		.name	= "ebi",
	},
	{
		.start	= INT_GRAPHICS,
		.end	= INT_GRAPHICS,
		.flags	= IORESOURCE_IRQ,
		.name	= "gfx",
	},
};

static struct platform_device hw3d_device = {
	.name		= "msm_hw3d",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(resources_hw3d),
	.resource	= resources_hw3d,
};
#endif

#if defined(CONFIG_GPU_MSM_KGSL)
static struct resource msm_kgsl_resources[] = {
	{
		.name	= "kgsl_reg_memory",
		.start	= MSM_GPU_REG_PHYS,
		.end	= MSM_GPU_REG_PHYS + MSM_GPU_REG_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.name	= "kgsl_phys_memory",
		.flags	= IORESOURCE_MEM,
	},
	{
#ifdef CONFIG_ARCH_MSM7X30
		.name   = "kgsl_yamato_irq",
		.start  = INT_GRP_3D,
		.end    = INT_GRP_3D,
#else
		.start	= INT_GRAPHICS,
		.end	= INT_GRAPHICS,
#endif
		.flags	= IORESOURCE_IRQ,
	},
#ifdef CONFIG_ARCH_MSM7X30
	{
		.name   = "kgsl_g12_reg_memory",
		.start  = MSM_GPU_2D_REG_PHYS, /* Z180 base address */
		.end    = MSM_GPU_2D_REG_PHYS + MSM_GPU_2D_REG_SIZE - 1,
		.flags  = IORESOURCE_MEM,
	},
	{
		.name   = "kgsl_g12_irq",
		.start  = INT_GRP_2D,
		.end    = INT_GRP_2D,
		.flags  = IORESOURCE_IRQ,
	},
#endif
};

#ifdef CONFIG_ARCH_MSM7X30
static struct kgsl_platform_data kgsl_pdata = {
#ifdef CONFIG_MSM_NPA_SYSTEM_BUS
	/* NPA Flow IDs */
	.high_axi_3d = MSM_AXI_FLOW_3D_GPU_HIGH,
	.high_axi_2d = MSM_AXI_FLOW_2D_GPU_HIGH,
#else
	/* AXI rates in KHz */
	.high_axi_3d = 192000,
	.high_axi_2d = 192000,
#endif
	.max_grp2d_freq = 0,
	.min_grp2d_freq = 0,
	.set_grp2d_async = NULL, /* HW workaround, run Z180 SYNC @ 192 MHZ */
	.max_grp3d_freq = 245000000,
	.min_grp3d_freq = 192000000,
	.set_grp3d_async = set_grp3d_async,
};
#endif

static struct platform_device msm_kgsl_device = {
	.name		= "kgsl",
	.id		= -1,
	.resource	= msm_kgsl_resources,
	.num_resources	= ARRAY_SIZE(msm_kgsl_resources),
#ifdef CONFIG_ARCH_MSM7X30
	.dev = {
		.platform_data = &kgsl_pdata,
	},
#endif
};

#if !defined(CONFIG_ARCH_MSM7X30)
#define PWR_RAIL_GRP_CLK               8
static int kgsl_power_rail_mode(int follow_clk)
{
       int mode = follow_clk ? 0 : 1;
       int rail_id = PWR_RAIL_GRP_CLK;

       return msm_proc_comm(PCOM_CLKCTL_RPC_RAIL_CONTROL, &rail_id, &mode);
}

static int kgsl_power(bool on)
{
       int cmd;
       int rail_id = PWR_RAIL_GRP_CLK;

       cmd = on ? PCOM_CLKCTL_RPC_RAIL_ENABLE : PCOM_CLKCTL_RPC_RAIL_DISABLE;
       return msm_proc_comm(cmd, &rail_id, NULL);
}
#endif

static void pc_clk_reset(unsigned id)
{
	int r;
	r = msm_proc_comm(PCOM_CLKCTL_RPC_RESET, &id, NULL);
//	printk("PCOM_CLKCTL_RPC_ENABLE = %d\n", r);
	return r;
}


// Cotulla: added for photon reset
void kgsl_boot_reset(void)
{
	pc_clk_reset(8);
	kgsl_power_rail_mode(0);
	kgsl_power(false);
	pc_clk_reset(8);
	mdelay(150);
	kgsl_power(true);
	pc_clk_reset(8);
}

#endif

void __init msm_add_mem_devices(struct msm_pmem_setting *setting)
{
	if (setting->pmem_size) {
		pmem_pdata.start = setting->pmem_start;
		pmem_pdata.size = setting->pmem_size;
		platform_device_register(&pmem_device);
	}

	if (setting->pmem_adsp_size) {
		pmem_adsp_pdata.start = setting->pmem_adsp_start;
		pmem_adsp_pdata.size = setting->pmem_adsp_size;
		platform_device_register(&pmem_adsp_device);
	}

#if defined(CONFIG_MSM_HW3D)
	if (setting->pmem_gpu0_size && setting->pmem_gpu1_size) {
		struct resource *res;

		res = platform_get_resource_byname(&hw3d_device, IORESOURCE_MEM,
						   "smi");
		res->start = setting->pmem_gpu0_start;
		res->end = res->start + setting->pmem_gpu0_size - 1;

		res = platform_get_resource_byname(&hw3d_device, IORESOURCE_MEM,
						   "ebi");
		res->start = setting->pmem_gpu1_start;
		res->end = res->start + setting->pmem_gpu1_size - 1;
		platform_device_register(&hw3d_device);
	}
#endif

	if (setting->pmem_camera_size) {
		pmem_camera_pdata.start = setting->pmem_camera_start;
		pmem_camera_pdata.size = setting->pmem_camera_size;
		platform_device_register(&pmem_camera_device);
	}

	if (setting->ram_console_size) {
		ram_console_resource[0].start = setting->ram_console_start;
		ram_console_resource[0].end = setting->ram_console_start
			+ setting->ram_console_size - 1;
		platform_device_register(&ram_console_device);
	}

#if defined(CONFIG_GPU_MSM_KGSL)
	if (setting->kgsl_size) {
		msm_kgsl_resources[1].start = setting->kgsl_start;
		msm_kgsl_resources[1].end = setting->kgsl_start
			+ setting->kgsl_size - 1;
/* due to 7x30 gpu hw bug, we have to apply clk
 * first then power on gpu, thus we move power on
 * into kgsl driver
 */
#if !defined(CONFIG_ARCH_MSM7X30)
		kgsl_power_rail_mode(0);
		kgsl_power(true);
#endif
		platform_device_register(&msm_kgsl_device);
	}
#endif

#ifdef CONFIG_MSM_CAMERA_7X30
		platform_device_register(&msm_vpe_device);
#endif
}

#define PM_LIBPROG      0x30000061
#if (CONFIG_MSM_AMSS_VERSION == 6220) || (CONFIG_MSM_AMSS_VERSION == 6225)
#define PM_LIBVERS      0xfb837d0b
#else
#define PM_LIBVERS      0x10001
#endif

#if 1
static struct platform_device *msm_serial_devices[] __initdata = {
	&msm_device_uart1,
	&msm_device_uart2,
	&msm_device_uart3,
	#ifdef CONFIG_SERIAL_MSM_HS
	&msm_device_uart_dm1,
	&msm_device_uart_dm2,
	#endif
};

int __init msm_add_serial_devices(unsigned num)
{
	if (num > MSM_SERIAL_NUM)
		return -EINVAL;

	return platform_device_register(msm_serial_devices[num]);
}
#endif

#define ATAG_SMI 0x4d534D71
/* setup calls mach->fixup, then parse_tags, parse_cmdline
 * We need to setup meminfo in mach->fixup, so this function
 * will need to traverse each tag to find smi tag.
 */
int __init parse_tag_smi(const struct tag *tags)
{
	int smi_sz = 0, find = 0;
	struct tag *t = (struct tag *)tags;

	for (; t->hdr.size; t = tag_next(t)) {
		if (t->hdr.tag == ATAG_SMI) {
			printk(KERN_DEBUG "find the smi tag\n");
			find = 1;
			break;
		}
	}
	if (!find)
		return -1;

	printk(KERN_DEBUG "parse_tag_smi: smi size = %d\n", t->u.mem.size);
	smi_sz = t->u.mem.size;
	return smi_sz;
}
__tagtable(ATAG_SMI, parse_tag_smi);


#define ATAG_HWID 0x4d534D72
int __init parse_tag_hwid(const struct tag *tags)
{
	int hwid = 0, find = 0;
	struct tag *t = (struct tag *)tags;

	for (; t->hdr.size; t = tag_next(t)) {
		if (t->hdr.tag == ATAG_HWID) {
			printk(KERN_DEBUG "find the hwid tag\n");
			find = 1;
			break;
		}
	}

	if (find)
		hwid = t->u.revision.rev;
	printk(KERN_DEBUG "parse_tag_hwid: hwid = 0x%x\n", hwid);
	return hwid;
}
__tagtable(ATAG_HWID, parse_tag_hwid);

static char *keycap_tag = NULL;
static int __init board_keycaps_tag(char *get_keypads)
{
	if(strlen(get_keypads))
		keycap_tag = get_keypads;
	else
		keycap_tag = NULL;
	return 1;
}
__setup("androidboot.keycaps=", board_keycaps_tag);

void board_get_keycaps_tag(char **ret_data)
{
	*ret_data = keycap_tag;
}
EXPORT_SYMBOL(board_get_keycaps_tag);

static char *cid_tag = NULL;
static int __init board_set_cid_tag(char *get_hboot_cid)
{
	if(strlen(get_hboot_cid))
		cid_tag = get_hboot_cid;
	else
		cid_tag = NULL;
	return 1;
}
__setup("androidboot.cid=", board_set_cid_tag);

void board_get_cid_tag(char **ret_data)
{
	*ret_data = cid_tag;
}
EXPORT_SYMBOL(board_get_cid_tag);

static char *carrier_tag = NULL;
static int __init board_set_carrier_tag(char *get_hboot_carrier)
{
	if(strlen(get_hboot_carrier))
		carrier_tag = get_hboot_carrier;
	else
		carrier_tag = NULL;
	return 1;
}
__setup("androidboot.carrier=", board_set_carrier_tag);

void board_get_carrier_tag(char **ret_data)
{
	*ret_data = carrier_tag;
}
EXPORT_SYMBOL(board_get_carrier_tag);

/* G-Sensor calibration value */
#define ATAG_GS         0x5441001d

unsigned int gs_kvalue;
EXPORT_SYMBOL(gs_kvalue);

static int __init parse_tag_gs_calibration(const struct tag *tag)
{
	gs_kvalue = tag->u.revision.rev;
	printk(KERN_DEBUG "%s: gs_kvalue = 0x%x\n", __func__, gs_kvalue);
	return 0;
}

__tagtable(ATAG_GS, parse_tag_gs_calibration);

/* Proximity sensor calibration values */
#define ATAG_PS         0x5441001c

unsigned int ps_kparam1;
EXPORT_SYMBOL(ps_kparam1);

unsigned int ps_kparam2;
EXPORT_SYMBOL(ps_kparam2);

static int __init parse_tag_ps_calibration(const struct tag *tag)
{
	ps_kparam1 = tag->u.serialnr.low;
	ps_kparam2 = tag->u.serialnr.high;

	printk(KERN_DEBUG "%s: ps_kparam1 = 0x%x, ps_kparam2 = 0x%x\n",
		__func__, ps_kparam1, ps_kparam2);

	return 0;
}

__tagtable(ATAG_PS, parse_tag_ps_calibration);

/******************************************************************************
 * Acoustic driver settings
 ******************************************************************************/
static struct msm_rpc_endpoint *mic_endpoint = NULL;

static void amss_5225_mic_bias_callback(bool on) {
	  struct {
			  struct rpc_request_hdr hdr;
			  uint32_t data;
	  } req;

	  if (!mic_endpoint)
			  mic_endpoint = msm_rpc_connect(0x30000061, 0x0, 0);
	  if (!mic_endpoint) {
			  printk(KERN_ERR "%s: couldn't open rpc endpoint\n", __func__);
			  return;
	  }
	  req.data=cpu_to_be32(on);
	  msm_rpc_call(mic_endpoint, 0x1c, &req, sizeof(req), 5 * HZ);
}
//for photon
void amss_4735_mic_bias_callback(bool on)
{
	int ret;
	int mprog=0x30000061;
	int mvers=0x10001;
	
	struct {
		struct rpc_request_hdr hdr;
		uint32_t data;
	} req;

	if (!mic_endpoint)
		mic_endpoint = msm_rpc_connect(mprog, mvers, 0);
	if (!mic_endpoint) {
		printk("Couldn't open rpc endpoint 0x%x vers 0x%x\n",mprog,mvers);
		mvers=0x0;
		mic_endpoint = msm_rpc_connect(mprog, mvers, 0);
		if (!mic_endpoint) {
			printk("Couldn't open rpc endpoint 0x%x vers 0x%x\n",mprog,mvers);
			return;
		}
	}
	req.data=cpu_to_be32(on);
	ret = msm_rpc_call(mic_endpoint, 0x1c, &req, sizeof(req), 5 * HZ);
	if (ret < 0)
		printk(KERN_ERR "%s: rpc call failed! (%d)\n", __func__, ret);
}

//Table for photon
struct htc_acoustic_wce_amss_data amss_4735_acoustic_data = {
	.volume_table = 0, //0xacc71aa8
	.wb_volume_table = 0x19A, //0xACC71C42
	.ce_table = 0x17b4, //0xacc7325c
	.adie_table = 0xbb4, //0xacc7265c
	.codec_table = 0x334, //0xacc71ddc
	.mic_offset = (MSM_SHARED_RAM_BASE+0x719a8), //0xacc719a8
	.voc_cal_field_size = 17, //0x11
	.mic_bias_callback = amss_4735_mic_bias_callback,
};

struct platform_device acoustic_device = {
	.name = "htc_acoustic",
	.id = -1,
	.dev = {
		.platform_data = &amss_4735_acoustic_data,
		},
};
