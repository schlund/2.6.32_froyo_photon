/* arch/arm/mach-msm/htc_battery_smem.c
 * Based on: htc_battery.c by HTC and Google
 *
 * updated by r0bin and photon community (2011)
 * Copyright (C) 2008 HTC Corporation.
 * Copyright (C) 2008 Google, Inc.
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

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/power_supply.h>
#include <linux/platform_device.h>
#include <linux/debugfs.h>
#include <linux/wakelock.h>
#include <linux/io.h>
#include <asm/gpio.h>
#include <mach/board.h>
#include <asm/mach-types.h>
#include <linux/io.h>

#include "proc_comm_wince.h"

#include <mach/msm_iomap.h>
#include <mach/htc_battery_smem_def.h>
#include <mach/htc_battery_smem.h>

static struct wake_lock vbus_wake_lock;
static int bat_suspended = 0;
static int batt_vref = 0, batt_vref_half = 0;
static int g_usb_online;

enum {
	DEBUG_BATT	= 1<<0,
	DEBUG_CABLE	= 1<<1,
	DEBUG_LOG	= 1<<2,
};
static int debug_mask = 0;
module_param_named(debug, debug_mask, int, S_IRUGO | S_IWUSR | S_IWGRP);

/* Default battery will be the photon 1200mAh.
 * batt_param will be set after battery detection but must be initialized
 * because it may be used before battery is correctly detected
 * sBattery_Parameters is defined in htc_battery_smem.h.
 * To add a new battery profile, just add it in the htc_battery_smem.h and modify the battery 
 * detection routine of the device
 */
static struct sBattery_Parameters* batt_param = (struct sBattery_Parameters*)&sBatParams_photon[0];

#define MODULE_NAME "htc_battery"

#define TRACE_BATT 1
//#undef TRACE_BATT

#if TRACE_BATT
 #define BATT(x...) printk(KERN_INFO "[BATT] " x)
#else
 #define BATT(x...) do {} while (0)
#endif

#define BATT_ERR(x...) printk(KERN_ERR "[BATT_ERR] " x)


/* battery detail logger */
#define HTC_BATTERY_BATTLOGGER		1
//#undef HTC_BATTERY_BATTLOGGER

#if HTC_BATTERY_BATTLOGGER
 #include <linux/rtc.h>
 #define BATTLOG(x...) do { \
 struct timespec ts; \
 struct rtc_time tm; \
 getnstimeofday(&ts); \
 rtc_time_to_tm(ts.tv_sec, &tm); \
 printk(KERN_INFO "[BATTLOG];%d-%02d-%02d %02d:%02d:%02d", \
 tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, \
 tm.tm_hour, tm.tm_min, tm.tm_sec); \
 printk(";" x); \
 } while (0)
#else
 #define BATTLOG(x...) do {} while (0)
#endif

typedef enum {
	DISABLE = 0,
	ENABLE_SLOW_CHG,
	ENABLE_FAST_CHG
} batt_ctl_t;

/* This order is the same as htc_power_supplies[]
 * And it's also the same as htc_cable_status_update()
 */
typedef enum {
	CHARGER_BATTERY = 0,
	CHARGER_USB,
	CHARGER_AC
} charger_type_t;

struct battery_info_reply {
	u32 batt_id;		/* Battery ID from ADC */
	u32 batt_vol;		/* Battery voltage from ADC */
	u32 batt_temp;		/* Battery Temperature (C)corrected value from formula and ADC */
	s32 batt_current;	/* Battery charge current from ADC */
	s32 batt_discharge;	/* Battery discharge current from ADC */
	u32 level;		/* formula */
	u32 charging_source;	/* 0: no cable, 1:usb, 2:AC */
	u32 charging_enabled;	/* 0: Disable, 1: Enable */
	u32 full_bat;		/* Full capacity of battery (mAh) */
	u32 batt_tempRAW;	/* Battery Temperature (C) from formula and ADC */
};

struct htc_battery_info {
	int present;
	unsigned long update_time;

	/* lock to protect the battery info */
	struct mutex lock;

	struct battery_info_reply rep;
	smem_batt_t *resources;
};

static struct htc_battery_info htc_batt_info;
static struct battery_info_reply old_batt_info;

static unsigned int cache_time = 1000;

static int htc_battery_initial = 0;
static bool not_yet_started = true;
static unsigned int time_stamp = 0;

/* simple maf filter stuff - how much old values should be used for recalc ...*/
#define BATT_MAF_SIZE 6
static short volt_maf_buffer[BATT_MAF_SIZE];
static short volt_maf_size = 0;
static short volt_maf_last = 0;

static void maf_add_value( short volt )
{
	// check if we need to correct the index
	if ( volt_maf_last == BATT_MAF_SIZE-1 )
		volt_maf_last = 0;

	// add value to filter buffer
	volt_maf_buffer[volt_maf_last] = volt;
	volt_maf_last++;

	if ( volt_maf_size != BATT_MAF_SIZE-1 )
		volt_maf_size++;	
}

/* calculated on the fly.... no caching */
static short maf_get_avarage(void)
{
	int i;
	int maf_temp;

	// make sure we only do it when we have data
	if ( volt_maf_size == 0 )
		return 0;

	// no need todo the avaraging
	if ( volt_maf_size == 1 )
		return volt_maf_buffer[0];

	// our start value is the first sample
	maf_temp = volt_maf_buffer[0];

	for (i=1; i < volt_maf_size; i++) {
		maf_temp = ( maf_temp + volt_maf_buffer[i] ) / 2;		
	}

	return maf_temp;
}

static void maf_clear(void)
{
	int i;
	for ( i = 0; i < BATT_MAF_SIZE;i++ )
		volt_maf_buffer[i] = 0;

	volt_maf_size = 0;
	volt_maf_last = 0;
}

/* ADC linear correction numbers.
 */
static u32 htc_adc_a = 0;					// Account for Divide Resistors
static u32 htc_adc_b = 0;
static u32 htc_adc_range = 0x1000;	// 12 bit adc range correction.
static u32 batt_vendor = 0;

