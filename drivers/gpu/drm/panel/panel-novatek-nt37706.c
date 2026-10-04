// SPDX-License-Identifier: GPL-2.0-only
/*
 * Novatek NT37706 DSI panel driver
 *
 * Init sequence from the Xiaomi vendor device tree, converted with
 * linux-mdss-dsi-panel-driver-generator.
 *
 * Copyright (c) 2026, Yassin Rezai <yassinrsocial@proton.me>
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

#include <drm/display/drm_dsc.h>
#include <drm/display/drm_dsc_helper.h>
#include <drm/drm_connector.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

#include <video/mipi_display.h>

#define NT37706_DCS_SWITCH_PAGE		0xf0

struct nt37706 {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct drm_dsc_config dsc;
	struct gpio_desc *reset_gpio;
	struct regulator_bulk_data *supplies;
};

static const struct regulator_bulk_data nt37706_supplies[] = {
	{ .supply = "vddio" },
	{ .supply = "vdd" },
	{ .supply = "vci" },
};

static inline struct nt37706 *to_nt37706(struct drm_panel *panel)
{
	return container_of(panel, struct nt37706, panel);
}

#define nt37706_switch_page(dsi_ctx, page) \
	mipi_dsi_dcs_write_seq_multi((dsi_ctx), NT37706_DCS_SWITCH_PAGE, \
				     0x55, 0xaa, 0x52, 0x08, (page))

static void nt37706_reset(struct nt37706 *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(11000, 12000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(1000, 2000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(11000, 12000);
}

static void xiaomi_peridot_csot_init(struct mipi_dsi_multi_context *dsi_ctx)
{
	nt37706_switch_page(dsi_ctx, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xdf, 0x09);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x6f, 0x01);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xdf, 0x40);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x6f, 0x31);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xdf, 0x00, 0x1a);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x6f, 0x34);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xdf, 0x23);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x6f, 0x2d);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xc0,
				     0x00, 0x32, 0x00, 0x04, 0x50);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x6f, 0x01);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xc7, 0x01);
	nt37706_switch_page(dsi_ctx, 0x01);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x6f, 0x2a);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xb9, 0x10);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x6f, 0x0c);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xb9, 0x1a);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x6f, 0x05);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xbb, 0xa2);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x6f, 0x1c);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xbb, 0xa2);
	nt37706_switch_page(dsi_ctx, 0x08);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x6f, 0xce);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xbc, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x6f, 0xcf);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xbc,
				     0x00, 0x88, 0x00, 0xb6, 0x01, 0x11, 0x00,
				     0x44);
	nt37706_switch_page(dsi_ctx, 0x08);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x6f, 0x70);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xb9,
				     0x02, 0x00, 0x80, 0x00, 0x10, 0xa4, 0x95,
				     0xc0, 0x80, 0x83, 0x86, 0x89, 0x8d, 0x90,
				     0x93, 0x96);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x6f, 0x80);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xb9,
				     0x96, 0x80, 0x80, 0x80, 0x82, 0x84, 0x87,
				     0x89, 0x8b, 0x8b, 0x80, 0x80, 0x80, 0x80,
				     0x80, 0x82);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x6f, 0x90);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xb9,
				     0x84, 0x85, 0x87, 0x80, 0x80, 0x80, 0x80,
				     0x80, 0x80, 0x80, 0x80, 0x82, 0x80, 0x80,
				     0x80, 0x80);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x6f, 0xa0);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xb9,
				     0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x7f,
				     0x7f, 0x7e, 0x7d, 0x7c, 0x7c, 0x7b, 0x7b,
				     0x80, 0x80);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x6f, 0xb0);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xb9,
				     0x80, 0x7f, 0x7f, 0x7e, 0x7e, 0x7d, 0x7d,
				     0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x7f,
				     0x7f, 0x7e);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x6f, 0xc0);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xb9,
				     0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
				     0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
				     0x80, 0x80);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x6f, 0xd0);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xb9,
				     0x80, 0x80, 0x80, 0x8d, 0x99, 0xa6, 0xb2,
				     0xbf, 0xcb, 0xd8, 0xd8, 0x80, 0x80, 0x80,
				     0x87, 0x8e);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x6f, 0xe0);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xb9,
				     0x94, 0x9b, 0xa2, 0xa2, 0x80, 0x80, 0x80,
				     0x80, 0x80, 0x86, 0x8b, 0x91, 0x96, 0x80,
				     0x80, 0x80);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x6f, 0xf0);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xb9,
				     0x80, 0x80, 0x80, 0x80, 0x80, 0x86, 0x80,
				     0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
				     0x80);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xbe, 0x03);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x6f, 0x0c);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xe9,
				     0x18, 0xf7, 0x0f, 0x0f, 0x1c, 0x51, 0x0f,
				     0xab);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x88,
				     0x81, 0x02, 0x61, 0x09, 0x85);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0xaa, 0x55, 0xa5, 0x80);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x6f, 0x2a);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xf4, 0x08);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x6f, 0x46);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xf4, 0x07, 0x09);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x6f, 0x4a);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xf4, 0x08, 0x0a);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x6f, 0x56);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xf4, 0x44, 0x44);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0xaa, 0x55, 0xa5, 0x81);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x6f, 0x3c);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xf5, 0x84);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x17, 0x03);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x71, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x8d,
				     0x00, 0x00, 0x04, 0xc3, 0x00, 0x00, 0x0a,
				     0x97);
	mipi_dsi_dcs_set_column_address_multi(dsi_ctx, 0, 1220 - 1);
	mipi_dsi_dcs_set_page_address_multi(dsi_ctx, 0, 2712 - 1);
	/* DDIC side of the DSC configuration */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x90, 0x03, 0x43);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x91,
				     0xab, 0x28, 0x00, 0x0c, 0xc2, 0x00, 0x02,
				     0x32, 0x01, 0x31, 0x00, 0x08, 0x08, 0xbb,
				     0x07, 0x7b, 0x10, 0xf0);
	mipi_dsi_dcs_set_tear_on_multi(dsi_ctx, MIPI_DSI_DCS_TEAR_MODE_VBLANK);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, MIPI_DCS_SET_DISPLAY_BRIGHTNESS,
				     0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, MIPI_DCS_WRITE_CONTROL_DISPLAY,
				     0x20);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x57, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x2f, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, MIPI_DCS_SET_GAMMA_CURVE, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x5f, 0x00, 0x40);
	nt37706_switch_page(dsi_ctx, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xbe, 0x47, 0x45);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x6f, 0x05);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xbe, 0x28);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x6f, 0x19);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xbe, 0x10, 0x91, 0x00, 0xab);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x6f, 0x0d);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xd8, 0x02);
	nt37706_switch_page(dsi_ctx, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x6f, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xba,
				     0x00, 0x51, 0x00, 0x1d, 0xc0, 0x2b, 0x10);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x6f, 0x07);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xba,
				     0x00, 0x51, 0x00, 0x1d, 0x03, 0xd3, 0x10);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x6f, 0x10);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xba, 0x00, 0x1d, 0x00, 0x2b);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xbb,
				     0x00, 0x51, 0x00, 0x1d, 0x00, 0x2b, 0x70);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xb4, 0x0a, 0xe0);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x6f, 0x2e);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xb4, 0x0e, 0x88);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x6f, 0xb8);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xb4, 0x0a, 0xe0);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x6f, 0x13);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xc0, 0x00, 0xae);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x6f, 0x67);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xc0,
				     0x0a, 0xe0, 0x0e, 0x88, 0x15, 0xc0);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x3b,
				     0x00, 0x14, 0x00, 0x34, 0x00, 0x14, 0x03,
				     0xdc, 0x00, 0x14, 0x0b, 0x14);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x6f, 0x10);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x3b, 0x00, 0x14, 0x00, 0x34);
	nt37706_switch_page(dsi_ctx, 0x01);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xea, 0x91);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x6f, 0x08);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xea, 0x46);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x6f, 0x0b);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xea, 0x91);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x6f, 0x13);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xea, 0x70);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x6f, 0x04);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xc3, 0x0a);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x6f, 0x09);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xc3, 0x0a);
	nt37706_switch_page(dsi_ctx, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xb5,
				     0x80, 0x02, 0x16, 0x46, 0x00, 0x00, 0x29,
				     0x2c, 0x23, 0x32, 0x00, 0x00, 0x2c, 0x23,
				     0x32, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x6f, 0x10);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xb5,
				     0x00, 0x23, 0x23, 0x00, 0x00, 0x00, 0x00,
				     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				     0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x6f, 0x22);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xb5, 0x20);
	nt37706_switch_page(dsi_ctx, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x6f, 0x02);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xbe, 0x4c, 0x4b);
	nt37706_switch_page(dsi_ctx, 0x01);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xd1, 0x07, 0x04, 0x04);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x6f, 0x08);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xd1, 0x01);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x6f, 0x0b);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xd1, 0x01);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xb5, 0x20);

	mipi_dsi_dcs_exit_sleep_mode_multi(dsi_ctx);
	mipi_dsi_msleep(dsi_ctx, 120);
	mipi_dsi_dcs_set_display_on_multi(dsi_ctx);

	nt37706_switch_page(dsi_ctx, 0x04);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xcb, 0x66);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xdd,
				     0x05, 0x08, 0x1f, 0x3e, 0x9b);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x6f, 0x05);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xdd,
				     0x05, 0x08, 0x1f, 0x3e, 0x9b);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x6f, 0x0a);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xdd,
				     0x05, 0x08, 0x1f, 0x3e, 0x9b);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x6f, 0x0f);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xdd,
				     0x05, 0x08, 0x1f, 0x3e, 0x9b);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x6f, 0x14);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xdd,
				     0x05, 0x08, 0x1f, 0x3e, 0x9b);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x6f, 0x19);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xdd,
				     0x05, 0x08, 0x1f, 0x3e, 0x9b);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x6f, 0x5a);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xd2,
				     0x10, 0x10, 0x10, 0x10, 0x10, 0x14);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x6f, 0x60);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xd2,
				     0x10, 0x10, 0x10, 0x10, 0x10, 0x0c);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x6f, 0x66);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xd2,
				     0x12, 0x12, 0x12, 0x12, 0x12, 0x0c);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x6f, 0x6c);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xd2,
				     0x12, 0x12, 0x12, 0x12, 0x12, 0x0a);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x6f, 0x72);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xd2,
				     0x12, 0x12, 0x12, 0x12, 0x12, 0x08);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x6f, 0x78);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xd2,
				     0x28, 0x28, 0x28, 0x28, 0x28, 0x14);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x6f, 0x7e);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xd2,
				     0x1a, 0x1a, 0x1a, 0x1a, 0x1a, 0x10);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x6f, 0x84);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xd2,
				     0x1a, 0x1a, 0x1a, 0x1a, 0x1a, 0x10);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x6f, 0x8a);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xd2,
				     0x1a, 0x1a, 0x1a, 0x1a, 0x1a, 0x10);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x6f, 0x90);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xd2,
				     0x1a, 0x1a, 0x1a, 0x1a, 0x1a, 0x08);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x6f, 0x96);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xd2,
				     0x14, 0x14, 0x14, 0x14, 0x14, 0x10);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x6f, 0x9c);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xd2,
				     0x14, 0x14, 0x14, 0x14, 0x14, 0x08);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x6f, 0xa2);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xd2,
				     0x0d, 0x0d, 0x0d, 0x0d, 0x0d, 0x08);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x6f, 0xa8);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xd2,
				     0x0d, 0x0d, 0x0d, 0x0d, 0x0d, 0x08);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x6f, 0xae);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xd2,
				     0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x08);
	nt37706_switch_page(dsi_ctx, 0x00);
	for (u8 i = 0x80; i <= 0x88; i++) {
		mipi_dsi_dcs_write_var_seq_multi(dsi_ctx, 0xe6, i);
		mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x6f, 0xea);
		mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xe8,
					     0x10, 0x10, 0x20, 0x00, 0x00, 0x00);
	}
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xe6, 0x00);
}

