/* Copyright (c) 2012-2015, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/msm_kgsl.h>

#include "kgsl.h"
#include "adreno.h"
#include "kgsl_sharedmem.h"
#include "kgsl_cffdump.h"
#include "a3xx_reg.h"
#include "adreno_a3xx.h"
#include "adreno_a4xx.h"
#include "a4xx_reg.h"
#include "adreno_cp_parser.h"
#include "adreno_trace.h"

/*
 * Set of registers to dump for A3XX on snapshot.
 * Registers in pairs - first value is the start offset, second
 * is the stop offset (inclusive)
 */

const unsigned int a3xx_registers[] = {
	/* RBBM REGISTERS */
	0x0000, 0x0002, 0x0010, 0x0012, 0x0018, 0x0018, 0x0020, 0x0027,
	0x0029, 0x002b, 0x002d, 0x0033, 0x0040, 0x0042, 0x0050, 0x005c,
	0x0060, 0x006c, 0x0080, 0x0082, 0x0084, 0x0088, 0x0090, 0x00e5,
	0x00ea, 0x00ed, 0x0100, 0x0100, 0x0110, 0x0123,
	/* CP REGISTERS */
	0x01c0, 0x01c1, 0x01c3, 0x01c5, 0x01c7, 0x01c7, 0x01c9, 0x01ca,
	0x01cc, 0x01cd, 0x01d4, 0x01dd, 0x01ea, 0x01ea, 0x01ec, 0x01f1,
	0x01f5, 0x01f6, 0x01f8, 0x01f9, 0x01fc, 0x01ff, 0x0440, 0x0440,
	0x0443, 0x0443, 0x0445, 0x0445, 0x044d, 0x044f, 0x0452, 0x046f,
	0x047c, 0x047c, 0x047f, 0x047f, 0x0578, 0x057f, 0x0600, 0x0602,
	0x0605, 0x0607, 0x060a, 0x060e, 0x0612, 0x0614,
	/* VSC REGISTERS */
	0x0c01, 0x0c02, 0x0c06, 0x0c1d,
	/* PC REGISTERS */
	0x0c3d, 0x0c3f, 0x0c48, 0x0c4b,
	/* GRAS REGISTERS */
	0x0c80, 0x0c81, 0x0c88, 0x0c8b, 0x0ca0, 0x0cb7,
	/* RB REGISTERS */
	0x0cc0, 0x0cc1, 0x0cc6, 0x0cc7,
	/* MARB REGISTERS */
	0x0ce4, 0x0ce5,
	/* VSC REGISTERS */
	0x0d00, 0x0d01, 0x0d24, 0x0d33,
	/* VFD REGISTERS*/
	0x0e41, 0x0e45,
	/* VPC REGISTERS */
	0x0e64, 0x0e65,
	/* UCHE REGISTERS */
	0x0e80, 0x0e82, 0x0e84, 0x0e89, 0x0ea0, 0x0ea1, 0x0ea4, 0x0ea7,
	/* SP REGISTERS */
	0x0ec0, 0x0ec2, 0x0ec4, 0x0ecb, 0x0ee0, 0x0ee0,
	/* TPL1 REGISTERS */
	0x0f00, 0x0f01, 0x0f03, 0x0f09,
	/* GRAS CTX 0 */
	0x2040, 0x2040, 0x2044, 0x2044, 0x2048, 0x204d, 0x2068, 0x2069,
	0x206c, 0x206f, 0x2070, 0x2070, 0x2072, 0x2072, 0x2074, 0x2075,
	0x2079, 0x207a,
	/* RB CTX 0 */
	0x20c0, 0x20d3, 0x20e4, 0x20ef, 0x2100, 0x2109, 0x210c, 0x210c,
	0x210e, 0x210e, 0x2110, 0x2111, 0x2114, 0x2115,
	/* PC CTX 0 */
	0x21e4, 0x21e4, 0x21ea, 0x21ea, 0x21ec, 0x21ed, 0x21f0, 0x21f0,
	/* VFD CTX 0 */
	0x2240, 0x227e,
	/* VPC CTX 0 */
	0x2280, 0x228b,
	/* SP CTX 0 */
	0x22c0, 0x22c0, 0x22c4, 0x22ce, 0x22d0, 0x22d8, 0x22df, 0x22e6,
	0x22e8, 0x22e9, 0x22ec, 0x22ec, 0x22f0, 0x22f7, 0x22ff, 0x22ff,
	/* TP CTX 0 */
	0x2340, 0x2343,
	/* GRAS CTX 1 */
	0x2440, 0x2440, 0x2444, 0x2444, 0x2448, 0x244d, 0x2468, 0x2469,
	0x246c, 0x246f, 0x2470, 0x2470, 0x2472, 0x2472, 0x2474, 0x2475,
	0x2479, 0x247a,
	/* RB CTX 1 */
	0x24c0, 0x24d3, 0x24e4, 0x24ef, 0x2500, 0x2509, 0x250c, 0x250c,
	0x250e, 0x250e, 0x2510, 0x2511, 0x2514, 0x2515,
	/* PC CTX 1 */
	0x25e4, 0x25e4, 0x25ea, 0x25ea, 0x25ec, 0x25ed, 0x25f0, 0x25f0,
	/* VFD CTX 1 */
	0x2640, 0x267e,
	/* VPC CTX 1 */
	0x2680, 0x268b,
	/* SP CTX 1 */
	0x26c0, 0x26c0, 0x26c4, 0x26ce, 0x26d0, 0x26d8, 0x26df, 0x26e6,
	0x26e8, 0x26e9, 0x26ec, 0x26ec, 0x26f0, 0x26f7, 0x26ff, 0x26ff,
	/* TP CTX 1 */
	0x2740, 0x2743,
	/* VBIF REGISTERS */
	0x300C, 0x300E, 0x301C, 0x301D, 0x302A, 0x302A, 0x302C, 0x302D,
	0x3030, 0x3031, 0x3034, 0x3036, 0x303C, 0x303C, 0x305E, 0x305F,
};

const unsigned int a3xx_registers_count = ARRAY_SIZE(a3xx_registers) / 2;

/* Removed the following HLSQ register ranges from being read during
 * fault tolerance since reading the registers may cause the device to hang:
 */
const unsigned int a3xx_hlsq_registers[] = {
	0x0e00, 0x0e05, 0x0e0c, 0x0e0c, 0x0e22, 0x0e23,
	0x2200, 0x2212, 0x2214, 0x2217, 0x221a, 0x221a,
	0x2600, 0x2612, 0x2614, 0x2617, 0x261a, 0x261a,
};

const unsigned int a3xx_hlsq_registers_count =
			ARRAY_SIZE(a3xx_hlsq_registers) / 2;

/* The set of additional registers to be dumped for A330 */

const unsigned int a330_registers[] = {
	0x1d0, 0x1d0, 0x1d4, 0x1d4, 0x453, 0x453,
};

const unsigned int a330_registers_count = ARRAY_SIZE(a330_registers) / 2;

/*
 * Define registers for a3xx that contain addresses used by the
 * cp parser logic
 */
const unsigned int a3xx_cp_addr_regs[ADRENO_CP_ADDR_MAX] = {
	ADRENO_REG_DEFINE(ADRENO_CP_ADDR_VSC_PIPE_DATA_ADDRESS_0,
				A3XX_VSC_PIPE_DATA_ADDRESS_0),
	ADRENO_REG_DEFINE(ADRENO_CP_ADDR_VSC_PIPE_DATA_LENGTH_0,
				A3XX_VSC_PIPE_DATA_LENGTH_0),
	ADRENO_REG_DEFINE(ADRENO_CP_ADDR_VSC_PIPE_DATA_ADDRESS_1,
				A3XX_VSC_PIPE_DATA_ADDRESS_1),
	ADRENO_REG_DEFINE(ADRENO_CP_ADDR_VSC_PIPE_DATA_LENGTH_1,
				A3XX_VSC_PIPE_DATA_LENGTH_1),
	ADRENO_REG_DEFINE(ADRENO_CP_ADDR_VSC_PIPE_DATA_ADDRESS_2,
				A3XX_VSC_PIPE_DATA_ADDRESS_2),
	ADRENO_REG_DEFINE(ADRENO_CP_ADDR_VSC_PIPE_DATA_LENGTH_2,
				A3XX_VSC_PIPE_DATA_LENGTH_2),
	ADRENO_REG_DEFINE(ADRENO_CP_ADDR_VSC_PIPE_DATA_ADDRESS_3,
				A3XX_VSC_PIPE_DATA_ADDRESS_3),
	ADRENO_REG_DEFINE(ADRENO_CP_ADDR_VSC_PIPE_DATA_LENGTH_3,
				A3XX_VSC_PIPE_DATA_LENGTH_3),
	ADRENO_REG_DEFINE(ADRENO_CP_ADDR_VSC_PIPE_DATA_ADDRESS_4,
				A3XX_VSC_PIPE_DATA_ADDRESS_4),
	ADRENO_REG_DEFINE(ADRENO_CP_ADDR_VSC_PIPE_DATA_LENGTH_4,
				A3XX_VSC_PIPE_DATA_LENGTH_4),
	ADRENO_REG_DEFINE(ADRENO_CP_ADDR_VSC_PIPE_DATA_ADDRESS_5,
				A3XX_VSC_PIPE_DATA_ADDRESS_5),
	ADRENO_REG_DEFINE(ADRENO_CP_ADDR_VSC_PIPE_DATA_LENGTH_5,
				A3XX_VSC_PIPE_DATA_LENGTH_5),
	ADRENO_REG_DEFINE(ADRENO_CP_ADDR_VSC_PIPE_DATA_ADDRESS_6,
				A3XX_VSC_PIPE_DATA_ADDRESS_6),
	ADRENO_REG_DEFINE(ADRENO_CP_ADDR_VSC_PIPE_DATA_LENGTH_6,
				A3XX_VSC_PIPE_DATA_LENGTH_6),
	ADRENO_REG_DEFINE(ADRENO_CP_ADDR_VSC_PIPE_DATA_ADDRESS_7,
				A3XX_VSC_PIPE_DATA_ADDRESS_7),
	ADRENO_REG_DEFINE(ADRENO_CP_ADDR_VSC_PIPE_DATA_LENGTH_7,
				A3XX_VSC_PIPE_DATA_LENGTH_7),
	ADRENO_REG_DEFINE(ADRENO_CP_ADDR_VFD_FETCH_INSTR_1_0,
				A3XX_VFD_FETCH_INSTR_1_0),
	ADRENO_REG_DEFINE(ADRENO_CP_ADDR_VFD_FETCH_INSTR_1_1,
				A3XX_VFD_FETCH_INSTR_1_1),
	ADRENO_REG_DEFINE(ADRENO_CP_ADDR_VFD_FETCH_INSTR_1_2,
				A3XX_VFD_FETCH_INSTR_1_2),
	ADRENO_REG_DEFINE(ADRENO_CP_ADDR_VFD_FETCH_INSTR_1_3,
				A3XX_VFD_FETCH_INSTR_1_3),
	ADRENO_REG_DEFINE(ADRENO_CP_ADDR_VFD_FETCH_INSTR_1_4,
				A3XX_VFD_FETCH_INSTR_1_4),
	ADRENO_REG_DEFINE(ADRENO_CP_ADDR_VFD_FETCH_INSTR_1_5,
				A3XX_VFD_FETCH_INSTR_1_5),
	ADRENO_REG_DEFINE(ADRENO_CP_ADDR_VFD_FETCH_INSTR_1_6,
				A3XX_VFD_FETCH_INSTR_1_6),
	ADRENO_REG_DEFINE(ADRENO_CP_ADDR_VFD_FETCH_INSTR_1_7,
				A3XX_VFD_FETCH_INSTR_1_7),
	ADRENO_REG_DEFINE(ADRENO_CP_ADDR_VFD_FETCH_INSTR_1_8,
				A3XX_VFD_FETCH_INSTR_1_8),
	ADRENO_REG_DEFINE(ADRENO_CP_ADDR_VFD_FETCH_INSTR_1_9,
				A3XX_VFD_FETCH_INSTR_1_9),
	ADRENO_REG_DEFINE(ADRENO_CP_ADDR_VFD_FETCH_INSTR_1_10,
				A3XX_VFD_FETCH_INSTR_1_A),
	ADRENO_REG_DEFINE(ADRENO_CP_ADDR_VFD_FETCH_INSTR_1_11,
				A3XX_VFD_FETCH_INSTR_1_B),
	ADRENO_REG_DEFINE(ADRENO_CP_ADDR_VFD_FETCH_INSTR_1_12,
				A3XX_VFD_FETCH_INSTR_1_C),
	ADRENO_REG_DEFINE(ADRENO_CP_ADDR_VFD_FETCH_INSTR_1_13,
				A3XX_VFD_FETCH_INSTR_1_D),
	ADRENO_REG_DEFINE(ADRENO_CP_ADDR_VFD_FETCH_INSTR_1_14,
				A3XX_VFD_FETCH_INSTR_1_E),
	ADRENO_REG_DEFINE(ADRENO_CP_ADDR_VFD_FETCH_INSTR_1_15,
				A3XX_VFD_FETCH_INSTR_1_F),
	ADRENO_REG_DEFINE(ADRENO_CP_ADDR_VSC_SIZE_ADDRESS,
				A3XX_VSC_SIZE_ADDRESS),
	ADRENO_REG_DEFINE(ADRENO_CP_ADDR_SP_VS_PVT_MEM_ADDR,
				A3XX_SP_VS_PVT_MEM_ADDR_REG),
	ADRENO_REG_DEFINE(ADRENO_CP_ADDR_SP_FS_PVT_MEM_ADDR,
				A3XX_SP_FS_PVT_MEM_ADDR_REG),
	ADRENO_REG_DEFINE(ADRENO_CP_ADDR_SP_VS_OBJ_START_REG,
				A3XX_SP_VS_OBJ_START_REG),
	ADRENO_REG_DEFINE(ADRENO_CP_ADDR_SP_FS_OBJ_START_REG,
				A3XX_SP_FS_OBJ_START_REG),
};

