/* FIXME: insert comments */

#include <linux/delay.h>
#include <linux/types.h>
#include <linux/i2c.h>
#include <linux/uaccess.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/regulator/consumer.h>
#include <media/msm_camera.h>
#include <media/v4l2-subdev.h>
#include <mach/gpio.h>
#include <mach/camera.h>
#include <asm/mach-types.h>
#include "msm.h"
#include "msm_sensor.h"
#include "msm_ispif.h"

#define MOTSOC1_DEFAULT_MCLK_RATE        24000000

#define ISP_CORE_EN 95
#define CAM1_REG_EN 54
#define ISP_RESET 97

struct motsoc1_work_t {
	struct work_struct work;
};
static struct motsoc1_work_t *motsoc1_sensorw;
static struct i2c_client *motsoc1_client;
static struct msm_sensor_ctrl_t motsoc1_s_ctrl;

static DECLARE_WAIT_QUEUE_HEAD(motsoc1_wait_queue);
DEFINE_MUTEX(motsoc1_mutex);

static int32_t config_csi;
static int32_t config_sensor;

static struct regulator *reg_2p7, *reg_1p2, *reg_1p8;

static struct v4l2_subdev_info motsoc1_subdev_info[] = {
	{
		.code       = V4L2_MBUS_FMT_YUYV8_2X8,
		.colorspace = V4L2_COLORSPACE_JPEG,
		.fmt        = 1,
		.order      = 0,
	},
	{ /* this is a lie, needed for capture */
		.code       = V4L2_MBUS_FMT_SBGGR10_1X10,
		.colorspace = V4L2_COLORSPACE_JPEG,
		.fmt        = 1,
		.order      = 0,
	},
};

/* The indexes to this table are of type msm_sensor_resolution_t, determined
 * from motsoc1_mode_res table in motsoc1_u.c */
static struct msm_sensor_output_info_t motsoc1_dimensions[] = {
	{ /* MSM_SENSOR_RES_FULL */
		/* Used for raw/jpeg capture.  Actual x dimension is 2048, but
		   since this is a "YUV" sensor, the x dimension is multiplied
		   by 2 */
		.x_output = 1024,
		.y_output = 2560,
		.line_length_pclk = 1024,
		.frame_length_lines = 2560,
		.vt_pixel_clk = 200000000,
		.op_pixel_clk = 200000000,
	},
	{ /* MSM_SENSOR_RES_QTR */
		/* use for preview/video */
		.x_output = 1920,
		.y_output = 1080,
		.line_length_pclk = 1920,
		.frame_length_lines = 1080,
		.vt_pixel_clk = 200000000,
		.op_pixel_clk = 200000000,
	},
};

static int motsoc1_allow_i2c_errors;
static int motsoc1_is_active;

static int motsoc1_i2c_txrx(
		unsigned char *txbuf, int txlen,
		unsigned char *rxbuf, int rxlen)
{
	int rc = 0;
	int msgnum = 2;
	struct i2c_msg msg[] = {
		{ /* TX */
			.addr = motsoc1_client->addr,
			.flags = 0,
			.len = txlen,
			.buf = txbuf,
		},
		{ /* RX */
			.addr = motsoc1_client->addr,
			.flags = I2C_M_RD,
			.len = rxlen,
			.buf = rxbuf,
		},
	};
	if (rxbuf == NULL || rxlen == 0)
		msgnum = 1;
	rc = i2c_transfer(motsoc1_client->adapter, msg, msgnum);
	if (rc < 0 && !motsoc1_allow_i2c_errors)
		pr_err("motsoc1_i2c_txrx: error %d\n", rc);
	return rc;
}

#ifdef MOTSOC1_DEBUG
static int motsoc1_readRam(
		unsigned long ramaddr, unsigned char *buf, unsigned long len)
{
	int rc = 0;
	unsigned char tb[20];
	unsigned char rb[100];

	if (len > (sizeof(rb)-3)) {
		pr_err("* read request too long\n");
		return -EINVAL;
	}

	tb[0] = 0x00;
	tb[1] = 0x03;
	tb[2] = 0xFF & (ramaddr >> 24);
	tb[3] = 0xFF & (ramaddr >> 16);
	tb[4] = 0xFF & (ramaddr >> 8);
	tb[5] = 0xFF & (ramaddr);
	tb[6] = 0xFF & (len >> 8);
	tb[7] = 0xFF & (len);

	rc = motsoc1_i2c_txrx(tb, 8, rb, (len+3));
	if (rc < 0)
		pr_err("motsoc1_readRam: error %d\n", rc);

	memcpy(buf, &rb[3], len);

	return rc;
}
#endif