static int nt37706_prepare(struct drm_panel *panel)
{
	struct nt37706 *ctx = to_nt37706(panel);
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = dsi };
	struct drm_dsc_picture_parameter_set pps;
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(nt37706_supplies), ctx->supplies);
	if (ret < 0)
		return ret;

	/* VCI needs to settle before the reset sequence */
	usleep_range(10000, 11000);

	nt37706_reset(ctx);

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;
	xiaomi_peridot_csot_init(&dsi_ctx);

	drm_dsc_pps_payload_pack(&pps, &ctx->dsc);
	mipi_dsi_picture_parameter_set_multi(&dsi_ctx, &pps);
	if (dsi_ctx.accum_err) {
		ret = dsi_ctx.accum_err;
		dev_err(&dsi->dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		regulator_bulk_disable(ARRAY_SIZE(nt37706_supplies),
				       ctx->supplies);
		return ret;
	}

	return 0;
}

static int nt37706_unprepare(struct drm_panel *panel)
{
	struct nt37706 *ctx = to_nt37706(panel);
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = dsi };

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);
	if (dsi_ctx.accum_err)
		dev_err(&dsi->dev, "Failed to un-initialize panel: %d\n",
			dsi_ctx.accum_err);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(nt37706_supplies), ctx->supplies);

	return 0;
}