#define GET_BATT_ID         readl(MSM_SHARED_RAM_BASE + 0xFC0DC) 
#define GET_ADC_VREF        readl(MSM_SHARED_RAM_BASE + 0xFC0E0) 
#define GET_ADC_0_5_VREF    readl(MSM_SHARED_RAM_BASE + 0xFC0E4) 

static int get_battery_id_detection( struct battery_info_reply *buffer );
static int htc_get_batt_info( struct battery_info_reply *buffer );

static int init_battery_settings( struct battery_info_reply *buffer ) {

	if ( buffer == NULL )
		return -EINVAL;

	if ( htc_get_batt_info( buffer ) < 0 )
		return -EINVAL;

	mutex_lock( &htc_batt_info.lock );

	batt_vref = GET_ADC_VREF;
	batt_vref_half = GET_ADC_0_5_VREF;

	if ( batt_vref - batt_vref_half >= 500 ) {
		// set global correction var
		htc_adc_a = 625000 / ( batt_vref - batt_vref_half );
		htc_adc_b = 1250000 - ( batt_vref * htc_adc_a );
	}

	// calculate the current adc range correction.
	htc_adc_range = ( batt_vref * 0x1000 ) / 1250;

	if ( get_battery_id_detection( buffer ) < 0 ) {
		mutex_unlock(&htc_batt_info.lock);
		if(debug_mask&DEBUG_LOG)
			BATT_ERR("Critical Error on: get_battery_id_detection: VREF=%d; 0.5-VREF=%d; ADC_A=%d; ADC_B=%d; htc_adc_range=%d; batt_id=%d; batt_vendor=%d; full_bat=%d\n", \
			batt_vref, batt_vref_half, htc_adc_a, htc_adc_b, htc_adc_range, buffer->batt_id, batt_vendor, buffer->full_bat);
		return -EINVAL;
	}

	mutex_unlock(&htc_batt_info.lock);
	if(debug_mask&DEBUG_LOG)
		BATT("init_battery_settings: VREF=%d; 0.5-VREF=%d; ADC_A=%d; ADC_B=%d; htc_adc_range=%d; batt_id=%d; batt_vendor=%d; full_bat=%d\n", \
		batt_vref, batt_vref_half, htc_adc_a, htc_adc_b, htc_adc_range, buffer->batt_id, batt_vendor, buffer->full_bat);

	return 0;
}

static int get_battery_id_detection( struct battery_info_reply *buffer ) {
	u32 batt_id;
	struct msm_dex_command dex;

	dex.cmd = PCOM_GET_BATTERY_ID;
	msm_proc_comm_wince( &dex, 0 );

	batt_id = GET_BATT_ID;

	/* buffer->batt_id will be overwritten on next battery reading so we can use it as
	 * a temp variable to pass it to machine specific battery detection
	 */
	buffer->batt_id = batt_id;
	// apply the adc range correction
	buffer->batt_id = ( buffer->batt_id * 0xA28 ) / htc_adc_range;  
        //photon batt capacity = 1200Mah
	buffer->full_bat = 1200000;

	//update battery params
	batt_param = (struct sBattery_Parameters*) sBatParams_photon[0];	
	return 0;
}

static enum power_supply_property htc_battery_properties[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CAPACITY,
};

static enum power_supply_property htc_power_properties[] = {
	POWER_SUPPLY_PROP_ONLINE,
};

static char *supply_list[] = {
	"battery",
};

/* HTC dedicated attributes */
static ssize_t htc_battery_show_property(struct device *dev,
					  struct device_attribute *attr,
					  char *buf);

static int htc_power_get_property(struct power_supply *psy,
				    enum power_supply_property psp,
				    union power_supply_propval *val);

static int htc_battery_get_property(struct power_supply *psy,
				    enum power_supply_property psp,
				    union power_supply_propval *val);

static struct power_supply htc_power_supplies[] = {
	{
		.name = "battery",
		.type = POWER_SUPPLY_TYPE_BATTERY,
		.properties = htc_battery_properties,
		.num_properties = ARRAY_SIZE(htc_battery_properties),
		.get_property = htc_battery_get_property,
	},
	{
		.name = "usb",
		.type = POWER_SUPPLY_TYPE_USB,
		.supplied_to = supply_list,
		.num_supplicants = ARRAY_SIZE(supply_list),
		.properties = htc_power_properties,
		.num_properties = ARRAY_SIZE(htc_power_properties),
		.get_property = htc_power_get_property,
	},
	{
		.name = "ac",
		.type = POWER_SUPPLY_TYPE_MAINS,
		.supplied_to = supply_list,
		.num_supplicants = ARRAY_SIZE(supply_list),
		.properties = htc_power_properties,
		.num_properties = ARRAY_SIZE(htc_power_properties),
		.get_property = htc_power_get_property,
	},
};

/* -------------------------------------------------------------------------- */

#if defined(CONFIG_DEBUG_FS)
int htc_battery_set_charging(batt_ctl_t ctl);
static int batt_debug_set(void *data, u64 val)
{
	return htc_battery_set_charging((batt_ctl_t) val);
}

static int batt_debug_get(void *data, u64 *val)
{
	return -ENOSYS;
}

DEFINE_SIMPLE_ATTRIBUTE(batt_debug_fops, batt_debug_get, batt_debug_set, "%llu\n");
static int __init batt_debug_init(void)
{
	struct dentry *dent;

	dent = debugfs_create_dir("htc_battery", 0);
	if (IS_ERR(dent))
		return PTR_ERR(dent);

	debugfs_create_file("charger_state", 0644, dent, NULL, &batt_debug_fops);

	return 0;
}

device_initcall(batt_debug_init);
#endif