static int motsoc1_setCP(unsigned char cat, unsigned char param,
		unsigned char *buf, unsigned char len)
{
	int rc = 0;
	unsigned char txbuf[32];
	if (len > 28) {
		pr_err("motsoc1_setCP: buffer too long (%d)\n", len);
		return -EIO;
	}
	txbuf[0] = len + 4;
	txbuf[1] = 0x02;
	txbuf[2] = cat;
	txbuf[3] = param;
	memcpy(&txbuf[4], buf, len);
	rc = motsoc1_i2c_txrx(txbuf, txbuf[0], 0, 0);
	if (rc < 0)
		pr_err("motsoc1_setCP: error %d on %02x/%02x\n",
				rc, cat, param);
	return rc;
}
static int motsoc1_setCP1(unsigned char cat, unsigned char param,
		unsigned char val)
{
	unsigned char b[1];
	b[0] = val;
	return motsoc1_setCP(cat, param, b, 1);
}
#ifdef FUNCUSE
static int motsoc1_setCP2(unsigned char cat, unsigned char param,
		unsigned short val)
{
	unsigned char b[2];
	b[0] = val >> 8;
	b[1] = val & 0xFF;
	return motsoc1_setCP(cat, param, b, 2);
}
#endif
static int motsoc1_setCP4(unsigned char cat, unsigned char param,
		unsigned long val)
{
	unsigned char b[4];
	b[0] = val >> 24;
	b[1] = val >> 16;
	b[2] = val >> 8;
	b[3] = val & 0xFF;
	return motsoc1_setCP(cat, param, b, 4);
}

static int motsoc1_getCP(unsigned char cat, unsigned char param,
		unsigned char *buf, unsigned char len)
{
	int rc = 0;
	unsigned char txbuf[5];
	unsigned char rxbuf[32];
	if (len > 31) {
		pr_err("motsoc1_getCP: buffer too long (%d)\n", len);
		return -EIO;
	}
	txbuf[0] = 0x05;
	txbuf[1] = 0x01;
	txbuf[2] = cat;
	txbuf[3] = param;
	txbuf[4] = len;
	rc = motsoc1_i2c_txrx(txbuf, txbuf[0], rxbuf, len+1);
	if (rc < 0 && !motsoc1_allow_i2c_errors)
		pr_err("motsoc1_getCP: error %d on %02x/%02x\n",
				rc, cat, param);
	memcpy(buf, &rxbuf[1], len);
	return rc;
}
static int motsoc1_getCP1(unsigned char cat, unsigned char param,
		unsigned char *val)
{
	unsigned char b[1];
	int rc = motsoc1_getCP(cat, param, b, 1);
	if (val && rc >= 0)
		*val = b[0];
	return rc;
}
#ifdef FUNCUSE
static int motsoc1_getCP2(unsigned char cat, unsigned char param,
		unsigned short *val)
{
	unsigned char b[2];
	int rc = motsoc1_getCP(cat, param, b, 2);
	if (val && rc >= 0)
		*val = (b[0]*0x100 + b[1]);
	return rc;
}
#endif
static int motsoc1_getCP4(unsigned char cat, unsigned char param,
		unsigned long *val)
{
	unsigned char b[4];
	int rc = motsoc1_getCP(cat, param, b, 4);
	if (val && rc >= 0)
		*val = (b[0]*0x1000000 + b[1]*0x10000 + b[2]*0x100 + b[3]);
	return rc;
}
static int motsoc1_pollCP1(unsigned char cat, unsigned char param,
		unsigned char mask, unsigned char val,
		unsigned short pollcnt, unsigned short pollms)
{
	int rc = 0;
	int i;
	unsigned char b;
	for (i = 0; i < pollcnt; i++) {
		/* for polling, allow i2c errors */
		motsoc1_allow_i2c_errors = true;
		rc = motsoc1_getCP1(cat, param, &b);
		motsoc1_allow_i2c_errors = false;
		if (rc >= 0)
			if (val == (b & mask))
				break;
		msleep(pollms);
	}
	if (i == pollcnt) {
		pr_err("motsoc1_pollCP1: timeout on %02x/%02x\n", cat, param);
		return -EIO;
	}
	return rc;
}

