/* arch/arm/mach-msm/htc_fb_console.c
 *
 * By Octavian Voicu <octavian@voicu.gmail.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */


/*
 * Ugly hack to get some basic console working for HTC Diamond but should
 * also work for Raphael and other similar phones. Will register itself
 * as an early console to show boot messages. This won't work unless
 * the LCD is already powered up and functional (from wince, boot
 * using Haret). We just DMA pixels to the LCD, all configuration
 * must already be done.
 *
 * Not exactly very clean nor optimized, but it's a one night hack
 * and it works :)
 *
 * If anyone makes any progress on HTC Diamond drop me a line
 * (see email at the top), I do wanna see Android on my HTC Diamond!
 *
 */

#include <linux/console.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/font.h>
#include <linux/delay.h>
#include <asm/io.h>
#include <mach/msm_iomap.h>
#include <mach/msm_fb.h>
#include <asm/pgtable.h>
#include <asm/mach/map.h>
#include "../../../drivers/video/msm/mdp_hw.h"


typedef struct mddi_video_stream mddi_video_stream;
typedef struct mddi_llentry mddi_llentry;
typedef struct mddi_register_access mddi_register_access;


struct __attribute__((packed)) mddi_video_stream 
{
    unsigned short length;      /* length in bytes excluding this field */
    unsigned short type;        /* MDDI_TYPE_VIDEO_STREAM */
    unsigned short client_id;   /* set to zero */
    
    unsigned short format;
    unsigned short pixattr;

    unsigned short left;
    unsigned short top;
    unsigned short right;
    unsigned short bottom;

    unsigned short start_x;
    unsigned short start_y;

    unsigned short pixels;

    unsigned short crc;
    unsigned short reserved;
};

struct __attribute__((packed)) mddi_register_access
{
    unsigned short length;
    unsigned short type;
    unsigned short client_id;

    unsigned short rw_info;    /* flag below | count of reg_data */
#define MDDI_WRITE     (0 << 14)
#define MDDI_READ      (2 << 14)
#define MDDI_READ_RESP (3 << 14)
    
    unsigned reg_addr;
    unsigned short crc;        /* 16 bit crc of the above */

    unsigned reg_data;         /* "list" of 3byte data values */
};

struct __attribute__((packed)) mddi_llentry 
{
    unsigned short flags;
    unsigned short header_count;
    unsigned short data_count;
    void *data;
    mddi_llentry *next;
    unsigned short reserved;
    union 
    {
        mddi_video_stream v;
        mddi_register_access r;
        unsigned _[12];
    } u;
};

#define PIXATTR_BOTH_EYES      3
#define PIXATTR_TO_ALL         (3 << 6)
#define TYPE_VIDEO_STREAM      16


#define MDDI_REG(off) 	       (MSM_MDDI_BASE + (off))
#define MDDI_PRI_PTR           MDDI_REG(0x0008)
#define MDDI_STAT              MDDI_REG(0x0028)
#define CMD_HIBERNATE          0x0300
#define CMD_LINK_ACTIVE        0x0900
#define MDDI_CMD               MDDI_REG(0x0000)

#define MDDI_STAT_PRI_LINK_LIST_DONE         (1 << 5)




/* Defined in /arch/arm/mm/mmu.c, helps us map physical memory to virtual memory.
 * It should work pretty early in the boot process, that why we use it */
extern void create_mapping(struct map_desc *md);

/* Defined in include/linux/fb.h. When, on write, a registered framebuffer device is detected,
 * we immediately unregister ourselves. */
#ifndef CONFIG_HTC_FB_CONSOLE_BOOT
extern int num_registered_fb;
#endif

/* Green message (color = 2), the reset color to white (color = 6) */
#define HTC_FB_MSG		("\n\n" "\x1b" "2" "HTC Linux framebuffer console by druidu" "\x1b" "7" "\n\n")

/* LCD resolution, can differ per device (below is based on HTCLEO) */
#define HTC_FB_LCD_WIDTH	320
#define HTC_FB_LCD_HEIGHT	480