unsigned int adreno_a3xx_rbbm_clock_ctl_default(struct adreno_device
							*adreno_dev)
{
	if (adreno_is_a304(adreno_dev))
		return A304_RBBM_CLOCK_CTL_DEFAULT;
	else if (adreno_is_a305(adreno_dev))
		return A305_RBBM_CLOCK_CTL_DEFAULT;
	else if (adreno_is_a305c(adreno_dev))
		return A305C_RBBM_CLOCK_CTL_DEFAULT;
	else if (adreno_is_a306(adreno_dev))
		return A306_RBBM_CLOCK_CTL_DEFAULT;
	else if (adreno_is_a310(adreno_dev))
		return A310_RBBM_CLOCK_CTL_DEFAULT;
	else if (adreno_is_a320(adreno_dev))
		return A320_RBBM_CLOCK_CTL_DEFAULT;
	else if (adreno_is_a330v2(adreno_dev))
		return A330v2_RBBM_CLOCK_CTL_DEFAULT;
	else if (adreno_is_a330(adreno_dev))
		return A330_RBBM_CLOCK_CTL_DEFAULT;
	else if (adreno_is_a305b(adreno_dev))
		return A305B_RBBM_CLOCK_CTL_DEFAULT;

	BUG_ON(1);
}

static const unsigned int _a3xx_pwron_fixup_fs_instructions[] = {
	0x00000000, 0x302CC300, 0x00000000, 0x302CC304,
	0x00000000, 0x302CC308, 0x00000000, 0x302CC30C,
	0x00000000, 0x302CC310, 0x00000000, 0x302CC314,
	0x00000000, 0x302CC318, 0x00000000, 0x302CC31C,
	0x00000000, 0x302CC320, 0x00000000, 0x302CC324,
	0x00000000, 0x302CC328, 0x00000000, 0x302CC32C,
	0x00000000, 0x302CC330, 0x00000000, 0x302CC334,
	0x00000000, 0x302CC338, 0x00000000, 0x302CC33C,
	0x00000000, 0x00000400, 0x00020000, 0x63808003,
	0x00060004, 0x63828007, 0x000A0008, 0x6384800B,
	0x000E000C, 0x6386800F, 0x00120010, 0x63888013,
	0x00160014, 0x638A8017, 0x001A0018, 0x638C801B,
	0x001E001C, 0x638E801F, 0x00220020, 0x63908023,
	0x00260024, 0x63928027, 0x002A0028, 0x6394802B,
	0x002E002C, 0x6396802F, 0x00320030, 0x63988033,
	0x00360034, 0x639A8037, 0x003A0038, 0x639C803B,
	0x003E003C, 0x639E803F, 0x00000000, 0x00000400,
	0x00000003, 0x80D60003, 0x00000007, 0x80D60007,
	0x0000000B, 0x80D6000B, 0x0000000F, 0x80D6000F,
	0x00000013, 0x80D60013, 0x00000017, 0x80D60017,
	0x0000001B, 0x80D6001B, 0x0000001F, 0x80D6001F,
	0x00000023, 0x80D60023, 0x00000027, 0x80D60027,
	0x0000002B, 0x80D6002B, 0x0000002F, 0x80D6002F,
	0x00000033, 0x80D60033, 0x00000037, 0x80D60037,
	0x0000003B, 0x80D6003B, 0x0000003F, 0x80D6003F,
	0x00000000, 0x03000000, 0x00000000, 0x00000000,
};

/**
 * adreno_a3xx_pwron_fixup_init() - Initalize a special command buffer to run a
 * post-power collapse shader workaround
 * @adreno_dev: Pointer to a adreno_device struct
 *
 * Some targets require a special workaround shader to be executed after
 * power-collapse.  Construct the IB once at init time and keep it
 * handy
 *
 * Returns: 0 on success or negative on error
 */