static int init_batt_gpio(void)
{
	//r0bin: A9 will shutdown the phone if battery is pluged out, so don't bother
	//if (gpio_request(htc_batt_info.resources->gpio_battery_detect, "batt_detect") < 0)
	//	goto gpio_failed;
	
	//charge control
	if (gpio_request(htc_batt_info.resources->gpio_charger_enable, "charger_en") < 0)
	{
		printk("%s: gpio request charger_en failed!\n",__FUNCTION__);
		goto gpio_failed;
	}
	
	//high speed or low speed charge
	if (gpio_request(htc_batt_info.resources->gpio_charger_fast_dis, "fast_charge_dis") < 0){
		printk("%s: gpio request gpio_charger_fast_dis no%d failed!\n",__FUNCTION__,htc_batt_info.resources->gpio_charger_fast_dis );
		goto gpio_failed;
	}
	if (gpio_request(htc_batt_info.resources->gpio_charger_fast_en, "fast_charge_en") < 0){
		printk("%s: gpio request gpio_charger_fast_en no%d failed!\n",__FUNCTION__,htc_batt_info.resources->gpio_charger_fast_en );
		goto gpio_failed;
	}
	//r0bin: no need of another gpio, we detect AC through other means
	//if ( machine_is_htckovsky() || machine_is_htctopaz() )
	//	if (gpio_request(htc_batt_info.resources->gpio_ac_detect, "ac_detect") < 0)
	//		goto gpio_failed;
	
	return 0;

gpio_failed:
	return -EINVAL;
}

/*
 *	battery_charging_ctrl - battery charing control.
 * 	@ctl:			battery control command
 *
 */
static int battery_charging_ctrl(batt_ctl_t ctl)
{
	int result = 0;

	switch (ctl) {
	case DISABLE:
		if(debug_mask&DEBUG_CABLE)
			BATT("CTRL charger OFF\n");
		/* 0 for enable; 1 disable */
		result = gpio_direction_output(htc_batt_info.resources->gpio_charger_fast_dis, 1);
		result = gpio_direction_output(htc_batt_info.resources->gpio_charger_fast_en, 0);
		result = gpio_direction_output(htc_batt_info.resources->gpio_charger_enable, 1);
		break;
	case ENABLE_SLOW_CHG:
		if(debug_mask&DEBUG_CABLE)
			BATT("CTRL charger ON (SLOW)\n");
		result = gpio_direction_output(htc_batt_info.resources->gpio_charger_fast_dis, 1);
		result = gpio_direction_output(htc_batt_info.resources->gpio_charger_fast_en, 0);
		result = gpio_direction_output(htc_batt_info.resources->gpio_charger_enable, 0);
		break;
	case ENABLE_FAST_CHG:
		if(debug_mask&DEBUG_CABLE)
			BATT("CTRL charger ON (FAST)\n");
		result = gpio_direction_output(htc_batt_info.resources->gpio_charger_fast_dis, 0);
		result = gpio_direction_output(htc_batt_info.resources->gpio_charger_fast_en, 1);
		result = gpio_direction_output(htc_batt_info.resources->gpio_charger_enable, 0);
		break;
	default:
		BATT_ERR("Not supported battery ctr called.!\n");
		result = -EINVAL;
		break;
	}

	return result;
}

int htc_battery_set_charging(batt_ctl_t ctl)
{
	int rc;

	if ((rc = battery_charging_ctrl(ctl)) < 0)
		goto result;

	if (!htc_battery_initial) {
		htc_batt_info.rep.charging_enabled = ctl & 0x3;
	} else {
		mutex_lock(&htc_batt_info.lock);
		htc_batt_info.rep.charging_enabled = ctl & 0x3;
		mutex_unlock(&htc_batt_info.lock);
	}
result:
	return rc;
}

int htc_battery_status_update(u32 curr_level)
{
	int notify;
	unsigned charge = 0;

	if (!htc_battery_initial)
		return 0;

	mutex_lock(&htc_batt_info.lock);
	notify = (htc_batt_info.rep.level != curr_level);
	htc_batt_info.rep.level = curr_level;
#ifdef FAST_USB_CHARGE
	/* If battery is above 95%, switch to slow charging.
	 * If battery is below 90%, switch to fast charging.
	 */
	if (curr_level > 95 && htc_batt_info.rep.charging_enabled == ENABLE_FAST_CHG)
		charge = ENABLE_SLOW_CHG;
	else if (curr_level < 90 && htc_batt_info.rep.charging_enabled == ENABLE_SLOW_CHG)
		charge = ENABLE_FAST_CHG;
#endif
	mutex_unlock(&htc_batt_info.lock);

	if (notify)
		power_supply_changed(&htc_power_supplies[CHARGER_BATTERY]);
#ifdef FAST_USB_CHARGE
	if (charge)
		htc_battery_set_charging(charge);
#endif
	return 0;
}

