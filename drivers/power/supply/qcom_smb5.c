// SPDX-License-Identifier: GPL-2.0-only
/*
 * Qualcomm PM6150 SMB5 charger driver
 *
 * This driver intentionally owns only the sink-charging pieces of SMB5.
 * Type-C/CC/PD is handled by qcom_pmic_typec and source VBUS/OTG is handled
 * by qcom_usb_vbus-regulator.
 */

#include <linux/devm-helpers.h>
#include <linux/iio/consumer.h>
#include <linux/iio/adc/qcom-vadc-common.h>
#include <linux/interrupt.h>
#include <linux/minmax.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_wakeirq.h>
#include <linux/power_supply.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/thermal.h>
#include <linux/timekeeping.h>
#include <linux/workqueue.h>

/* CHGR peripheral, relative to the charger base. */
#define SMB5_CHGR_STATUS_1			0x006
#define SMB5_CHGR_STATUS_MASK			GENMASK(2, 0)
#define SMB5_CHGR_STATUS_2			0x007
#define SMB5_CHGR_BAT_OV_BIT			BIT(1)
#define SMB5_CHGR_STATUS_7			0x00d
#define SMB5_CHGR_TEMP_HOT_SOFT_BIT		BIT(5)
#define SMB5_CHGR_TEMP_COLD_SOFT_BIT		BIT(4)
#define SMB5_CHGR_TEMP_TOO_HOT_BIT		BIT(3)
#define SMB5_CHGR_TEMP_TOO_COLD_BIT		BIT(2)
#define SMB5_CHGR_ENABLE_CMD			0x042
#define SMB5_CHGR_ENABLE_BIT			BIT(0)
#define SMB5_CHGR_CFG2				0x051
#define SMB5_CHGR_RECHG_MASK			GENMASK(2, 1)
#define SMB5_CHGR_VBAT_RECHG_BIT		BIT(2)
#define SMB5_CHGR_INHIBIT_BIT			BIT(0)
#define SMB5_CHGR_FCC_CFG			0x061
#define SMB5_CHGR_ITERM_UP_MSB			0x067
#define SMB5_CHGR_RECHG_SAMPLE_CFG		0x06b
#define SMB5_CHGR_RECHG_SAMPLE_MASK		GENMASK(3, 2)
#define SMB5_CHGR_RECHG_SAMPLE_3		BIT(3)
#define SMB5_CHGR_ADC_TERM_CFG			0x06c
#define SMB5_CHGR_ADC_TERM_SAMPLE_COUNT_BIT	BIT(0)
#define SMB5_CHGR_FV_CFG			0x070
#define SMB5_CHGR_RECHG_THD_MSB			0x07e
#define SMB5_CHGR_JEITA_ENABLE			0x090
#define SMB5_CHGR_JEITA_HARD_LIMIT_BIT		BIT(4)
#define SMB5_CHGR_JEITA_HARD_HOT_MSB		0x098
#define SMB5_CHGR_JEITA_HARD_COLD_MSB		0x09a
#define SMB5_CHGR_SAFETY_TIMER_CFG		0x0a2
#define SMB5_CHGR_SAFETY_TIMER_1536_MIN	0x03

/* BATIF peripheral at 0x1200. */
#define SMB5_BATIF_OFFSET			0x200
#define SMB5_BATIF_INT_RT_STS			(SMB5_BATIF_OFFSET + 0x10)
#define SMB5_BATIF_TERMINAL_MISSING_BIT		BIT(5)
#define SMB5_BATIF_THERM_OR_ID_MISSING_BIT	BIT(4)
#define SMB5_BATIF_OV_BIT			BIT(2)

/* USBIN peripheral at 0x1300. */
#define SMB5_USBIN_OFFSET			0x300
#define SMB5_USBIN_APSD_STATUS			(SMB5_USBIN_OFFSET + 0x07)
#define SMB5_USBIN_APSD_DONE_BIT		BIT(0)
#define SMB5_USBIN_APSD_RESULT			(SMB5_USBIN_OFFSET + 0x08)
#define SMB5_USBIN_APSD_RESULT_MASK		GENMASK(6, 0)
#define SMB5_USBIN_FLOAT_BIT			BIT(4)
#define SMB5_USBIN_DCP_BIT			BIT(3)
#define SMB5_USBIN_CDP_BIT			BIT(2)
#define SMB5_USBIN_OCP_BIT			BIT(1)
#define SMB5_USBIN_SDP_BIT			BIT(0)
#define SMB5_USBIN_INT_RT_STS			(SMB5_USBIN_OFFSET + 0x10)
#define SMB5_USBIN_PLUGIN_BIT			BIT(4)
#define SMB5_USBIN_OV_BIT			BIT(3)
#define SMB5_USBIN_VASHDN_BIT			BIT(1)
#define SMB5_USBIN_SUSPEND_CMD			(SMB5_USBIN_OFFSET + 0x40)
#define SMB5_USBIN_SUSPEND_BIT			BIT(0)
#define SMB5_USBIN_APSD_CMD			(SMB5_USBIN_OFFSET + 0x41)
#define SMB5_USBIN_APSD_RERUN_BIT		BIT(0)
#define SMB5_USBIN_ICL_OVERRIDE_CMD		(SMB5_USBIN_OFFSET + 0x42)
#define SMB5_USBIN_ICL_OVERRIDE_BIT		BIT(0)
#define SMB5_USBIN_ADAPTER_ALLOW_CFG		(SMB5_USBIN_OFFSET + 0x60)
#define SMB5_USBIN_ADAPTER_ALLOW_5V		0
#define SMB5_USBIN_OPTIONS_1_CFG		(SMB5_USBIN_OFFSET + 0x62)
#define SMB5_USBIN_HVDCP_AUTH_BIT		BIT(6)
#define SMB5_USBIN_HVDCP_AUTO_BIT		BIT(5)
#define SMB5_USBIN_BC12_DETECT_BIT		BIT(3)
#define SMB5_USBIN_HVDCP_ENABLE_BIT		BIT(2)
#define SMB5_USBIN_LOAD_CFG			(SMB5_USBIN_OFFSET + 0x65)
#define SMB5_USBIN_ICL_OVERRIDE_AFTER_APSD_BIT	BIT(4)
#define SMB5_USBIN_ICL_OPTIONS			(SMB5_USBIN_OFFSET + 0x66)
#define SMB5_USBIN_USB3_MODE_BIT		BIT(2)
#define SMB5_USBIN_USB51_MODE_BIT		BIT(1)
#define SMB5_USBIN_MODE_CHG_BIT			BIT(0)
#define SMB5_USBIN_ICL_CFG			(SMB5_USBIN_OFFSET + 0x70)
#define SMB5_USBIN_AICL_OPTIONS			(SMB5_USBIN_OFFSET + 0x80)
#define SMB5_USBIN_AICL_SUSPEND_COLLAPSE_BIT	BIT(7)
#define SMB5_USBIN_AICL_PERIODIC_BIT		BIT(4)
#define SMB5_USBIN_AICL_ADC_BIT			BIT(3)
#define SMB5_USBIN_AICL_ENABLE_BIT		BIT(2)

