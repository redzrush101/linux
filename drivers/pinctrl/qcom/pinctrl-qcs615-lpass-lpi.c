// SPDX-License-Identifier: GPL-2.0-only
/* QCS615/SM6150 LPASS LPI GPIOs */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_clock.h>
#include <linux/pm_runtime.h>

#include "pinctrl-lpass-lpi.h"

enum qcs615_lpi_functions {
	LPI_MUX_dmic1_clk,
	LPI_MUX_dmic1_data,
	LPI_MUX_dmic2_clk,
	LPI_MUX_dmic2_data,
	LPI_MUX_swr_tx_clk,
	LPI_MUX_swr_tx_data,
	LPI_MUX_swr_rx_clk,
	LPI_MUX_swr_rx_data,
	LPI_MUX_lpi_cdc_rst,
	LPI_MUX_gpio,
	LPI_MUX__,
};

#define QCS615_LPI_PIN(n) PINCTRL_PIN(n, "gpio" #n)
static const struct pinctrl_pin_desc qcs615_lpi_pins[] = {
	QCS615_LPI_PIN(0), QCS615_LPI_PIN(1),
	QCS615_LPI_PIN(2), QCS615_LPI_PIN(3),
	QCS615_LPI_PIN(4), QCS615_LPI_PIN(5),
	QCS615_LPI_PIN(6), QCS615_LPI_PIN(7),
	QCS615_LPI_PIN(8), QCS615_LPI_PIN(9),
	QCS615_LPI_PIN(10), QCS615_LPI_PIN(11),
	QCS615_LPI_PIN(12), QCS615_LPI_PIN(13),
	QCS615_LPI_PIN(14), QCS615_LPI_PIN(15),
	QCS615_LPI_PIN(16), QCS615_LPI_PIN(17),
	QCS615_LPI_PIN(18), QCS615_LPI_PIN(19),
	QCS615_LPI_PIN(20), QCS615_LPI_PIN(21),
	QCS615_LPI_PIN(22), QCS615_LPI_PIN(23),
	QCS615_LPI_PIN(24), QCS615_LPI_PIN(25),
	QCS615_LPI_PIN(26), QCS615_LPI_PIN(27),
	QCS615_LPI_PIN(28), QCS615_LPI_PIN(29),
	QCS615_LPI_PIN(30), QCS615_LPI_PIN(31),
};

static const char * const dmic1_clk_groups[] = { "gpio26" };
static const char * const dmic1_data_groups[] = { "gpio27" };
static const char * const dmic2_clk_groups[] = { "gpio28" };
static const char * const dmic2_data_groups[] = { "gpio29" };
static const char * const swr_tx_clk_groups[] = { "gpio18" };
static const char * const swr_tx_data_groups[] = { "gpio19", "gpio20" };
static const char * const swr_rx_clk_groups[] = { "gpio21" };
static const char * const swr_rx_data_groups[] = { "gpio22", "gpio23" };
static const char * const lpi_cdc_rst_groups[] = { "gpio24" };