static int motsoc1_camstart(void)
{
	int rc = 0;
	unsigned char byte;

	rc = motsoc1_getCP1(0x0, 0x00, &byte);
	if (rc < 0)
		return rc;
	if (byte == 0x44)
		return 0;

	rc = motsoc1_setCP1(0xF, 0x12, 0x01);
	if (rc < 0)
		return rc;
	rc = motsoc1_pollCP1(0x0, 0x1C, 0x01, 0x01, 20, 10);
	if (rc < 0)
		return rc;
	rc = motsoc1_getCP1(0x0, 0x00, &byte);
	if (byte != 0x44) {
		pr_err("motsoc1_camstart: incorrect customer code (0x%02x)\n",
				byte);
		return -EIO;
	}

	return 0;
}

static int32_t motsoc1_sensor_setting(int update_type, int rt)
{
	int32_t rc = 0;
	struct msm_camera_csid_params    motsoc1_csid_params;
	struct msm_camera_csiphy_params  motsoc1_csiphy_params;

	pr_info("motsoc1_sensor_setting ut=%d rt=%d\n", update_type, rt);

	v4l2_subdev_notify(motsoc1_s_ctrl.sensor_v4l2_subdev,
		NOTIFY_ISPIF_STREAM, (void *)ISPIF_STREAM(
		PIX0, ISPIF_OFF_IMMEDIATELY));
	/*TODO: add way to ensure we are not streaming*/
	msleep(30);

	switch (update_type) {
	case MSM_SENSOR_REG_INIT:
		/*
		if (rt == SENSOR_PREVIEW_MODE || rt == SENSOR_SNAPSHOT_MODE) {
		}
		*/
		break;
	case MSM_SENSOR_UPDATE_PERIODIC:
		if (rt == SENSOR_PREVIEW_MODE) {
			if (config_csi == 0) {
				struct msm_camera_csid_vc_cfg
					motsoc1_vccfg[] = {
					{0, 0x1E, CSI_DECODE_8BIT},
					{1, CSI_EMBED_DATA, CSI_DECODE_8BIT},
				};
				pr_info("motsoc1: configuring CSI/CSIPHY"
						" for preview\n");
				motsoc1_csid_params.lane_cnt = 2;
				motsoc1_csid_params.lane_assign = 0xe4;
				motsoc1_csid_params.lut_params.num_cid =
					ARRAY_SIZE(motsoc1_vccfg);
				motsoc1_csid_params.lut_params.vc_cfg =
					&motsoc1_vccfg[0];
				motsoc1_csiphy_params.lane_cnt = 2;
				motsoc1_csiphy_params.settle_cnt = 7;
				v4l2_subdev_notify(
					motsoc1_s_ctrl.sensor_v4l2_subdev,
					NOTIFY_CSID_CFG,
					&motsoc1_csid_params);
				v4l2_subdev_notify(
					motsoc1_s_ctrl.sensor_v4l2_subdev,
					NOTIFY_CID_CHANGE, NULL);
				mb();
				v4l2_subdev_notify(
					motsoc1_s_ctrl.sensor_v4l2_subdev,
					NOTIFY_CSIPHY_CFG,
					&motsoc1_csiphy_params);
				mb();
				usleep_range(20000, 20000);
				config_csi = 1;
			}

			v4l2_subdev_notify(motsoc1_s_ctrl.sensor_v4l2_subdev,
				NOTIFY_PCLK_CHANGE,
				&motsoc1_dimensions[0].op_pixel_clk);
			v4l2_subdev_notify(motsoc1_s_ctrl.sensor_v4l2_subdev,
				NOTIFY_ISPIF_STREAM,
				(void *)ISPIF_STREAM(
					PIX0, ISPIF_ON_FRAME_BOUNDARY));

			if (config_sensor == 0) {
				/* send cam-start if necessary */
				rc = motsoc1_camstart();
				if (rc < 0)
					return rc;
				/* set res to 1920x1080 */
				rc = motsoc1_setCP1(0x1, 0x01, 0x25);
				if (rc < 0)
					return rc;
				/* FIXME: set movie caf,
				 * eventually only enable when recording */
				rc = motsoc1_setCP1(0x0A, 0x4D, 0x01);
				if (rc < 0)
					return rc;
				/* go to monitor mode */
				rc = motsoc1_setCP1(0x0, 0x0B, 0x02);
				if (rc < 0)
					return rc;
				/* poll for transition */
				rc = motsoc1_pollCP1(
						0x0, 0x0C, 0xFF, 0x02, 20, 10);
				if (rc < 0)
					return rc;
				msleep(40);

				/*FIXME start hardcodings for focusing*/
				/* set af mode to caf*/
				rc = motsoc1_setCP1(0x0A, 0x00, 0x06);
				if (rc < 0)
					return rc;
				/* set af mode to start*/
				rc = motsoc1_setCP1(0x0A, 0x02, 0x01);
				if (rc < 0)
					return rc;
				/*FIXME end hardcodings for focusing*/

				pr_info("motsoc1: sensor started\n");
				config_sensor = 1;
			}
		} else if (rt == SENSOR_SNAPSHOT_MODE) {
#define CHK(_rc_) {if ((_rc_) < 0) \
	{pr_err("motsoc1: failed line %d\n", __LINE__); return rc; } }
			struct msm_camera_csid_vc_cfg motsoc1_vccfg[] = {
				{0, 0x30, CSI_DECODE_8BIT},
				{1, CSI_EMBED_DATA, CSI_DECODE_8BIT},
			};
			pr_info("motsoc1: performing still capture\n");
			/* enable int */
			CHK(motsoc1_setCP1(0x00, 0x10, 0x88));
			/* set still mode */
			CHK(motsoc1_setCP1(0x00, 0x0B, 0x03));
			/* poll for shutter - FIXME */
			/* poll for capture int */
			CHK(motsoc1_pollCP1(0x00, 0x1C, 0xFF, 0x08, 200, 50));

			/* select main frame #1 */
			CHK(motsoc1_setCP1(0x0C, 0x06, 0x01));
			/* set max jpeg size 5MB */
			CHK(motsoc1_setCP4(0x0B, 0x0F, (5*1024*1024)));
			/* set output format JPEG(with header:422) */
			CHK(motsoc1_setCP1(0x0B, 0x00, 0x01));
			/* set image size (13MP) */
			CHK(motsoc1_setCP1(0x0B, 0x01, 0x2C));
			pr_info("motsoc1: configuring CSI/CSIPHY for capture\n");
			motsoc1_csid_params.lane_cnt = 2;
			motsoc1_csid_params.lane_assign = 0xe4;
			motsoc1_csid_params.lut_params.num_cid =
				ARRAY_SIZE(motsoc1_vccfg);
			motsoc1_csid_params.lut_params.vc_cfg =
				&motsoc1_vccfg[0];
			motsoc1_csiphy_params.lane_cnt = 2;
			motsoc1_csiphy_params.settle_cnt = 7;
			v4l2_subdev_notify(
				motsoc1_s_ctrl.sensor_v4l2_subdev,
				NOTIFY_CSID_CFG,
				&motsoc1_csid_params);
			v4l2_subdev_notify(
				motsoc1_s_ctrl.sensor_v4l2_subdev,
				NOTIFY_CID_CHANGE, NULL);
			mb();
			v4l2_subdev_notify(motsoc1_s_ctrl.sensor_v4l2_subdev,
				NOTIFY_CSIPHY_CFG,
				&motsoc1_csiphy_params);
			if (rc < 0) {
				pr_err("motsoc1: failed csiphy config (%d)\n",
						rc);
				return rc;
			}
			mb();
			usleep_range(10000, 10000);

			v4l2_subdev_notify(motsoc1_s_ctrl.sensor_v4l2_subdev,
				NOTIFY_PCLK_CHANGE,
				&motsoc1_dimensions[2].op_pixel_clk);
			usleep_range(10000, 10000);
			v4l2_subdev_notify(motsoc1_s_ctrl.sensor_v4l2_subdev,
				NOTIFY_ISPIF_STREAM,
				(void *)ISPIF_STREAM(
					PIX0, ISPIF_ON_FRAME_BOUNDARY));

			config_csi = 0;
			config_sensor = 0;
		} else {
			panic("motsoc1_sensor_setting unhandled mode\n");
		}
		break;
	default:
		panic("motsoc1_sensor_setting unhandled type\n");
		rc = -EINVAL;
		break;
	}