static const struct drm_display_mode xiaomi_peridot_csot_mode = {
	.clock = (1220 + 10 + 16 + 80) * (2712 + 52 + 4 + 16) * 120 / 1000,
	.hdisplay = 1220,
	.hsync_start = 1220 + 10,
	.hsync_end = 1220 + 10 + 16,
	.htotal = 1220 + 10 + 16 + 80,
	.vdisplay = 2712,
	.vsync_start = 2712 + 52,
	.vsync_end = 2712 + 52 + 4,
	.vtotal = 2712 + 52 + 4 + 16,
	.width_mm = 70,
	.height_mm = 155,
	.type = DRM_MODE_TYPE_DRIVER,
};

static int nt37706_get_modes(struct drm_panel *panel,
			     struct drm_connector *connector)
{
	static const unsigned int refresh_rates[] = { 120, 90, 60 };
	const struct drm_display_mode *base = &xiaomi_peridot_csot_mode;
	struct drm_display_mode *mode;
	unsigned int extra_vfp;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(refresh_rates); i++) {
		mode = drm_mode_duplicate(connector->dev, base);
		if (!mode)
			return -ENOMEM;

		/* Keep the pixel clock fixed and extend the vertical front porch. */
		extra_vfp = base->vtotal * (120 - refresh_rates[i]) / refresh_rates[i];
		mode->vsync_start += extra_vfp;
		mode->vsync_end += extra_vfp;
		mode->vtotal += extra_vfp;
		if (!i)
			mode->type |= DRM_MODE_TYPE_PREFERRED;

		drm_mode_set_name(mode);
		drm_mode_probed_add(connector, mode);
	}

	connector->display_info.width_mm = base->width_mm;
	connector->display_info.height_mm = base->height_mm;

	return ARRAY_SIZE(refresh_rates);
}

