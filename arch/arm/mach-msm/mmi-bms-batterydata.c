/* Copyright (c) 2011, Motorola Mobility. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 */
#include <linux/mfd/pm8xxx/pm8921-bms.h>
#include <linux/mfd/pm8xxx/pm8921-charger.h>
#include <linux/power/mmi-battery.h>
#include "board-mmi.h"

/* EG30 SDI Tables and Charging Parameters */
static struct single_row_lut mmi_eg30_sdi_fcc_temp = {
	.x	= {-10, 0, 23, 60},
	.y	= {1677, 1901, 1980, 1946},
	.cols	= 3,
};

static struct single_row_lut mmi_eg30_sdi_fcc_sf = {
	.x	= {1, 100, 200, 300, 400, 500},
	.y	= {100, 96, 94, 92, 90, 88},
	.cols	= 6,
};

static struct pc_sf_lut mmi_eg30_sdi_pc_sf = {
	.rows		= 10,
	.cols		= 5,
	.cycles		= {100, 200, 300, 400, 500},
	.percent	= {100, 90, 80, 70, 60, 50, 40, 30, 20, 10},
	.sf		= {
			{100, 100, 100, 100, 100},
			{100, 100, 100, 100, 100},
			{100, 100, 100, 100, 100},
			{100, 100, 100, 100, 100},
			{100, 100, 100, 100, 100},
			{100, 100, 100, 100, 100},
			{100, 100, 100, 100, 100},
			{100, 100, 100, 100, 100},
			{100, 100, 100, 100, 100},
			{100, 100, 100, 100, 100}
	},
};

static struct pc_temp_ocv_lut mmi_eg30_sdi_pc_temp_ocv = {
	.rows		= 29,
	.cols		= 3,
	.temp		= {-10, 23, 60},
	.percent	= {100, 95, 90, 85, 80, 75, 70, 65, 60, 55,
				50, 45, 40, 35, 30, 25, 20, 15, 10,
				9, 8, 7, 6, 5, 4, 3, 2, 1, 0
	},
	.ocv		= {
			{4300, 4300, 4300},
			{4268, 4264, 4250},
			{4222, 4219, 4202},
			{4169, 4165, 4148},
			{4116, 4112, 4096},
			{4061, 4073, 4048},
			{3989, 4004, 4004},
			{3936, 3972, 3964},
			{3906, 3937, 3927},
			{3876, 3886, 3875},
			{3848, 3848, 3843},
			{3824, 3824, 3819},
			{3805, 3806, 3801},
			{3790, 3792, 3786},
			{3781, 3781, 3765},
			{3769, 3775, 3743},
			{3750, 3754, 3721},
			{3730, 3717, 3684},
			{3705, 3696, 3671},
			{3703, 3694, 3670},
			{3702, 3692, 3669},
			{3700, 3691, 3666},
			{3697, 3688, 3660},
			{3691, 3677, 3638},
			{3667, 3647, 3597},
			{3618, 3592, 3539},
			{3545, 3516, 3461},
			{3431, 3398, 3347},
			{3200, 3200, 3200}
	},
};

static struct pm8921_bms_battery_data  mmi_eg30_sdi_metering_data = {
	.fcc			= 1940,
	.fcc_temp_lut		= &mmi_eg30_sdi_fcc_temp,
	.fcc_sf_lut		= &mmi_eg30_sdi_fcc_sf,
	.pc_temp_ocv_lut	= &mmi_eg30_sdi_pc_temp_ocv,
	.pc_sf_lut		= &mmi_eg30_sdi_pc_sf,
};

static struct pm8921_charger_battery_data mmi_eg30_sdi_charging_data = {
	.max_voltage			= 4350,
	.min_voltage			= 3200,
	.resume_voltage_delta		= 100,
	.term_current			= 86,
	.cool_temp			= 0,
	.warm_temp			= 45,
	.max_bat_chg_current		= 1843,
	.cool_bat_chg_current		= 0,
	.warm_bat_chg_current		= 0,
	.cool_bat_voltage		= 4000,
	.warm_bat_voltage		= 4000,
	.step_charge_current		= 1120,
	.step_charge_voltage		= 4200,
};

static struct mmi_battery_cell mmi_eg30_sdi_cell_data = {
	.capacity = 0xC2,
	.peak_voltage = 0xB9,
	.dc_impedance = 0x5A,
	.cell_id = 0x4247, /* Cell code BG */
};

/* EV30 Tables and Charging Parameters */
static struct single_row_lut mmi_ev30_fcc_temp = {
	.x	= {-10, 23, 60},
	.y	= {2278, 2503, 2485},
	.cols	= 3,
};

