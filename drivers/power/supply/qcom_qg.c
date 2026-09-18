// SPDX-License-Identifier: GPL-2.0-only
/*
 * Qualcomm PMIC QGauge fuel gauge driver
 *
 * QGauge samples battery voltage and current but has no coulomb counter.
 * Estimate the open-circuit voltage from the averaged samples and the
 * battery's internal resistance, and map it through the battery OCV table.
 */

#include <linux/iio/consumer.h>
#include <linux/interrupt.h>
#include <linux/minmax.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/unaligned.h>

#define QG_STATUS1_REG			0x08
#define QG_BATTERY_PRESENT_BIT		BIT(0)

#define QG_S2_NORMAL_AVG_V_DATA0_REG	0x80
#define QG_S2_NORMAL_AVG_I_DATA0_REG	0x82
#define QG_LAST_ADC_V_DATA0_REG		0xc0
#define QG_LAST_ADC_I_DATA0_REG		0xc2

#define QG_VOLTAGE_NUMERATOR		194637
#define QG_CURRENT_NUMERATOR		152588

struct qcom_qg {
	struct device *dev;
	struct regmap *regmap;
	struct power_supply *psy;
	struct power_supply_battery_info *info;
	struct iio_channel *batt_therm;
	unsigned int base;
};

static int qcom_qg_read_word(struct qcom_qg *qg, unsigned int reg, u16 *val)
{
	u8 buf[2];
	int ret;

	ret = regmap_bulk_read(qg->regmap, qg->base + reg, buf, sizeof(buf));
	if (ret)
		return ret;

	*val = get_unaligned_le16(buf);
	return 0;
}

static int qcom_qg_get_voltage(struct qcom_qg *qg, unsigned int reg, int *uv)
{
	u16 raw;
	int ret;

	ret = qcom_qg_read_word(qg, reg, &raw);
	if (ret)
		return ret;

	*uv = div_u64((u64)raw * QG_VOLTAGE_NUMERATOR, 1000);
	return 0;
}

/* The ADC reports discharge as positive; power_supply uses the opposite sign. */
static int qcom_qg_get_current(struct qcom_qg *qg, unsigned int reg, int *ua)
{
	u16 raw;
	int ret;

	ret = qcom_qg_read_word(qg, reg, &raw);
	if (ret)
		return ret;

	*ua = -div_s64((s64)(s16)raw * QG_CURRENT_NUMERATOR, 1000);
	return 0;
}

static int qcom_qg_get_temp(struct qcom_qg *qg, int *decicelsius)
{
	int millicelsius;
	int ret;

	ret = iio_read_channel_processed(qg->batt_therm, &millicelsius);
	if (ret)
		return ret;

	*decicelsius = millicelsius / 100;
	return 0;
}

static int qcom_qg_get_ocv(struct qcom_qg *qg, int temp, int *uv)
{
	struct power_supply_battery_info *info = qg->info;
	int vavg, iavg, rint;
	int ret;

	ret = qcom_qg_get_voltage(qg, QG_S2_NORMAL_AVG_V_DATA0_REG, &vavg);
	if (ret)
		return ret;

	ret = qcom_qg_get_current(qg, QG_S2_NORMAL_AVG_I_DATA0_REG, &iavg);
	if (ret)
		return ret;

	rint = max(info->factory_internal_resistance_uohm, 0);
	if (rint > 0 && info->resist_table)
		rint = rint / 100 *
		       power_supply_temp2resist_simple(info->resist_table,
						       info->resist_table_size,
						       temp / 10);

	/* The terminal voltage rises above OCV while charging and drops below it on load. */
	*uv = vavg - div_s64((s64)iavg * rint, 1000000);
	return 0;
}

static int qcom_qg_get_capacity(struct qcom_qg *qg, int *capacity)
{
	int temp, ocv;
	int ret;

	ret = qcom_qg_get_temp(qg, &temp);
	if (ret)
		return ret;

	ret = qcom_qg_get_ocv(qg, temp, &ocv);
	if (ret)
		return ret;

	*capacity = power_supply_batinfo_ocv2cap(qg->info, ocv, temp / 10);
	return *capacity < 0 ? *capacity : 0;
}

static int qcom_qg_get_status(struct qcom_qg *qg, int *status)
{
	union power_supply_propval val;
	int ret;

	ret = power_supply_get_property_from_supplier(qg->psy,
						      POWER_SUPPLY_PROP_STATUS,
						      &val);
	if (!ret) {
		*status = val.intval;
		return 0;
	}

	ret = qcom_qg_get_current(qg, QG_LAST_ADC_I_DATA0_REG, &val.intval);
	if (ret)
		return ret;

	if (val.intval > 0)
		*status = POWER_SUPPLY_STATUS_CHARGING;
	else if (val.intval < 0)
		*status = POWER_SUPPLY_STATUS_DISCHARGING;
	else
		*status = POWER_SUPPLY_STATUS_NOT_CHARGING;
	return 0;
}

static enum power_supply_property qcom_qg_properties[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_AVG,
	POWER_SUPPLY_PROP_VOLTAGE_OCV,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CURRENT_AVG,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_TEMP,
};