return rc;
}

int32_t motsoc1_active(void)
{
	return motsoc1_is_active;
}

int32_t motsoc1_start_transfer(void)
{
	int32_t rc = 0;
	unsigned long jpegsize = 0;
	pr_info("motsoc1: transfer start\n");
	/* start transfer */
	CHK(motsoc1_setCP1(0x0C, 0x09, 0x01));
	/* poll for transfer complete */
	CHK(motsoc1_pollCP1(0x00, 0x1C, 0xFF, 0x08, 200, 50));
	pr_info("motsoc1: transfer complete\n");
	/* read jpeg size */
	CHK(motsoc1_getCP4(0x0C, 0x0D, &jpegsize));
	pr_info("motsoc1: jpeg size: %lu\n", jpegsize);

#ifdef DEBUGUSE
	{
		unsigned char buf[20];
		rc = motsoc1_readRam(0x80000000, buf, 20);
		if (rc < 0) {
			pr_err("* could not read ram %d\n", rc);
		} else {
			int i;
			for (i = 0; i < 20; i++)
				pr_info("motsoc1: jpeg: %d -> 0x%02x\n",
						i, buf[i]);
		}
	}
#endif

	return rc;
}

static int32_t motsoc1_video_config(void)
{
	int32_t rc = 0;
	rc = motsoc1_sensor_setting(
			MSM_SENSOR_UPDATE_PERIODIC, SENSOR_PREVIEW_MODE);
	return rc;
}

