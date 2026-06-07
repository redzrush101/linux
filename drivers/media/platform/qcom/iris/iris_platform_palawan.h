/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * Copyright (c) 2026, Alexandr Zubtsov <devizure@mainlining.org>
 */

#ifndef __IRIS_PLATFORM_PALAWAN_H__
#define __IRIS_PLATFORM_PALAWAN_H__

static struct platform_inst_caps platform_inst_cap_palawan = {
	.min_frame_width = 96,
	.max_frame_width = 4096,
	.min_frame_height = 96,
	.max_frame_height = 4096,
	.max_mbpf = (4096 * 2176) / 256,
	.mb_cycles_vpp = 200,
	.mb_cycles_fw = 489583,
	.mb_cycles_fw_vpp = 66234,
	.max_frame_rate = MAXIMUM_FPS,
	.max_operating_rate = MAXIMUM_FPS,
};

#endif