static bool on_battery;
int htc_cable_status_update(int status)
{
	int rc = 0;
	unsigned source;
	unsigned last_source;
	unsigned vbus_status;
#ifdef FAST_USB_CHARGE
	unsigned charger;
#endif
	vbus_status = readl(MSM_SHARED_RAM_BASE+0xfc00c);

	if (!htc_battery_initial)
		return 0;
	
	mutex_lock(&htc_batt_info.lock);
	if(vbus_status && g_usb_online) {
		status=CHARGER_USB;	/* vbus present, usb connection online (perhaps breaks kovsky ?) */
		on_battery = false;
#ifdef FAST_USB_CHARGE
		charger = ENABLE_FAST_CHG;
#endif
	} else if (vbus_status && !g_usb_online) {
		status=CHARGER_AC;	/* vbus present, no usb */
		on_battery = false;
#ifdef FAST_USB_CHARGE
		charger = ENABLE_FAST_CHG;
#endif
	} else {
		g_usb_online = 0;
		status=CHARGER_BATTERY;
		on_battery = true;
#ifdef FAST_USB_CHARGE
		charger = DISABLE;
#endif
	}
	printk("%s, vbus=%d,usbonline=%d status=%d\n",__func__,vbus_status,g_usb_online,status);
	last_source = htc_batt_info.rep.charging_source;

	switch(status) {
	case CHARGER_BATTERY:
		if(debug_mask&DEBUG_CABLE)
			BATT("cable NOT PRESENT\n");
		htc_batt_info.rep.charging_source = CHARGER_BATTERY;
		break;
	case CHARGER_USB:
		if(debug_mask&DEBUG_CABLE)
			BATT("cable USB\n");
		htc_batt_info.rep.charging_source = CHARGER_USB;
		break;
	case CHARGER_AC:
		if(debug_mask&DEBUG_CABLE)
			BATT("cable AC\n");
		htc_batt_info.rep.charging_source = CHARGER_AC;
		break;
	default:
		BATT_ERR("%s - Not supported cable status received!\n", __FUNCTION__);
		rc = -EINVAL;
	}
	source = htc_batt_info.rep.charging_source;
	mutex_unlock(&htc_batt_info.lock);
#ifdef FAST_USB_CHARGE
	if (charger == ENABLE_FAST_CHG && htc_batt_info.rep.level > 95)
		charger = ENABLE_SLOW_CHG;
	htc_battery_set_charging(charger);
#else
        htc_battery_set_charging(status);
#endif
	msm_hsusb_set_vbus_state((source==CHARGER_USB) || (source==CHARGER_AC));

	if (  source == CHARGER_USB || source==CHARGER_AC ) {
		wake_lock(&vbus_wake_lock);
	} else if(last_source != source) {
		/* give userspace some time to see the uevent and update
		 * LED state or whatnot...
		 */
		wake_lock_timeout(&vbus_wake_lock, HZ / 2);
	} else {
		wake_unlock(&vbus_wake_lock);
	}

	/* make sure that we only change the powersupply state if we really have to */
	if (source == CHARGER_BATTERY || last_source == CHARGER_BATTERY)
		power_supply_changed(&htc_power_supplies[CHARGER_BATTERY]);
	if (source == CHARGER_USB || last_source == CHARGER_USB)
		power_supply_changed(&htc_power_supplies[CHARGER_USB]);
	if (source == CHARGER_AC || last_source == CHARGER_AC)
		power_supply_changed(&htc_power_supplies[CHARGER_AC]);

	return rc;
}

/* A9 reports USB charging when helf AC cable in and China AC charger. */
/* Work arround: notify userspace AC charging first,
and notify USB charging again when receiving usb connected notification from usb driver. */
void notify_usb_connected(int online)
{
	printk(KERN_DEBUG "%s: online=%d\n", __func__, online);
	printk("%s: online=%d\n", __func__, online);

	g_usb_online = online;
	if (not_yet_started) return;
	
	mutex_lock(&htc_batt_info.lock);
	if (online && htc_batt_info.rep.charging_source == CHARGER_AC) {
		mutex_unlock(&htc_batt_info.lock);
		htc_cable_status_update(CHARGER_USB);
		mutex_lock(&htc_batt_info.lock);
	} else if (online) {
		//BATT
		printk("warning: usb connected but charging source=%d\n", htc_batt_info.rep.charging_source);
	}
	mutex_unlock(&htc_batt_info.lock);
}

struct htc_batt_info_u16 {
	volatile u16 batt_id;
	volatile u16 batt_temp;
	volatile u16 batt_vol;
	volatile s16 batt_charge;
	volatile u16 batt_discharge;
};

/* Common routine to retrieve temperature from lookup table */
static int htc_battery_temperature_lut( int av_index )
{
	// everything below 0 is HOT
	if ( av_index < 0 )
		av_index = 0;

	// max size of the table, everything higher than 1347 would
	// cause the battery to freeze in a instance.
	if ( av_index > 2600 )
		av_index = 2600;

	return temp_table[ av_index ];
}

//used for last charge_status
#define CHARGE_STATUS_AGESIZE 6
static int charge_status_age[CHARGE_STATUS_AGESIZE];
static int old_level = 100;
static int charge_curr_ref = 0;
static long current_loaded_mAs = 0;
static int max_curr = 0;
static int stop_charge_counter = 0;
static int old_current = 0;
static unsigned int old_time = 0;
//to be removed when debug finished
unsigned long mcurr_jiffies = 0;
unsigned long mold_jiffies = 0;
#define BATT_CAPACITY_PHOTON 1200