/* Set max console size for a 4x4 font */
#define HTC_FB_CON_MAX_ROWS	(HTC_FB_LCD_WIDTH / 4)
#define HTC_FB_CON_MAX_COLS	(HTC_FB_LCD_HEIGHT / 4)

/* Device dependent settings, currently configured for HTCLEO */
#define HTC_FB_BASE		0xF9500000 /* virtual page for our fb */
#define HTC_FB_PHYS		0x20000000
#define HTC_FB_SIZE		0x00100000

#define MSM_MDDI_BASE		0xF9600000
#define MSM_MDDI_PHYS		0xAA600000
#define MSM_MDDI_SIZE		0x00100000

#define MDDI_LIST_BASE		(0xF9500000 + (320 * 480 * 2))
#define MDDI_LIST_PHYS		(0x20000000 + (320 * 480 * 2))


/* Pack color data in 565 RGB format; r and b are 5 bits, g is 6 bits */
#define HTC_FB_RGB(r, g, b) 	((((r) & 0x1f) << 11) | (((g) & 0x3f) << 5) | (((b) & 0x1f) << 0))

/* Some standard colors */
unsigned short htc_fb_colors[8] = 
{
	HTC_FB_RGB(0x00, 0x00, 0x00), /* Black */
	HTC_FB_RGB(0x1f, 0x00, 0x00), /* Red */
	HTC_FB_RGB(0x00, 0x15, 0x00), /* Green */
	HTC_FB_RGB(0x0f, 0x15, 0x00), /* Brown */
	HTC_FB_RGB(0x00, 0x00, 0x1f), /* Blue */
	HTC_FB_RGB(0x1f, 0x00, 0x1f), /* Magenta */
	HTC_FB_RGB(0x00, 0x3f, 0x1f), /* Cyan */
	HTC_FB_RGB(0x1f, 0x3f, 0x1f)  /* White */
};

/* We can use any font which has width <= 8 pixels */
const struct font_desc *htc_fb_default_font;

/* Pointer to font data (255 * font_rows bytes of data)  */
const unsigned char *htc_fb_font_data;

/* Size of font in pixels */
unsigned int htc_fb_font_cols, htc_fb_font_rows;

/* Size of console in chars */
unsigned int htc_fb_console_cols, htc_fb_console_rows;

/* Current position of cursor (where next character will be written) */
unsigned int htc_fb_cur_x, htc_fb_cur_y;

/* Current fg / bg colors */
unsigned char htc_fb_cur_fg, htc_fb_cur_bg;

/* Buffer to hold characters and attributes */
unsigned char htc_fb_chars[HTC_FB_CON_MAX_ROWS][HTC_FB_CON_MAX_COLS];
unsigned char htc_fb_fg[HTC_FB_CON_MAX_ROWS][HTC_FB_CON_MAX_COLS];
unsigned char htc_fb_bg[HTC_FB_CON_MAX_ROWS][HTC_FB_CON_MAX_COLS];

static void htc_fb_console_write(struct console *console, const char *s, unsigned int count);

/* Console data */
static struct console htc_fb_console = {
	.name	= "htc_fb",
	.write	= htc_fb_console_write,
	.flags	=
#ifdef CONFIG_HTC_FB_CONSOLE_BOOT
		CON_BOOT |
#endif
		CON_PRINTBUFFER | CON_ENABLED,
	.index	= -1,
};

// Redefine these defines so that we can easily borrow the code, first parameter is ignored
#undef mdp_writel
#define mdp_writel(mdp, value, offset) writel(value, HTC_MDP_BASE + offset)
#undef mdp_readl
#define mdp_readl(mdp, offset) readl(HTC_MDP_BASE + offset)


static mddi_llentry *mlist;

#if 0