/* MISC peripheral at 0x1600. */
#define SMB5_MISC_OFFSET			0x600
#define SMB5_MISC_WDOG_PET			(SMB5_MISC_OFFSET + 0x43)
#define SMB5_MISC_WDOG_PET_BIT			BIT(0)
#define SMB5_MISC_SMB_EN_CMD			(SMB5_MISC_OFFSET + 0x48)
#define SMB5_MISC_SMB_EN_OVERRIDE_VALUE_BIT	BIT(4)
#define SMB5_MISC_SMB_EN_OVERRIDE_BIT		BIT(3)
#define SMB5_MISC_AICL_CMD			(SMB5_MISC_OFFSET + 0x44)
#define SMB5_MISC_AICL_RERUN_BIT		BIT(0)
#define SMB5_MISC_WD_CFG			(SMB5_MISC_OFFSET + 0x51)
#define SMB5_MISC_WD_AFP_ENABLE_BIT		BIT(7)
#define SMB5_MISC_WD_BARK_IRQ_BIT		BIT(6)
#define SMB5_MISC_WD_BITE_IRQ_BIT		BIT(5)
#define SMB5_MISC_WD_TIMER_PLUGIN_BIT		BIT(1)
#define SMB5_MISC_WD_TIMEOUT_CFG		(SMB5_MISC_OFFSET + 0x53)
#define SMB5_MISC_WD_BITE_DISABLE_CHG_BIT	BIT(7)
#define SMB5_MISC_WD_BARK_TIMEOUT_MASK		GENMASK(3, 2)
#define SMB5_MISC_WD_BARK_64S			BIT(3)
#define SMB5_MISC_WD_BITE_TIMEOUT_MASK		GENMASK(1, 0)
#define SMB5_MISC_WD_BITE_8S			GENMASK(1, 0)
#define SMB5_MISC_AICL_RERUN_TIME_CFG		(SMB5_MISC_OFFSET + 0x61)
#define SMB5_MISC_AICL_RERUN_12S		0x01

#define SMB5_CURRENT_STEP_UA			50000
#define SMB5_ADC_VOLTAGE_NUMERATOR		194637
#define SMB5_FV_MIN_UV				3600000
#define SMB5_FV_MAX_UV				4790000
#define SMB5_FV_STEP_UV			10000
#define SMB5_SAFE_INPUT_DEFAULT_UA		100000
#define SMB5_SDP_CURRENT_UA			100000
#define SMB5_USB51_500_CURRENT_UA		500000
#define SMB5_CDP_CURRENT_UA			1500000
#define SMB5_DCP_CURRENT_UA			1500000
#define SMB5_REEVALUATE_MS			15000
#define SMB5_BATT_ID_PULLUP_OHM			100000
#define SMB5_BATT_ID_VREF_UV			1875000
#define SMB5_HARD_JEITA_GUARD_MC		1000
#define SMB5_THERMAL_FCC_UA			500000
#define SMB5_CHARGE_TIMEOUT_SEC			(182 * 60)
#define SMB5_RECHARGE_TIMEOUT_SEC		(90 * 60)

enum smb5_cooling_state {
	SMB5_COOLING_NORMAL,
	SMB5_COOLING_THROTTLE,
	SMB5_COOLING_DISABLE,
};

enum smb5_charge_state {
	SMB5_INHIBIT_CHARGE = 0,
	SMB5_TRICKLE_CHARGE,
	SMB5_PRE_CHARGE,
	SMB5_FULLON_CHARGE,
	SMB5_TAPER_CHARGE,
	SMB5_TERMINATE_CHARGE,
	SMB5_PAUSE_CHARGE,
	SMB5_DISABLE_CHARGE,
};

struct qcom_smb5 {
	struct device *dev;
	struct regmap *regmap;
	struct power_supply *psy;
	struct power_supply_battery_info *batt_info;
	struct iio_channel *usbin_v;
	struct iio_channel *batt_therm;
	struct iio_channel *batt_id;
	struct delayed_work state_work;
	unsigned int base;
	unsigned int input_limit_ua;
	unsigned int input_limit_max_ua;
	unsigned int charge_current_ua;
	unsigned int charge_voltage_uv;
	int cable_irq;
	int hard_temp_hysteresis_c;
	unsigned long thermal_state;
	u64 charge_started_sec;
	unsigned int charge_timeout_sec;
	bool recharge_cycle;
	bool safety_expired;
	bool temperature_blocked;
	bool charging_enabled;
	bool suspended;
};

static int smb5_update_bits(struct qcom_smb5 *chip, unsigned int reg,
			    unsigned int mask, unsigned int val)
{
	return regmap_update_bits(chip->regmap, chip->base + reg, mask, val);
}

static int smb5_read(struct qcom_smb5 *chip, unsigned int reg, unsigned int *val)
{
	return regmap_read(chip->regmap, chip->base + reg, val);
}

static int smb5_write(struct qcom_smb5 *chip, unsigned int reg, unsigned int val)
{
	return regmap_write(chip->regmap, chip->base + reg, val);
}

static int smb5_bulk_write_be16(struct qcom_smb5 *chip, unsigned int reg, u16 val)
{
	u8 buf[2] = { val >> 8, val & 0xff };

	return regmap_bulk_write(chip->regmap, chip->base + reg, buf, sizeof(buf));
}