static struct single_row_lut mmi_ev30_fcc_sf = {
	.x	= {1, 100, 200, 300, 400, 500},
	.y	= {100, 96, 94, 92, 90, 88},
	.cols	= 6,
};

static struct pc_sf_lut mmi_ev30_pc_sf = {
	.rows		= 10,
	.cols		= 5,
	.cycles		= {100, 200, 300, 400, 500},
	.percent	= {100, 90, 80, 70, 60, 50, 40, 30, 20, 10},
	.sf		= {
			{100, 100, 100, 100, 100},
			{100, 100, 100, 100, 100},
			{100, 100, 100, 100, 100},
			{100, 100, 100, 100, 100},
			{100, 100, 100, 100, 100},
			{100, 100, 100, 100, 100},
			{100, 100, 100, 100, 100},
			{100, 100, 100, 100, 100},
			{100, 100, 100, 100, 100},
			{100, 100, 100, 100, 100}
	},
};

static struct pc_temp_ocv_lut mmi_ev30_pc_temp_ocv = {
	.rows		= 29,
	.cols		= 3,
	.temp		= {-10, 23, 60},
	.percent	= {100, 95, 90, 85, 80, 75, 70, 65, 60, 55,
				50, 45, 40, 35, 30, 25, 20, 15, 10,
				9, 8, 7, 6, 5, 4, 3, 2, 1, 0
	},
	.ocv		= {
			{4300, 4300, 4300},
			{4268, 4253, 4248},
			{4200, 4209, 4235},
			{4160, 4155, 4142},
			{4112, 4104, 4092},
			{4061, 4059, 4045},
			{3999, 4004, 4001},
			{3950, 3966, 3962},
			{3912, 3932, 3926},
			{3876, 3885, 3878},
			{3847, 3847, 3845},
			{3823, 3823, 3821},
			{3805, 3805, 3801},
			{3792, 3791, 3785},
			{3783, 3781, 3761},
			{3771, 3770, 3738},
			{3750, 3743, 3714},
			{3718, 3701, 3681},
			{3702, 3689, 3667},
			{3700, 3688, 3666},
			{3698, 3686, 3663},
			{3694, 3681, 3657},
			{3685, 3670, 3641},
			{3662, 3643, 3610},
			{3623, 3602, 3567},
			{3570, 3548, 3513},
			{3499, 3476, 3443},
			{3396, 3375, 3347},
			{3200, 3200, 3200}
	},
};

static struct pm8921_bms_battery_data  mmi_ev30_metering_data = {
	.fcc			= 2500,
	.fcc_temp_lut		= &mmi_ev30_fcc_temp,
	.fcc_sf_lut		= &mmi_ev30_fcc_sf,
	.pc_temp_ocv_lut	= &mmi_ev30_pc_temp_ocv,
	.pc_sf_lut		= &mmi_ev30_pc_sf,
};

static struct pm8921_charger_battery_data mmi_ev30_charging_data = {
	.max_voltage			= 4350,
	.min_voltage			= 3200,
	.resume_voltage_delta		= 100,
	.term_current			= 213,
	.cool_temp			= 0,
	.warm_temp			= 45,
	.max_bat_chg_current		= 2375,
	.cool_bat_chg_current		= 0,
	.warm_bat_chg_current		= 0,
	.cool_bat_voltage		= 4000,
	.warm_bat_voltage		= 4000,
	.step_charge_current		= 1600,
	.step_charge_voltage		= 4200,
};

static struct mmi_battery_cell mmi_ev30_cid5858_cell_data = {
	.capacity = 0xFA,
	.peak_voltage = 0xB9,
	.dc_impedance = 0x55,
	.cell_id = 0x5858, /* Cell code XX */
};

static struct mmi_battery_cell mmi_ev30_cid_4246_cell_data = {
	.capacity = 0xFA,
	.peak_voltage = 0xB9,
	.dc_impedance = 0x55,
	.cell_id = 0x4246, /* Cell code BF */
};

/* EB41 Tables and Charging Parameters */
static struct single_row_lut mmi_eb41_fcc_temp = {
	.x	= {-10, 23, 60},
	.y	= {1626, 1767, 1749},
	.cols	= 3,
};

static struct single_row_lut mmi_eb41_fcc_sf = {
	.x	= {1, 100, 200, 300, 400, 500},
	.y	= {100, 96, 94, 92, 90, 88},
	.cols	= 6,
};