/* Common routine to compute the battery level */
static void htc_battery_level_compute( struct battery_info_reply *buffer )
{
	int result = 0;
	int i, volt, ccurrent, volt_discharge_resistor, corrected_volt;
	int temp, temp_correct_volt = 0;	
	
	temp =  buffer->batt_temp;
	volt = buffer->batt_vol;	
	ccurrent = buffer->batt_current;	
	
//r0bin: do we really need this? our algo is different
#ifdef USE_AGING_ALGORITHM
	/* aging, not to calc with first values after charging status will be changed */
	for ( ( i = CHARGE_STATUS_AGESIZE - 1); i > 0; i--) {
		charge_status_age[i] = charge_status_age[(i - 1)];
	}
	charge_status_age[0] = buffer->charging_enabled+1;// 0 will be used on empty values / 1 = batt / 2 = charging
	for (i=1; i < CHARGE_STATUS_AGESIZE; i++) {
		if ( charge_status_age[i] < 1 )
			charge_status_age[i] = charge_status_age[0];
		if ( charge_status_age[0] != charge_status_age[i] ) {
			if ( debug_mask&DEBUG_LOG )
				BATT("Charger status changed: Charge_New=%d; Charge_Old[%d/%d]=%d\n",
				charge_status_age[0], (i + 1), (CHARGE_STATUS_AGESIZE - 1), charge_status_age[i] );
			buffer->level = old_level;
			return;
		}
	}
#endif
	BATTLOG("htc_battery_level_compute called\n");
	//Algorithm for discharge
	if ( on_battery )
	{
		charge_curr_ref = 0;
		//discharge resistor correction
		volt_discharge_resistor = ( abs( ccurrent ) * batt_param->volt_discharge_res_coeff ) / 100;
		corrected_volt = volt + volt_discharge_resistor;
		
		//low temperature correction
		if(temp > 250)
			temp_correct_volt = 0;
		else
			temp_correct_volt = -( temp_correct_volt + ( ( batt_param->temp_correction_const * ( ( 250 - temp ) * abs( ccurrent ) ) ) / 10000 ) );

		//compute battery level
		if ( (corrected_volt - temp_correct_volt ) >= batt_param->full_volt_threshold ) {
			result = 100;
		} else if ( ( corrected_volt - temp_correct_volt ) >= batt_param->max_volt_threshold ) {
			result = ( ( ( ( corrected_volt - temp_correct_volt - batt_param->max_volt_threshold ) * 10 ) / batt_param->max_volt_dynslope ) + ( batt_param->max_volt_perc_start / 10 ) );
		} else if ( ( corrected_volt - temp_correct_volt ) >= batt_param->med_volt_threshold ) {
			result = ( ( ( ( corrected_volt - temp_correct_volt - batt_param->med_volt_threshold ) * 10 ) / batt_param->med_volt_dynslope ) + ( batt_param->med_volt_perc_start / 10 ) );
		} else if ( ( corrected_volt - temp_correct_volt ) >= batt_param->mid_volt_threshold ) {
			result = ( ( ( ( corrected_volt - temp_correct_volt - batt_param->mid_volt_threshold ) * 10 ) / batt_param->mid_volt_dynslope ) + ( batt_param->mid_volt_perc_start / 10 ) );
		} else if ( ( corrected_volt - temp_correct_volt ) >= batt_param->min_volt_threshold ) {
			result = ( ( ( ( corrected_volt - temp_correct_volt - batt_param->min_volt_threshold ) * 10 ) / batt_param->min_volt_dynslope ) + ( batt_param->min_volt_perc_start / 10 ) );
		} else if ( ( corrected_volt - temp_correct_volt ) >= batt_param->low_volt_threshold ) {
			result = ( ( ( ( corrected_volt - temp_correct_volt - batt_param->low_volt_threshold ) * 10 ) / batt_param->low_volt_dynslope ) + ( batt_param->low_volt_perc_start / 10 ) );
		} else if ( ( corrected_volt - temp_correct_volt ) >= batt_param->cri_volt_threshold ) {
			result = ( ( ( ( corrected_volt - temp_correct_volt - batt_param->cri_volt_threshold ) * 10 ) / batt_param->cri_volt_dynslope ) + ( batt_param->cri_volt_perc_start / 10 ) );
		} else {
			result = 0;
		}
		printk("%s: discharging, raw level=%d\n",__func__,result);
	}
	//Algorithm for charge
	else
	{
		//first time: take last percentage
		if(charge_curr_ref == 0)
		{
			charge_curr_ref = (old_level*(BATT_CAPACITY_PHOTON-100))/100;
			current_loaded_mAs=0;
			max_curr = 0;
			stop_charge_counter = 0;
			printk("%s: first time charging, old_level=%d, curr_ref=%d\n",__func__,old_level,charge_curr_ref);
		}else{
			mcurr_jiffies = jiffies;
			//increment charge current (convert to mAh)
			unsigned int udelta_msec= ((jiffies_to_msecs(mcurr_jiffies)/*/10*/)-old_time);
			//if time delta > 20sec, dont compute there is an error!
			if(udelta_msec < 20000)
			{
				int delta_msec= (int)udelta_msec;
				int delta_mAs = ((old_current * delta_msec)/1000);
				current_loaded_mAs += delta_mAs;
				printk("%s udelta_msec=%u delta_msec=%d old_current=%d delta_mAs=%d\n",
						__func__,udelta_msec,delta_msec,old_current,delta_mAs);
			}else{
				printk("%s udelta_msec=%u: jiffies corruption\n",__func__,udelta_msec);
			}
		}
		old_current = ccurrent;
		mold_jiffies = jiffies;
		old_time = jiffies_to_msecs(mold_jiffies)/*/10*/; 
		
		if(ccurrent > max_curr)
			max_curr = ccurrent;
			
		//compute percentage VS total battery capacity
		result = ((charge_curr_ref + (current_loaded_mAs/3600))*100)/(BATT_CAPACITY_PHOTON-100);
		printk("%s: charging, raw level=%d, ccurrent=%d, charge_curr_ref=%d, current_loaded_mAh=%ld, TOTAL charged=%ld\n",__func__,result,ccurrent,charge_curr_ref,(current_loaded_mAs/3600),(charge_curr_ref + (current_loaded_mAs/3600)));
		
		//if we compute it wrong, at least don't charge too much! (don't go below 100mA)
		if((ccurrent < 100) && (max_curr > 100))
			stop_charge_counter++;
		else
			stop_charge_counter = 0;
		//10 samples below 100mAh: time to stop charging!
		if(stop_charge_counter >= 10)
		{
			result = 100;
			printk("%s: charging, emergency stop, batt is full!\n",__func__);
		}
	}

	//avoid out of bound values
	if (result > 99) {
		buffer->level = 100;
	} else if (result < 0) {
		buffer->level = 0;
	//avoid variations more than 2% per sample
	} else if ( ( result > (old_level + 2) ) && (result < 98) ) {
		buffer->level = old_level + 2;
	} else if ( result < (old_level - 2) ) {
		buffer->level = old_level - 2;
	} else {
		buffer->level = result;
	}
                
//#if HTC_BATTERY_BATTLOGGER
//	if(debug_mask&DEBUG_LOG)
//		BATTLOG("STAT; level=;%d; level_old=;%d; level-calc=;%d; volt=;%d; temp=;%d; current=;%d; Charge=;%d; corr_volt=;%d; corr_temp_volt=;%d; discharge_volt_resist=;%d;\n", \
//		buffer->level, old_level, result, buffer->batt_vol, buffer->batt_temp, buffer->batt_current, htc_batt_info.rep.charging_source, corrected_volt, temp_correct_volt, volt_discharge_resistor );
//#endif

	old_level = buffer->level;
}