int adreno_a3xx_pwron_fixup_init(struct adreno_device *adreno_dev)
{
	unsigned int *cmds;
	int count = ARRAY_SIZE(_a3xx_pwron_fixup_fs_instructions);
	int ret;

	/* Return if the fixup is already in place */
	if (test_bit(ADRENO_DEVICE_PWRON_FIXUP, &adreno_dev->priv))
		return 0;

	ret = kgsl_allocate_global(&adreno_dev->dev,
		&adreno_dev->pwron_fixup, PAGE_SIZE,
		KGSL_MEMFLAGS_GPUREADONLY, 0);

	if (ret)
		return ret;

	cmds = adreno_dev->pwron_fixup.hostptr;

	*cmds++ = cp_type0_packet(A3XX_UCHE_CACHE_INVALIDATE0_REG, 2);
	*cmds++ = 0x00000000;
	*cmds++ = 0x90000000;
	*cmds++ = cp_type3_packet(CP_WAIT_FOR_IDLE, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type3_packet(CP_REG_RMW, 3);
	*cmds++ = A3XX_RBBM_CLOCK_CTL;
	*cmds++ = 0xFFFCFFFF;
	*cmds++ = 0x00010000;
	*cmds++ = cp_type3_packet(CP_WAIT_FOR_IDLE, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_HLSQ_CONTROL_0_REG, 1);
	*cmds++ = 0x1E000150;
	*cmds++ = cp_type3_packet(CP_WAIT_FOR_IDLE, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type3_packet(CP_SET_CONSTANT, 2);
	*cmds++ = CP_REG(A3XX_HLSQ_CONTROL_0_REG);
	*cmds++ = 0x1E000150;
	*cmds++ = cp_type3_packet(CP_WAIT_FOR_IDLE, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_HLSQ_CONTROL_0_REG, 1);
	*cmds++ = 0x1E000150;
	*cmds++ = cp_type0_packet(A3XX_HLSQ_CONTROL_1_REG, 1);
	*cmds++ = 0x00000040;
	*cmds++ = cp_type0_packet(A3XX_HLSQ_CONTROL_2_REG, 1);
	*cmds++ = 0x80000000;
	*cmds++ = cp_type0_packet(A3XX_HLSQ_CONTROL_3_REG, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_HLSQ_VS_CONTROL_REG, 1);
	*cmds++ = 0x00000001;
	*cmds++ = cp_type0_packet(A3XX_HLSQ_FS_CONTROL_REG, 1);
	*cmds++ = 0x0D001002;
	*cmds++ = cp_type0_packet(A3XX_HLSQ_CONST_VSPRESV_RANGE_REG, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_HLSQ_CONST_FSPRESV_RANGE_REG, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_HLSQ_CL_NDRANGE_0_REG, 1);
	*cmds++ = 0x00401101;
	*cmds++ = cp_type0_packet(A3XX_HLSQ_CL_NDRANGE_1_REG, 1);
	*cmds++ = 0x00000400;
	*cmds++ = cp_type0_packet(A3XX_HLSQ_CL_NDRANGE_2_REG, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_HLSQ_CL_NDRANGE_3_REG, 1);
	*cmds++ = 0x00000001;
	*cmds++ = cp_type0_packet(A3XX_HLSQ_CL_NDRANGE_4_REG, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_HLSQ_CL_NDRANGE_5_REG, 1);
	*cmds++ = 0x00000001;
	*cmds++ = cp_type0_packet(A3XX_HLSQ_CL_NDRANGE_6_REG, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_HLSQ_CL_CONTROL_0_REG, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_HLSQ_CL_CONTROL_1_REG, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_HLSQ_CL_KERNEL_CONST_REG, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_HLSQ_CL_KERNEL_GROUP_X_REG, 1);
	*cmds++ = 0x00000010;
	*cmds++ = cp_type0_packet(A3XX_HLSQ_CL_KERNEL_GROUP_Y_REG, 1);
	*cmds++ = 0x00000001;
	*cmds++ = cp_type0_packet(A3XX_HLSQ_CL_KERNEL_GROUP_Z_REG, 1);
	*cmds++ = 0x00000001;
	*cmds++ = cp_type0_packet(A3XX_HLSQ_CL_WG_OFFSET_REG, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_SP_SP_CTRL_REG, 1);
	*cmds++ = 0x00040000;
	*cmds++ = cp_type0_packet(A3XX_SP_VS_CTRL_REG0, 1);
	*cmds++ = 0x0000000A;
	*cmds++ = cp_type0_packet(A3XX_SP_VS_CTRL_REG1, 1);
	*cmds++ = 0x00000001;
	*cmds++ = cp_type0_packet(A3XX_SP_VS_PARAM_REG, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_SP_VS_OUT_REG_0, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_SP_VS_OUT_REG_1, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_SP_VS_OUT_REG_2, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_SP_VS_OUT_REG_3, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_SP_VS_OUT_REG_4, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_SP_VS_OUT_REG_5, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_SP_VS_OUT_REG_6, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_SP_VS_OUT_REG_7, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_SP_VS_VPC_DST_REG_0, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_SP_VS_VPC_DST_REG_1, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_SP_VS_VPC_DST_REG_2, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_SP_VS_VPC_DST_REG_3, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_SP_VS_OBJ_OFFSET_REG, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_SP_VS_OBJ_START_REG, 1);
	*cmds++ = 0x00000004;
	*cmds++ = cp_type0_packet(A3XX_SP_VS_PVT_MEM_PARAM_REG, 1);
	*cmds++ = 0x04008001;
	*cmds++ = cp_type0_packet(A3XX_SP_VS_PVT_MEM_ADDR_REG, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_SP_VS_PVT_MEM_SIZE_REG, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_SP_VS_LENGTH_REG, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_SP_FS_CTRL_REG0, 1);
	*cmds++ = 0x0DB0400A;
	*cmds++ = cp_type0_packet(A3XX_SP_FS_CTRL_REG1, 1);
	*cmds++ = 0x00300402;
	*cmds++ = cp_type0_packet(A3XX_SP_FS_OBJ_OFFSET_REG, 1);
	*cmds++ = 0x00010000;
	*cmds++ = cp_type0_packet(A3XX_SP_FS_OBJ_START_REG, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_SP_FS_PVT_MEM_PARAM_REG, 1);
	*cmds++ = 0x04008001;
	*cmds++ = cp_type0_packet(A3XX_SP_FS_PVT_MEM_ADDR_REG, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_SP_FS_PVT_MEM_SIZE_REG, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_SP_FS_FLAT_SHAD_MODE_REG_0, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_SP_FS_FLAT_SHAD_MODE_REG_1, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_SP_FS_OUTPUT_REG, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_SP_FS_MRT_REG_0, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_SP_FS_MRT_REG_1, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_SP_FS_MRT_REG_2, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_SP_FS_MRT_REG_3, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_SP_FS_IMAGE_OUTPUT_REG_0, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_SP_FS_IMAGE_OUTPUT_REG_1, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_SP_FS_IMAGE_OUTPUT_REG_2, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_SP_FS_IMAGE_OUTPUT_REG_3, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_SP_FS_LENGTH_REG, 1);
	*cmds++ = 0x0000000D;
	*cmds++ = cp_type0_packet(A3XX_GRAS_CL_CLIP_CNTL, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_CL_GB_CLIP_ADJ, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_CL_VPORT_XOFFSET, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_CL_VPORT_XSCALE, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_CL_VPORT_YOFFSET, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_CL_VPORT_YSCALE, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_CL_VPORT_ZOFFSET, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_CL_VPORT_ZSCALE, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_CL_USER_PLANE_X0, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_CL_USER_PLANE_Y0, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_CL_USER_PLANE_Z0, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_CL_USER_PLANE_W0, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_CL_USER_PLANE_X1, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_CL_USER_PLANE_Y1, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_CL_USER_PLANE_Z1, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_CL_USER_PLANE_W1, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_CL_USER_PLANE_X2, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_CL_USER_PLANE_Y2, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_CL_USER_PLANE_Z2, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_CL_USER_PLANE_W2, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_CL_USER_PLANE_X3, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_CL_USER_PLANE_Y3, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_CL_USER_PLANE_Z3, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_CL_USER_PLANE_W3, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_CL_USER_PLANE_X4, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_CL_USER_PLANE_Y4, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_CL_USER_PLANE_Z4, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_CL_USER_PLANE_W4, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_CL_USER_PLANE_X5, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_CL_USER_PLANE_Y5, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_CL_USER_PLANE_Z5, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_CL_USER_PLANE_W5, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_SU_POINT_MINMAX, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_SU_POINT_SIZE, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_SU_POLY_OFFSET_OFFSET, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_SU_POLY_OFFSET_SCALE, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_SU_MODE_CONTROL, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_SC_CONTROL, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_SC_SCREEN_SCISSOR_TL, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_SC_SCREEN_SCISSOR_BR, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_SC_WINDOW_SCISSOR_BR, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_SC_WINDOW_SCISSOR_TL, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_TSE_DEBUG_ECO, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_PERFCOUNTER0_SELECT, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_PERFCOUNTER1_SELECT, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_PERFCOUNTER2_SELECT, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_PERFCOUNTER3_SELECT, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_MODE_CONTROL, 1);
	*cmds++ = 0x00008000;
	*cmds++ = cp_type0_packet(A3XX_RB_RENDER_CONTROL, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_MSAA_CONTROL, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_ALPHA_REFERENCE, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_MRT_CONTROL0, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_MRT_CONTROL1, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_MRT_CONTROL2, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_MRT_CONTROL3, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_MRT_BUF_INFO0, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_MRT_BUF_INFO1, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_MRT_BUF_INFO2, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_MRT_BUF_INFO3, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_MRT_BUF_BASE0, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_MRT_BUF_BASE1, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_MRT_BUF_BASE2, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_MRT_BUF_BASE3, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_MRT_BLEND_CONTROL0, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_MRT_BLEND_CONTROL1, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_MRT_BLEND_CONTROL2, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_MRT_BLEND_CONTROL3, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_BLEND_RED, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_BLEND_GREEN, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_BLEND_BLUE, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_BLEND_ALPHA, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_CLEAR_COLOR_DW0, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_CLEAR_COLOR_DW1, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_CLEAR_COLOR_DW2, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_CLEAR_COLOR_DW3, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_COPY_CONTROL, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_COPY_DEST_BASE, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_COPY_DEST_PITCH, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_COPY_DEST_INFO, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_DEPTH_CONTROL, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_DEPTH_CLEAR, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_DEPTH_BUF_INFO, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_DEPTH_BUF_PITCH, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_STENCIL_CONTROL, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_STENCIL_CLEAR, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_STENCIL_BUF_INFO, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_STENCIL_BUF_PITCH, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_STENCIL_REF_MASK, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_STENCIL_REF_MASK_BF, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_LRZ_VSC_CONTROL, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_WINDOW_OFFSET, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_SAMPLE_COUNT_CONTROL, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_SAMPLE_COUNT_ADDR, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_Z_CLAMP_MIN, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_Z_CLAMP_MAX, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_GMEM_BASE_ADDR, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_DEBUG_ECO_CONTROLS_ADDR, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_PERFCOUNTER0_SELECT, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_PERFCOUNTER1_SELECT, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_FRAME_BUFFER_DIMENSION, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type3_packet(CP_LOAD_STATE, 4);
	*cmds++ = (1 << CP_LOADSTATE_DSTOFFSET_SHIFT) |
		(0 << CP_LOADSTATE_STATESRC_SHIFT) |
		(6 << CP_LOADSTATE_STATEBLOCKID_SHIFT) |
		(1 << CP_LOADSTATE_NUMOFUNITS_SHIFT);
	*cmds++ = (1 << CP_LOADSTATE_STATETYPE_SHIFT) |
		(0 << CP_LOADSTATE_EXTSRCADDR_SHIFT);
	*cmds++ = 0x00400000;
	*cmds++ = 0x00000000;
	*cmds++ = cp_type3_packet(CP_LOAD_STATE, 4);
	*cmds++ = (2 << CP_LOADSTATE_DSTOFFSET_SHIFT) |
		(6 << CP_LOADSTATE_STATEBLOCKID_SHIFT) |
		(1 << CP_LOADSTATE_NUMOFUNITS_SHIFT);
	*cmds++ = (1 << CP_LOADSTATE_STATETYPE_SHIFT);
	*cmds++ = 0x00400220;
	*cmds++ = 0x00000000;
	*cmds++ = cp_type3_packet(CP_LOAD_STATE, 4);
	*cmds++ = (6 << CP_LOADSTATE_STATEBLOCKID_SHIFT) |
		(1 << CP_LOADSTATE_NUMOFUNITS_SHIFT);
	*cmds++ = (1 << CP_LOADSTATE_STATETYPE_SHIFT);
	*cmds++ = 0x00000000;
	*cmds++ = 0x00000000;
	*cmds++ = cp_type3_packet(CP_LOAD_STATE, 2 + count);
	*cmds++ = (6 << CP_LOADSTATE_STATEBLOCKID_SHIFT) |
		(13 << CP_LOADSTATE_NUMOFUNITS_SHIFT);
	*cmds++ = 0x00000000;

	memcpy(cmds, _a3xx_pwron_fixup_fs_instructions, count << 2);

	cmds += count;

	*cmds++ = cp_type3_packet(CP_EXEC_CL, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type3_packet(CP_WAIT_FOR_IDLE, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_HLSQ_CL_CONTROL_0_REG, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type3_packet(CP_WAIT_FOR_IDLE, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_HLSQ_CONTROL_0_REG, 1);
	*cmds++ = 0x1E000150;
	*cmds++ = cp_type3_packet(CP_WAIT_FOR_IDLE, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type3_packet(CP_SET_CONSTANT, 2);
	*cmds++ = CP_REG(A3XX_HLSQ_CONTROL_0_REG);
	*cmds++ = 0x1E000050;
	*cmds++ = cp_type3_packet(CP_WAIT_FOR_IDLE, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type3_packet(CP_REG_RMW, 3);
	*cmds++ = A3XX_RBBM_CLOCK_CTL;
	*cmds++ = 0xFFFCFFFF;
	*cmds++ = 0x00000000;
	*cmds++ = cp_type3_packet(CP_WAIT_FOR_IDLE, 1);
	*cmds++ = 0x00000000;

	/*
	 * Remember the number of dwords in the command buffer for when we
	 * program the indirect buffer call in the ringbuffer
	 */
	adreno_dev->pwron_fixup_dwords =
		(cmds - (unsigned int *) adreno_dev->pwron_fixup.hostptr);

	/* Mark the flag in ->priv to show that we have the fix */
	set_bit(ADRENO_DEVICE_PWRON_FIXUP, &adreno_dev->priv);
	return 0;
}

/*
 * a3xx_gpudev_init() - Initialize gpudev specific fields
 * @adreno_dev: Pointer to adreno device
 */
void a3xx_gpudev_init(struct adreno_device *adreno_dev)
{
	struct adreno_gpudev *gpudev;
	const struct adreno_reg_offsets *reg_offsets;

	if (adreno_is_a306(adreno_dev)) {
		gpudev = ADRENO_GPU_DEVICE(adreno_dev);
		reg_offsets = gpudev->reg_offsets;
		reg_offsets->offsets[ADRENO_REG_VBIF_XIN_HALT_CTRL0] =
			A3XX_VBIF2_XIN_HALT_CTRL0;
		reg_offsets->offsets[ADRENO_REG_VBIF_XIN_HALT_CTRL1] =
			A3XX_VBIF2_XIN_HALT_CTRL1;
		gpudev->vbif_xin_halt_ctrl0_mask =
				A3XX_VBIF2_XIN_HALT_CTRL0_MASK;
	}
}

/*
 * a3xx_rb_init() - Initialize ringbuffer
 * @adreno_dev: Pointer to adreno device
 * @rb: Pointer to the ringbuffer of device
 *
 * Submit commands for ME initialization, common function shared between
 * a3xx and a4xx devices
 */
int a3xx_rb_init(struct adreno_device *adreno_dev,
			 struct adreno_ringbuffer *rb)
{
	unsigned int *cmds;
	cmds = adreno_ringbuffer_allocspace(rb, 18);
	if (IS_ERR(cmds))
		return PTR_ERR(cmds);
	if (cmds == NULL)
		return -ENOSPC;

	*cmds++ = cp_type3_packet(CP_ME_INIT, 17);

	*cmds++ = 0x000003f7;
	*cmds++ = 0x00000000;
	*cmds++ = 0x00000000;
	*cmds++ = 0x00000000;
	*cmds++ = 0x00000080;
	*cmds++ = 0x00000100;
	*cmds++ = 0x00000180;
	*cmds++ = 0x00006600;
	*cmds++ = 0x00000150;
	*cmds++ = 0x0000014e;
	*cmds++ = 0x00000154;
	*cmds++ = 0x00000001;
	*cmds++ = 0x00000000;
	*cmds++ = 0x00000000;

	/* Enable protected mode registers for A3XX/A4XX */
	*cmds++ = 0x20000000;

	*cmds++ = 0x00000000;
	*cmds++ = 0x00000000;

	adreno_ringbuffer_submit(rb, NULL);

	return 0;
}

/*
 * a3xx_err_callback() - Call back for a3xx error interrupts
 * @adreno_dev: Pointer to device
 * @bit: Interrupt bit
 */
void a3xx_err_callback(struct adreno_device *adreno_dev, int bit)
{
	struct kgsl_device *device = &adreno_dev->dev;
	unsigned int reg;

	switch (bit) {
	case A3XX_INT_RBBM_AHB_ERROR: {
		kgsl_regread(device, A3XX_RBBM_AHB_ERROR_STATUS, &reg);

		/*
		 * Return the word address of the erroring register so that it
		 * matches the register specification
		 */
		KGSL_DRV_CRIT(device,
			"RBBM | AHB bus error | %s | addr=%x | ports=%x:%x\n",
			reg & (1 << 28) ? "WRITE" : "READ",
			(reg & 0xFFFFF) >> 2, (reg >> 20) & 0x3,
			(reg >> 24) & 0xF);

		/* Clear the error */
		kgsl_regwrite(device, A3XX_RBBM_AHB_CMD, (1 << 3));

		return;
	}
	case A3XX_INT_RBBM_ATB_BUS_OVERFLOW:
		KGSL_DRV_CRIT_RATELIMIT(device, "RBBM: ATB bus oveflow\n");
		break;
	case A3XX_INT_CP_T0_PACKET_IN_IB:
		KGSL_DRV_CRIT_RATELIMIT(device,
			"ringbuffer TO packet in IB interrupt\n");
		break;
	case A3XX_INT_CP_OPCODE_ERROR:
		KGSL_DRV_CRIT_RATELIMIT(device,
			"ringbuffer opcode error interrupt\n");
		break;
	case A3XX_INT_CP_RESERVED_BIT_ERROR:
		KGSL_DRV_CRIT_RATELIMIT(device,
			"ringbuffer reserved bit error interrupt\n");
		break;
	case A3XX_INT_CP_HW_FAULT:
		kgsl_regread(device, A3XX_CP_HW_FAULT, &reg);
		KGSL_DRV_CRIT_RATELIMIT(device,
			"CP | Ringbuffer HW fault | status=%x\n", reg);
		break;
	case A3XX_INT_CP_REG_PROTECT_FAULT:
		kgsl_regread(device, A3XX_CP_PROTECT_STATUS, &reg);
		KGSL_DRV_CRIT(device,
			"CP | Protected mode error| %s | addr=%x\n",
			reg & (1 << 24) ? "WRITE" : "READ",
			(reg & 0xFFFFF) >> 2);
		return;
	case A3XX_INT_CP_AHB_ERROR_HALT:
		KGSL_DRV_CRIT_RATELIMIT(device,
			"ringbuffer AHB error interrupt\n");
		break;
	case A3XX_INT_UCHE_OOB_ACCESS:
		KGSL_DRV_CRIT_RATELIMIT(device, "UCHE: Out of bounds access\n");
		break;
	default:
		KGSL_DRV_CRIT_RATELIMIT(device, "Unknown interrupt\n");
	}
}

static int a3xx_perfcounter_enable_pwr(struct adreno_device *adreno_dev,
	unsigned int counter)
{
	unsigned int in, out;

	if (counter > 1)
		return -EINVAL;

	kgsl_regread(&adreno_dev->dev, A3XX_RBBM_RBBM_CTL, &in);

	if (counter == 0)
		out = in | RBBM_RBBM_CTL_RESET_PWR_CTR0;
	else
		out = in | RBBM_RBBM_CTL_RESET_PWR_CTR1;

	kgsl_regwrite(&adreno_dev->dev, A3XX_RBBM_RBBM_CTL, out);

	if (counter == 0)
		out = in | RBBM_RBBM_CTL_ENABLE_PWR_CTR0;
	else
		out = in | RBBM_RBBM_CTL_ENABLE_PWR_CTR1;

	kgsl_regwrite(&adreno_dev->dev, A3XX_RBBM_RBBM_CTL, out);

	return 0;
}

static int a3xx_perfcounter_enable_vbif2(struct adreno_device *adreno_dev,
					 unsigned int counter,
					 unsigned int countable)
{
	struct kgsl_device *device = &adreno_dev->dev;
	struct adreno_perfcounters *counters = ADRENO_PERFCOUNTERS(adreno_dev);
	struct adreno_perfcount_register *reg;

	if (counters == NULL)
		return -EINVAL;

	if (counter > 3 || countable > A3XX_VBIF2_PERF_CNT_SEL_MASK)
		return -EINVAL;

	reg = &counters->groups[KGSL_PERFCOUNTER_GROUP_VBIF].regs[counter];
	/* Write 1, followed by 0 to CLR register for clearing the counter */
	kgsl_regwrite(device, reg->select - A3XX_VBIF2_PERF_CLR_REG_SEL_OFF, 1);
	kgsl_regwrite(device, reg->select - A3XX_VBIF2_PERF_CLR_REG_SEL_OFF, 0);
	kgsl_regwrite(device, reg->select, countable);
	/* enable reg is 8 DWORDS before select reg */
	kgsl_regwrite(device, reg->select - A3XX_VBIF2_PERF_EN_REG_SEL_OFF, 1);
	counters->groups[KGSL_PERFCOUNTER_GROUP_VBIF].regs[counter].value = 0;
	return 0;

}

static int a3xx_perfcounter_enable_vbif(struct adreno_device *adreno_dev,
					 unsigned int counter,
					 unsigned int countable)
{
	struct kgsl_device *device = &adreno_dev->dev;
	unsigned int in, out, bit, sel;

	if (counter > 1 || countable > 0x7f)
		return -EINVAL;

	kgsl_regread(device, A3XX_VBIF_PERF_CNT_EN, &in);
	kgsl_regread(device, A3XX_VBIF_PERF_CNT_SEL, &sel);

	if (counter == 0) {
		bit = VBIF_PERF_CNT_0;
		sel = (sel & ~VBIF_PERF_CNT_0_SEL_MASK) | countable;
	} else {
		bit = VBIF_PERF_CNT_1;
		sel = (sel & ~VBIF_PERF_CNT_1_SEL_MASK)
			| (countable << VBIF_PERF_CNT_1_SEL);
	}

	out = in | bit;

	kgsl_regwrite(device, A3XX_VBIF_PERF_CNT_SEL, sel);

	kgsl_regwrite(device, A3XX_VBIF_PERF_CNT_CLR, bit);
	kgsl_regwrite(device, A3XX_VBIF_PERF_CNT_CLR, 0);

	kgsl_regwrite(device, A3XX_VBIF_PERF_CNT_EN, out);
	return 0;
}

static int a3xx_perfcounter_enable_vbif2_pwr(struct adreno_device *adreno_dev,
					     unsigned int counter)
{
	struct kgsl_device *device = &adreno_dev->dev;
	struct adreno_perfcounters *counters = ADRENO_PERFCOUNTERS(adreno_dev);
	struct adreno_perfcount_register *reg;

	if (counters == NULL || counter > 2)
		return -EINVAL;

	reg = &counters->groups[KGSL_PERFCOUNTER_GROUP_VBIF_PWR].regs[counter];
	/* Write 1, followed by 0 to CLR register for clearing the counter */
	kgsl_regwrite(device, reg->select + A3XX_VBIF2_PERF_PWR_CLR_REG_EN_OFF,
			1);
	kgsl_regwrite(device, reg->select + A3XX_VBIF2_PERF_PWR_CLR_REG_EN_OFF,
			0);
	kgsl_regwrite(device, reg->select, 1);
	counters->groups[KGSL_PERFCOUNTER_GROUP_VBIF_PWR]
		.regs[counter].value = 0;
	return 0;
}

static int a3xx_perfcounter_enable_vbif_pwr(struct adreno_device *adreno_dev,
					     unsigned int counter)
{
	struct kgsl_device *device = &adreno_dev->dev;
	unsigned int in, out, bit;

	if (counter > 2)
		return -EINVAL;

	kgsl_regread(device, A3XX_VBIF_PERF_CNT_EN, &in);
	if (counter == 0)
		bit = VBIF_PERF_PWR_CNT_0;
	else if (counter == 1)
		bit = VBIF_PERF_PWR_CNT_1;
	else
		bit = VBIF_PERF_PWR_CNT_2;

	out = in | bit;

	kgsl_regwrite(device, A3XX_VBIF_PERF_CNT_CLR, bit);
	kgsl_regwrite(device, A3XX_VBIF_PERF_CNT_CLR, 0);

	kgsl_regwrite(device, A3XX_VBIF_PERF_CNT_EN, out);
	return 0;
}

/*
 * a3xx_perfcounter_enable - Configure a performance counter for a countable
 * @adreno_dev -  Adreno device to configure
 * @group - Desired performance counter group
 * @counter - Desired performance counter in the group
 * @countable - Desired countable
 *
 * Function is used for both a3xx and a4xx cores
 * Physically set up a counter within a group with the desired countable
 * Return 0 on success else error code
 */

int a3xx_perfcounter_enable(struct adreno_device *adreno_dev,
	unsigned int group, unsigned int counter, unsigned int countable)
{
	struct adreno_gpudev *gpudev = ADRENO_GPU_DEVICE(adreno_dev);
	struct adreno_perfcounters *counters = ADRENO_PERFCOUNTERS(adreno_dev);
	struct adreno_perfcount_register *reg;
	int i;
	int ret = 0;

	/* Special cases */
	if (group == KGSL_PERFCOUNTER_GROUP_PWR)
		return a3xx_perfcounter_enable_pwr(adreno_dev, counter);

	if (group == KGSL_PERFCOUNTER_GROUP_VBIF) {
		if (adreno_is_a4xx(adreno_dev))
			return a4xx_perfcounter_enable_vbif(adreno_dev, counter,
								countable);
		else if (adreno_is_a306(adreno_dev) ||
			adreno_is_a304(adreno_dev))
			return a3xx_perfcounter_enable_vbif2(adreno_dev,
								counter,
								countable);
		else
			return a3xx_perfcounter_enable_vbif(adreno_dev, counter,
								countable);
	}

	if (group == KGSL_PERFCOUNTER_GROUP_VBIF_PWR) {
		if (adreno_is_a4xx(adreno_dev))
			return a4xx_perfcounter_enable_vbif_pwr(adreno_dev,
								counter);
		else if (adreno_is_a306(adreno_dev) ||
			adreno_is_a304(adreno_dev))
			return a3xx_perfcounter_enable_vbif2_pwr(adreno_dev,
								counter);
		else
			return a3xx_perfcounter_enable_vbif_pwr(adreno_dev,
								counter);
	}

	if (counters == NULL || group >= counters->group_count)
		return -EINVAL;

	if ((0 == counters->groups[group].reg_count) ||
		(counter >= counters->groups[group].reg_count))
		return -EINVAL;

	/*
	 * check whether the countable is valid or not by matching it against
	 * the list on invalid countables
	 */
	if (gpudev->invalid_countables) {
		struct adreno_invalid_countables invalid_countable =
			gpudev->invalid_countables[group];
		for (i = 0; i < invalid_countable.num_countables; i++)
			if (countable == invalid_countable.countables[i])
				return -EACCES;
	}
	reg = &(counters->groups[group].regs[counter]);

	if (test_bit(ADRENO_DEVICE_STARTED, &adreno_dev->priv)) {
		struct adreno_ringbuffer *rb = &adreno_dev->ringbuffers[0];
		unsigned int cmds[4];
		int ret;

		cmds[0] = cp_type3_packet(CP_WAIT_FOR_IDLE, 1);
		cmds[1] = 0;
		cmds[2] = cp_type0_packet(reg->select, 1);
		cmds[3] = countable;
		/* submit to highest priority RB always */
		ret = adreno_ringbuffer_issuecmds(rb, 0, cmds, 4);
		if (ret)
			goto done;
		/* wait for the above commands submitted to complete */
		ret = adreno_ringbuffer_waittimestamp(rb, rb->timestamp,
				ADRENO_IDLE_TIMEOUT);
		if (ret)
			KGSL_DRV_ERR(rb->device,
			"Perfcounter %u/%u/%u start via commands failed %d\n",
			group, counter, countable, ret);
	} else {
		/* Select the desired perfcounter */
		kgsl_regwrite(&adreno_dev->dev, reg->select, countable);
	}
done:
	if (!ret)
		counters->groups[group].regs[counter].value = 0;
	return 0;
}

/*
 * a3xx_perfcounter_read_pwr() - Read power counter value
 * @adreno_dev: Device onwhich counter is running
 * @counter: The counter to read in power counter group
 *
 * Function is used for reading both a3xx and a4xx power counter
 * Returns the counter value on success else 0
 */
static uint64_t a3xx_perfcounter_read_pwr(struct adreno_device *adreno_dev,
				unsigned int counter)
{
	struct kgsl_device *device = &adreno_dev->dev;
	struct adreno_perfcounters *counters = ADRENO_PERFCOUNTERS(adreno_dev);
	struct adreno_perfcount_register *reg;
	unsigned int in = 0, out, lo = 0, hi = 0;
	unsigned int enable_bit;

	if (counters == NULL || counter > 1)
		return 0;

	if (adreno_is_a3xx(adreno_dev)) {
		if (0 == counter)
			enable_bit = RBBM_RBBM_CTL_ENABLE_PWR_CTR0;
		else
			enable_bit = RBBM_RBBM_CTL_ENABLE_PWR_CTR1;
		/* freeze counter */
		adreno_readreg(adreno_dev, ADRENO_REG_RBBM_RBBM_CTL, &in);
		out = (in & ~enable_bit);
		adreno_writereg(adreno_dev, ADRENO_REG_RBBM_RBBM_CTL, out);
	}

	reg = &counters->groups[KGSL_PERFCOUNTER_GROUP_PWR].regs[counter];
	kgsl_regread(device, reg->offset, &lo);
	kgsl_regread(device, reg->offset_hi, &hi);

	/* restore the counter control value */
	if (adreno_is_a3xx(adreno_dev))
		adreno_writereg(adreno_dev, ADRENO_REG_RBBM_RBBM_CTL, in);

	return ((((uint64_t) hi) << 32) | lo)
		+ counters->groups[KGSL_PERFCOUNTER_GROUP_PWR]
				.regs[counter].value;
}

static uint64_t a3xx_perfcounter_read_vbif2(struct adreno_device *adreno_dev,
				unsigned int counter)
{
	struct kgsl_device *device = &adreno_dev->dev;
	struct adreno_perfcounters *counters = ADRENO_PERFCOUNTERS(adreno_dev);
	struct adreno_perfcount_register *reg;
	unsigned int lo = 0, hi = 0;

	if (counters == NULL || counter > 3)
		return 0;

	reg = &counters->groups[KGSL_PERFCOUNTER_GROUP_VBIF].regs[counter];

	/* freeze counter */
	kgsl_regwrite(device, reg->select - A3XX_VBIF2_PERF_EN_REG_SEL_OFF, 0);
	kgsl_regread(device, reg->offset, &lo);
	kgsl_regread(device, reg->offset_hi, &hi);

	/* un-freeze counter */
	kgsl_regwrite(device, reg->select - A3XX_VBIF2_PERF_EN_REG_SEL_OFF, 1);

	return ((((uint64_t) hi) << 32) | lo)
		+ counters->groups[KGSL_PERFCOUNTER_GROUP_VBIF]
			.regs[counter].value;
}


static uint64_t a3xx_perfcounter_read_vbif(struct adreno_device *adreno_dev,
				unsigned int counter)
{
	struct adreno_perfcounters *counters = ADRENO_PERFCOUNTERS(adreno_dev);
	struct kgsl_device *device = &adreno_dev->dev;
	struct adreno_perfcount_register *reg;
	unsigned int in, out, lo = 0, hi = 0;

	if (counters == NULL || counter > 1)
		return 0;

	/* freeze counter */
	kgsl_regread(device, A3XX_VBIF_PERF_CNT_EN, &in);
	if (counter == 0)
		out = (in & ~VBIF_PERF_CNT_0);
	else
		out = (in & ~VBIF_PERF_CNT_1);
	kgsl_regwrite(device, A3XX_VBIF_PERF_CNT_EN, out);

	reg = &counters->groups[KGSL_PERFCOUNTER_GROUP_VBIF].regs[counter];
	kgsl_regread(device, reg->offset, &lo);
	kgsl_regread(device, reg->offset_hi, &hi);

	/* restore the perfcounter value */
	kgsl_regwrite(device, A3XX_VBIF_PERF_CNT_EN, in);

	return ((((uint64_t) hi) << 32) | lo)
		+ counters->groups[KGSL_PERFCOUNTER_GROUP_VBIF]
				.regs[counter].value;
}

static uint64_t
	a3xx_perfcounter_read_vbif2_pwr(struct adreno_device *adreno_dev,
				unsigned int counter)
{
	struct kgsl_device *device = &adreno_dev->dev;
	struct adreno_perfcounters *counters = ADRENO_PERFCOUNTERS(adreno_dev);
	struct adreno_perfcount_register *reg;
	unsigned int lo = 0, hi = 0;

	 if (counters == NULL || counter > 2)
		return 0;

	reg = &counters->groups[KGSL_PERFCOUNTER_GROUP_VBIF_PWR].regs[counter];

	/* freeze counter */
	kgsl_regwrite(device, reg->select, 0);
	kgsl_regread(device, reg->offset, &lo);
	kgsl_regread(device, reg->offset_hi, &hi);

	/* un-freeze counter */
	kgsl_regwrite(device, reg->select, 1);

	return ((((uint64_t) hi) << 32) | lo)
		+ counters->groups[KGSL_PERFCOUNTER_GROUP_VBIF_PWR]
			.regs[counter].value;
}

static uint64_t a3xx_perfcounter_read_vbif_pwr(struct adreno_device *adreno_dev,
				unsigned int counter)
{
	struct adreno_perfcounters *counters = ADRENO_PERFCOUNTERS(adreno_dev);
	struct kgsl_device *device = &adreno_dev->dev;
	struct adreno_perfcount_register *reg;
	unsigned int in, out, lo = 0, hi = 0;

	if (counters == NULL || counter > 2)
		return 0;

	/* freeze counter */
	kgsl_regread(device, A3XX_VBIF_PERF_CNT_EN, &in);
	if (0 == counter)
		out = (in & ~VBIF_PERF_PWR_CNT_0);
	else
		out = (in & ~VBIF_PERF_PWR_CNT_2);
	kgsl_regwrite(device, A3XX_VBIF_PERF_CNT_EN, out);

	reg = &counters->groups[KGSL_PERFCOUNTER_GROUP_VBIF_PWR].regs[counter];
	kgsl_regread(device, reg->offset, &lo);
	kgsl_regread(device, reg->offset_hi , &hi);
	/* restore the perfcounter value */
	kgsl_regwrite(device, A3XX_VBIF_PERF_CNT_EN, in);

	return ((((uint64_t) hi) << 32) | lo)
		+ counters->groups[KGSL_PERFCOUNTER_GROUP_VBIF_PWR]
				.regs[counter].value;
}

/*
 * a3xx_perfcounter_read() - Reads a performance counter
 * @adreno_dev: The device on which the counter is running
 * @group: The group of the counter
 * @counter: The counter within the group
 *
 * Function is used to read the counter of both a3xx and a4xx devices
 * Returns the 64 bit counter value on success else 0.
 */
uint64_t a3xx_perfcounter_read(struct adreno_device *adreno_dev,
	unsigned int group, unsigned int counter)
{
	struct adreno_perfcounters *counters = ADRENO_PERFCOUNTERS(adreno_dev);
	struct kgsl_device *device = &adreno_dev->dev;
	struct adreno_perfcount_register *reg;
	unsigned int lo = 0, hi = 0;
	unsigned int in = 0, out;

	if (group == KGSL_PERFCOUNTER_GROUP_VBIF_PWR) {
		if (adreno_is_a4xx(adreno_dev))
			return a4xx_perfcounter_read_vbif_pwr(adreno_dev,
								counter);
		else if (adreno_is_a306(adreno_dev) ||
			adreno_is_a304(adreno_dev))
			return a3xx_perfcounter_read_vbif2_pwr(adreno_dev,
								counter);
		else
			return a3xx_perfcounter_read_vbif_pwr(adreno_dev,
								counter);
	}

	if (group == KGSL_PERFCOUNTER_GROUP_VBIF) {
		if (adreno_is_a4xx(adreno_dev))
			return a4xx_perfcounter_read_vbif(adreno_dev,
							counter);
		else if (adreno_is_a306(adreno_dev) ||
			adreno_is_a304(adreno_dev))
			return a3xx_perfcounter_read_vbif2(adreno_dev,
							counter);
		else
			return a3xx_perfcounter_read_vbif(adreno_dev,
							counter);
	}

	if (group == KGSL_PERFCOUNTER_GROUP_PWR)
		return a3xx_perfcounter_read_pwr(adreno_dev, counter);

	if (counters == NULL || group >= counters->group_count)
		return 0;

	if ((0 == counters->groups[group].reg_count) ||
		(counter >= counters->groups[group].reg_count))
		return 0;

	reg = &(counters->groups[group].regs[counter]);

	/* Freeze the counter */
	if (adreno_is_a3xx(adreno_dev)) {
		kgsl_regread(device, A3XX_RBBM_PERFCTR_CTL, &in);
		out = in & ~RBBM_PERFCTR_CTL_ENABLE;
		kgsl_regwrite(device, A3XX_RBBM_PERFCTR_CTL, out);
	}

	/* Read the values */
	kgsl_regread(device, reg->offset, &lo);
	kgsl_regread(device, reg->offset_hi, &hi);

	/* Re-Enable the counter */
	if (adreno_is_a3xx(adreno_dev))
		kgsl_regwrite(device, A3XX_RBBM_PERFCTR_CTL, in);
	return (((uint64_t) hi) << 32) | lo;
}

#define A3XX_INT_MASK \
	((1 << A3XX_INT_RBBM_AHB_ERROR) |        \
	 (1 << A3XX_INT_RBBM_ATB_BUS_OVERFLOW) | \
	 (1 << A3XX_INT_CP_T0_PACKET_IN_IB) |    \
	 (1 << A3XX_INT_CP_OPCODE_ERROR) |       \
	 (1 << A3XX_INT_CP_RESERVED_BIT_ERROR) | \
	 (1 << A3XX_INT_CP_HW_FAULT) |           \
	 (1 << A3XX_INT_CP_IB1_INT) |            \
	 (1 << A3XX_INT_CP_IB2_INT) |            \
	 (1 << A3XX_INT_CP_RB_INT) |             \
	 (1 << A3XX_INT_CACHE_FLUSH_TS) |	 \
	 (1 << A3XX_INT_CP_REG_PROTECT_FAULT) |  \
	 (1 << A3XX_INT_CP_AHB_ERROR_HALT) |     \
	 (1 << A3XX_INT_UCHE_OOB_ACCESS))

static struct adreno_irq_funcs a3xx_irq_funcs[] = {
	ADRENO_IRQ_CALLBACK(NULL),                    /* 0 - RBBM_GPU_IDLE */
	ADRENO_IRQ_CALLBACK(a3xx_err_callback),  /* 1 - RBBM_AHB_ERROR */
	ADRENO_IRQ_CALLBACK(NULL),  /* 2 - RBBM_REG_TIMEOUT */
	ADRENO_IRQ_CALLBACK(NULL),  /* 3 - RBBM_ME_MS_TIMEOUT */
	ADRENO_IRQ_CALLBACK(NULL),  /* 4 - RBBM_PFP_MS_TIMEOUT */
	ADRENO_IRQ_CALLBACK(a3xx_err_callback),  /* 5 - RBBM_ATB_BUS_OVERFLOW */
	ADRENO_IRQ_CALLBACK(NULL),  /* 6 - RBBM_VFD_ERROR */
	ADRENO_IRQ_CALLBACK(NULL),	/* 7 - CP_SW */
	ADRENO_IRQ_CALLBACK(a3xx_err_callback),  /* 8 - CP_T0_PACKET_IN_IB */
	ADRENO_IRQ_CALLBACK(a3xx_err_callback),  /* 9 - CP_OPCODE_ERROR */
	/* 10 - CP_RESERVED_BIT_ERROR */
	ADRENO_IRQ_CALLBACK(a3xx_err_callback),
	ADRENO_IRQ_CALLBACK(a3xx_err_callback),  /* 11 - CP_HW_FAULT */
	ADRENO_IRQ_CALLBACK(NULL),	             /* 12 - CP_DMA */
	ADRENO_IRQ_CALLBACK(adreno_cp_callback),   /* 13 - CP_IB2_INT */
	ADRENO_IRQ_CALLBACK(adreno_cp_callback),   /* 14 - CP_IB1_INT */
	ADRENO_IRQ_CALLBACK(adreno_cp_callback),   /* 15 - CP_RB_INT */
	/* 16 - CP_REG_PROTECT_FAULT */
	ADRENO_IRQ_CALLBACK(a3xx_err_callback),
	ADRENO_IRQ_CALLBACK(NULL),	       /* 17 - CP_RB_DONE_TS */
	ADRENO_IRQ_CALLBACK(NULL),	       /* 18 - CP_VS_DONE_TS */
	ADRENO_IRQ_CALLBACK(NULL),	       /* 19 - CP_PS_DONE_TS */
	ADRENO_IRQ_CALLBACK(adreno_cp_callback), /* 20 - CP_CACHE_FLUSH_TS */
	/* 21 - CP_AHB_ERROR_FAULT */
	ADRENO_IRQ_CALLBACK(a3xx_err_callback),
	ADRENO_IRQ_CALLBACK(NULL),	       /* 22 - Unused */
	ADRENO_IRQ_CALLBACK(NULL),	       /* 23 - Unused */
	/* 24 - MISC_HANG_DETECT */
	ADRENO_IRQ_CALLBACK(adreno_hang_int_callback),
	ADRENO_IRQ_CALLBACK(a3xx_err_callback),  /* 25 - UCHE_OOB_ACCESS */
	/* 26 to 31 - Unused */
};

static struct adreno_irq a3xx_irq = {
	.funcs = a3xx_irq_funcs,
	.funcs_count = ARRAY_SIZE(a3xx_irq_funcs),
	.mask = A3XX_INT_MASK,
};

static unsigned int counter_delta(struct adreno_device *adreno_dev,
			unsigned int reg, unsigned int *counter)
{
	struct kgsl_device *device = &adreno_dev->dev;
	unsigned int val;
	unsigned int ret = 0;

	/* Read the value */
	if (reg == ADRENO_REG_RBBM_PERFCTR_PWR_1_LO)
		adreno_readreg(adreno_dev, reg, &val);
	else
		kgsl_regread(device, reg, &val);

	/* Return 0 for the first read */
	if (*counter != 0) {
		if (val < *counter)
			ret = (0xFFFFFFFF - *counter) + val;
		else
			ret = val - *counter;
	}

	*counter = val;
	return ret;
}

/*
 * a3xx_busy_cycles() - Returns number of gpu cycles
 * @adreno_dev: Pointer to device ehose cycles are checked
 *
 * Returns number of busy cycles since the last time this function is called
 * Function is common between a3xx and a4xx devices
 */
void a3xx_busy_cycles(struct adreno_device *adreno_dev,
				struct adreno_busy_data *data)
{
	struct kgsl_device *device = &adreno_dev->dev;
	struct adreno_busy_data *busy = &adreno_dev->busy_data;

	memset(data, 0, sizeof(*data));

	data->gpu_busy = counter_delta(adreno_dev,
					ADRENO_REG_RBBM_PERFCTR_PWR_1_LO,
					&busy->gpu_busy);
	if (device->pwrctrl.bus_control) {
		data->vbif_ram_cycles = counter_delta(adreno_dev,
					adreno_dev->ram_cycles_lo,
					&busy->vbif_ram_cycles);
		data->vbif_starved_ram = counter_delta(adreno_dev,
					adreno_dev->starved_ram_lo,
					&busy->vbif_starved_ram);
	}
}

/* VBIF registers start after 0x3000 so use 0x0 as end of list marker */
static const struct adreno_vbif_data a304_vbif[] = {
	{ A3XX_VBIF_ROUND_ROBIN_QOS_ARB, 0x0003 },
	{0, 0},
};

static const struct adreno_vbif_data a305_vbif[] = {
	/* Set up 16 deep read/write request queues */
	{ A3XX_VBIF_IN_RD_LIM_CONF0, 0x10101010 },
	{ A3XX_VBIF_IN_RD_LIM_CONF1, 0x10101010 },
	{ A3XX_VBIF_OUT_RD_LIM_CONF0, 0x10101010 },
	{ A3XX_VBIF_OUT_WR_LIM_CONF0, 0x10101010 },
	{ A3XX_VBIF_DDR_OUT_MAX_BURST, 0x0000303 },
	{ A3XX_VBIF_IN_WR_LIM_CONF0, 0x10101010 },
	{ A3XX_VBIF_IN_WR_LIM_CONF1, 0x10101010 },
	/* Enable WR-REQ */
	{ A3XX_VBIF_GATE_OFF_WRREQ_EN, 0x0000FF },
	/* Set up round robin arbitration between both AXI ports */
	{ A3XX_VBIF_ARB_CTL, 0x00000030 },
	/* Set up AOOO */
	{ A3XX_VBIF_OUT_AXI_AOOO_EN, 0x0000003C },
	{ A3XX_VBIF_OUT_AXI_AOOO, 0x003C003C },
	{0, 0},
};

static const struct adreno_vbif_data a305b_vbif[] = {
	{ A3XX_VBIF_IN_RD_LIM_CONF0, 0x00181818 },
	{ A3XX_VBIF_IN_WR_LIM_CONF0, 0x00181818 },
	{ A3XX_VBIF_OUT_RD_LIM_CONF0, 0x00000018 },
	{ A3XX_VBIF_OUT_WR_LIM_CONF0, 0x00000018 },
	{ A3XX_VBIF_DDR_OUT_MAX_BURST, 0x00000303 },
	{ A3XX_VBIF_ROUND_ROBIN_QOS_ARB, 0x0003 },
	{0, 0},
};

static const struct adreno_vbif_data a305c_vbif[] = {
	{ A3XX_VBIF_IN_RD_LIM_CONF0, 0x00101010 },
	{ A3XX_VBIF_IN_WR_LIM_CONF0, 0x00101010 },
	{ A3XX_VBIF_OUT_RD_LIM_CONF0, 0x00000010 },
	{ A3XX_VBIF_OUT_WR_LIM_CONF0, 0x00000010 },
	{ A3XX_VBIF_DDR_OUT_MAX_BURST, 0x00000101 },
	{ A3XX_VBIF_ARB_CTL, 0x00000010 },
	/* Set up AOOO */
	{ A3XX_VBIF_OUT_AXI_AOOO_EN, 0x00000007 },
	{ A3XX_VBIF_OUT_AXI_AOOO, 0x00070007 },
	{0, 0},
};

static const struct adreno_vbif_data a306_vbif[] = {
	{ A3XX_VBIF_ROUND_ROBIN_QOS_ARB, 0x0003 },
	{ A3XX_VBIF_OUT_RD_LIM_CONF0, 0x0000000A },
	{ A3XX_VBIF_OUT_WR_LIM_CONF0, 0x0000000A },
	{0, 0},
};

static const struct adreno_vbif_data a310_vbif[] = {
	{ A3XX_VBIF_ABIT_SORT, 0x0001000F },
	{ A3XX_VBIF_ABIT_SORT_CONF, 0x000000A4 },
	/* Enable WR-REQ */
	{ A3XX_VBIF_GATE_OFF_WRREQ_EN, 0x00000001 },
	/* Set up VBIF_ROUND_ROBIN_QOS_ARB */
	{ A3XX_VBIF_ROUND_ROBIN_QOS_ARB, 0x3 },
	{ A3XX_VBIF_IN_RD_LIM_CONF0, 0x18180C0C },
	{ A3XX_VBIF_IN_WR_LIM_CONF0, 0x1818000C },
	{0, 0},
};

static const struct adreno_vbif_data a320_vbif[] = {
	/* Set up 16 deep read/write request queues */
	{ A3XX_VBIF_IN_RD_LIM_CONF0, 0x10101010 },
	{ A3XX_VBIF_IN_RD_LIM_CONF1, 0x10101010 },
	{ A3XX_VBIF_OUT_RD_LIM_CONF0, 0x10101010 },
	{ A3XX_VBIF_OUT_WR_LIM_CONF0, 0x10101010 },
	{ A3XX_VBIF_DDR_OUT_MAX_BURST, 0x0000303 },
	{ A3XX_VBIF_IN_WR_LIM_CONF0, 0x10101010 },
	{ A3XX_VBIF_IN_WR_LIM_CONF1, 0x10101010 },
	/* Enable WR-REQ */
	{ A3XX_VBIF_GATE_OFF_WRREQ_EN, 0x0000FF },
	/* Set up round robin arbitration between both AXI ports */
	{ A3XX_VBIF_ARB_CTL, 0x00000030 },
	/* Set up AOOO */
	{ A3XX_VBIF_OUT_AXI_AOOO_EN, 0x0000003C },
	{ A3XX_VBIF_OUT_AXI_AOOO, 0x003C003C },
	/* Enable 1K sort */
	{ A3XX_VBIF_ABIT_SORT, 0x000000FF },
	{ A3XX_VBIF_ABIT_SORT_CONF, 0x000000A4 },
	{0, 0},
};

static const struct adreno_vbif_data a330_vbif[] = {
	/* Set up 16 deep read/write request queues */
	{ A3XX_VBIF_IN_RD_LIM_CONF0, 0x18181818 },
	{ A3XX_VBIF_IN_RD_LIM_CONF1, 0x00001818 },
	{ A3XX_VBIF_OUT_RD_LIM_CONF0, 0x00001818 },
	{ A3XX_VBIF_OUT_WR_LIM_CONF0, 0x00001818 },
	{ A3XX_VBIF_DDR_OUT_MAX_BURST, 0x0000303 },
	{ A3XX_VBIF_IN_WR_LIM_CONF0, 0x18181818 },
	{ A3XX_VBIF_IN_WR_LIM_CONF1, 0x00001818 },
	/* Enable WR-REQ */
	{ A3XX_VBIF_GATE_OFF_WRREQ_EN, 0x00003F },
	/* Set up round robin arbitration between both AXI ports */
	{ A3XX_VBIF_ARB_CTL, 0x00000030 },
	/* Set up VBIF_ROUND_ROBIN_QOS_ARB */
	{ A3XX_VBIF_ROUND_ROBIN_QOS_ARB, 0x0001 },
	/* Set up AOOO */
	{ A3XX_VBIF_OUT_AXI_AOOO_EN, 0x0000003F },
	{ A3XX_VBIF_OUT_AXI_AOOO, 0x003F003F },
	/* Enable 1K sort */
	{ A3XX_VBIF_ABIT_SORT, 0x0001003F },
	{ A3XX_VBIF_ABIT_SORT_CONF, 0x000000A4 },
	/* Disable VBIF clock gating. This is to enable AXI running
	 * higher frequency than GPU.
	 */
	{ A3XX_VBIF_CLKON, 1 },
	{0, 0},
};

/*
 * Most of the VBIF registers on 8974v2 have the correct values at power on, so
 * we won't modify those if we don't need to
 */
static const struct adreno_vbif_data a330v2_vbif[] = {
	/* Enable 1k sort */
	{ A3XX_VBIF_ABIT_SORT, 0x0001003F },
	{ A3XX_VBIF_ABIT_SORT_CONF, 0x000000A4 },
	/* Enable WR-REQ */
	{ A3XX_VBIF_GATE_OFF_WRREQ_EN, 0x00003F },
	{ A3XX_VBIF_DDR_OUT_MAX_BURST, 0x0000303 },
	/* Set up VBIF_ROUND_ROBIN_QOS_ARB */
	{ A3XX_VBIF_ROUND_ROBIN_QOS_ARB, 0x0003 },
	{0, 0},
};

/*
 * Most of the VBIF registers on a330v2.1 have the correct values at power on,
 * so we won't modify those if we don't need to
 */
static const struct adreno_vbif_data a330v21_vbif[] = {
	/* Enable WR-REQ */
	{ A3XX_VBIF_GATE_OFF_WRREQ_EN, 0x1 },
	/* Set up VBIF_ROUND_ROBIN_QOS_ARB */
	{ A3XX_VBIF_ROUND_ROBIN_QOS_ARB, 0x0003 },
	{ A3XX_VBIF_IN_RD_LIM_CONF0, 0x18180c0c },
	{0, 0},
};

static const struct adreno_vbif_platform a3xx_vbif_platforms[] = {
	{ adreno_is_a304, a304_vbif },
	{ adreno_is_a305, a305_vbif },
	{ adreno_is_a305c, a305c_vbif },
	{ adreno_is_a306, a306_vbif },
	{ adreno_is_a310, a310_vbif },
	{ adreno_is_a320, a320_vbif },
	/* A330v2.1 needs to be ahead of A330v2 so the right device matches */
	{ adreno_is_a330v21, a330v21_vbif},
	/* A330v2 needs to be ahead of A330 so the right device matches */
	{ adreno_is_a330v2, a330v2_vbif },
	{ adreno_is_a330, a330_vbif },
	{ adreno_is_a305b, a305b_vbif },
};

/*
 * Define the available perfcounter groups - these get used by
 * adreno_perfcounter_get and adreno_perfcounter_put
 */

static struct adreno_perfcount_register a3xx_perfcounters_cp[] = {
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_CP_0_LO,
		A3XX_RBBM_PERFCTR_CP_0_HI, 0, A3XX_CP_PERFCOUNTER_SELECT },
};

static struct adreno_perfcount_register a3xx_perfcounters_rbbm[] = {
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_RBBM_0_LO,
		A3XX_RBBM_PERFCTR_RBBM_0_HI, 1, A3XX_RBBM_PERFCOUNTER0_SELECT },
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_RBBM_1_LO,
		A3XX_RBBM_PERFCTR_RBBM_1_HI, 2, A3XX_RBBM_PERFCOUNTER1_SELECT },
};

static struct adreno_perfcount_register a3xx_perfcounters_pc[] = {
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_PC_0_LO,
		A3XX_RBBM_PERFCTR_PC_0_HI, 3, A3XX_PC_PERFCOUNTER0_SELECT },
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_PC_1_LO,
		A3XX_RBBM_PERFCTR_PC_1_HI, 4, A3XX_PC_PERFCOUNTER1_SELECT },
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_PC_2_LO,
		A3XX_RBBM_PERFCTR_PC_2_HI, 5, A3XX_PC_PERFCOUNTER2_SELECT },
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_PC_3_LO,
		A3XX_RBBM_PERFCTR_PC_3_HI, 6, A3XX_PC_PERFCOUNTER3_SELECT },
};

