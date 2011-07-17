#ifndef _HTC_BATTERY_SMEM_DEF_H_
#define _HTC_BATTERY_SMEM_DEF_H_

struct smem_battery_resources {
	unsigned short gpio_battery_detect;
	unsigned short gpio_charger_enable;
	unsigned short gpio_charger_current_select;
	unsigned short gpio_ac_detect;
	unsigned short gpio_charger_fast_dis;
	unsigned short gpio_charger_fast_en;
	unsigned smem_offset;
	unsigned short smem_field_size;
};

typedef struct smem_battery_resources smem_batt_t;

typedef enum {
	DISABLE = 0,
	ENABLE_SLOW_CHG,
	ENABLE_FAST_CHG
} batt_ctl_t;

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

#endif