static const struct drm_panel_funcs nt37706_panel_funcs = {
	.prepare = nt37706_prepare,
	.unprepare = nt37706_unprepare,
	.get_modes = nt37706_get_modes,
};

static int nt37706_bl_update_status(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	u16 brightness = backlight_get_brightness(bl);
	int ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_set_display_brightness_large(dsi, brightness);
	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	return ret < 0 ? ret : 0;
}

static const struct backlight_ops nt37706_bl_ops = {
	.update_status = nt37706_bl_update_status,
};

static struct backlight_device *
nt37706_create_backlight(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	const struct backlight_properties props = {
		.type = BACKLIGHT_RAW,
		.brightness = 2047,
		.max_brightness = 4095,
	};

	return devm_backlight_device_register(dev, dev_name(dev), dev, dsi,
					      &nt37706_bl_ops, &props);
}

static int nt37706_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct nt37706 *ctx;
	int ret;

	ctx = devm_drm_panel_alloc(dev, struct nt37706, panel,
				   &nt37706_panel_funcs,
				   DRM_MODE_CONNECTOR_DSI);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	ret = devm_regulator_bulk_get_const(dev, ARRAY_SIZE(nt37706_supplies),
					    nt37706_supplies, &ctx->supplies);
	if (ret < 0)
		return ret;

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
				     "Failed to get reset-gpios\n");

	ctx->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB101010;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
			  MIPI_DSI_CLOCK_NON_CONTINUOUS |
			  MIPI_DSI_MODE_DSC_ALL_SLICES_IN_PKT;

	ctx->panel.prepare_prev_first = true;
	ctx->panel.backlight = nt37706_create_backlight(dsi);
	if (IS_ERR(ctx->panel.backlight))
		return dev_err_probe(dev, PTR_ERR(ctx->panel.backlight),
				     "Failed to create backlight\n");

	/* This panel only supports DSC; unconditionally enable it */
	dsi->dsc = &ctx->dsc;
	ctx->dsc.dsc_version_major = 1;
	ctx->dsc.dsc_version_minor = 2;
	ctx->dsc.slice_height = 12;
	ctx->dsc.slice_width = 610;
	ctx->dsc.slice_count = 1220 / ctx->dsc.slice_width;
	ctx->dsc.bits_per_component = 10;
	ctx->dsc.bits_per_pixel = 8 << 4; /* 4 fractional bits */
	ctx->dsc.block_pred_enable = true;

	ret = devm_drm_panel_add(dev, &ctx->panel);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to add panel\n");

	ret = devm_mipi_dsi_attach(dev, dsi);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to attach to DSI host\n");

	return 0;
}

static const struct of_device_id nt37706_of_match[] = {
	{ .compatible = "xiaomi,peridot-csot-nt37706" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, nt37706_of_match);

static struct mipi_dsi_driver nt37706_driver = {
	.probe = nt37706_probe,
	.driver = {
		.name = "panel-novatek-nt37706",
		.of_match_table = nt37706_of_match,
	},
};
module_mipi_dsi_driver(nt37706_driver);

MODULE_AUTHOR("Yassin Rezai <yassinrsocial@proton.me>");
MODULE_DESCRIPTION("Panel driver for Novatek NT37706 based AMOLED DSI panels");
MODULE_LICENSE("GPL");