static int smb5_set_input_limit(struct qcom_smb5 *chip, unsigned int ua)
{
	unsigned int raw;
	int ret;

	ua = min(ua, chip->input_limit_max_ua);

	/*
	 * USB51 mode is the downstream-defined path for 100/500 mA SDP-style
	 * limits. Higher currents use the high-current register and the software
	 * high-current override state.
	 */
	if (ua <= SMB5_USB51_500_CURRENT_UA) {
		unsigned int options = 0;

		if (ua >= SMB5_USB51_500_CURRENT_UA) {
			ua = SMB5_USB51_500_CURRENT_UA;
			options = SMB5_USBIN_USB51_MODE_BIT;
		} else {
			ua = SMB5_SAFE_INPUT_DEFAULT_UA;
		}

		ret = smb5_update_bits(chip, SMB5_USBIN_ICL_OPTIONS,
					SMB5_USBIN_USB3_MODE_BIT |
					SMB5_USBIN_USB51_MODE_BIT |
					SMB5_USBIN_MODE_CHG_BIT,
					options);
		if (ret)
			return ret;

		ret = smb5_update_bits(chip, SMB5_USBIN_ICL_OVERRIDE_CMD,
					SMB5_USBIN_ICL_OVERRIDE_BIT,
					SMB5_USBIN_ICL_OVERRIDE_BIT);
		if (ret)
			return ret;

		ret = smb5_update_bits(chip, SMB5_USBIN_LOAD_CFG,
					SMB5_USBIN_ICL_OVERRIDE_AFTER_APSD_BIT, 0);
		if (ret)
			return ret;

		ret = smb5_update_bits(chip, SMB5_MISC_AICL_CMD,
					SMB5_MISC_AICL_RERUN_BIT,
					SMB5_MISC_AICL_RERUN_BIT);
		if (ret)
			return ret;
	} else {
		ua = rounddown(ua, (unsigned int)SMB5_CURRENT_STEP_UA);
		raw = ua / SMB5_CURRENT_STEP_UA;

		ret = smb5_write(chip, SMB5_USBIN_ICL_CFG, raw);
		if (ret)
			return ret;

		ret = smb5_update_bits(chip, SMB5_USBIN_ICL_OPTIONS,
					SMB5_USBIN_MODE_CHG_BIT,
					SMB5_USBIN_MODE_CHG_BIT);
		if (ret)
			return ret;

		ret = smb5_update_bits(chip, SMB5_USBIN_ICL_OVERRIDE_CMD,
					SMB5_USBIN_ICL_OVERRIDE_BIT, 0);
		if (ret)
			return ret;

		ret = smb5_update_bits(chip, SMB5_USBIN_LOAD_CFG,
					SMB5_USBIN_ICL_OVERRIDE_AFTER_APSD_BIT,
					SMB5_USBIN_ICL_OVERRIDE_AFTER_APSD_BIT);
		if (ret)
			return ret;
	}

	chip->input_limit_ua = ua;

	return 0;
}

static int smb5_set_charge_current(struct qcom_smb5 *chip, unsigned int ua)
{
	unsigned int raw;
	int ret;

	ua = min(ua, 8000000U);
	raw = ua / SMB5_CURRENT_STEP_UA;
	ret = smb5_write(chip, SMB5_CHGR_FCC_CFG, raw);
	if (!ret)
		chip->charge_current_ua = raw * SMB5_CURRENT_STEP_UA;

	return ret;
}

static int smb5_set_charge_voltage(struct qcom_smb5 *chip, unsigned int uv)
{
	unsigned int raw;
	int ret;

	if (uv < SMB5_FV_MIN_UV || uv > SMB5_FV_MAX_UV)
		return -EINVAL;

	raw = (uv - SMB5_FV_MIN_UV) / SMB5_FV_STEP_UV;
	ret = smb5_write(chip, SMB5_CHGR_FV_CFG, raw);
	if (!ret)
		chip->charge_voltage_uv = SMB5_FV_MIN_UV + raw * SMB5_FV_STEP_UV;

	return ret;
}

static int smb5_set_charging(struct qcom_smb5 *chip, bool enable)
{
	int ret;

	ret = smb5_update_bits(chip, SMB5_CHGR_ENABLE_CMD, SMB5_CHGR_ENABLE_BIT,
			       enable ? SMB5_CHGR_ENABLE_BIT : 0);
	if (!ret)
		chip->charging_enabled = enable;

	return ret;
}

static int smb5_set_usb_suspend(struct qcom_smb5 *chip, bool suspend)
{
	return smb5_update_bits(chip, SMB5_USBIN_SUSPEND_CMD, SMB5_USBIN_SUSPEND_BIT,
				suspend ? SMB5_USBIN_SUSPEND_BIT : 0);
}

static int smb5_input_present(struct qcom_smb5 *chip, int *present)
{
	unsigned int status;
	int ret;

	ret = smb5_read(chip, SMB5_USBIN_INT_RT_STS, &status);
	if (ret)
		return ret;

	*present = !!(status & SMB5_USBIN_PLUGIN_BIT);
	return 0;
}

static int smb5_is_online(struct qcom_smb5 *chip, int *online)
{
	union power_supply_propval supplier;
	int present, ret;

	ret = smb5_input_present(chip, &present);
	if (ret)
		return ret;
	if (!present) {
		*online = 0;
		return 0;
	}

	/* TCPM reports ONLINE only while this port is sinking VBUS. */
	ret = power_supply_get_property_from_supplier(chip->psy,
						      POWER_SUPPLY_PROP_ONLINE,
						      &supplier);
	if (ret) {
		/* Failing closed avoids enabling the sink path while source/OTG is active. */
		*online = 0;
		return 0;
	}

	*online = supplier.intval > 0;
	return 0;
}

static int smb5_get_usb_type(struct qcom_smb5 *chip, int *type)
{
	union power_supply_propval supplier;
	unsigned int status, result;
	int online, ret;

	ret = smb5_is_online(chip, &online);
	if (ret)
		return ret;
	if (!online) {
		*type = POWER_SUPPLY_USB_TYPE_UNKNOWN;
		return 0;
	}

	ret = smb5_read(chip, SMB5_USBIN_APSD_STATUS, &status);
	if (ret)
		return ret;

	if (status & SMB5_USBIN_APSD_DONE_BIT) {
		ret = smb5_read(chip, SMB5_USBIN_APSD_RESULT, &result);
		if (ret)
			return ret;

		result &= SMB5_USBIN_APSD_RESULT_MASK;
		if (result & SMB5_USBIN_FLOAT_BIT) {
			/* A70 treats a floating legacy source conservatively until resolved. */
			*type = POWER_SUPPLY_USB_TYPE_UNKNOWN;
			return 0;
		} else if (result & SMB5_USBIN_CDP_BIT)
			*type = POWER_SUPPLY_USB_TYPE_CDP;
		else if (result & (SMB5_USBIN_DCP_BIT | SMB5_USBIN_OCP_BIT))
			*type = POWER_SUPPLY_USB_TYPE_DCP;
		else if (result & SMB5_USBIN_SDP_BIT)
			*type = POWER_SUPPLY_USB_TYPE_SDP;
		else
			*type = POWER_SUPPLY_USB_TYPE_UNKNOWN;

		if (*type != POWER_SUPPLY_USB_TYPE_UNKNOWN)
			return 0;
	}

	ret = power_supply_get_property_from_supplier(chip->psy,
						      POWER_SUPPLY_PROP_USB_TYPE,
						      &supplier);
	if (!ret)
		*type = supplier.intval;
	else
		*type = POWER_SUPPLY_USB_TYPE_UNKNOWN;

	return 0;
}