static void mddi_update(void)
{

    writel(MDDI_LIST_PHYS, MDDI_PRI_PTR);
/*
    while (!(readl(MDDI_STAT) & MDDI_STAT_PRI_LINK_LIST_DONE))
	;		
*/

    while (!(readl(MSM_MDDI_BASE + 0x18) & 0x8000))
	;		

    *(volatile unsigned int*)(0xF8003000 + 0x808) |= 0x1000000;
}

#else

static void mddi_update(void)
{
    int n;

    for (n = 0; n < HTC_FB_LCD_HEIGHT; n++)
    {   
	uint32_t cur = MDDI_LIST_PHYS + sizeof(mddi_llentry) * n;
 
        writel(cur, MDDI_PRI_PTR);

    	while (!(readl(MSM_MDDI_BASE + 0x18) & 0x8000))
		;		
/*
        while (!(readl(MDDI_STAT) & MDDI_STAT_PRI_LINK_LIST_DONE))
	    ;		
*/
    }
    //*(volatile unsigned int*)(0xF8003000 + 0x808) |= 0x1000000;
}


#endif


#if 0

static void mddi_init(void)
{
    int n;


    writel(0x400, MSM_MDDI_BASE + 0x00); // reset
    writel(0x001, MSM_MDDI_BASE + 0x04);
    writel(0x3C00, MSM_MDDI_BASE + 0x10);
    writel(0x003, MSM_MDDI_BASE + 0x14);
    writel(0x005, MSM_MDDI_BASE + 0x34);
    writel(0x00C, MSM_MDDI_BASE + 0x38);

    writel(0x09C, MSM_MDDI_BASE + 0x48);
    writel(0x0D0, MSM_MDDI_BASE + 0x4C);
    writel(0x03C, MSM_MDDI_BASE + 0x50);
    writel(0x002, MSM_MDDI_BASE + 0x2C);
    writel(0x060, MSM_MDDI_BASE + 0x24);
    writel(0x020, MSM_MDDI_BASE + 0x54);
    writel(0xA850F, MSM_MDDI_BASE + 0x68);
    writel(0x60006, MSM_MDDI_BASE + 0x6C);

    writel(0x301, MSM_MDDI_BASE + 0x00);
    writel(0x900, MSM_MDDI_BASE + 0x00);
    writel(0x500, MSM_MDDI_BASE + 0x00);
    writel(0x000, MSM_MDDI_BASE + 0x1C);

    writel(0x6004, MSM_MDDI_BASE + 0x1C);

    writel(0x020, MSM_MDDI_BASE + 0x54);

    mlist = (mddi_llentry *)MDDI_LIST_BASE;

    for(n = 0; n < (HTC_FB_LCD_HEIGHT / 8); n++) 
    {
        unsigned y = n * 8;
        unsigned pixels = HTC_FB_LCD_WIDTH * 8;
        volatile mddi_video_stream *vs = &(mlist[n].u.v);

        vs->length = sizeof(mddi_video_stream) - 2 + (pixels * 2);
        vs->type = TYPE_VIDEO_STREAM;
        vs->client_id = 0;
        vs->format = 0x5565; // FORMAT_16BPP;
        vs->pixattr = PIXATTR_BOTH_EYES | PIXATTR_TO_ALL;

	// mp       
        vs->left = 0;
        vs->right = HTC_FB_LCD_WIDTH - 1;
        vs->top = y;
        vs->bottom = y + 7;
        
        vs->start_x = 0;
        vs->start_y = y;
        
        vs->pixels = pixels;
        vs->crc = 0;
        vs->reserved = 0;
        
        mlist[n].header_count = sizeof(mddi_video_stream) - 2;
        mlist[n].data_count = pixels * 2;
        mlist[n].reserved = 0;
        mlist[n].data = (unsigned) (HTC_FB_PHYS + (y * HTC_FB_LCD_WIDTH * 2));

        mlist[n].flags = 0;
        mlist[n].next = (unsigned) (MDDI_LIST_PHYS + (n + 1) * sizeof(mddi_llentry));    	
    }

    mlist[n-1].flags = 1;
    mlist[n-1].next = 0;

//    *(volatile unsigned int*)(0xF8003000 + 0x808) |= 0x1000000;

//    writel(CMD_HIBERNATE, MDDI_CMD);
//    writel(CMD_LINK_ACTIVE, MDDI_CMD);

}