static int qcom_qg_get_property(struct power_supply *psy,
				enum power_supply_property psp,
				union power_supply_propval *val)
{
	struct qcom_qg *qg = power_supply_get_drvdata(psy);
	unsigned int status;
	int ret, temp;

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		return qcom_qg_get_status(qg, &val->intval);
	case POWER_SUPPLY_PROP_PRESENT:
		ret = regmap_read(qg->regmap, qg->base + QG_STATUS1_REG, &status);
		if (!ret)
			val->intval = !!(status & QG_BATTERY_PRESENT_BIT);
		return ret;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		return qcom_qg_get_voltage(qg, QG_LAST_ADC_V_DATA0_REG, &val->intval);
	case POWER_SUPPLY_PROP_VOLTAGE_AVG:
		return qcom_qg_get_voltage(qg, QG_S2_NORMAL_AVG_V_DATA0_REG, &val->intval);
	case POWER_SUPPLY_PROP_VOLTAGE_OCV:
		ret = qcom_qg_get_temp(qg, &temp);
		if (ret)
			return ret;
		return qcom_qg_get_ocv(qg, temp, &val->intval);
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		return qcom_qg_get_current(qg, QG_LAST_ADC_I_DATA0_REG, &val->intval);
	case POWER_SUPPLY_PROP_CURRENT_AVG:
		return qcom_qg_get_current(qg, QG_S2_NORMAL_AVG_I_DATA0_REG, &val->intval);
	case POWER_SUPPLY_PROP_CAPACITY:
		return qcom_qg_get_capacity(qg, &val->intval);
	case POWER_SUPPLY_PROP_TEMP:
		return qcom_qg_get_temp(qg, &val->intval);
	default:
		return -EINVAL;
	}
}

/* The core has parsed the monitored battery by the time this runs. */
static int qcom_qg_init(struct power_supply *psy)
{
	struct qcom_qg *qg = power_supply_get_drvdata(psy);

	if (!psy->battery_info || !psy->battery_info->ocv_table[0])
		return dev_err_probe(qg->dev, -EINVAL,
				     "battery OCV table is required\n");

	qg->info = psy->battery_info;
	return 0;
}

static void qcom_qg_external_power_changed(struct power_supply *psy)
{
	power_supply_changed(psy);
}

static irqreturn_t qcom_qg_irq(int irq, void *data)
{
	struct qcom_qg *qg = data;

	power_supply_changed(qg->psy);
	return IRQ_HANDLED;
}

static const struct power_supply_desc qcom_qg_psy_desc = {
	.name = "pm6150-battery",
	.type = POWER_SUPPLY_TYPE_BATTERY,
	.properties = qcom_qg_properties,
	.num_properties = ARRAY_SIZE(qcom_qg_properties),
	.get_property = qcom_qg_get_property,
	.init = qcom_qg_init,
	.external_power_changed = qcom_qg_external_power_changed,
};

static int qcom_qg_probe(struct platform_device *pdev)
{
	static const char * const irq_names[] = {
		"qg-batt-missing", "qg-vbat-low", "qg-vbat-empty",
		"qg-fifo-done", "qg-good-ocv",
	};
	struct power_supply_config cfg = {};
	struct qcom_qg *qg;
	int i, irq, ret;

	qg = devm_kzalloc(&pdev->dev, sizeof(*qg), GFP_KERNEL);
	if (!qg)
		return -ENOMEM;

	qg->dev = &pdev->dev;
	qg->regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!qg->regmap)
		return dev_err_probe(qg->dev, -ENODEV, "failed to get regmap\n");

	ret = device_property_read_u32(qg->dev, "reg", &qg->base);
	if (ret)
		return dev_err_probe(qg->dev, ret, "failed to get base address\n");

	qg->batt_therm = devm_iio_channel_get(qg->dev, "batt-therm");
	if (IS_ERR(qg->batt_therm))
		return dev_err_probe(qg->dev, PTR_ERR(qg->batt_therm),
				     "failed to get battery thermistor\n");

	cfg.drv_data = qg;
	cfg.fwnode = dev_fwnode(qg->dev);
	qg->psy = devm_power_supply_register(qg->dev, &qcom_qg_psy_desc, &cfg);
	if (IS_ERR(qg->psy))
		return dev_err_probe(qg->dev, PTR_ERR(qg->psy),
				     "failed to register battery\n");

	for (i = 0; i < ARRAY_SIZE(irq_names); i++) {
		irq = platform_get_irq_byname(pdev, irq_names[i]);
		if (irq < 0)
			return dev_err_probe(qg->dev, irq, "failed to get %s IRQ\n",
					     irq_names[i]);

		ret = devm_request_threaded_irq(qg->dev, irq, NULL, qcom_qg_irq,
						IRQF_ONESHOT, irq_names[i], qg);
		if (ret)
			return dev_err_probe(qg->dev, ret, "failed to request %s IRQ\n",
						     irq_names[i]);
	}

	return 0;
}

static const struct of_device_id qcom_qg_of_match[] = {
	{ .compatible = "qcom,pm6150-qg" },
	{ }
};
MODULE_DEVICE_TABLE(of, qcom_qg_of_match);

static struct platform_driver qcom_qg_driver = {
	.probe = qcom_qg_probe,
	.driver = {
		.name = "qcom-pmic-qg",
		.of_match_table = qcom_qg_of_match,
	},
};
module_platform_driver(qcom_qg_driver);

MODULE_DESCRIPTION("Qualcomm PMIC QGauge fuel gauge driver");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("IIO_CONSUMER");