static struct adreno_perfcount_register a3xx_perfcounters_vfd[] = {
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_VFD_0_LO,
		A3XX_RBBM_PERFCTR_VFD_0_HI, 7, A3XX_VFD_PERFCOUNTER0_SELECT },
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_VFD_1_LO,
		A3XX_RBBM_PERFCTR_VFD_1_HI, 8, A3XX_VFD_PERFCOUNTER1_SELECT },
};

static struct adreno_perfcount_register a3xx_perfcounters_hlsq[] = {
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_HLSQ_0_LO,
		A3XX_RBBM_PERFCTR_HLSQ_0_HI, 9,
		A3XX_HLSQ_PERFCOUNTER0_SELECT },
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_HLSQ_1_LO,
		A3XX_RBBM_PERFCTR_HLSQ_1_HI, 10,
		A3XX_HLSQ_PERFCOUNTER1_SELECT },
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_HLSQ_2_LO,
		A3XX_RBBM_PERFCTR_HLSQ_2_HI, 11,
		A3XX_HLSQ_PERFCOUNTER2_SELECT },
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_HLSQ_3_LO,
		A3XX_RBBM_PERFCTR_HLSQ_3_HI, 12,
		A3XX_HLSQ_PERFCOUNTER3_SELECT },
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_HLSQ_4_LO,
		A3XX_RBBM_PERFCTR_HLSQ_4_HI, 13,
		A3XX_HLSQ_PERFCOUNTER4_SELECT },
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_HLSQ_5_LO,
		A3XX_RBBM_PERFCTR_HLSQ_5_HI, 14,
		A3XX_HLSQ_PERFCOUNTER5_SELECT },
};