static int32_t motsoc1_snapshot_config(void)
{
	int32_t rc = 0;
	rc = motsoc1_sensor_setting(
			MSM_SENSOR_UPDATE_PERIODIC, SENSOR_SNAPSHOT_MODE);
	return rc;
}

static int32_t motsoc1_raw_snapshot_config(void)
{
	return motsoc1_snapshot_config();
}

static int32_t motsoc1_set_sensor_mode(int mode, int res)
{
	int32_t rc = 0;
	pr_info("motsoc1_set_sensor_mode mode=%d res=%d\n", mode, res);
	switch (mode) {
	/*case SENSOR_PREVIEW_MODE:*/
	case 2: /*SENSOR_MODE_PREVIEW*/
	case 3: /*SENSOR_MODE_VIDEO*/
		rc = motsoc1_video_config();
		break;
	/*case SENSOR_SNAPSHOT_MODE:*/
	case 0: /*SENSOR_MODE_SNAPSHOT*/
		rc = motsoc1_snapshot_config();
		break;
	/*case SENSOR_RAW_SNAPSHOT_MODE:*/
	case 1: /*SENSOR_MODE_RAW_SNAPSHOT*/
		rc = motsoc1_raw_snapshot_config();
		break;
	default:
		panic("motsoc1_set_sensor_mode unhandled mode\n");
		rc = -EINVAL;
		break;
	}
	return rc;
}

static int32_t motsoc1_regulator_on(
		struct regulator **reg, char *regname, int uV)
{
	int32_t rc = 0;
	pr_info("motsoc1_regulator_on: %s %d\n",
			regname, uV);

	*reg = regulator_get(NULL, regname);
	if (IS_ERR(*reg)) {
		pr_err("motsoc1: failed to get %s (%ld)\n",
				regname, PTR_ERR(*reg));
		goto reg_on_done;
	}
	rc = regulator_set_voltage(*reg, uV, uV);
	if (rc) {
		pr_err("motsoc1: failed to set voltage for %s (%d)\n",
				regname, rc);
		goto reg_on_done;
	}
	rc = regulator_enable(*reg);
	if (rc) {
		pr_err("motsoc1: failed to enable %s (%d)\n",
				regname, rc);
		goto reg_on_done;
	}
reg_on_done:
	return rc;
}

static int32_t motsoc1_regulator_off(
		struct regulator *reg, char *regname)
{
	int32_t rc = 0;

	if (reg) {
		pr_info("motsoc1_regulator_off: %s\n",
				regname);

		rc = regulator_disable(reg);
		if (rc) {
			pr_err("motsoc1: failed to disable %s (%d)\n",
					regname, rc);
			goto reg_off_done;
		}
		regulator_put(reg);
	}
reg_off_done:
	return rc;
}