static struct pc_sf_lut mmi_eb41_pc_sf = {
	.rows		= 10,
	.cols		= 5,
	.cycles		= {100, 200, 300, 400, 500},
	.percent	= {100, 90, 80, 70, 60, 50, 40, 30, 20, 10},
	.sf		= {
			{100, 100, 100, 100, 100},
			{100, 100, 100, 100, 100},
			{100, 100, 100, 100, 100},
			{100, 100, 100, 100, 100},
			{100, 100, 100, 100, 100},
			{100, 100, 100, 100, 100},
			{100, 100, 100, 100, 100},
			{100, 100, 100, 100, 100},
			{100, 100, 100, 100, 100},
			{100, 100, 100, 100, 100}
	},
};

static struct pc_temp_ocv_lut mmi_eb41_pc_temp_ocv = {
	.rows		= 29,
	.cols		= 3,
	.temp		= {-10, 23, 60},
	.percent	= {100, 95, 90, 85, 80, 75, 70, 65, 60, 55,
				50, 45, 40, 35, 30, 25, 20, 15, 10,
				9, 8, 7, 6, 5, 4, 3, 2, 1, 0
	},
	.ocv		= {
			{4310, 4310, 4310},
			{4267, 4261, 4248},
			{4218, 4214, 4202},
			{4162, 4159, 4147},
			{4107, 4106, 4095},
			{4053, 4057, 4046},
			{3995, 4009, 4001},
			{3946, 3968, 3960},
			{3908, 3929, 3923},
			{3875, 3882, 3878},
			{3846, 3845, 3841},
			{3823, 3821, 3817},
			{3802, 3803, 3798},
			{3786, 3789, 3783},
			{3771, 3779, 3765},
			{3752, 3770, 3743},
			{3733, 3749, 3720},
			{3719, 3707, 3683},
			{3700, 3688, 3666},
			{3697, 3686, 3664},
			{3693, 3685, 3661},
			{3688, 3682, 3657},
			{3680, 3677, 3651},
			{3664, 3662, 3635},
			{3636, 3632, 3601},
			{3592, 3585, 3549},
			{3523, 3516, 3478},
			{3418, 3414, 3375},
			{3200, 3200, 3200}
	},
};

static struct pm8921_bms_battery_data  mmi_eb41_metering_data = {
	.fcc			= 1767,
	.fcc_temp_lut		= &mmi_eb41_fcc_temp,
	.fcc_sf_lut		= &mmi_eb41_fcc_sf,
	.pc_temp_ocv_lut	= &mmi_eb41_pc_temp_ocv,
	.pc_sf_lut		= &mmi_eb41_pc_sf,
};

static struct pm8921_charger_battery_data mmi_eb41_charging_data = {
	.max_voltage			= 4350,
	.min_voltage			= 3200,
	.resume_voltage_delta		= 100,
	.term_current			= 58,
	.cool_temp			= 0,
	.warm_temp			= 45,
	.max_bat_chg_current		= 1678,
	.cool_bat_chg_current		= 0,
	.warm_bat_chg_current		= 0,
	.cool_bat_voltage		= 4000,
	.warm_bat_voltage		= 4000,
	.step_charge_current		= 1130,
	.step_charge_voltage		= 4200,
};

static struct mmi_battery_cell mmi_eb41_cell_data = {
	.capacity = 0xAE,
	.peak_voltage = 0xB9,
	.dc_impedance = 0x5E,
	.cell_id = 0x4157,
};

/* EB20 Tables and Charging Parameters */
static struct single_row_lut mmi_eb20_fcc_temp = {
	.x	= {-10, 23, 60},
	.y	= {1608, 1767, 1749},
	.cols	= 3,
};

static struct single_row_lut mmi_eb20_fcc_sf = {
	.x	= {1, 100, 200, 300, 400, 500},
	.y	= {100, 97, 95, 93, 92, 90},
	.cols	= 6,
};

static struct pc_sf_lut mmi_eb20_pc_sf = {
	.rows		= 10,
	.cols		= 5,
	.cycles		= {100, 200, 300, 400, 500},
	.percent	= {100, 90, 80, 70, 60, 50, 40, 30, 20, 10},
	.sf		= {
			{100, 100, 100, 100, 100},
			{100, 100, 100, 100, 100},
			{100, 100, 100, 100, 100},
			{100, 100, 100, 100, 100},
			{100, 100, 100, 100, 100},
			{100, 100, 100, 100, 100},
			{100, 100, 100, 100, 100},
			{100, 100, 100, 100, 100},
			{100, 100, 100, 100, 100},
			{100, 100, 100, 100, 100}
	},
};