static void fix_batt_values(struct battery_info_reply *buffer) {                   

	/*if there are wrong values */                                              
	if ( buffer->batt_vol > 4250 )
		buffer->batt_vol = 4250;                                            
	if ( buffer->batt_vol < 2600 )
		buffer->batt_vol = 2600;
	if ( buffer->batt_current > 500 )
		buffer->batt_current = 500;                                        
	if ( buffer->batt_current < -500 )
		buffer->batt_current = -500;                                                     
	if ( buffer->batt_tempRAW > 500 )
		buffer->batt_temp = 500;                                        
	else if ( buffer->batt_tempRAW < 0 )
		buffer->batt_temp = 0;
	else
		buffer->batt_temp = buffer->batt_tempRAW;
}  

void printBattBuff(struct battery_info_reply *buffer,char *txt)
{
	printk( "r0bin %s: batt_id=%d;volt=%d;tempRaw=%dC;temp=%dC;current=%d;discharge=%d;LEVEL=%d;charging src=%d;charging?%d;adc_range=%d\n",
			txt,buffer->batt_id,buffer->batt_vol,buffer->batt_tempRAW,buffer->batt_temp,
			buffer->batt_current,buffer->batt_discharge,buffer->level,buffer->charging_source,buffer->charging_enabled,htc_adc_range);
		}

/* Photon battery data corrections */
/* r0bin: algorithms by pwel and munjeni */
static int htc_photon_batt_corr( struct battery_info_reply *buffer )
{
	int av_index;

	/* battery voltage, pwel's algorithm */
	buffer->batt_vol = (15871*buffer->batt_vol)/(batt_vref*10);  //( ( buffer->batt_vol * 5200 ) / htc_adc_range );  
	
	/* convert readed value to mA, pwel's algorithm */
	buffer->batt_current = (237* ( buffer->batt_current - ((3025*buffer->batt_discharge)/1000)))/1000;

	/* cardsharing algo on temp */
	av_index = ( buffer->batt_tempRAW );
	buffer->batt_tempRAW = htc_battery_temperature_lut( av_index );

	return 0;
}


static int htc_get_batt_smem_info(struct battery_info_reply *buffer)
{
	volatile struct htc_batt_info_u16 *batt_16 = NULL;
	struct msm_dex_command dex;
	
	//send DEX to update smem values
	dex.cmd = PCOM_GET_BATTERY_DATA;
	msm_proc_comm_wince(&dex, 0);
	mutex_lock(&htc_batt_info.lock);

	//now read latest values
	batt_16 = (void *)(MSM_SHARED_RAM_BASE + htc_batt_info.resources->smem_offset);
	buffer->batt_vol = batt_16->batt_vol;
	buffer->batt_current = batt_16->batt_charge;
	buffer->batt_tempRAW = batt_16->batt_temp;
	buffer->batt_id = batt_16->batt_id;
	buffer->batt_discharge = batt_16->batt_discharge;
	printBattBuff(buffer,"RAW VALUES");
	
	return 0;
}

/* values we got are not stable enough: apply diamond/blackstone correction
*  reads 5 times ADC level, removes the highest and lowest value and use the average value
*/
static int htc_get_batt_smem_info_5times(struct battery_info_reply *buffer)
{
	int	i = 0;
	int volt_lowest_val = 0xFFFF, volt_highest_val = 0, current_lowest_val = 0xFFFF, current_highest_val = 0;
	int volt_sum = 0, current_sum = 0;
	
	do
	{
		htc_get_batt_smem_info(buffer);
		/* Saves the lowest and highest value of batt_vol and current */
		if(buffer->batt_vol < volt_lowest_val) {
			volt_lowest_val = buffer->batt_vol;
		}

		if(buffer->batt_vol > volt_highest_val) {
			volt_highest_val = buffer->batt_vol;
		}

		volt_sum += buffer->batt_vol;

		if(buffer->batt_current < current_lowest_val) {
			current_lowest_val = buffer->batt_current;
		}

		if(buffer->batt_current > current_highest_val) {
			current_highest_val = buffer->batt_current;
		}

		current_sum += buffer->batt_current;
		i++;
		mdelay(2);
		mutex_unlock(&htc_batt_info.lock);
	}
	while(i < 5);

	mutex_lock(&htc_batt_info.lock);

	/* Remove the highest and lowest value */
	volt_sum = volt_sum - volt_lowest_val - volt_highest_val;
	current_sum = current_sum - current_lowest_val - current_highest_val;
	buffer->batt_vol = volt_sum / 3;
	buffer->batt_current = current_sum / 3;
	
	printk("%s, final vals: volt=%d curr=%d\n",__func__,buffer->batt_vol,buffer->batt_current);
	return 0;
}

//usage: backup batteryinfo data
void memcpyBattInfo(struct battery_info_reply *source, struct battery_info_reply *dest)
{
	dest->batt_id = source->batt_id;
	dest->batt_vol = source->batt_vol;
	dest->batt_temp = source->batt_temp;
	dest->batt_current = source->batt_current;
	dest->batt_discharge = source->batt_discharge;
	dest->level = source->level;
	dest->charging_source = source->charging_source;
	dest->charging_enabled = source->charging_enabled;
	dest->full_bat = source->full_bat;
	dest->batt_tempRAW = source->batt_tempRAW;
}