static int smb5_get_status(struct qcom_smb5 *chip, int *status)
{
	unsigned int state;
	int online, ret;

	ret = smb5_is_online(chip, &online);
	if (ret)
		return ret;
	if (!online) {
		*status = POWER_SUPPLY_STATUS_DISCHARGING;
		return 0;
	}

	ret = smb5_read(chip, SMB5_CHGR_STATUS_1, &state);
	if (ret)
		return ret;

	switch (state & SMB5_CHGR_STATUS_MASK) {
	case SMB5_TRICKLE_CHARGE:
	case SMB5_PRE_CHARGE:
	case SMB5_FULLON_CHARGE:
	case SMB5_TAPER_CHARGE:
		*status = POWER_SUPPLY_STATUS_CHARGING;
		break;
	case SMB5_TERMINATE_CHARGE:
	case SMB5_INHIBIT_CHARGE:
		*status = POWER_SUPPLY_STATUS_FULL;
		break;
	case SMB5_PAUSE_CHARGE:
	case SMB5_DISABLE_CHARGE:
	default:
		*status = POWER_SUPPLY_STATUS_NOT_CHARGING;
		break;
	}

	return 0;
}

static int smb5_get_health(struct qcom_smb5 *chip, int *health)
{
	unsigned int batif, stat2, stat7, usbin;
	int ret;

	ret = smb5_read(chip, SMB5_BATIF_INT_RT_STS, &batif);
	if (ret)
		return ret;
	ret = smb5_read(chip, SMB5_CHGR_STATUS_2, &stat2);
	if (ret)
		return ret;
	ret = smb5_read(chip, SMB5_CHGR_STATUS_7, &stat7);
	if (ret)
		return ret;
	ret = smb5_read(chip, SMB5_USBIN_INT_RT_STS, &usbin);
	if (ret)
		return ret;

	if ((batif & SMB5_BATIF_OV_BIT) || (stat2 & SMB5_CHGR_BAT_OV_BIT))
		*health = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
	else if (batif & (SMB5_BATIF_TERMINAL_MISSING_BIT |
			  SMB5_BATIF_THERM_OR_ID_MISSING_BIT))
		*health = POWER_SUPPLY_HEALTH_UNSPEC_FAILURE;
	else if (stat7 & SMB5_CHGR_TEMP_TOO_HOT_BIT)
		*health = POWER_SUPPLY_HEALTH_OVERHEAT;
	else if (stat7 & SMB5_CHGR_TEMP_TOO_COLD_BIT)
		*health = POWER_SUPPLY_HEALTH_COLD;
	else if (stat7 & SMB5_CHGR_TEMP_HOT_SOFT_BIT)
		*health = POWER_SUPPLY_HEALTH_WARM;
	else if (stat7 & SMB5_CHGR_TEMP_COLD_SOFT_BIT)
		*health = POWER_SUPPLY_HEALTH_COOL;
	else if (usbin & (SMB5_USBIN_OV_BIT | SMB5_USBIN_VASHDN_BIT))
		*health = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
	else
		*health = POWER_SUPPLY_HEALTH_GOOD;

	return 0;
}

static int smb5_read_iio(struct iio_channel *chan, int *val)
{
	if (IS_ERR(chan))
		return PTR_ERR(chan);

	return iio_read_channel_processed(chan, val);
}

static int smb5_get_battery_id(struct qcom_smb5 *chip, int *resistance_ohm)
{
	int uv, ret;
	u64 resistance;

	ret = smb5_read_iio(chip->batt_id, &uv);
	if (ret)
		return ret;
	if (uv <= 0 || uv >= SMB5_BATT_ID_VREF_UV)
		return -ERANGE;

	resistance = div64_u64((u64)uv * SMB5_BATT_ID_PULLUP_OHM,
			       SMB5_BATT_ID_VREF_UV - uv);
	if (resistance > INT_MAX)
		return -ERANGE;

	*resistance_ohm = resistance;
	return 0;
}

static int smb5_get_charge_limits(struct qcom_smb5 *chip, unsigned int *current_ua,
				  unsigned int *voltage_uv, bool *allow)
{
	int temp_mc, low, high, hard_low, hard_high;
	int ret;

	ret = smb5_read_iio(chip->batt_therm, &temp_mc);
	if (ret)
		return ret;

	hard_low = chip->batt_info->temp_min;
	hard_high = chip->batt_info->temp_max;
	low = chip->batt_info->temp_alert_min;
	high = chip->batt_info->temp_alert_max;

	if (chip->temperature_blocked) {
		if (temp_mc >= (hard_low + chip->hard_temp_hysteresis_c) * 1000 &&
		    temp_mc <= (hard_high - chip->hard_temp_hysteresis_c) * 1000)
			chip->temperature_blocked = false;
	} else if (temp_mc < hard_low * 1000 || temp_mc >= hard_high * 1000) {
		chip->temperature_blocked = true;
	}

	if (chip->temperature_blocked) {
		*allow = false;
		return 0;
	}

	if (temp_mc < low * 1000) {
		*current_ua = chip->batt_info->alert_low_temp_charge_current_ua;
		*voltage_uv = chip->batt_info->alert_low_temp_charge_voltage_uv;
	} else if (temp_mc >= high * 1000) {
		*current_ua = chip->batt_info->alert_high_temp_charge_current_ua;
		*voltage_uv = chip->batt_info->alert_high_temp_charge_voltage_uv;
	} else {
		*current_ua = chip->batt_info->constant_charge_current_max_ua;
		*voltage_uv = chip->batt_info->constant_charge_voltage_max_uv;
	}

	*allow = true;
	return 0;
}

static unsigned int smb5_apsd_current_limit(int usb_type)
{
	switch (usb_type) {
	case POWER_SUPPLY_USB_TYPE_CDP:
		return SMB5_CDP_CURRENT_UA;
	case POWER_SUPPLY_USB_TYPE_DCP:
		return SMB5_DCP_CURRENT_UA;
	case POWER_SUPPLY_USB_TYPE_SDP:
		return SMB5_SDP_CURRENT_UA;
	case POWER_SUPPLY_USB_TYPE_C:
	case POWER_SUPPLY_USB_TYPE_PD:
	case POWER_SUPPLY_USB_TYPE_PD_PPS:
		return UINT_MAX;
	default:
		return SMB5_SAFE_INPUT_DEFAULT_UA;
	}
}