static struct adreno_perfcount_register a3xx_perfcounters_vpc[] = {
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_VPC_0_LO,
		A3XX_RBBM_PERFCTR_VPC_0_HI, 15, A3XX_VPC_PERFCOUNTER0_SELECT },
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_VPC_1_LO,
		A3XX_RBBM_PERFCTR_VPC_1_HI, 16, A3XX_VPC_PERFCOUNTER1_SELECT },
};

static struct adreno_perfcount_register a3xx_perfcounters_tse[] = {
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_TSE_0_LO,
		A3XX_RBBM_PERFCTR_TSE_0_HI, 17, A3XX_GRAS_PERFCOUNTER0_SELECT },
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_TSE_1_LO,
		A3XX_RBBM_PERFCTR_TSE_1_HI, 18, A3XX_GRAS_PERFCOUNTER1_SELECT },
};

static struct adreno_perfcount_register a3xx_perfcounters_ras[] = {
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_RAS_0_LO,
		A3XX_RBBM_PERFCTR_RAS_0_HI, 19, A3XX_GRAS_PERFCOUNTER2_SELECT },
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_RAS_1_LO,
		A3XX_RBBM_PERFCTR_RAS_1_HI, 20, A3XX_GRAS_PERFCOUNTER3_SELECT },
};