static struct pc_temp_ocv_lut mmi_eb20_pc_temp_ocv = {
	.rows		= 29,
	.cols		= 3,
	.temp		= {-10, 23, 60},
	.percent	= {100, 95, 90, 85, 80, 75, 70, 65, 60, 55,
				50, 45, 40, 35, 30, 25, 20, 15, 10,
				9, 8, 7, 6, 5, 4, 3, 2, 1, 0
	},
	.ocv		= {
			{4310, 4310, 4310},
			{4278, 4272, 4260},
			{4231, 4227, 4216},
			{4177, 4172, 4162},
			{4125, 4120, 4111},
			{4076, 4071, 4062},
			{4020, 4023, 4017},
			{3967, 3981, 3975},
			{3923, 3941, 3936},
			{3886, 3892, 3887},
			{3855, 3854, 3851},
			{3831, 3830, 3827},
			{3811, 3810, 3806},
			{3796, 3795, 3788},
			{3784, 3782, 3766},
			{3770, 3768, 3741},
			{3747, 3740, 3716},
			{3715, 3704, 3676},
			{3700, 3687, 3665},
			{3698, 3685, 3663},
			{3695, 3684, 3660},
			{3690, 3680, 3655},
			{3681, 3671, 3640},
			{3659, 3644, 3610},
			{3620, 3601, 3568},
			{3564, 3548, 3514},
			{3492, 3475, 3445},
			{3390, 3376, 3349},
			{3200, 3200, 3200}
	},
};

static struct pm8921_bms_battery_data  mmi_eb20_metering_data = {
	.fcc			= 1767,
	.fcc_temp_lut		= &mmi_eb20_fcc_temp,
	.fcc_sf_lut		= &mmi_eb20_fcc_sf,
	.pc_temp_ocv_lut	= &mmi_eb20_pc_temp_ocv,
	.pc_sf_lut		= &mmi_eb20_pc_sf,
};

static struct pm8921_charger_battery_data mmi_eb20_charging_data = {
	.max_voltage			= 4350,
	.min_voltage			= 3200,
	.resume_voltage_delta		= 100,
	.term_current			= 80,
	.cool_temp			= 0,
	.warm_temp			= 45,
	.max_bat_chg_current		= 1678,
	.cool_bat_chg_current		= 0,
	.warm_bat_chg_current		= 0,
	.cool_bat_voltage		= 4000,
	.warm_bat_voltage		= 4000,
	.step_charge_current		= 1130,
	.step_charge_voltage		= 4200,
};

static struct mmi_battery_cell mmi_eb20_cell_data = {
	.capacity = 0xAF,
	.peak_voltage = 0xB9,
	.dc_impedance = 0x6D,
	.cell_id = 0x4241,
};

/* EB20 Preliminary Tables and Charging Parameters */
static struct pm8921_bms_battery_data  mmi_eb20_pre_metering_data = {
	.fcc			= 1767,
	.fcc_temp_lut		= &mmi_eb20_fcc_temp,
	.fcc_sf_lut		= &mmi_eb20_fcc_sf,
	.pc_temp_ocv_lut	= &mmi_eb20_pc_temp_ocv,
	.pc_sf_lut		= &mmi_eb20_pc_sf,
};

static struct pm8921_charger_battery_data mmi_eb20_pre_charging_data = {
	.max_voltage			= 4350,
	.min_voltage			= 3200,
	.resume_voltage_delta		= 100,
	.term_current			= 80,
	.cool_temp			= 0,
	.warm_temp			= 45,
	.max_bat_chg_current		= 1678,
	.cool_bat_chg_current		= 0,
	.warm_bat_chg_current		= 0,
	.cool_bat_voltage		= 4000,
	.warm_bat_voltage		= 4000,
	.step_charge_current		= 1130,
	.step_charge_voltage		= 4200,
};

static struct mmi_battery_cell mmi_eb20_pre_cell_data = {
	.capacity = 0xAE,
	.peak_voltage = 0xB9,
	.dc_impedance = 0x5A,
	.cell_id = 0x4241,
};

/* Default Tables and Charging Parameters */
static struct single_row_lut mmi_df_fcc_temp = {
	.x	= {-10, 23, 60},
	.y	= {1608, 1767, 1749},
	.cols	= 3,
};

static struct single_row_lut mmi_df_fcc_sf = {
	.x	= {1, 100, 200, 300, 400, 500},
	.y	= {100, 97, 95, 93, 92, 90},
	.cols	= 6,
};