#else

static void mddi_init(void)
{
    int n;

    writel(0x400, MSM_MDDI_BASE + 0x00); // reset
    writel(0x001, MSM_MDDI_BASE + 0x04);
    writel(0x3C00, MSM_MDDI_BASE + 0x10);
    writel(0x003, MSM_MDDI_BASE + 0x14);
    writel(0x005, MSM_MDDI_BASE + 0x34);
    writel(0x00C, MSM_MDDI_BASE + 0x38);

    writel(0x09C, MSM_MDDI_BASE + 0x48);
    writel(0x0D0, MSM_MDDI_BASE + 0x4C);
    writel(0x03C, MSM_MDDI_BASE + 0x50);
    writel(0x002, MSM_MDDI_BASE + 0x2C);
    writel(0x060, MSM_MDDI_BASE + 0x24);
    writel(0x020, MSM_MDDI_BASE + 0x54);
    writel(0xA850F, MSM_MDDI_BASE + 0x68);
    writel(0x60006, MSM_MDDI_BASE + 0x6C);

    writel(0x301, MSM_MDDI_BASE + 0x00);
    writel(0x900, MSM_MDDI_BASE + 0x00);
    writel(0x500, MSM_MDDI_BASE + 0x00);
    writel(0x000, MSM_MDDI_BASE + 0x1C);

    writel(0x6004, MSM_MDDI_BASE + 0x1C);

    writel(0x020, MSM_MDDI_BASE + 0x54);


    mlist = (mddi_llentry *)MDDI_LIST_BASE;

    for(n = 0; n < HTC_FB_LCD_HEIGHT; n++) 
    {
        unsigned y = n;
        unsigned pixels = HTC_FB_LCD_WIDTH;
        volatile mddi_video_stream *vs = &(mlist[n].u.v);

        vs->length = sizeof(mddi_video_stream) - 2 + (pixels * 2);
        vs->type = 0x10;
        vs->client_id = 0;
        vs->format = 0x5565; // FORMAT_16BPP;
        vs->pixattr = PIXATTR_BOTH_EYES | PIXATTR_TO_ALL;
        
        vs->left = 0;
        vs->right = HTC_FB_LCD_WIDTH - 1;
        vs->top = y;
        vs->bottom = y + 1;
        
        vs->start_x = 0;
        vs->start_y = y;
        
        vs->pixels = pixels;
        vs->crc = 0;
        vs->reserved = 0;
        
        mlist[n].header_count = sizeof(mddi_video_stream) - 2;
        mlist[n].data_count = pixels * 2;
        mlist[n].reserved = 0;
        mlist[n].data = (unsigned) (HTC_FB_PHYS + (y * HTC_FB_LCD_WIDTH * 2));

        mlist[n].flags = 1;
        mlist[n].next = 0; 
    }


//    writel(CMD_HIBERNATE, MDDI_CMD);
//    writel(CMD_LINK_ACTIVE, MDDI_CMD);

//    *(volatile unsigned int*)(0xF8003000 + 0x808) |= 0x1000000;
}



#endif
/* Update the framebuffer from the character buffer then start DMA */
static void htc_fb_console_update(void)
{
	unsigned int memaddr, fbram, stride, width, height, x, y, i, j, r1, c1, r2, c2;
	unsigned int dma2_cfg;
	unsigned short ld_param = 0; /* 0=PRIM, 1=SECD, 2=EXT */
	unsigned short *ptr;
	unsigned char ch;

	fbram = HTC_FB_PHYS;
	memaddr = HTC_FB_BASE;

	ptr = (unsigned short*) memaddr;
	for (i = 0; i < htc_fb_console_rows * htc_fb_font_rows; i++) {
		r1 = i / htc_fb_font_rows;
		r2 = i % htc_fb_font_rows;
		for (j = 0; j < htc_fb_console_cols * htc_fb_font_cols; j++) {
			c1 = j / htc_fb_font_cols;
			c2 = j % htc_fb_font_cols;
			ch = htc_fb_chars[r1][c1];
			*ptr++ = htc_fb_font_data[(((int) ch) * htc_fb_font_rows) + r2] & ((1 << (htc_fb_font_cols - 1)) >> c2)
				? htc_fb_colors[htc_fb_fg[r1][c1]]
				: htc_fb_colors[htc_fb_bg[r1][c1]];
		}
		ptr += HTC_FB_LCD_WIDTH - htc_fb_console_cols * htc_fb_font_cols;
	}

	mddi_update();
}