static struct adreno_perfcount_register a3xx_perfcounters_uche[] = {
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_UCHE_0_LO,
		A3XX_RBBM_PERFCTR_UCHE_0_HI, 21,
		A3XX_UCHE_PERFCOUNTER0_SELECT },
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_UCHE_1_LO,
		A3XX_RBBM_PERFCTR_UCHE_1_HI, 22,
		A3XX_UCHE_PERFCOUNTER1_SELECT },
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_UCHE_2_LO,
		A3XX_RBBM_PERFCTR_UCHE_2_HI, 23,
		A3XX_UCHE_PERFCOUNTER2_SELECT },
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_UCHE_3_LO,
		A3XX_RBBM_PERFCTR_UCHE_3_HI, 24,
		A3XX_UCHE_PERFCOUNTER3_SELECT },
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_UCHE_4_LO,
		A3XX_RBBM_PERFCTR_UCHE_4_HI, 25,
		A3XX_UCHE_PERFCOUNTER4_SELECT },
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_UCHE_5_LO,
		A3XX_RBBM_PERFCTR_UCHE_5_HI, 26,
		A3XX_UCHE_PERFCOUNTER5_SELECT },
};

static struct adreno_perfcount_register a3xx_perfcounters_tp[] = {
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_TP_0_LO,
		A3XX_RBBM_PERFCTR_TP_0_HI, 27, A3XX_TP_PERFCOUNTER0_SELECT },
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_TP_1_LO,
		A3XX_RBBM_PERFCTR_TP_1_HI, 28, A3XX_TP_PERFCOUNTER1_SELECT },
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_TP_2_LO,
		A3XX_RBBM_PERFCTR_TP_2_HI, 29, A3XX_TP_PERFCOUNTER2_SELECT },
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_TP_3_LO,
		A3XX_RBBM_PERFCTR_TP_3_HI, 30, A3XX_TP_PERFCOUNTER3_SELECT },
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_TP_4_LO,
		A3XX_RBBM_PERFCTR_TP_4_HI, 31, A3XX_TP_PERFCOUNTER4_SELECT },
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_TP_5_LO,
		A3XX_RBBM_PERFCTR_TP_5_HI, 32, A3XX_TP_PERFCOUNTER5_SELECT },
};