static int htc_get_batt_info(struct battery_info_reply *buffer)
{
    int last_source, new_source;
	unsigned int time_now;
	
	// sanity checks
	if ( buffer == NULL )
		return -EINVAL;
	if ( !htc_batt_info.resources || !htc_batt_info.resources->smem_offset ) {
		BATT_ERR("smem_offset not set\n" );
		return -EINVAL;
	}
    last_source = htc_batt_info.rep.charging_source;
	BATTLOG("htc_get_batt_info called\n");

	//don't stress our driver! minimum interval = 1sec
	time_now = jiffies_to_msecs(jiffies);
	printk("%s: diff=%u ms, time_now=%u ms, time_stamp=%u ms, jiffies=%lu\n",__func__,(time_now - time_stamp),time_now, time_stamp,jiffies);
	if(time_stamp && ((time_now - time_stamp)<1000))
	{
		//if yes, no need to compute again: just copy previous computed values and exit!
		memcpyBattInfo(&old_batt_info,buffer);
		printk("%s: don't stress DEX, time diff=%u\n",__func__,(time_now - time_stamp));
		return 0;
	}

	//read raw SMEM values 
	//htc_get_batt_smem_info(buffer);
	htc_get_batt_smem_info_5times(buffer);

	time_stamp = jiffies_to_msecs(jiffies);
/*
	//check if values are similar to previous poll 
	if( (buffer->batt_vol == old_batt_info.batt_vol) && 
		(buffer->batt_tempRAW == old_batt_info.batt_tempRAW) &&
		(buffer->batt_current == old_batt_info.batt_current) &&
		(buffer->batt_discharge == old_batt_info.batt_discharge) &&
		(buffer->batt_id == old_batt_info.batt_id)){
		//if yes, no need to compute again: just copy previous computed values and exit!
		memcpyBattInfo(&old_batt_info,buffer);
		printk("%s: smem values identical, no need to compute again, level=%d\n",__func__,buffer->level);
		return 0;
	}
*/
	//if not, START computing!
	//1st, calculate the avarage raw voltage, in order to avoid strong voltage variationst, 
	maf_add_value( buffer->batt_vol );
	buffer->batt_vol = maf_get_avarage();
	
	//check if charger is enabled
	if (gpio_get_value(htc_batt_info.resources->gpio_charger_enable) == 0) {
		buffer->charging_enabled = 1;
		buffer->charging_source = CHARGER_USB;
	} else {
		buffer->charging_enabled = 0;
		buffer->charging_source = CHARGER_BATTERY;
	}

	if(debug_mask&DEBUG_LOG)
		BATT("CHARGER_enabled=%d; CHARGER_source=%d\n", buffer->charging_enabled, buffer->charging_source);
	/* should it be done before correction */
	new_source = buffer->charging_source;
	buffer->charging_source = last_source;
	mutex_unlock(&htc_batt_info.lock);
	htc_cable_status_update(new_source);
	mutex_lock(&htc_batt_info.lock);

	/* get real volt, temp and current values */
	htc_photon_batt_corr( buffer );
	printBattBuff(buffer,"DECODED VALUES");
	/* fix values in case they are out of bounds */
	fix_batt_values(buffer);
	/* compute battery level based on battery values */
	htc_battery_level_compute( buffer );
	printBattBuff(buffer,"COMPUTATION FINISHED");
	
	/* backup values for next time */
	memcpyBattInfo(buffer, &old_batt_info);
	
	mutex_unlock(&htc_batt_info.lock);
	return 0;
}

/* -------------------------------------------------------------------------- */
static int htc_power_get_property(struct power_supply *psy,
				    enum power_supply_property psp,
				    union power_supply_propval *val)
{
	charger_type_t charger;