/* Clear screen and buffers */
static void htc_fb_console_clear(void)
{
	/* Default white on black, clear everything */
	memset((void*) (HTC_FB_BASE), 0, HTC_FB_LCD_WIDTH * HTC_FB_LCD_HEIGHT * 2);
	memset(htc_fb_chars, 0, htc_fb_console_cols * htc_fb_console_rows);
	memset(htc_fb_fg, 7, htc_fb_console_cols * htc_fb_console_rows);
	memset(htc_fb_bg, 0, htc_fb_console_cols * htc_fb_console_rows);
	htc_fb_cur_x = htc_fb_cur_y = 0;
	htc_fb_cur_fg = 7;
	htc_fb_cur_bg = 0;
	htc_fb_console_update();
}

static struct console htc_fb_console;

/* Write a string to character buffer; handles word wrapping, auto-scrolling, etc
 * After that, calls htc_fb_console_update to send data to the LCD */
static void htc_fb_console_write(struct console *console, const char *s, unsigned int count)
{
	unsigned int i, j, k, scroll;
	const char *p;

#ifndef CONFIG_HTC_FB_CONSOLE_BOOT
	// See if a framebuffer has been registered. If so, we disable this console to prevent conflict with
	// other FB devices (i.e. msm_fb).
	if (num_registered_fb > 0) {
		printk(KERN_INFO "htc_fb_console: framebuffer device detected, disabling boot console\n");
		console->flags = 0;
		return;
	}
#endif

	scroll = 0;
	for (k = 0, p = s; k < count; k++, p++) {
		if (*p == '\n') {
			/* Jump to next line */
			scroll = 1;
		} else if (*p == '\t') {
			/* Tab size 8 chars */
			htc_fb_cur_x = (htc_fb_cur_x + 7) % 8;
			if (htc_fb_cur_x >= htc_fb_console_cols) {
				scroll = 1;
			}
		} else if (*p == '\x1b') {
			/* Escape char (ascii 27)
			 * Some primitive way to change color:
			 * \x1b followed by one digit to represent color (0 black ... 7 white) */
			if (k < count - 1) {
				p++;
				htc_fb_cur_fg = *p - '0';
				if (htc_fb_cur_fg >= 8) {
					htc_fb_cur_fg = 7;
				}
			}
		} else if (*p != '\r') {
			/* Ignore \r, other cars get written here */
			htc_fb_chars[htc_fb_cur_y][htc_fb_cur_x] = *p;
			htc_fb_fg[htc_fb_cur_y][htc_fb_cur_x] = htc_fb_cur_fg;
			htc_fb_bg[htc_fb_cur_y][htc_fb_cur_x] = htc_fb_cur_bg;
			htc_fb_cur_x++;
			if (htc_fb_cur_x >= htc_fb_console_cols) {
				scroll = 1;
			}
		}
		if (scroll) {
			scroll = 0;
			htc_fb_cur_x = 0;
			htc_fb_cur_y++;
			if (htc_fb_cur_y == htc_fb_console_rows) {
				/* Scroll below last line, shift all rows up
				 * Should have used a bigger buffer so no shift,
				 * would actually be needed -- but hey, it's a one night hack */
				htc_fb_cur_y--;
				for (i = 1; i < htc_fb_console_rows; i++) {
					for (j = 0; j < htc_fb_console_cols; j++) {
						htc_fb_chars[i - 1][j] = htc_fb_chars[i][j];
						htc_fb_fg[i - 1][j] = htc_fb_fg[i][j];
						htc_fb_bg[i - 1][j] = htc_fb_bg[i][j];
					}
				}
				for (j = 0; j < htc_fb_console_cols; j++) {
					htc_fb_chars[htc_fb_console_rows - 1][j] = 0;
					htc_fb_fg[htc_fb_console_rows - 1][j] = htc_fb_cur_fg;
					htc_fb_bg[htc_fb_console_rows - 1][j] = htc_fb_cur_bg;
				}
			}
		}
	}

	htc_fb_console_update();

#ifdef CONFIG_HTC_FB_CONSOLE_DELAY
	/* Delay so we can see what's there, we have no keys to scroll */
	mdelay(500);
#endif
}