static int32_t motsoc1_power_on(const struct msm_camera_sensor_info *data)
{
	int32_t rc = 0;
	int use_l17 = 0;
	int use_l29 = 0;

	pr_info("motsoc1_power_on (%04x)\n", system_rev);
	mutex_lock(&motsoc1_mutex);

	/* Teufel P1/P2, Qinara P1 use L17 instead of L8 */
	if ((machine_is_teufel() && system_rev < 0x8300) ||
		(machine_is_qinara() && system_rev < 0x8200))
		use_l17 = 1;

	/* Teufel P1/P2, Qinara P1, Vanquish P1 use L29 instead of L4 */
	if ((machine_is_teufel() && system_rev < 0x8300) ||
		(machine_is_qinara() && system_rev < 0x8200) ||
		(machine_is_vanquish() && system_rev < 0x8200))
		use_l29 = 1;

	if (use_l17)
		rc = motsoc1_regulator_on(&reg_2p7, "8921_l17", 2700000);
	else
		rc = motsoc1_regulator_on(&reg_2p7, "8921_l8", 2700000);
	if (rc < 0)
		goto power_on_done;

	rc = motsoc1_regulator_on(&reg_1p2, "8921_l12", 1200000);
	if (rc < 0)
		goto power_on_done;

	if (use_l29)
		rc = motsoc1_regulator_on(&reg_1p8, "8921_l29", 1800000);
	else
		reg_1p8 = NULL; /* 8921_l4 always on */
	if (rc < 0)
		goto power_on_done;

	/* obtain gpios */
	rc = gpio_request(ISP_CORE_EN, "motsoc1");
	if (rc < 0) {
		pr_err("motsoc1: gpio request ISP_CORE_EN failed (%d)\n", rc);
		goto power_on_done;
	}
	rc = gpio_request(CAM1_REG_EN, "motsoc1");
	if (rc < 0) {
		pr_err("motsoc1: gpio request CAM1_REG_EN failed (%d)\n", rc);
		goto power_on_done;
	}
	rc = gpio_request(ISP_RESET, "motsoc1");
	if (rc < 0) {
		pr_err("motsoc1: gpio request ISP_RESET failed (%d)\n", rc);
		goto power_on_done;
	}

	/* enable supply to core */
	pr_info("motsoc1: enable ISP_CORE_EN\n");
	gpio_direction_output(ISP_CORE_EN, 1);

	/* enable cam1 regs */
	pr_info("motsoc1: enable CAM1_REG_EN\n");
	gpio_direction_output(CAM1_REG_EN, 1);

	/* set mclk */
	msm_camio_clk_rate_set(MOTSOC1_DEFAULT_MCLK_RATE);
	usleep_range(1000, 2000);

	/* toggle reset */
	pr_info("motsoc1: toggle reset\n");
	gpio_direction_output(ISP_RESET, 0);
	usleep_range(5000, 6000);
	gpio_set_value_cansleep(ISP_RESET, 1);
	msleep(50);

	motsoc1_is_active = true;
	pr_info("motsoc1 power on complete\n");
power_on_done:
	mutex_unlock(&motsoc1_mutex);
	return rc;
}

static int32_t motsoc1_power_off(const struct msm_camera_sensor_info *data)
{
	pr_info("motsoc1_power_off\n");
	mutex_lock(&motsoc1_mutex);

	/* assert ISP_RESET */
	gpio_direction_output(ISP_RESET, 0);

	/* turn off mclk */
	/* FIXME: might be done outside this driver */

	/* disable cam1 regs */
	gpio_direction_output(CAM1_REG_EN, 0);

	/* disable core supply */
	gpio_direction_output(ISP_CORE_EN, 0);

	/* free gpios */
	gpio_free(ISP_RESET);
	gpio_free(CAM1_REG_EN);
	gpio_free(ISP_CORE_EN);

	motsoc1_regulator_off(reg_2p7, "2.7");
	motsoc1_regulator_off(reg_1p2, "1.2");
	motsoc1_regulator_off(reg_1p8, "1.8");

	motsoc1_is_active = false;

	mutex_unlock(&motsoc1_mutex);
	return 0;
}

static int motsoc1_probe_init_done(const struct msm_camera_sensor_info *data)
{
	CDBG("motsoc1_probe_init_done\n");
	return motsoc1_power_off(data);
}

static int motsoc1_probe_init_sensor(const struct msm_camera_sensor_info *data)
{
	int32_t rc = 0;
	unsigned char byte = 0;
	pr_info("motsoc1_probe_init_sensor\n");

	rc = motsoc1_power_on(data);
	if (rc < 0)
		goto init_probe_fail;

	/* Device will be in flash mode after releasing reset.  We won't read
	 * any kind of chipid here because the device may not have been
	 * flashed yet.  Just make sure an i2c transaction doesn't fail and
	 * returns 0xFF, indicating flash mode
	 */
	rc = motsoc1_getCP1(0, 0, &byte);
	if (rc < 0) {
		pr_err("motsoc1: i2c read failed (%d)\n", rc);
		goto init_probe_fail;
	}
	if (byte != 0xFF) {
		pr_err("motsoc1: mode check failed (0x%02x)\n", byte);
		rc = -ENODEV;
		goto init_probe_fail;
	}

	goto init_probe_done;
init_probe_fail:
	motsoc1_probe_init_done(data);
	return rc;
init_probe_done:
	pr_info("motsoc1_probe_init_sensor done\n");
	return rc;
}