static int smb5_apply_policy(struct qcom_smb5 *chip)
{
	union power_supply_propval supplier;
	unsigned int batif, usbin, current_ua, charge_current_ua, charge_voltage_uv;
	unsigned int state;
	u64 now;
	bool temp_ok;
	int batt_id_ohm, online, usb_type, ret;

	ret = smb5_is_online(chip, &online);
	if (ret)
		return ret;

	if (!online) {
		smb5_set_charging(chip, false);
		smb5_set_usb_suspend(chip, true);
		smb5_set_input_limit(chip, SMB5_SAFE_INPUT_DEFAULT_UA);
		chip->charge_started_sec = 0;
		chip->charge_timeout_sec = 0;
		chip->recharge_cycle = false;
		chip->safety_expired = false;
		return 0;
	}

	ret = smb5_read(chip, SMB5_BATIF_INT_RT_STS, &batif);
	if (ret)
		return ret;
	ret = smb5_read(chip, SMB5_USBIN_INT_RT_STS, &usbin);
	if (ret)
		return ret;

	if ((batif & (SMB5_BATIF_TERMINAL_MISSING_BIT |
		      SMB5_BATIF_THERM_OR_ID_MISSING_BIT | SMB5_BATIF_OV_BIT)) ||
	    (usbin & (SMB5_USBIN_OV_BIT | SMB5_USBIN_VASHDN_BIT))) {
		smb5_set_charging(chip, false);
		smb5_set_usb_suspend(chip, true);
		return 0;
	}

	ret = smb5_get_battery_id(chip, &batt_id_ohm);
	if (ret)
		return ret;
	if (!power_supply_battery_bti_in_range(chip->batt_info, batt_id_ohm)) {
		dev_warn_ratelimited(chip->dev, "battery ID is outside the configured profile\n");
		smb5_set_charging(chip, false);
		smb5_set_usb_suspend(chip, true);
		return 0;
	}

	ret = smb5_get_charge_limits(chip, &charge_current_ua, &charge_voltage_uv,
				     &temp_ok);
	if (ret)
		return ret;
	if (!temp_ok) {
		smb5_set_charging(chip, false);
		chip->charge_started_sec = 0;
		return 0;
	}

	if (READ_ONCE(chip->thermal_state) >= SMB5_COOLING_DISABLE) {
		smb5_set_charging(chip, false);
		return 0;
	}
	if (READ_ONCE(chip->thermal_state) == SMB5_COOLING_THROTTLE)
		charge_current_ua = min(charge_current_ua, (unsigned int)SMB5_THERMAL_FCC_UA);

	ret = smb5_read(chip, SMB5_CHGR_STATUS_1, &state);
	if (ret)
		return ret;
	state &= SMB5_CHGR_STATUS_MASK;
	if (state == SMB5_TERMINATE_CHARGE || state == SMB5_INHIBIT_CHARGE) {
		chip->charge_started_sec = 0;
		chip->charge_timeout_sec = 0;
		chip->recharge_cycle = true;
	} else if (chip->safety_expired) {
		smb5_set_charging(chip, false);
		return 0;
	}

	ret = smb5_set_charge_current(chip, charge_current_ua);
	if (ret)
		return ret;

	ret = smb5_set_charge_voltage(chip, charge_voltage_uv);
	if (ret)
		return ret;

	ret = smb5_get_usb_type(chip, &usb_type);
	if (ret)
		return ret;

	current_ua = min(smb5_apsd_current_limit(usb_type), chip->input_limit_max_ua);
	ret = power_supply_get_property_from_supplier(chip->psy,
							      POWER_SUPPLY_PROP_CURRENT_MAX,
							      &supplier);
	if (!ret && supplier.intval > 0)
		current_ua = min(current_ua, (unsigned int)supplier.intval);
	else if (usb_type == POWER_SUPPLY_USB_TYPE_C ||
		 usb_type == POWER_SUPPLY_USB_TYPE_PD ||
		 usb_type == POWER_SUPPLY_USB_TYPE_PD_PPS)
		current_ua = SMB5_SAFE_INPUT_DEFAULT_UA;

	ret = smb5_set_input_limit(chip, current_ua);
	if (ret)
		return ret;

	ret = smb5_set_usb_suspend(chip, false);
	if (ret)
		return ret;

	ret = smb5_set_charging(chip, true);
	if (ret)
		return ret;

	now = ktime_get_boottime_seconds();
	if (!chip->charge_started_sec) {
		chip->charge_started_sec = now;
		chip->charge_timeout_sec = chip->recharge_cycle ?
			SMB5_RECHARGE_TIMEOUT_SEC : SMB5_CHARGE_TIMEOUT_SEC;
		chip->recharge_cycle = false;
	} else if (now - chip->charge_started_sec >= chip->charge_timeout_sec) {
		chip->safety_expired = true;
		dev_warn(chip->dev, "software charging safety timer expired\n");
		return smb5_set_charging(chip, false);
	}

	return 0;
}

static void smb5_state_work(struct work_struct *work)
{
	struct qcom_smb5 *chip = container_of(to_delayed_work(work),
							struct qcom_smb5, state_work);
	int online;

	if (READ_ONCE(chip->suspended))
		return;

	if (smb5_apply_policy(chip))
		dev_err_ratelimited(chip->dev, "failed to update charging policy\n");

	power_supply_changed(chip->psy);
	if (!smb5_input_present(chip, &online) && online)
		mod_delayed_work(system_dfl_wq, &chip->state_work,
				 msecs_to_jiffies(SMB5_REEVALUATE_MS));
}

static void smb5_external_power_changed(struct power_supply *psy)
{
	struct qcom_smb5 *chip = power_supply_get_drvdata(psy);

	if (!READ_ONCE(chip->suspended))
		mod_delayed_work(system_dfl_wq, &chip->state_work, 0);
}

static enum power_supply_property smb5_properties[] = {
	POWER_SUPPLY_PROP_MANUFACTURER,
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_USB_TYPE,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
};

static int smb5_get_property(struct power_supply *psy,
			     enum power_supply_property psp,
			     union power_supply_propval *val)
{
	struct qcom_smb5 *chip = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_MANUFACTURER:
		val->strval = "Qualcomm";
		return 0;
	case POWER_SUPPLY_PROP_MODEL_NAME:
		val->strval = "PM6150 SMB5";
		return 0;
	case POWER_SUPPLY_PROP_STATUS:
		return smb5_get_status(chip, &val->intval);
	case POWER_SUPPLY_PROP_HEALTH:
		return smb5_get_health(chip, &val->intval);
	case POWER_SUPPLY_PROP_ONLINE:
		return smb5_is_online(chip, &val->intval);
	case POWER_SUPPLY_PROP_USB_TYPE:
		return smb5_get_usb_type(chip, &val->intval);
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		val->intval = chip->input_limit_ua;
		return 0;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		val->intval = chip->charge_current_ua;
		return 0;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
		val->intval = chip->charge_voltage_uv;
		return 0;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		return smb5_read_iio(chip->usbin_v, &val->intval);
	default:
		return -EINVAL;
	}
}