// Make sure we don't init twice
static bool htc_fb_console_init_done = false;

/* Init console on LCD using MDDI/MDP interface to transfer pixel data.
 * We can DMA to the board, as long as we give a physical address to the LCD
 * controller and use the coresponding virtual address to write pixels to.
 * The physical address I used is the one wince had for the framebuffer */
int __init htc_fb_console_init(void)
{
	struct map_desc map, map_mdp;

//	return 0;

	if (htc_fb_console_init_done)
	{
		printk(KERN_INFO "htc_fb_console_init: already initialized, bailing out\n");
		return 0;
	}
	htc_fb_console_init_done = true;

	/* Map the framebuffer Windows was using, as we know the physical address */
	map.pfn = __phys_to_pfn(HTC_FB_PHYS & SECTION_MASK);
	map.virtual = HTC_FB_BASE;
	map.length = (unsigned long)HTC_FB_SIZE;
	map.type = MT_DEVICE; // MT_MEMORY;
	/* Ugly hack, but we're not sure what works and what doesn't,
	 * so better use the lowest level we have for setting the mapping */
	create_mapping(&map);
	
	/* Map the MDP */
	map_mdp.pfn = __phys_to_pfn(MSM_MDDI_PHYS & SECTION_MASK);
	map_mdp.virtual = MSM_MDDI_BASE;
	map_mdp.length = (unsigned long)MSM_MDDI_SIZE;
	map_mdp.type = MT_DEVICE; // MT_MEMORY;
	create_mapping(&map_mdp);

	/* Init font (we support any font that has width <= 8; height doesn't matter) */
	htc_fb_default_font = get_default_font(HTC_FB_LCD_WIDTH, HTC_FB_LCD_HEIGHT, 0xFF, 0xFFFFFFFF);
	if (!htc_fb_default_font) {
		printk(KERN_WARNING "Can't find a suitable font for htc_fb\n");
		return -1;
	}

	htc_fb_font_data = htc_fb_default_font->data;
	htc_fb_font_cols = htc_fb_default_font->width;
	htc_fb_font_rows = htc_fb_default_font->height;
	htc_fb_console_cols = HTC_FB_LCD_WIDTH / htc_fb_font_cols;
	if (htc_fb_console_cols > HTC_FB_CON_MAX_COLS)
		htc_fb_console_cols = HTC_FB_CON_MAX_COLS;
	htc_fb_console_rows = HTC_FB_LCD_HEIGHT / htc_fb_font_rows;
	if (htc_fb_console_rows > HTC_FB_CON_MAX_ROWS)
		htc_fb_console_rows = HTC_FB_CON_MAX_ROWS;

	mddi_init();

	/* Clear the buffer; we could probably see the Haret output if we didn't clear
	 * the buffer (if it used same physical address) */
	htc_fb_console_clear();

	/* Welcome message */
	htc_fb_console_write(&htc_fb_console, HTC_FB_MSG, strlen(HTC_FB_MSG));

	/* Register console */
	register_console(&htc_fb_console);
	console_verbose();

	return 0;
}

console_initcall(htc_fb_console_init);