static struct pc_sf_lut mmi_df_pc_sf = {
	.rows		= 10,
	.cols		= 5,
	.cycles		= {100, 200, 300, 400, 500},
	.percent	= {100, 90, 80, 70, 60, 50, 40, 30, 20, 10},
	.sf		= {
			{100, 100, 100, 100, 100},
			{100, 100, 100, 100, 100},
			{100, 100, 100, 100, 100},
			{100, 100, 100, 100, 100},
			{100, 100, 100, 100, 100},
			{100, 100, 100, 100, 100},
			{100, 100, 100, 100, 100},
			{100, 100, 100, 100, 100},
			{100, 100, 100, 100, 100},
			{100, 100, 100, 100, 100}
	},
};

static struct pc_temp_ocv_lut mmi_df_pc_temp_ocv = {
	.rows		= 29,
	.cols		= 3,
	.temp		= {-10, 23, 60},
	.percent	= {100, 95, 90, 85, 80, 75, 70, 65, 60, 55,
				50, 45, 40, 35, 30, 25, 20, 15, 10,
				9, 8, 7, 6, 5, 4, 3, 2, 1, 0
	},
	.ocv		= {
			{4310, 4310, 4310},
			{4278, 4272, 4260},
			{4231, 4227, 4216},
			{4177, 4172, 4162},
			{4125, 4120, 4111},
			{4076, 4071, 4062},
			{4020, 4023, 4017},
			{3967, 3981, 3975},
			{3923, 3941, 3936},
			{3886, 3892, 3887},
			{3855, 3854, 3851},
			{3831, 3830, 3827},
			{3811, 3810, 3806},
			{3796, 3795, 3788},
			{3784, 3782, 3766},
			{3770, 3768, 3741},
			{3747, 3740, 3716},
			{3715, 3704, 3676},
			{3700, 3687, 3665},
			{3698, 3685, 3663},
			{3695, 3684, 3660},
			{3690, 3680, 3655},
			{3681, 3671, 3640},
			{3659, 3644, 3610},
			{3620, 3601, 3568},
			{3564, 3548, 3514},
			{3492, 3475, 3445},
			{3390, 3376, 3349},
			{3200, 3200, 3200}
	},
};

static struct pm8921_bms_battery_data  mmi_df_metering_data = {
	.fcc			= 1767,
	.fcc_temp_lut		= &mmi_df_fcc_temp,
	.fcc_sf_lut		= &mmi_df_fcc_sf,
	.pc_temp_ocv_lut	= &mmi_df_pc_temp_ocv,
	.pc_sf_lut		= &mmi_df_pc_sf,
};

static struct pm8921_charger_battery_data mmi_df_charging_data = {
	.max_voltage			= 4200,
	.min_voltage			= 3200,
	.resume_voltage_delta		= 100,
	.term_current			= 80,
	.cool_temp			= 0,
	.warm_temp			= 45,
	.max_bat_chg_current		= 1678,
	.cool_bat_chg_current		= 0,
	.warm_bat_chg_current		= 0,
	.cool_bat_voltage		= 3800,
	.warm_bat_voltage		= 3800,
	.step_charge_current		= 0,
	.step_charge_voltage		= 0,
};

static struct mmi_battery_cell mmi_df_cell_data = {
	.capacity = 0xFF,
	.peak_voltage = 0xFF,
	.dc_impedance = 0xFF,
	.cell_id = 0x0000,
};

struct mmi_battery_list mmi_batts = {
	.num_cells = MMI_BATTERY_NUM,
	.cell_list = {
		&mmi_df_cell_data,
		&mmi_eb20_cell_data,
		&mmi_eb20_pre_cell_data,
		&mmi_eb41_cell_data,
		&mmi_ev30_cid5858_cell_data,
		&mmi_ev30_cid_4246_cell_data,
		&mmi_eg30_sdi_cell_data,
	},
	.bms_list = {
		 &mmi_df_metering_data,
		 &mmi_eb20_metering_data,
		 &mmi_eb20_pre_metering_data,
		 &mmi_eb41_metering_data,
		 &mmi_ev30_metering_data,
		 &mmi_ev30_metering_data,
		 &mmi_eg30_sdi_metering_data,
	 },
	.chrg_list = {
		 &mmi_df_charging_data,
		 &mmi_eb20_charging_data,
		 &mmi_eb20_pre_charging_data,
		 &mmi_eb41_charging_data,
		 &mmi_ev30_charging_data,
		 &mmi_ev30_charging_data,
		 &mmi_eg30_sdi_charging_data,
	 },
};