static const struct power_supply_desc smb5_psy_desc = {
	.name = "pm6150-charger",
	.type = POWER_SUPPLY_TYPE_USB,
	.usb_types = BIT(POWER_SUPPLY_USB_TYPE_SDP) |
		     BIT(POWER_SUPPLY_USB_TYPE_CDP) |
		     BIT(POWER_SUPPLY_USB_TYPE_DCP) |
		     BIT(POWER_SUPPLY_USB_TYPE_C) |
		     BIT(POWER_SUPPLY_USB_TYPE_PD) |
		     BIT(POWER_SUPPLY_USB_TYPE_PD_PPS) |
		     BIT(POWER_SUPPLY_USB_TYPE_UNKNOWN),
	.properties = smb5_properties,
	.num_properties = ARRAY_SIZE(smb5_properties),
	.get_property = smb5_get_property,
	.external_power_changed = smb5_external_power_changed,
};

static irqreturn_t smb5_event_irq(int irq, void *data)
{
	struct qcom_smb5 *chip = data;

	if (!READ_ONCE(chip->suspended))
		mod_delayed_work(system_dfl_wq, &chip->state_work, 0);
	return IRQ_HANDLED;
}

static int smb5_cooling_get_max_state(struct thermal_cooling_device *cdev,
					      unsigned long *state)
{
	*state = SMB5_COOLING_DISABLE;
	return 0;
}

static int smb5_cooling_get_cur_state(struct thermal_cooling_device *cdev,
					      unsigned long *state)
{
	struct qcom_smb5 *chip = cdev->devdata;

	*state = READ_ONCE(chip->thermal_state);
	return 0;
}

static int smb5_cooling_set_cur_state(struct thermal_cooling_device *cdev,
					      unsigned long state)
{
	struct qcom_smb5 *chip = cdev->devdata;

	if (state > SMB5_COOLING_DISABLE)
		return -EINVAL;

	WRITE_ONCE(chip->thermal_state, state);
	if (!READ_ONCE(chip->suspended))
		mod_delayed_work(system_dfl_wq, &chip->state_work, 0);

	return 0;
}

static const struct thermal_cooling_device_ops smb5_cooling_ops = {
	.get_max_state = smb5_cooling_get_max_state,
	.get_cur_state = smb5_cooling_get_cur_state,
	.set_cur_state = smb5_cooling_set_cur_state,
};

static int smb5_request_irq(struct qcom_smb5 *chip, struct platform_device *pdev,
			    const char *name, irqreturn_t (*handler)(int, void *),
			    int *irq_store)
{
	int irq, ret;

	irq = platform_get_irq_byname(pdev, name);
	if (irq < 0)
		return irq;

	ret = devm_request_threaded_irq(chip->dev, irq, NULL, handler, IRQF_ONESHOT,
					name, chip);
	if (ret)
		return ret;

	if (irq_store)
		*irq_store = irq;
	return 0;
}

static int smb5_configure_termination(struct qcom_smb5 *chip)
{
	int term_ua = chip->batt_info->charge_term_current_ua;
	s16 raw;
	int ret;

	if (term_ua <= 0)
		return -EINVAL;

	/* PM6150 ADC ITERM is signed with +/-10 A full scale. Charging is negative. */
	raw = -div_s64((s64)term_ua * 32767, 10000000);
	ret = smb5_bulk_write_be16(chip, SMB5_CHGR_ITERM_UP_MSB, (u16)raw);
	if (ret)
		return ret;

	return smb5_update_bits(chip, SMB5_CHGR_ADC_TERM_CFG,
				SMB5_CHGR_ADC_TERM_SAMPLE_COUNT_BIT,
				SMB5_CHGR_ADC_TERM_SAMPLE_COUNT_BIT);
}

static int smb5_configure_recharge(struct qcom_smb5 *chip)
{
	int uv = chip->batt_info->charge_restart_voltage_uv;
	u16 raw;
	int ret;

	if (uv <= 0)
		return -EINVAL;

	/* Same ADC scale used by downstream PM6150: Vraw = mV * 1e6 / 194637. */
	raw = div_u64((u64)(uv / 1000) * 1000000, SMB5_ADC_VOLTAGE_NUMERATOR);
	ret = smb5_bulk_write_be16(chip, SMB5_CHGR_RECHG_THD_MSB, raw);
	if (ret)
		return ret;

	ret = smb5_update_bits(chip, SMB5_CHGR_RECHG_SAMPLE_CFG,
				SMB5_CHGR_RECHG_SAMPLE_MASK, SMB5_CHGR_RECHG_SAMPLE_3);
	if (ret)
		return ret;

	return smb5_update_bits(chip, SMB5_CHGR_CFG2, SMB5_CHGR_RECHG_MASK,
					SMB5_CHGR_VBAT_RECHG_BIT);
}

static int smb5_configure_hard_jeita(struct qcom_smb5 *chip)
{
	int cold_mc, hot_mc;
	u16 cold_raw, hot_raw;
	int ret;

	/*
	 * Use the same 100 kOhm/1.875 V transfer function as BAT_THERM. The
	 * one-degree inward guard makes the hardware cutoff conservative while
	 * software still enforces the battery's exact operating range.
	 */
	cold_mc = chip->batt_info->temp_min * 1000 + SMB5_HARD_JEITA_GUARD_MC;
	hot_mc = chip->batt_info->temp_max * 1000 - SMB5_HARD_JEITA_GUARD_MC;
	if (cold_mc >= hot_mc)
		return -EINVAL;

	cold_raw = qcom_adc_tm5_temp_volt_scale(VADC_DEF_PRESCALING,
						 ADC5_FULL_SCALE_CODE, cold_mc);
	hot_raw = qcom_adc_tm5_temp_volt_scale(VADC_DEF_PRESCALING,
						ADC5_FULL_SCALE_CODE, hot_mc);

	ret = smb5_bulk_write_be16(chip, SMB5_CHGR_JEITA_HARD_HOT_MSB, hot_raw);
	if (ret)
		return ret;

	ret = smb5_bulk_write_be16(chip, SMB5_CHGR_JEITA_HARD_COLD_MSB, cold_raw);
	if (ret)
		return ret;

	return smb5_update_bits(chip, SMB5_CHGR_JEITA_ENABLE,
				SMB5_CHGR_JEITA_HARD_LIMIT_BIT,
				SMB5_CHGR_JEITA_HARD_LIMIT_BIT);
}