static struct adreno_perfcount_register a3xx_perfcounters_sp[] = {
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_SP_0_LO,
		A3XX_RBBM_PERFCTR_SP_0_HI, 33, A3XX_SP_PERFCOUNTER0_SELECT },
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_SP_1_LO,
		A3XX_RBBM_PERFCTR_SP_1_HI, 34, A3XX_SP_PERFCOUNTER1_SELECT },
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_SP_2_LO,
		A3XX_RBBM_PERFCTR_SP_2_HI, 35, A3XX_SP_PERFCOUNTER2_SELECT },
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_SP_3_LO,
		A3XX_RBBM_PERFCTR_SP_3_HI, 36, A3XX_SP_PERFCOUNTER3_SELECT },
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_SP_4_LO,
		A3XX_RBBM_PERFCTR_SP_4_HI, 37, A3XX_SP_PERFCOUNTER4_SELECT },
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_SP_5_LO,
		A3XX_RBBM_PERFCTR_SP_5_HI, 38, A3XX_SP_PERFCOUNTER5_SELECT },
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_SP_6_LO,
		A3XX_RBBM_PERFCTR_SP_6_HI, 39, A3XX_SP_PERFCOUNTER6_SELECT },
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_SP_7_LO,
		A3XX_RBBM_PERFCTR_SP_7_HI, 40, A3XX_SP_PERFCOUNTER7_SELECT },
};

static struct adreno_perfcount_register a3xx_perfcounters_rb[] = {
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_RB_0_LO,
		A3XX_RBBM_PERFCTR_RB_0_HI, 41, A3XX_RB_PERFCOUNTER0_SELECT },
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_RB_1_LO,
		A3XX_RBBM_PERFCTR_RB_1_HI, 42, A3XX_RB_PERFCOUNTER1_SELECT },
};

static struct adreno_perfcount_register a3xx_perfcounters_pwr[] = {
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_PWR_0_LO,
		A3XX_RBBM_PERFCTR_PWR_0_HI, -1, 0 },
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_PWR_1_LO,
		A3XX_RBBM_PERFCTR_PWR_1_HI, -1, 0 },
};

static struct adreno_perfcount_register a3xx_perfcounters_vbif[] = {
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_VBIF_PERF_CNT0_LO,
		A3XX_VBIF_PERF_CNT0_HI, -1, 0 },
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_VBIF_PERF_CNT1_LO,
		A3XX_VBIF_PERF_CNT1_HI, -1, 0 },
};
static struct adreno_perfcount_register a3xx_perfcounters_vbif_pwr[] = {
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_VBIF_PERF_PWR_CNT0_LO,
		A3XX_VBIF_PERF_PWR_CNT0_HI, -1, 0 },
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_VBIF_PERF_PWR_CNT1_LO,
		A3XX_VBIF_PERF_PWR_CNT1_HI, -1, 0 },
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_VBIF_PERF_PWR_CNT2_LO,
		A3XX_VBIF_PERF_PWR_CNT2_HI, -1, 0 },
};
static struct adreno_perfcount_register a3xx_perfcounters_vbif2[] = {
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_VBIF2_PERF_CNT_LOW0,
		A3XX_VBIF2_PERF_CNT_HIGH0, -1, A3XX_VBIF2_PERF_CNT_SEL0 },
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_VBIF2_PERF_CNT_LOW1,
		A3XX_VBIF2_PERF_CNT_HIGH1, -1, A3XX_VBIF2_PERF_CNT_SEL1 },
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_VBIF2_PERF_CNT_LOW2,
		A3XX_VBIF2_PERF_CNT_HIGH2, -1, A3XX_VBIF2_PERF_CNT_SEL2 },
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_VBIF2_PERF_CNT_LOW3,
		A3XX_VBIF2_PERF_CNT_HIGH3, -1, A3XX_VBIF2_PERF_CNT_SEL3 },
};
/*
 * Placing EN register in select field since vbif perf counters
 * dont have select register to program
 */
static struct adreno_perfcount_register a3xx_perfcounters_vbif2_pwr[] = {
	{ KGSL_PERFCOUNTER_NOT_USED, 0,
		0, A3XX_VBIF2_PERF_PWR_CNT_LOW0,
		A3XX_VBIF2_PERF_PWR_CNT_HIGH0, -1,
		A3XX_VBIF2_PERF_PWR_CNT_EN0 },
	{ KGSL_PERFCOUNTER_NOT_USED, 0,
		0, A3XX_VBIF2_PERF_PWR_CNT_LOW1,
		A3XX_VBIF2_PERF_PWR_CNT_HIGH1, -1,
		A3XX_VBIF2_PERF_PWR_CNT_EN1 },
	{ KGSL_PERFCOUNTER_NOT_USED, 0,
		0, A3XX_VBIF2_PERF_PWR_CNT_LOW2,
		A3XX_VBIF2_PERF_PWR_CNT_HIGH2, -1,
		A3XX_VBIF2_PERF_PWR_CNT_EN2 },
};

#define A3XX_PERFCOUNTER_GROUP(offset, name) \
	ADRENO_PERFCOUNTER_GROUP(a3xx, offset, name)

#define A3XX_PERFCOUNTER_GROUP_FLAGS(offset, name, flags) \
	ADRENO_PERFCOUNTER_GROUP_FLAGS(a3xx, offset, name, flags)

static struct adreno_perfcount_group a3xx_perfcounter_groups[] = {
	A3XX_PERFCOUNTER_GROUP(CP, cp),
	A3XX_PERFCOUNTER_GROUP(RBBM, rbbm),
	A3XX_PERFCOUNTER_GROUP(PC, pc),
	A3XX_PERFCOUNTER_GROUP(VFD, vfd),
	A3XX_PERFCOUNTER_GROUP(HLSQ, hlsq),
	A3XX_PERFCOUNTER_GROUP(VPC, vpc),
	A3XX_PERFCOUNTER_GROUP(TSE, tse),
	A3XX_PERFCOUNTER_GROUP(RAS, ras),
	A3XX_PERFCOUNTER_GROUP(UCHE, uche),
	A3XX_PERFCOUNTER_GROUP(TP, tp),
	A3XX_PERFCOUNTER_GROUP(SP, sp),
	A3XX_PERFCOUNTER_GROUP(RB, rb),
	A3XX_PERFCOUNTER_GROUP_FLAGS(PWR, pwr,
		ADRENO_PERFCOUNTER_GROUP_FIXED),
	A3XX_PERFCOUNTER_GROUP(VBIF, vbif),
	A3XX_PERFCOUNTER_GROUP_FLAGS(VBIF_PWR, vbif_pwr,
		ADRENO_PERFCOUNTER_GROUP_FIXED),
};

static struct adreno_perfcounters a3xx_perfcounters = {
	a3xx_perfcounter_groups,
	ARRAY_SIZE(a3xx_perfcounter_groups),
};

struct adreno_ft_perf_counters a3xx_ft_perf_counters[] = {
	{KGSL_PERFCOUNTER_GROUP_SP, SP_ALU_ACTIVE_CYCLES},
	{KGSL_PERFCOUNTER_GROUP_SP, SP0_ICL1_MISSES},
	{KGSL_PERFCOUNTER_GROUP_SP, SP_FS_CFLOW_INSTRUCTIONS},
	{KGSL_PERFCOUNTER_GROUP_TSE, TSE_INPUT_PRIM_NUM},
};

/**
 * a3xx_perfcounter_init() - Allocate performance counters for use in the kernel
 * @adreno_dev: Pointer to an adreno_device structure
 */
int a3xx_perfcounter_init(struct adreno_device *adreno_dev)
{
	struct adreno_perfcounters *counters = ADRENO_PERFCOUNTERS(adreno_dev);

	if (counters == NULL)
		return -EINVAL;

	/* SP[3] counter is broken on a330 so disable it if a330 device */
	if (adreno_is_a330(adreno_dev))
		a3xx_perfcounters_sp[3].countable = KGSL_PERFCOUNTER_BROKEN;

	if (adreno_is_a306(adreno_dev) ||
		adreno_is_a304(adreno_dev)) {
		counters->groups[KGSL_PERFCOUNTER_GROUP_VBIF].regs =
			a3xx_perfcounters_vbif2;
		counters->groups[KGSL_PERFCOUNTER_GROUP_VBIF_PWR].regs =
			a3xx_perfcounters_vbif2_pwr;
	}

	return 0;
}

/**
 * a3xx_protect_init() - Initializes register protection on a3xx
 * @adreno_dev: Pointer to the device structure
 * Performs register writes to enable protected access to sensitive
 * registers
 */
static void a3xx_protect_init(struct adreno_device *adreno_dev)
{
	struct kgsl_device *device = &adreno_dev->dev;
	int index = 0;
	struct kgsl_protected_registers *iommu_regs;

	/* enable access protection to privileged registers */
	kgsl_regwrite(device, A3XX_CP_PROTECT_CTRL, 0x00000007);

	/* RBBM registers */
	adreno_set_protected_registers(adreno_dev, &index, 0x18, 0);
	adreno_set_protected_registers(adreno_dev, &index, 0x20, 2);
	adreno_set_protected_registers(adreno_dev, &index, 0x33, 0);
	adreno_set_protected_registers(adreno_dev, &index, 0x42, 0);
	adreno_set_protected_registers(adreno_dev, &index, 0x50, 4);
	adreno_set_protected_registers(adreno_dev, &index, 0x63, 0);
	adreno_set_protected_registers(adreno_dev, &index, 0x100, 4);

	/* CP registers */
	adreno_set_protected_registers(adreno_dev, &index, 0x1C0, 5);
	adreno_set_protected_registers(adreno_dev, &index, 0x1EC, 1);
	adreno_set_protected_registers(adreno_dev, &index, 0x1F6, 1);
	adreno_set_protected_registers(adreno_dev, &index, 0x1F8, 2);
	adreno_set_protected_registers(adreno_dev, &index, 0x45E, 2);
	adreno_set_protected_registers(adreno_dev, &index, 0x460, 4);

	/* RB registers */
	adreno_set_protected_registers(adreno_dev, &index, 0xCC0, 0);

	/* VBIF registers */
	adreno_set_protected_registers(adreno_dev, &index, 0x3000, 6);

	/* SMMU registers */
	iommu_regs = kgsl_mmu_get_prot_regs(&device->mmu);
	if (iommu_regs)
		adreno_set_protected_registers(adreno_dev, &index,
				iommu_regs->base, iommu_regs->range);
}

static void a3xx_start(struct adreno_device *adreno_dev)
{
	struct kgsl_device *device = &adreno_dev->dev;
	struct adreno_gpudev *gpudev = ADRENO_GPU_DEVICE(adreno_dev);

	adreno_vbif_start(adreno_dev, a3xx_vbif_platforms,
			ARRAY_SIZE(a3xx_vbif_platforms));

	/* Make all blocks contribute to the GPU BUSY perf counter */
	kgsl_regwrite(device, A3XX_RBBM_GPU_BUSY_MASKED, 0xFFFFFFFF);

	/* Tune the hystersis counters for SP and CP idle detection */
	kgsl_regwrite(device, A3XX_RBBM_SP_HYST_CNT, 0x10);
	kgsl_regwrite(device, A3XX_RBBM_WAIT_IDLE_CLOCKS_CTL, 0x10);

	/* Enable the RBBM error reporting bits.  This lets us get
	   useful information on failure */

	kgsl_regwrite(device, A3XX_RBBM_AHB_CTL0, 0x00000001);

	/* Enable AHB error reporting */
	kgsl_regwrite(device, A3XX_RBBM_AHB_CTL1, 0xA6FFFFFF);

	/* Turn on the power counters */
	kgsl_regwrite(device, A3XX_RBBM_RBBM_CTL, 0x00030000);

	/* Turn on hang detection - this spews a lot of useful information
	 * into the RBBM registers on a hang */
	if (adreno_is_a330v2(adreno_dev)) {
		set_bit(ADRENO_DEVICE_HANG_INTR, &adreno_dev->priv);
		gpudev->irq->mask |= (1 << A3XX_INT_MISC_HANG_DETECT);
		kgsl_regwrite(device, A3XX_RBBM_INTERFACE_HANG_INT_CTL,
				(1 << 31) | 0xFFFF);
	} else
		kgsl_regwrite(device, A3XX_RBBM_INTERFACE_HANG_INT_CTL,
				(1 << 16) | 0xFFF);

	/* Enable 64-byte cacheline size. HW Default is 32-byte (0x000000E0). */
	kgsl_regwrite(device, A3XX_UCHE_CACHE_MODE_CONTROL_REG, 0x00000001);

	/* Enable VFD to access most of the UCHE (7 ways out of 8) */
	kgsl_regwrite(device, A3XX_UCHE_CACHE_WAYS_VFD, 0x07);

	/* Enable Clock gating */
	kgsl_regwrite(device, A3XX_RBBM_CLOCK_CTL,
		adreno_a3xx_rbbm_clock_ctl_default(adreno_dev));

	if (adreno_is_a330v2(adreno_dev))
		kgsl_regwrite(device, A3XX_RBBM_GPR0_CTL,
			A330v2_RBBM_GPR0_CTL_DEFAULT);
	else if (adreno_is_a330(adreno_dev))
		kgsl_regwrite(device, A3XX_RBBM_GPR0_CTL,
			A330_RBBM_GPR0_CTL_DEFAULT);
	else if (adreno_is_a310(adreno_dev))
		kgsl_regwrite(device, A3XX_RBBM_GPR0_CTL,
			A310_RBBM_GPR0_CTL_DEFAULT);

	if (ADRENO_FEATURE(adreno_dev, ADRENO_USES_OCMEM))
		kgsl_regwrite(device, A3XX_RB_GMEM_BASE_ADDR,
			(unsigned int)(adreno_dev->gmem_base >> 14));

	/* Turn on protection */
	a3xx_protect_init(adreno_dev);

	/* Turn on performance counters */
	kgsl_regwrite(device, A3XX_RBBM_PERFCTR_CTL, 0x01);

	kgsl_regwrite(device, A3XX_CP_DEBUG, A3XX_CP_DEBUG_DEFAULT);
	memset(&adreno_dev->busy_data, 0, sizeof(adreno_dev->busy_data));
}