int motsoc1_sensor_open_init(const struct msm_camera_sensor_info *data)
{
	int32_t rc = 0;
	pr_info("motsoc1_sensor_open_init\n");
	config_csi = 0;
	config_sensor = 0;

	if (data)
		motsoc1_s_ctrl.sensordata = data;

	rc = motsoc1_probe_init_sensor(data);
	if (rc < 0) {
		pr_err("motsoc1_sensor_open_init fail (%d)\n", rc);
		goto probe_fail;
	}

	rc = motsoc1_sensor_setting(MSM_SENSOR_REG_INIT, SENSOR_PREVIEW_MODE);
	if (rc < 0) {
		pr_err("motsoc1_sensor_setting (%d)\n", rc);
		goto init_fail;
	}
	goto init_done;
init_fail:
	motsoc1_probe_init_done(data);
probe_fail:
	return rc;
init_done:
	pr_info("motsoc1_sensor_open_init done\n");
	return rc;
}

static int motsoc1_init_client(struct i2c_client *client)
{
	init_waitqueue_head(&motsoc1_wait_queue);
	return 0;
}

static const struct i2c_device_id motsoc1_i2c_id[] = {
	{"motsoc1", (kernel_ulong_t)&motsoc1_s_ctrl},
	{ }
};

static struct msm_camera_i2c_client motsoc1_sensor_i2c_client = {
};

static int motsoc1_i2c_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	int rc = 0;
	pr_info("motsoc1_i2c_probe called\n");

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		pr_err("motsoc1_probe: i2c_check_functionality failed\n");
		/* FIXME rc = ??? */
		goto probe_failure;
	}

	motsoc1_sensorw = kzalloc(sizeof(struct motsoc1_work_t), GFP_KERNEL);
	if (!motsoc1_sensorw) {
		pr_err("motsoc1_probe: kzalloc failed\n");
		rc = -ENOMEM;
		goto probe_failure;
	}

	i2c_set_clientdata(client, motsoc1_sensorw);
	motsoc1_init_client(client);
	motsoc1_client = client;
	motsoc1_s_ctrl.sensor_i2c_client->client = client;

	pr_info("motsoc1_i2c_probe successful\n");
	return rc;
probe_failure:
	pr_err("motsoc1_i2c_probe failed %d\n", rc);
	return rc;
}

static int __exit motsoc1_remove(struct i2c_client *client)
{
	struct motsoc1_work_t *sensorw = i2c_get_clientdata(client);
	free_irq(client->irq, sensorw);
	motsoc1_client = NULL;
	kfree(sensorw);
	return 0;
}

static struct i2c_driver motsoc1_i2c_driver = {
	.id_table = motsoc1_i2c_id,
	.probe    = motsoc1_i2c_probe,
	.remove   = __exit_p(motsoc1_i2c_remove),
	.driver   = {
		.name = "motsoc1",
	},
};

int32_t motsoc1_sensor_get_output_info(struct msm_sensor_ctrl_t *s_ctrl,
		struct sensor_output_info_t *info)
{
	panic("motsoc1_sensor_get_output_info not implemented\n");
}

int motsoc1_sensor_config(void __user *argp)
{
	struct sensor_cfg_data cdata;
	int rc = 0;

	if (copy_from_user(&cdata, (void *)argp,
				sizeof(struct sensor_cfg_data)))
		return -EFAULT;

	mutex_lock(&motsoc1_mutex);

	pr_info("motsoc1_sensor_config: cfgtype = %d\n", cdata.cfgtype);
	switch (cdata.cfgtype) {
	case CFG_SET_MODE:
		rc = motsoc1_set_sensor_mode(cdata.mode, cdata.rs);
		break;
	case CFG_PWR_DOWN:
		/* FIXME: what to do here? */
		/*rc = motsoc1_power_down();*/
		break;
	case CFG_GET_OUTPUT_INFO:
		cdata.cfg.output_info.num_info = ARRAY_SIZE(motsoc1_dimensions);
		if (copy_to_user((void *)cdata.cfg.output_info.output_info,
				motsoc1_dimensions,
				sizeof(struct msm_sensor_output_info_t) *
				ARRAY_SIZE(motsoc1_dimensions))) {
			rc = -EFAULT;
			break;
		}
		if (copy_to_user((void *)argp,
					&cdata,
					sizeof(struct sensor_cfg_data)))
			rc = -EFAULT;
		break;
	case CFG_SENSOR_INIT:
		break;
	default:
		pr_err("motsoc1_sensor_config: unhandled %d\n",
				cdata.cfgtype);
		rc = -EFAULT;
		break;
	}

	mutex_unlock(&motsoc1_mutex);

	return rc;
}