static int smb5_init_hw(struct qcom_smb5 *chip)
{
	unsigned int hv_mask;
	int ret;

	/* Establish a fail-safe baseline before changing charger policy. */
	ret = smb5_set_charging(chip, false);
	if (ret)
		return ret;
	ret = smb5_set_usb_suspend(chip, true);
	if (ret)
		return ret;

	/* Force the PMIC secondary-charger SMB_EN output low for 5 V buck charging. */
	ret = smb5_update_bits(chip, SMB5_MISC_SMB_EN_CMD,
				SMB5_MISC_SMB_EN_OVERRIDE_BIT |
				SMB5_MISC_SMB_EN_OVERRIDE_VALUE_BIT,
				SMB5_MISC_SMB_EN_OVERRIDE_BIT);
	if (ret)
		return ret;

	/* Explicitly restrict adapter input to 5 V and leave all HVDCP disabled. */
	ret = smb5_write(chip, SMB5_USBIN_ADAPTER_ALLOW_CFG,
			 SMB5_USBIN_ADAPTER_ALLOW_5V);
	if (ret)
		return ret;

	hv_mask = SMB5_USBIN_HVDCP_AUTH_BIT | SMB5_USBIN_HVDCP_AUTO_BIT |
		  SMB5_USBIN_BC12_DETECT_BIT | SMB5_USBIN_HVDCP_ENABLE_BIT;
	ret = smb5_update_bits(chip, SMB5_USBIN_OPTIONS_1_CFG, hv_mask,
				SMB5_USBIN_BC12_DETECT_BIT);
	if (ret)
		return ret;

	ret = smb5_update_bits(chip, SMB5_USBIN_AICL_OPTIONS,
				SMB5_USBIN_AICL_SUSPEND_COLLAPSE_BIT |
				SMB5_USBIN_AICL_PERIODIC_BIT |
				SMB5_USBIN_AICL_ADC_BIT |
				SMB5_USBIN_AICL_ENABLE_BIT,
				SMB5_USBIN_AICL_SUSPEND_COLLAPSE_BIT |
				SMB5_USBIN_AICL_PERIODIC_BIT |
				SMB5_USBIN_AICL_ENABLE_BIT);
	if (ret)
		return ret;

	ret = smb5_write(chip, SMB5_MISC_AICL_RERUN_TIME_CFG,
			 SMB5_MISC_AICL_RERUN_12S);
	if (ret)
		return ret;

	ret = smb5_set_input_limit(chip, SMB5_SAFE_INPUT_DEFAULT_UA);
	if (ret)
		return ret;

	ret = smb5_set_charge_current(chip,
				      chip->batt_info->constant_charge_current_max_ua);
	if (ret)
		return ret;

	ret = smb5_set_charge_voltage(chip,
				      chip->batt_info->constant_charge_voltage_max_uv);
	if (ret)
		return ret;

	ret = smb5_configure_termination(chip);
	if (ret)
		return ret;

	ret = smb5_configure_recharge(chip);
	if (ret)
		return ret;

	ret = smb5_configure_hard_jeita(chip);
	if (ret)
		return ret;

	ret = smb5_write(chip, SMB5_CHGR_SAFETY_TIMER_CFG,
			 SMB5_CHGR_SAFETY_TIMER_1536_MIN);
	if (ret)
		return ret;

	/* A70 downstream has no charge-inhibit threshold configured. */
	ret = smb5_update_bits(chip, SMB5_CHGR_CFG2, SMB5_CHGR_INHIBIT_BIT, 0);
	if (ret)
		return ret;

	/* Match the upstream SMB2 policy: the charger watchdog is disabled. */
	ret = smb5_write(chip, SMB5_MISC_WD_TIMEOUT_CFG, 0);
	if (ret)
		return ret;

	ret = smb5_update_bits(chip, SMB5_MISC_WD_CFG,
					SMB5_MISC_WD_AFP_ENABLE_BIT |
					SMB5_MISC_WD_BARK_IRQ_BIT | SMB5_MISC_WD_BITE_IRQ_BIT |
					SMB5_MISC_WD_TIMER_PLUGIN_BIT,
					0);
	if (ret)
		return ret;

	/* Trigger a fresh BC1.2 classification after configuring source detection. */
	return smb5_update_bits(chip, SMB5_USBIN_APSD_CMD,
					SMB5_USBIN_APSD_RERUN_BIT, SMB5_USBIN_APSD_RERUN_BIT);
}

static int smb5_suspend(struct device *dev)
{
	struct qcom_smb5 *chip = dev_get_drvdata(dev);
	unsigned int current_ua, voltage_uv;
	int ret;

	WRITE_ONCE(chip->suspended, true);
	cancel_delayed_work_sync(&chip->state_work);

	ret = smb5_apply_policy(chip);
	if (ret)
		goto fail_safe;
	if (!chip->charging_enabled)
		return 0;

	/*
	 * No software temperature work runs while suspended. Keep the battery at
	 * the most conservative configured JEITA current/voltage and leave the
	 * independently-programmed hardware hard-JEITA cutoff active.
	 */
	current_ua = min3((unsigned int)chip->batt_info->constant_charge_current_max_ua,
			  (unsigned int)chip->batt_info->alert_low_temp_charge_current_ua,
			  (unsigned int)chip->batt_info->alert_high_temp_charge_current_ua);
	voltage_uv = min3((unsigned int)chip->batt_info->constant_charge_voltage_max_uv,
			  (unsigned int)chip->batt_info->alert_low_temp_charge_voltage_uv,
			  (unsigned int)chip->batt_info->alert_high_temp_charge_voltage_uv);

	ret = smb5_set_charge_current(chip, current_ua);
	if (ret)
		goto fail_safe;
	ret = smb5_set_charge_voltage(chip, voltage_uv);
	if (ret)
		goto fail_safe;
	ret = smb5_set_input_limit(chip, SMB5_SAFE_INPUT_DEFAULT_UA);
	if (ret)
		goto fail_safe;

	return 0;

fail_safe:
	smb5_set_charging(chip, false);
	smb5_set_usb_suspend(chip, true);
	return 0;
}