	mutex_lock(&htc_batt_info.lock);
	charger = htc_batt_info.rep.charging_source;
	mutex_unlock(&htc_batt_info.lock);

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		if (psy->type == POWER_SUPPLY_TYPE_MAINS)
			val->intval = (charger ==  CHARGER_AC ? 1 : 0);
		else if (psy->type == POWER_SUPPLY_TYPE_USB)
			val->intval = (charger ==  CHARGER_USB ? 1 : 0);
		else
			val->intval = 0;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int htc_battery_get_charging_status(void)
{
	u32 level;
	charger_type_t charger;
	int ret;

	mutex_lock(&htc_batt_info.lock);
	charger = htc_batt_info.rep.charging_source;

	switch (charger) {
	case CHARGER_BATTERY:
		ret = POWER_SUPPLY_STATUS_NOT_CHARGING;
		break;
	case CHARGER_USB:
	case CHARGER_AC:
		level = htc_batt_info.rep.level;
		if (level == 100)
			ret = POWER_SUPPLY_STATUS_FULL;
		else
			ret = POWER_SUPPLY_STATUS_CHARGING;
		break;
	default:
		ret = POWER_SUPPLY_STATUS_UNKNOWN;
	}
	mutex_unlock(&htc_batt_info.lock);
	return ret;
}

static int htc_battery_get_property(struct power_supply *psy,
				    enum power_supply_property psp,
				    union power_supply_propval *val)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = htc_battery_get_charging_status();
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		val->intval = POWER_SUPPLY_HEALTH_GOOD;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = htc_batt_info.present;
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		mutex_lock(&htc_batt_info.lock);
		val->intval = htc_batt_info.rep.level;
		mutex_unlock(&htc_batt_info.lock);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

void htc_battery_external_power_changed(struct power_supply *psy) {
	if(debug_mask&DEBUG_LOG)
		BATT("external power changed\n");
	maf_clear();
	return;
}

#define HTC_BATTERY_ATTR(_name)							\
{										\
	.attr = { .name = #_name, .mode = S_IRUGO, .owner = THIS_MODULE },	\
	.show = htc_battery_show_property,					\
	.store = NULL,								\
}

static struct device_attribute htc_battery_attrs[] = {
	HTC_BATTERY_ATTR(batt_id),
	HTC_BATTERY_ATTR(batt_vol),
	HTC_BATTERY_ATTR(batt_temp),
	HTC_BATTERY_ATTR(batt_current),
	HTC_BATTERY_ATTR(charging_source),
	HTC_BATTERY_ATTR(charging_enabled),
	HTC_BATTERY_ATTR(full_bat),
};

enum {
	BATT_ID = 0,
	BATT_VOL,
	BATT_TEMP,
	BATT_CURRENT,
	CHARGING_SOURCE,
	CHARGING_ENABLED,
	FULL_BAT,
};

static int htc_battery_create_attrs(struct device *dev)
{
	int i, rc;

	for (i = 0; i < ARRAY_SIZE(htc_battery_attrs); i++) {
		rc = device_create_file(dev, &htc_battery_attrs[i]);
		if (rc)
			goto htc_attrs_failed;
	}

	goto succeed;

htc_attrs_failed:
	while (i--)
		device_remove_file(dev, &htc_battery_attrs[i]);
succeed:
	return rc;
}

static ssize_t htc_battery_show_property(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	int i = 0;
	const ptrdiff_t off = attr - htc_battery_attrs;

	/* check cache time to decide if we need to update */
	if (htc_batt_info.update_time &&
            time_before(jiffies, htc_batt_info.update_time +
                                msecs_to_jiffies(cache_time)))
                goto dont_need_update;

	if (htc_get_batt_info(&htc_batt_info.rep) < 0) {
		BATT_ERR("%s: get_batt_info failed!!!\n", __FUNCTION__);
	} else {
		htc_batt_info.update_time = jiffies;
	}
dont_need_update:

	mutex_lock(&htc_batt_info.lock);
	switch (off) {
	case BATT_ID:
		i += scnprintf(buf + i, PAGE_SIZE - i, "%d\n",
			       htc_batt_info.rep.batt_id);
		break;
	case BATT_VOL:
		i += scnprintf(buf + i, PAGE_SIZE - i, "%d\n",
			       htc_batt_info.rep.batt_vol);
		break;
	case BATT_TEMP:
		i += scnprintf(buf + i, PAGE_SIZE - i, "%d\n",
			       htc_batt_info.rep.batt_temp);
		break;
	case BATT_CURRENT:
		i += scnprintf(buf + i, PAGE_SIZE - i, "%d\n",
			       htc_batt_info.rep.batt_current);
		break;
	case CHARGING_SOURCE:
		i += scnprintf(buf + i, PAGE_SIZE - i, "%d\n",
			       htc_batt_info.rep.charging_source);
		break;
	case CHARGING_ENABLED:
		i += scnprintf(buf + i, PAGE_SIZE - i, "%d\n",
			       htc_batt_info.rep.charging_enabled);
		break;
	case FULL_BAT:
		i += scnprintf(buf + i, PAGE_SIZE - i, "%d\n",
			       htc_batt_info.rep.full_bat);
		break;
	default:
		i = -EINVAL;
	}
	mutex_unlock(&htc_batt_info.lock);

	return i;
}

static int htc_battery_thread(void *data)
{
	struct battery_info_reply buffer;
	daemonize("battery");
	allow_signal(SIGKILL);

	while (!signal_pending((struct task_struct *)current)) {
		msleep(10000);
		if (!bat_suspended && !htc_get_batt_info(&buffer)) {
			htc_batt_info.update_time = jiffies;
			htc_battery_status_update(buffer.level);
		}
	}
	return 0;
}
static int htc_battery_probe(struct platform_device *pdev)
{
	int i, rc;
	htc_batt_info.resources = (smem_batt_t *)pdev->dev.platform_data;

	if (!htc_batt_info.resources) {
		BATT_ERR("%s: no pdata resources!\n", __FUNCTION__);
		return -EINVAL;
	}

	/* init battery gpio */
	if ((rc = init_batt_gpio()) < 0) {
		BATT_ERR("%s: init battery gpio failed!\n", __FUNCTION__);
		return rc;
	}
		
	/* init structure data member */
	htc_batt_info.update_time 	= jiffies;
	//r0bin: A9 will shutdown the phone if battery is pluged out, so this value is always 1
	htc_batt_info.present 		= 1; //gpio_get_value(htc_batt_info.resources->gpio_battery_detect);
	/* init power supplier framework */
	for (i = 0; i < ARRAY_SIZE(htc_power_supplies); i++) {
		rc = power_supply_register(&pdev->dev, &htc_power_supplies[i]);
		if (rc)
			BATT_ERR("Failed to register power supply (%d)\n", rc);
	}

	/* create htc detail attributes */
	htc_battery_create_attrs(htc_power_supplies[CHARGER_BATTERY].dev);

	/* init static battery settings */
	if ( init_battery_settings( &htc_batt_info.rep ) < 0)
		BATT_ERR("%s: init battery settings failed\n", __FUNCTION__);

	htc_battery_initial = 1;

	htc_batt_info.update_time = jiffies;
	kernel_thread(htc_battery_thread, NULL, CLONE_KERNEL);

	not_yet_started = false;
	return 0;
}

#if CONFIG_PM
static int htc_battery_suspend(struct platform_device* device, pm_message_t mesg)
{
	bat_suspended = 1;
	return 0;
}

static int htc_battery_resume(struct platform_device* device)
{
	bat_suspended = 0;
	return 0; 
}
#else
 #define htc_battery_suspend NULL
 #define htc_battery_resume NULL
#endif

static struct platform_driver htc_battery_driver = {
	.probe	= htc_battery_probe,
	.driver	= {
		.name	= MODULE_NAME,
		.owner	= THIS_MODULE,
	},
	.suspend = htc_battery_suspend,
	.resume = htc_battery_resume,
};

static int __init htc_battery_init(void)
{
	// this used to be WAKE_LOCK_SUSPEND, but make it an idle lock in order to
	// prevent msm_sleep() try to collapse arm11 (using idle_sleep mode) several
	// times a second which sooner or later get's the device to freeze when usb
	// is connected
	wake_lock_init(&vbus_wake_lock, WAKE_LOCK_IDLE, "vbus_present");
	mutex_init(&htc_batt_info.lock);
	platform_driver_register(&htc_battery_driver);
	BATT("HTC Battery Driver initialized\n");

	return 0;
}

late_initcall(htc_battery_init);
MODULE_DESCRIPTION("HTC Battery Driver");
MODULE_LICENSE("GPL");