static int motsoc1_sensor_release(void)
{
	pr_info("motsoc1_sensor_release\n");
	return motsoc1_power_off(motsoc1_s_ctrl.sensordata);
}

static int motsoc1_sensor_probe(struct msm_sensor_ctrl_t *s_ctrl,
		const struct msm_camera_sensor_info *info,
		struct msm_sensor_ctrl *s)
{
	int rc = 0;
	rc = i2c_add_driver(&motsoc1_i2c_driver);
	if (rc < 0 || motsoc1_client == NULL) {
		rc = -ENOTSUPP;
		goto probe_fail;
	}
	/*msm_camio_clk_rate_set(MOTSOC1_DEFAULT_MCLK_RATE);*/
	rc = motsoc1_probe_init_sensor(info);
	if (rc < 0)
		goto probe_fail;

	s->s_init    = motsoc1_sensor_open_init;
	s->s_release = motsoc1_sensor_release;
	s->s_config  = motsoc1_sensor_config;
	s->s_camera_type = BACK_CAMERA_2D;
	s->s_mount_angle = info->sensor_platform_info->mount_angle;

	motsoc1_probe_init_done(info);
	return rc;

probe_fail:
	pr_err("motsoc1_sensor_probe: sensor probe failed! (%d)\n", rc);
	i2c_del_driver(&motsoc1_i2c_driver);
	return rc;
}

static int motsoc1_enum_fmt(struct v4l2_subdev *sd, unsigned int index,
		enum v4l2_mbus_pixelcode *code)
{
	pr_info("motsoc1_enum_fmt: index=%d\n", index);
	if ((unsigned int)index >= ARRAY_SIZE(motsoc1_subdev_info))
		return -EINVAL;
	*code = motsoc1_subdev_info[index].code;
	return 0;
}

static struct v4l2_subdev_core_ops motsoc1_subdev_core_ops;
static struct v4l2_subdev_video_ops motsoc1_subdev_video_ops = {
	.enum_mbus_fmt = motsoc1_enum_fmt,
};

static struct v4l2_subdev_ops motsoc1_subdev_ops = {
	.core  = &motsoc1_subdev_core_ops,
	.video = &motsoc1_subdev_video_ops,
};

static int motsoc1_sensor_v4l2_probe(const struct msm_camera_sensor_info *info,
		struct v4l2_subdev *sdev, struct msm_sensor_ctrl *s)
{
	return msm_sensor_v4l2_probe(&motsoc1_s_ctrl, info, sdev, s);
}

static int motsoc1_probe(struct platform_device *pdev)
{
	return msm_sensor_register(pdev, motsoc1_sensor_v4l2_probe);
}

static struct platform_driver motsoc1_driver = {
	.probe  = motsoc1_probe,
	.driver = {
		.name  = "msm_camera_motsoc1",
		.owner = THIS_MODULE,
	},
};

static int __init motsoc1_init_module(void)
{
	return platform_driver_register(&motsoc1_driver);
}

static struct msm_sensor_fn_t motsoc1_func_tbl = {
	.sensor_get_output_info = motsoc1_sensor_get_output_info,
	.sensor_config          = motsoc1_sensor_config,
	.sensor_open_init       = motsoc1_sensor_open_init,
	.sensor_release         = motsoc1_sensor_release,
	.sensor_power_up        = motsoc1_power_on,
	.sensor_power_down      = motsoc1_power_off,
	.sensor_probe           = motsoc1_sensor_probe,
};

static struct msm_sensor_ctrl_t motsoc1_s_ctrl = {
	.sensor_i2c_client            = &motsoc1_sensor_i2c_client,
	.msm_sensor_mutex             = &motsoc1_mutex,
	.sensor_i2c_driver            = &motsoc1_i2c_driver,
	.sensor_v4l2_subdev_info      = motsoc1_subdev_info,
	.sensor_v4l2_subdev_info_size = ARRAY_SIZE(motsoc1_subdev_info),
	.sensor_v4l2_subdev_ops       = &motsoc1_subdev_ops,
	.func_tbl                     = &motsoc1_func_tbl,
};

module_init(motsoc1_init_module);
MODULE_DESCRIPTION("Motorola SOC #1");
MODULE_LICENSE("GPL v2");