static int smb5_resume(struct device *dev)
{
	struct qcom_smb5 *chip = dev_get_drvdata(dev);

	WRITE_ONCE(chip->suspended, false);
	mod_delayed_work(system_dfl_wq, &chip->state_work, 0);

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(smb5_pm_ops, smb5_suspend, smb5_resume);

static int smb5_probe(struct platform_device *pdev)
{
	static const char * const event_irqs[] = {
		"chg-state-change", "bat-temp", "bat-ov", "bat-therm-or-id-missing",
		"bat-terminal-missing", "usbin-collapse", "usbin-vashdn", "usbin-uv",
		"usbin-ov", "usbin-src-change", "usbin-icl-change", "aicl-done",
		"temp-change", "temp-change-smb",
	};
	struct power_supply_config cfg = {};
	struct thermal_cooling_device *cdev;
	struct qcom_smb5 *chip;
	u32 hard_temp_hysteresis, input_limit;
	int i, ret;

	chip = devm_kzalloc(&pdev->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->dev = &pdev->dev;
	chip->regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!chip->regmap)
		return dev_err_probe(chip->dev, -ENODEV, "failed to get regmap\n");

	ret = device_property_read_u32(chip->dev, "reg", &chip->base);
	if (ret)
		return dev_err_probe(chip->dev, ret, "failed to get charger base\n");

	ret = device_property_read_u32(chip->dev, "input-current-limit-microamp",
				       &input_limit);
	if (ret)
		return dev_err_probe(chip->dev, ret,
				     "input current limit is required\n");
	chip->input_limit_max_ua = input_limit;

	chip->usbin_v = devm_iio_channel_get(chip->dev, "usbin-v");
	if (IS_ERR(chip->usbin_v))
		return dev_err_probe(chip->dev, PTR_ERR(chip->usbin_v),
					     "failed to get USB input voltage channel\n");
	chip->batt_therm = devm_iio_channel_get(chip->dev, "batt-therm");
	if (IS_ERR(chip->batt_therm))
		return dev_err_probe(chip->dev, PTR_ERR(chip->batt_therm),
					     "failed to get battery thermistor\n");
	chip->batt_id = devm_iio_channel_get(chip->dev, "batt-id");
	if (IS_ERR(chip->batt_id))
		return dev_err_probe(chip->dev, PTR_ERR(chip->batt_id),
					     "failed to get battery ID channel\n");

	ret = device_property_read_u32(chip->dev, "qcom,hard-jeita-hysteresis-celsius",
				       &hard_temp_hysteresis);
	if (ret)
		return dev_err_probe(chip->dev, ret,
				     "hard JEITA hysteresis is required\n");
	chip->hard_temp_hysteresis_c = hard_temp_hysteresis;

	ret = devm_delayed_work_autocancel(chip->dev, &chip->state_work,
						  smb5_state_work);
	if (ret)
		return ret;

	cfg.drv_data = chip;
	cfg.fwnode = dev_fwnode(chip->dev);
	chip->psy = devm_power_supply_register(chip->dev, &smb5_psy_desc, &cfg);
	if (IS_ERR(chip->psy))
		return dev_err_probe(chip->dev, PTR_ERR(chip->psy),
				     "failed to register charger\n");

	ret = power_supply_get_battery_info(chip->psy, &chip->batt_info);
	if (ret)
		return dev_err_probe(chip->dev, ret, "failed to get battery data\n");

	if (chip->batt_info->constant_charge_current_max_ua <= 0 ||
	    chip->batt_info->constant_charge_voltage_max_uv <= 0 ||
	    chip->batt_info->charge_term_current_ua <= 0 ||
	    chip->batt_info->charge_restart_voltage_uv <= 0 ||
	    chip->batt_info->alert_low_temp_charge_current_ua <= 0 ||
	    chip->batt_info->alert_low_temp_charge_voltage_uv <= 0 ||
	    chip->batt_info->alert_high_temp_charge_current_ua <= 0 ||
	    chip->batt_info->alert_high_temp_charge_voltage_uv <= 0 ||
	    chip->batt_info->bti_resistance_ohm <= 0 ||
	    chip->batt_info->bti_resistance_tolerance <= 0 ||
	    !power_supply_battery_info_has_prop(chip->batt_info,
						POWER_SUPPLY_PROP_TEMP_MIN) ||
	    !power_supply_battery_info_has_prop(chip->batt_info,
						POWER_SUPPLY_PROP_TEMP_MAX) ||
	    !power_supply_battery_info_has_prop(chip->batt_info,
						POWER_SUPPLY_PROP_TEMP_ALERT_MIN) ||
	    !power_supply_battery_info_has_prop(chip->batt_info,
						POWER_SUPPLY_PROP_TEMP_ALERT_MAX) ||
	    chip->hard_temp_hysteresis_c <= 0 ||
	    chip->batt_info->temp_min + chip->hard_temp_hysteresis_c >=
		chip->batt_info->temp_max - chip->hard_temp_hysteresis_c)
		return dev_err_probe(chip->dev, -EINVAL,
					     "battery charging limits are incomplete\n");

	ret = smb5_init_hw(chip);
	if (ret)
		return dev_err_probe(chip->dev, ret, "failed to initialize SMB5\n");

	cdev = devm_thermal_of_cooling_device_register(chip->dev, 0,
						       dev_name(chip->dev), chip,
						       &smb5_cooling_ops);
	if (IS_ERR(cdev))
		return dev_err_probe(chip->dev, PTR_ERR(cdev),
					     "failed to register thermal cooling device\n");

	ret = smb5_request_irq(chip, pdev, "usbin-plugin", smb5_event_irq,
			       &chip->cable_irq);
	if (ret)
		return ret;

	for (i = 0; i < ARRAY_SIZE(event_irqs); i++) {
		ret = smb5_request_irq(chip, pdev, event_irqs[i], smb5_event_irq, NULL);
		if (ret)
			return dev_err_probe(chip->dev, ret, "failed to request %s IRQ\n",
					     event_irqs[i]);
	}

	devm_device_init_wakeup(chip->dev);
	ret = devm_pm_set_wake_irq(chip->dev, chip->cable_irq);
	if (ret)
		return dev_err_probe(chip->dev, ret, "failed to configure wake IRQ\n");

	platform_set_drvdata(pdev, chip);
	mod_delayed_work(system_dfl_wq, &chip->state_work, 0);

	return 0;
}

static const struct of_device_id smb5_of_match[] = {
	{ .compatible = "qcom,pm6150-charger" },
	{ }
};
MODULE_DEVICE_TABLE(of, smb5_of_match);

static struct platform_driver smb5_driver = {
	.probe = smb5_probe,
		.driver = {
			.name = "qcom-pm6150-smb5",
			.of_match_table = smb5_of_match,
			.pm = pm_sleep_ptr(&smb5_pm_ops),
		},
};
module_platform_driver(smb5_driver);

MODULE_DESCRIPTION("Qualcomm PM6150 SMB5 charger driver");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("IIO_CONSUMER");