static const struct lpi_pingroup qcs615_lpi_groups[] = {
	LPI_PINGROUP(0, LPI_NO_SLEW, _, _, _, _),
	LPI_PINGROUP(1, LPI_NO_SLEW, _, _, _, _),
	LPI_PINGROUP(2, LPI_NO_SLEW, _, _, _, _),
	LPI_PINGROUP(3, LPI_NO_SLEW, _, _, _, _),
	LPI_PINGROUP(4, LPI_NO_SLEW, _, _, _, _),
	LPI_PINGROUP(5, LPI_NO_SLEW, _, _, _, _),
	LPI_PINGROUP(6, LPI_NO_SLEW, _, _, _, _),
	LPI_PINGROUP(7, LPI_NO_SLEW, _, _, _, _),
	LPI_PINGROUP(8, LPI_NO_SLEW, _, _, _, _),
	LPI_PINGROUP(9, LPI_NO_SLEW, _, _, _, _),
	LPI_PINGROUP(10, LPI_NO_SLEW, _, _, _, _),
	LPI_PINGROUP(11, LPI_NO_SLEW, _, _, _, _),
	LPI_PINGROUP(12, LPI_NO_SLEW, _, _, _, _),
	LPI_PINGROUP(13, LPI_NO_SLEW, _, _, _, _),
	LPI_PINGROUP(14, LPI_NO_SLEW, _, _, _, _),
	LPI_PINGROUP(15, LPI_NO_SLEW, _, _, _, _),
	LPI_PINGROUP(16, LPI_NO_SLEW, _, _, _, _),
	LPI_PINGROUP(17, LPI_NO_SLEW, _, _, _, _),
	/* Function indices below follow the Samsung downstream LPI pin states. */
	LPI_PINGROUP(18, LPI_NO_SLEW, _, swr_tx_clk, _, _),
	LPI_PINGROUP(19, LPI_NO_SLEW, _, _, swr_tx_data, _),
	LPI_PINGROUP(20, LPI_NO_SLEW, _, swr_tx_data, _, _),
	LPI_PINGROUP(21, LPI_NO_SLEW, _, swr_rx_clk, _, _),
	LPI_PINGROUP(22, LPI_NO_SLEW, _, swr_rx_data, _, _),
	LPI_PINGROUP(23, LPI_NO_SLEW, _, swr_rx_data, _, _),
	LPI_PINGROUP(24, LPI_NO_SLEW, _, lpi_cdc_rst, _, _),
	LPI_PINGROUP(25, LPI_NO_SLEW, _, _, _, _),
	LPI_PINGROUP(26, LPI_NO_SLEW, dmic1_clk, _, _, _),
	LPI_PINGROUP(27, LPI_NO_SLEW, dmic1_data, _, _, _),
	LPI_PINGROUP(28, LPI_NO_SLEW, dmic2_clk, _, _, _),
	LPI_PINGROUP(29, LPI_NO_SLEW, dmic2_data, _, _, _),
	LPI_PINGROUP(30, LPI_NO_SLEW, _, _, _, _),
	LPI_PINGROUP(31, LPI_NO_SLEW, _, _, _, _),
};

static const struct lpi_function qcs615_lpi_functions[] = {
	LPI_FUNCTION(dmic1_clk), LPI_FUNCTION(dmic1_data),
	LPI_FUNCTION(dmic2_clk), LPI_FUNCTION(dmic2_data),
	LPI_FUNCTION(swr_tx_clk), LPI_FUNCTION(swr_tx_data),
	LPI_FUNCTION(swr_rx_clk), LPI_FUNCTION(swr_rx_data),
	LPI_FUNCTION(lpi_cdc_rst),
};

static const struct lpi_pinctrl_variant_data qcs615_lpi_data = {
	.pins = qcs615_lpi_pins,
	.npins = ARRAY_SIZE(qcs615_lpi_pins),
	.groups = qcs615_lpi_groups,
	.ngroups = ARRAY_SIZE(qcs615_lpi_groups),
	.functions = qcs615_lpi_functions,
	.nfunctions = ARRAY_SIZE(qcs615_lpi_functions),
	.flags = LPI_FLAG_SLEW_RATE_SAME_REG,
};

static const struct of_device_id qcs615_lpi_of_match[] = {
	{ .compatible = "qcom,qcs615-lpass-lpi-pinctrl", .data = &qcs615_lpi_data },
	{ }
};
MODULE_DEVICE_TABLE(of, qcs615_lpi_of_match);

static const struct dev_pm_ops qcs615_lpi_pm_ops = {
	RUNTIME_PM_OPS(pm_clk_suspend, pm_clk_resume, NULL)
};

static struct platform_driver qcs615_lpi_driver = {
	.driver = {
		.name = "qcom-qcs615-lpass-lpi-pinctrl",
		.of_match_table = qcs615_lpi_of_match,
		.pm = pm_ptr(&qcs615_lpi_pm_ops),
	},
	.probe = lpi_pinctrl_probe,
	.remove = lpi_pinctrl_remove,
};
module_platform_driver(qcs615_lpi_driver);

MODULE_DESCRIPTION("QCS615 LPASS LPI pin controller");
MODULE_LICENSE("GPL");