static struct adreno_coresight_register a3xx_coresight_registers[] = {
	{ A3XX_RBBM_DEBUG_BUS_CTL, 0x0001093F },
	{ A3XX_RBBM_EXT_TRACE_STOP_CNT, 0x00017fff },
	{ A3XX_RBBM_EXT_TRACE_START_CNT, 0x0001000f },
	{ A3XX_RBBM_EXT_TRACE_PERIOD_CNT, 0x0001ffff },
	{ A3XX_RBBM_EXT_TRACE_CMD, 0x00000001 },
	{ A3XX_RBBM_EXT_TRACE_BUS_CTL, 0x89100010 },
	{ A3XX_RBBM_DEBUG_BUS_STB_CTL0, 0x00000000 },
	{ A3XX_RBBM_DEBUG_BUS_STB_CTL1, 0xFFFFFFFE },
	{ A3XX_RBBM_INT_TRACE_BUS_CTL, 0x00201111 },
};

static ADRENO_CORESIGHT_ATTR(config_debug_bus,
	&a3xx_coresight_registers[0]);
static ADRENO_CORESIGHT_ATTR(config_trace_stop_cnt,
	&a3xx_coresight_registers[1]);
static ADRENO_CORESIGHT_ATTR(config_trace_start_cnt,
	&a3xx_coresight_registers[2]);
static ADRENO_CORESIGHT_ATTR(config_trace_period_cnt,
	&a3xx_coresight_registers[3]);
static ADRENO_CORESIGHT_ATTR(config_trace_cmd,
	&a3xx_coresight_registers[4]);
static ADRENO_CORESIGHT_ATTR(config_trace_bus_ctl,
	&a3xx_coresight_registers[5]);

static struct attribute *a3xx_coresight_attrs[] = {
	&coresight_attr_config_debug_bus.attr.attr,
	&coresight_attr_config_trace_start_cnt.attr.attr,
	&coresight_attr_config_trace_stop_cnt.attr.attr,
	&coresight_attr_config_trace_period_cnt.attr.attr,
	&coresight_attr_config_trace_cmd.attr.attr,
	&coresight_attr_config_trace_bus_ctl.attr.attr,
	NULL,
};

static const struct attribute_group a3xx_coresight_group = {
	.attrs = a3xx_coresight_attrs,
};

static const struct attribute_group *a3xx_coresight_groups[] = {
	&a3xx_coresight_group,
	NULL,
};

static struct adreno_coresight a3xx_coresight = {
	.registers = a3xx_coresight_registers,
	.count = ARRAY_SIZE(a3xx_coresight_registers),
	.groups = a3xx_coresight_groups,
};

/* Register offset defines for A3XX */
static unsigned int a3xx_register_offsets[ADRENO_REG_REGISTER_MAX] = {
	ADRENO_REG_DEFINE(ADRENO_REG_CP_ME_RAM_WADDR, A3XX_CP_ME_RAM_WADDR),
	ADRENO_REG_DEFINE(ADRENO_REG_CP_ME_RAM_DATA, A3XX_CP_ME_RAM_DATA),
	ADRENO_REG_DEFINE(ADRENO_REG_CP_PFP_UCODE_DATA, A3XX_CP_PFP_UCODE_DATA),
	ADRENO_REG_DEFINE(ADRENO_REG_CP_PFP_UCODE_ADDR, A3XX_CP_PFP_UCODE_ADDR),
	ADRENO_REG_DEFINE(ADRENO_REG_CP_WFI_PEND_CTR, A3XX_CP_WFI_PEND_CTR),
	ADRENO_REG_DEFINE(ADRENO_REG_CP_RB_BASE, A3XX_CP_RB_BASE),
	ADRENO_REG_DEFINE(ADRENO_REG_CP_RB_RPTR, A3XX_CP_RB_RPTR),
	ADRENO_REG_DEFINE(ADRENO_REG_CP_RB_WPTR, A3XX_CP_RB_WPTR),
	ADRENO_REG_DEFINE(ADRENO_REG_CP_CNTL, A3XX_CP_CNTL),
	ADRENO_REG_DEFINE(ADRENO_REG_CP_ME_CNTL, A3XX_CP_ME_CNTL),
	ADRENO_REG_DEFINE(ADRENO_REG_CP_RB_CNTL, A3XX_CP_RB_CNTL),
	ADRENO_REG_DEFINE(ADRENO_REG_CP_IB1_BASE, A3XX_CP_IB1_BASE),
	ADRENO_REG_DEFINE(ADRENO_REG_CP_IB1_BUFSZ, A3XX_CP_IB1_BUFSZ),
	ADRENO_REG_DEFINE(ADRENO_REG_CP_IB2_BASE, A3XX_CP_IB2_BASE),
	ADRENO_REG_DEFINE(ADRENO_REG_CP_IB2_BUFSZ, A3XX_CP_IB2_BUFSZ),
	ADRENO_REG_DEFINE(ADRENO_REG_CP_TIMESTAMP, A3XX_CP_SCRATCH_REG0),
	ADRENO_REG_DEFINE(ADRENO_REG_CP_SCRATCH_REG6, A3XX_CP_SCRATCH_REG6),
	ADRENO_REG_DEFINE(ADRENO_REG_CP_SCRATCH_REG7, A3XX_CP_SCRATCH_REG7),
	ADRENO_REG_DEFINE(ADRENO_REG_CP_ME_RAM_RADDR, A3XX_CP_ME_RAM_RADDR),
	ADRENO_REG_DEFINE(ADRENO_REG_CP_ROQ_ADDR, A4XX_CP_ROQ_ADDR),
	ADRENO_REG_DEFINE(ADRENO_REG_CP_ROQ_DATA, A3XX_CP_ROQ_DATA),
	ADRENO_REG_DEFINE(ADRENO_REG_CP_MERCIU_ADDR, A3XX_CP_MERCIU_ADDR),
	ADRENO_REG_DEFINE(ADRENO_REG_CP_MERCIU_DATA, A3XX_CP_MERCIU_DATA),
	ADRENO_REG_DEFINE(ADRENO_REG_CP_MERCIU_DATA2, A3XX_CP_MERCIU_DATA2),
	ADRENO_REG_DEFINE(ADRENO_REG_CP_MEQ_ADDR, A3XX_CP_MEQ_ADDR),
	ADRENO_REG_DEFINE(ADRENO_REG_CP_MEQ_DATA, A3XX_CP_MEQ_DATA),
	ADRENO_REG_DEFINE(ADRENO_REG_CP_PROTECT_REG_0, A3XX_CP_PROTECT_REG_0),
	ADRENO_REG_DEFINE(ADRENO_REG_RBBM_STATUS, A3XX_RBBM_STATUS),
	ADRENO_REG_DEFINE(ADRENO_REG_RBBM_PERFCTR_CTL, A3XX_RBBM_PERFCTR_CTL),
	ADRENO_REG_DEFINE(ADRENO_REG_RBBM_PERFCTR_LOAD_CMD0,
					A3XX_RBBM_PERFCTR_LOAD_CMD0),
	ADRENO_REG_DEFINE(ADRENO_REG_RBBM_PERFCTR_LOAD_CMD1,
					A3XX_RBBM_PERFCTR_LOAD_CMD1),
	ADRENO_REG_DEFINE(ADRENO_REG_RBBM_PERFCTR_PWR_1_LO,
					A3XX_RBBM_PERFCTR_PWR_1_LO),
	ADRENO_REG_DEFINE(ADRENO_REG_RBBM_INT_0_MASK, A3XX_RBBM_INT_0_MASK),
	ADRENO_REG_DEFINE(ADRENO_REG_RBBM_INT_0_STATUS, A3XX_RBBM_INT_0_STATUS),
	ADRENO_REG_DEFINE(ADRENO_REG_RBBM_INT_CLEAR_CMD,
				A3XX_RBBM_INT_CLEAR_CMD),
	ADRENO_REG_DEFINE(ADRENO_REG_RBBM_CLOCK_CTL, A3XX_RBBM_CLOCK_CTL),
	ADRENO_REG_DEFINE(ADRENO_REG_VPC_DEBUG_RAM_SEL,
				A3XX_VPC_VPC_DEBUG_RAM_SEL),
	ADRENO_REG_DEFINE(ADRENO_REG_VPC_DEBUG_RAM_READ,
				A3XX_VPC_VPC_DEBUG_RAM_READ),
	ADRENO_REG_DEFINE(ADRENO_REG_PA_SC_AA_CONFIG, A3XX_PA_SC_AA_CONFIG),
	ADRENO_REG_DEFINE(ADRENO_REG_RBBM_PM_OVERRIDE2, A3XX_RBBM_PM_OVERRIDE2),
	ADRENO_REG_DEFINE(ADRENO_REG_SQ_GPR_MANAGEMENT, A3XX_SQ_GPR_MANAGEMENT),
	ADRENO_REG_DEFINE(ADRENO_REG_SQ_INST_STORE_MANAGMENT,
				A3XX_SQ_INST_STORE_MANAGMENT),
	ADRENO_REG_DEFINE(ADRENO_REG_TP0_CHICKEN, A3XX_TP0_CHICKEN),
	ADRENO_REG_DEFINE(ADRENO_REG_RBBM_RBBM_CTL, A3XX_RBBM_RBBM_CTL),
	ADRENO_REG_DEFINE(ADRENO_REG_RBBM_SW_RESET_CMD, A3XX_RBBM_SW_RESET_CMD),
	ADRENO_REG_DEFINE(ADRENO_REG_UCHE_INVALIDATE0,
			A3XX_UCHE_CACHE_INVALIDATE0_REG),
	ADRENO_REG_DEFINE(ADRENO_REG_RBBM_PERFCTR_LOAD_VALUE_LO,
				A3XX_RBBM_PERFCTR_LOAD_VALUE_LO),
	ADRENO_REG_DEFINE(ADRENO_REG_RBBM_PERFCTR_LOAD_VALUE_HI,
				A3XX_RBBM_PERFCTR_LOAD_VALUE_HI),
	ADRENO_REG_DEFINE(ADRENO_REG_VBIF_XIN_HALT_CTRL0,
				A3XX_VBIF_XIN_HALT_CTRL0),
	ADRENO_REG_DEFINE(ADRENO_REG_VBIF_XIN_HALT_CTRL1,
				A3XX_VBIF_XIN_HALT_CTRL1),
};

const struct adreno_reg_offsets a3xx_reg_offsets = {
	.offsets = a3xx_register_offsets,
	.offset_0 = ADRENO_REG_REGISTER_MAX,
};

/*
 * Defined the size of sections dumped in snapshot, these values
 * may change after initialization based on the specific core
 */
static struct adreno_snapshot_sizes a3xx_snap_sizes = {
	.cp_state_deb = 0x14,
	.vpc_mem = 512,
	.cp_meq = 16,
	.shader_mem = 0x4000,
	.cp_merciu = 0,
	.roq = 128,
};

static struct adreno_snapshot_data a3xx_snapshot_data = {
	.sect_sizes = &a3xx_snap_sizes,
};

struct adreno_gpudev adreno_a3xx_gpudev = {
	.reg_offsets = &a3xx_reg_offsets,
	.ft_perf_counters = a3xx_ft_perf_counters,
	.ft_perf_counters_count = ARRAY_SIZE(a3xx_ft_perf_counters),
	.perfcounters = &a3xx_perfcounters,
	.irq = &a3xx_irq,
	.irq_trace = trace_kgsl_a3xx_irq_status,
	.snapshot_data = &a3xx_snapshot_data,
	.num_prio_levels = 1,
	.vbif_xin_halt_ctrl0_mask = A3XX_VBIF_XIN_HALT_CTRL0_MASK,

	.gpudev_init = a3xx_gpudev_init,
	.rb_init = a3xx_rb_init,
	.perfcounter_init = a3xx_perfcounter_init,
	.busy_cycles = a3xx_busy_cycles,
	.start = a3xx_start,
	.snapshot = a3xx_snapshot,
	.perfcounter_enable = a3xx_perfcounter_enable,
	.perfcounter_read = a3xx_perfcounter_read,
	.coresight = &a3xx_coresight,
};
