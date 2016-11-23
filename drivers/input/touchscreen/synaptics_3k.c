/* drivers/input/touchscreen/synaptics_3k.c - Synaptics 3k serious touch panel driver
 *
 * Copyright (C) 2010 HTC Corporation.
 *
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

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/hrtimer.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/synaptics_i2c_rmi.h>
#include <linux/slab.h>

#define SYN_I2C_RETRY_TIMES 10

struct synaptics_ts_data {
	uint16_t addr;
	struct i2c_client *client;
	struct input_dev *input_dev;
	struct workqueue_struct *syn_wq;
	int use_irq;
	struct hrtimer timer;
	struct work_struct  work;
	uint16_t max[2];
	uint32_t flags;
	int8_t sensitivity_adjust;
	uint8_t finger_support;
	uint16_t finger_pressed;
	int (*power)(int on);
	struct page_description page_table[18];
	int pre_finger_data[11][2];
	uint8_t debug_log_level;
	uint32_t raw_base;
	uint32_t raw_ref;
	uint64_t timestamp;
};

static struct synaptics_ts_data *gl_ts;
static const char SYNAPTICSNAME[]	= "Synaptics_3K";
static uint32_t syn_panel_version;

static bool scr_suspended = false;

#ifdef CONFIG_TOUCHSCREEN_SYNAPTICS_SWEEP2WAKE
/* S2W starts */
static int s2w_register_threshold = 9; /* beyond this threshold the panel will not register to apps */
static int s2w_min_distance = 600; /* power will toggle at this distance from start point */
static bool s2w_allow_stroke = false; /* use either direction for on/off */
static bool s2w_switch = false;
static bool s2w_temp_switch = false;
/* S2W ends */

/* DT2W starts */
static bool dt2w_switch = false;
static bool dt2w_temp_switch = false;
static unsigned int dt2w_min_duration = 100; /* msecs */
static unsigned int dt2w_max_duration = 500;  /* msecs */
static cputime64_t dt2w_start = 0;
static bool dt2w_screen = false; /* true if last touch was on display area */
/* DT2W ends */

static bool exec_count = true;
static bool barrier = false;
static bool mode = true;

static struct input_dev * sweep2wake_pwrdev;
static DEFINE_MUTEX(pwrlock);

static inline bool s2w_active(void) {
	return s2w_switch || dt2w_switch;
}

extern void sweep2wake_syn_setdev(struct input_dev * input_device) {
	sweep2wake_pwrdev = input_device;
}
EXPORT_SYMBOL(sweep2wake_syn_setdev);

static void sweep2wake_presspwr(struct work_struct * sweep2wake_presspwr_work) {
	if (!mutex_trylock(&pwrlock))
		return;
	printk(KERN_INFO "[TP] [sweep2wake]: mode=%d", mode);
	input_event(sweep2wake_pwrdev, EV_KEY, KEY_POWER, 1);
	input_sync(sweep2wake_pwrdev);
	msleep(100);
	input_event(sweep2wake_pwrdev, EV_KEY, KEY_POWER, 0);
	input_sync(sweep2wake_pwrdev);
	msleep(100);
	mutex_unlock(&pwrlock);
}
static DECLARE_WORK(sweep2wake_presspwr_work, sweep2wake_presspwr);

void sweep2wake_syn_pwrtrigger(void) {
	if (!work_busy(&sweep2wake_presspwr_work))
		schedule_work(&sweep2wake_presspwr_work);
}
#endif

static int i2c_syn_read(struct i2c_client *client, uint16_t addr, uint8_t *data, uint16_t length)
{
	uint8_t retry, buf;

	struct i2c_msg msg[] = {
		{
			.addr = client->addr,
			.flags = 0,
			.len = 1,
			.buf = &buf,
		},
		{
			.addr = client->addr,
			.flags = I2C_M_RD,
			.len = length,
			.buf = data,
		}
	};
	buf = addr & 0xFF;

	for (retry = 0; retry < SYN_I2C_RETRY_TIMES; retry++) {
		if (i2c_transfer(client->adapter, msg, 2) == 2)
			break;
		msleep(10);
	}

	if (retry == SYN_I2C_RETRY_TIMES) {
		printk(KERN_ERR "[TP]: i2c_read retry over %d\n",
				SYN_I2C_RETRY_TIMES);
		return -EIO;
	}
	return 0;
}

static ssize_t touch_vendor_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	ssize_t ret = 0;

	sprintf(buf, "%s_%#x\n", SYNAPTICSNAME, syn_panel_version);
	ret = strlen(buf) + 1;

	return ret;
}

static DEVICE_ATTR(vendor, 0444, touch_vendor_show, NULL);

static uint16_t syn_reg_addr;

static ssize_t register_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int ret = 0;
	uint8_t data;
	struct synaptics_ts_data *ts;
	ts = gl_ts;

	data = i2c_smbus_read_byte_data(ts->client, syn_reg_addr);
	if (data < 0) {
		printk(KERN_WARNING "[TP] %s: read fail\n", __func__);
		return ret;
	}
	ret += sprintf(buf, "addr: 0x%X, data: 0x%X\n", syn_reg_addr, data);
	return ret;
}

static ssize_t register_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int ret = 0;
	struct synaptics_ts_data *ts;
	char buf_tmp[4];
	uint8_t write_da;

	ts = gl_ts;
	memset(buf_tmp, 0x0, sizeof(buf_tmp));
	if ((buf[0] == 'r' || buf[0] == 'w') && buf[1] == ':' &&
		(buf[5] == ':' || buf[5] == '\n')) {
		memcpy(buf_tmp, buf + 2, 3);
		syn_reg_addr = simple_strtol(buf_tmp, NULL, 16);
		printk(KERN_DEBUG "[TP] %s: set syn_reg_addr is: 0x%X\n",
						__func__, syn_reg_addr);
		if (buf[0] == 'w' && buf[5] == ':' && buf[9] == '\n') {
			memcpy(buf_tmp, buf + 6, 3);
			write_da = simple_strtol(buf_tmp, NULL, 10);
			printk(KERN_DEBUG "[TP] write addr: 0x%X, data: 0x%X\n",
						syn_reg_addr, write_da);
			ret = i2c_smbus_write_byte_data(ts->client,
					syn_reg_addr, write_da);
			if (ret < 0) {
				printk(KERN_ERR "[TP] %s: write fail(%d)\n",
								__func__, ret);
			}
		}
	}

	return count;
}

static DEVICE_ATTR(register, 0644, register_show, register_store);

static ssize_t debug_level_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct synaptics_ts_data *ts = gl_ts;

	return sprintf(buf, "%d\n", ts->debug_log_level);
}

static ssize_t debug_level_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct synaptics_ts_data *ts = gl_ts;

	if (buf[0] >= '0' && buf[0] <= '9' && buf[1] == '\n')
		ts->debug_log_level = buf[0] - '0';

	return count;
}

static DEVICE_ATTR(debug_level, 0644, debug_level_show, debug_level_store);

#ifdef CONFIG_TOUCHSCREEN_SYNAPTICS_SWEEP2WAKE
/* S2W sysfs starts */
static ssize_t synaptics_sweep2wake_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	size_t count = 0;
	count += sprintf(buf, "%d\n", s2w_switch);
	return count;
}

static ssize_t synaptics_sweep2wake_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned long value;
	int ret = 0;

	ret = strict_strtoul(buf, 10, &value);
	if (ret < 0) {
		printk(KERN_INFO "[TP] [sweep2wake]: set s2w_switch failed %s\n", buf);
		return count;
	}
	if (scr_suspended) {
		s2w_temp_switch = (bool)value;
		printk(KERN_INFO "[TP] [sweep2wake]: screen is off, set s2w_temp_switch=%d\n", s2w_temp_switch);
		return count;
	}
	if (value == 0 || value == 1) {
		s2w_switch = (bool)value;
		s2w_temp_switch = s2w_switch;
		printk(KERN_INFO "[TP] [sweep2wake]: s2w_switch=%d\n", s2w_switch);
	} else {
		printk(KERN_INFO "[TP] [sweep2wake]: set s2w_switch failed - valid values are 0 or 1 - %s\n", buf);
	}
	return count;
}

static DEVICE_ATTR(sweep2wake, (S_IWUSR|S_IRUGO),
		synaptics_sweep2wake_show, synaptics_sweep2wake_store);

static ssize_t synaptics_s2w_allow_stroke_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	size_t count = 0;
	count += sprintf(buf, "%d\n", s2w_allow_stroke);
	return count;
}

static ssize_t synaptics_s2w_allow_stroke_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned long value;
	int ret = 0;

	ret = strict_strtoul(buf, 10, &value);

	if (ret < 0) {
		printk(KERN_INFO "[TP] [sweep2wake]: set s2w_allow_stroke failed %s\n", buf);
		return count;
	}
	if (value == 0 || value == 1) {
		s2w_allow_stroke = (bool)value;
		printk(KERN_INFO "[TP] [sweep2wake]: s2w_allow_stroke=%d\n", s2w_allow_stroke);
	} else {
		printk(KERN_INFO "[TP] [sweep2wake]: set s2w_allow_stroke failed - valid values are 0 or 1 - %s\n", buf);
	}
	return count;
}

static DEVICE_ATTR(s2w_allow_stroke, (S_IWUSR|S_IRUGO),
		synaptics_s2w_allow_stroke_show, synaptics_s2w_allow_stroke_store);

static ssize_t synaptics_s2w_register_threshold_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	size_t count = 0;
	count += sprintf(buf, "%d\n", s2w_register_threshold);
	return count;
}

static ssize_t synaptics_s2w_register_threshold_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned long value;
	int ret = 0;

	ret = strict_strtoul(buf, 10, &value);
	if (ret < 0) {
		printk(KERN_INFO "[TP] [sweep2wake]: set s2w_register_threshold failed - %s\n", buf);
		return count;
	}
	s2w_register_threshold = (int)value;
	printk(KERN_INFO "[TP] [sweep2wake]: s2w_register_threshold=%d\n", s2w_register_threshold);
	return count;
}

static DEVICE_ATTR(s2w_register_threshold, (S_IWUSR|S_IRUGO),
		synaptics_s2w_register_threshold_show, synaptics_s2w_register_threshold_store);

static ssize_t synaptics_s2w_min_distance_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	size_t count = 0;
	count += sprintf(buf, "%d\n", s2w_min_distance);
	return count;
}

static ssize_t synaptics_s2w_min_distance_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned long value;
	int ret = 0;
	ret = strict_strtoul(buf, 10, &value);
	if (ret < 0) {
		printk(KERN_INFO "[TP] [sweep2wake]: set s2w_min_distance failed - %s\n", buf);
		return count;
	}
	s2w_min_distance = (int)value;
	printk(KERN_INFO "[TP] [sweep2wake]: s2w_min_distance=%d", s2w_min_distance);
	return count;
}

static DEVICE_ATTR(s2w_min_distance, (S_IWUSR|S_IRUGO),
		synaptics_s2w_min_distance_show, synaptics_s2w_min_distance_store);
/* S2W sysfs ends */

/* DT2W sysfs starts */
static ssize_t synaptics_dt2w_switch_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	size_t count = 0;
	count += sprintf(buf, "%d\n", dt2w_switch);
	return count;
}

static ssize_t synaptics_dt2w_switch_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned long value;
	int ret = 0;
	ret = strict_strtoul(buf, 10, &value);

	if (ret < 0) {
		printk(KERN_INFO "[TP] [sweep2wake]: set dt2w_switch failed - %s\n", buf);
		return count;
	}
	if (scr_suspended) {
		dt2w_temp_switch = (bool)value;
		printk(KERN_INFO "[TP] [sweep2wake]: screen is off, set dt2w_temp_switch=%d\n", dt2w_temp_switch);
		return count;
	}
	if (value == 0 || value == 1) {
		dt2w_switch = (bool)value;
		dt2w_temp_switch = dt2w_switch;
		printk(KERN_INFO "[TP] [sweep2wake]: dt2w_switch=%d\n", dt2w_switch);
	} else {
		printk(KERN_INFO "[TP] [sweep2wake]: set dt2w_switch failed - valid values are 0 or 1 - %s\n", buf);
	}
	return count;
}

static DEVICE_ATTR(doubletap2wake, (S_IWUSR|S_IRUGO),
		synaptics_dt2w_switch_show, synaptics_dt2w_switch_store);

static ssize_t synaptics_dt2w_min_duration_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	size_t count = 0;
	count += sprintf(buf, "%d\n", dt2w_min_duration);
	return count;
}

static ssize_t synaptics_dt2w_min_duration_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned long value;
	int ret = 0;
	ret = strict_strtoul(buf, 10, &value);

	if (ret < 0) {
		printk(KERN_INFO "[TP] [sweep2wake]: set dt2w_min_duration failed - %s\n", buf);
		return count;
	}
	if (value > 0) {
		dt2w_min_duration = (int)value;
		printk(KERN_INFO "[TP] [sweep2wake]: dt2w_min_duration=%d\n", dt2w_min_duration);
	} else {
		printk(KERN_INFO "[TP] [sweep2wake]: set dt2w_min_duration failed - valid values are positive integers - %s\n", buf);
	}
	return count;
}

static DEVICE_ATTR(dt2w_min_duration, (S_IWUSR|S_IRUGO),
		synaptics_dt2w_min_duration_show, synaptics_dt2w_min_duration_store);

static ssize_t synaptics_dt2w_max_duration_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	size_t count = 0;
	count += sprintf(buf, "%d\n", dt2w_max_duration);
	return count;
}

static ssize_t synaptics_dt2w_max_duration_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned long value;
	int ret = 0;
	ret = strict_strtoul(buf, 10, &value);

	if (ret < 0) {
		printk(KERN_INFO "[TP] [sweep2wake]: set dt2w_max_duration failed - %s\n", buf);
		return count;
	}
	if (value > 0) {
		dt2w_max_duration = (int)value;
		printk(KERN_INFO "[TP] [sweep2wake]: dt2w_max_duration=%d\n", dt2w_max_duration);
	} else {
		printk(KERN_INFO "[TP] [sweep2wake]: set dt2w_max_duration failed - valid values are positive integers - %s\n", buf);
	}
	return count;
}

static DEVICE_ATTR(dt2w_max_duration, (S_IWUSR|S_IRUGO),
		synaptics_dt2w_max_duration_show, synaptics_dt2w_max_duration_store);
/* DT2W sysfs ends */
#endif

static struct kobject *android_touch_kobj;

static int synaptics_touch_sysfs_init(void)
{
	int ret;
	android_touch_kobj = kobject_create_and_add("android_touch", NULL);
	if (android_touch_kobj == NULL) {
		printk(KERN_ERR "[TP] %s: subsystem_register failed\n", __func__);
		ret = -ENOMEM;
		return ret;
	}
#ifdef CONFIG_TOUCHSCREEN_SYNAPTICS_SWEEP2WAKE
	if (sysfs_create_file(android_touch_kobj, &dev_attr_sweep2wake.attr) ||
			sysfs_create_file(android_touch_kobj, &dev_attr_s2w_allow_stroke.attr) ||
			sysfs_create_file(android_touch_kobj, &dev_attr_s2w_register_threshold.attr) ||
			sysfs_create_file(android_touch_kobj, &dev_attr_s2w_min_distance.attr) ||
			sysfs_create_file(android_touch_kobj, &dev_attr_doubletap2wake.attr) ||
			sysfs_create_file(android_touch_kobj, &dev_attr_dt2w_min_duration.attr) ||
			sysfs_create_file(android_touch_kobj, &dev_attr_dt2w_max_duration.attr))
		return -ENOMEM;
#endif
	ret = sysfs_create_file(android_touch_kobj, &dev_attr_vendor.attr);
	if (ret) {
		printk(KERN_ERR "[TP] touch_sysfs_init: sysfs_create_group failed\n");
		return ret;
	}
	ret = sysfs_create_file(android_touch_kobj, &dev_attr_debug_level.attr);
	if (ret) {
		printk(KERN_ERR "[TP] %s: sysfs_create_file failed\n", __func__);
		return ret;
	}
	syn_reg_addr = 0;
	ret = sysfs_create_file(android_touch_kobj, &dev_attr_register.attr);
	if (ret) {
		printk(KERN_ERR "[TP] %s: sysfs_create_file failed\n", __func__);
		return ret;
	}

	return 0;
}

static void synaptics_touch_sysfs_remove(void)
{
#ifdef CONFIG_TOUCHSCREEN_SYNAPTICS_SWEEP2WAKE
	sysfs_remove_file(android_touch_kobj, &dev_attr_sweep2wake.attr);
	sysfs_remove_file(android_touch_kobj, &dev_attr_s2w_allow_stroke.attr);
	sysfs_remove_file(android_touch_kobj, &dev_attr_s2w_register_threshold.attr);
	sysfs_remove_file(android_touch_kobj, &dev_attr_s2w_min_distance.attr);
	sysfs_remove_file(android_touch_kobj, &dev_attr_doubletap2wake.attr);
	sysfs_remove_file(android_touch_kobj, &dev_attr_dt2w_min_duration.attr);
	sysfs_remove_file(android_touch_kobj, &dev_attr_dt2w_max_duration.attr);
#endif
	sysfs_remove_file(android_touch_kobj, &dev_attr_debug_level.attr);
	kobject_del(android_touch_kobj);
}

static int synaptics_init_panel(struct synaptics_ts_data *ts)
{
	int ret = 0;

	if (ts->sensitivity_adjust) {
		ret = i2c_smbus_write_byte_data(ts->client,
			ts->page_table[2].value + 0x48, ts->sensitivity_adjust); /* Set Sensitivity */
		if (ret < 0)
			printk(KERN_ERR "[TP] i2c_smbus_write_byte_data failed for Sensitivity Set\n");
	}

	/* Position Threshold */
	i2c_smbus_write_byte_data(ts->client, ts->page_table[2].value+2, 3);
	i2c_smbus_write_byte_data(ts->client, ts->page_table[2].value+3, 3);

	/* 2D Gesture Enable */
	i2c_smbus_write_byte_data(ts->client, ts->page_table[2].value+10, 0);
	i2c_smbus_write_byte_data(ts->client, ts->page_table[2].value+11, 0);

	/* Configured */
	i2c_smbus_write_byte_data(ts->client, ts->page_table[8].value, 0x80);

	return ret;
}

static void synaptics_ts_work_func(struct work_struct *work)
{
	int i;
	int ret;
	uint8_t start_reg;
#ifdef CONFIG_TOUCHSCREEN_SYNAPTICS_SWEEP2WAKE
	// -1 = not touched; -2 = touched on screen; >=0 = touched on button panel
	static int downx = -1;
#endif

	struct synaptics_ts_data *ts = container_of(work, struct synaptics_ts_data, work);
	uint8_t buf[(ts->finger_support * 21 + 11) / 4];

	start_reg = ts->page_table[9].value;
	ret = i2c_syn_read(ts->client, start_reg, buf, sizeof(buf));
	msleep(10);
	if (ret < 0 || ((buf[0] & 0x0F))) {
		if (ret < 0)
			printk(KERN_ERR "[TP] synaptics_ts_work_func: i2c_transfer failed\n");
		else
			printk(KERN_ERR "[TP] synaptics_ts_work_func: Status ERROR: %d\n", buf[0] & 0x0F);
		/* reset touch control */
		if (ts->power) {
			ret = ts->power(0);
			if (ret < 0)
				printk(KERN_ERR "[TP] synaptics_ts_resume power off failed\n");
			msleep(10);
			ret = ts->power(1);
			if (ret < 0)
				printk(KERN_ERR	"[TP] synaptics_ts_resume power on failed\n");
		} else {
			i2c_smbus_write_byte_data(ts->client, ts->page_table[7].value, 0x01);
			msleep(250);
		}
		synaptics_init_panel(ts);
		if (!ts->use_irq)
			hrtimer_start(&ts->timer, ktime_set(1, 0), HRTIMER_MODE_REL);
		else
			i2c_smbus_write_byte_data(ts->client, ts->page_table[8].value + 1, 4);
	} else {

		int finger_data[ts->finger_support][4];
		int base = (ts->finger_support + 11) / 4;
		uint8_t loop_i, loop_j, finger_pressed = 0, finger_count = 0;
		uint8_t finger_press_changed = 0, finger_release_changed = 0;
		uint8_t pos_mask = 0x0f;

		if (ts->debug_log_level & 0x1) {
			printk("Touch:");
			for (i = 0; i < sizeof(buf); i++)
				printk(" %2x", buf[i]);
			printk("\n");
		}
		for (loop_i = 0; loop_i < ts->finger_support; loop_i++)
			if (buf[1 + (ts->finger_support + 3) / 4] >> (loop_i * 2) & 0x03) {
				finger_pressed |= 1 << loop_i;
				finger_count++;
			}
		if (ts->finger_pressed != finger_pressed && ts->pre_finger_data[0][0] < 2) {
			finger_press_changed = ts->finger_pressed ^ finger_pressed;
			finger_release_changed = finger_press_changed & (~finger_pressed);
			finger_press_changed &= finger_pressed;
			ts->finger_pressed = finger_pressed;
		}
		if (finger_pressed == 0) {
			if (!scr_suspended) {
#ifdef CONFIG_TOUCHSCREEN_COMPATIBLE_REPORT
                  //			input_report_abs(ts->input_dev, ABS_MT_PRESSURE, 0);
			input_report_key(ts->input_dev, BTN_TOUCH, 0);
#else
			input_report_abs(ts->input_dev, ABS_MT_AMPLITUDE, 0);
			input_report_abs(ts->input_dev, ABS_MT_POSITION, 1 << 31);
#endif
			}
#ifdef CONFIG_TOUCHSCREEN_SYNAPTICS_SWEEP2WAKE
			/* if finger released, reset count & barriers */
			if ((((finger_count > 0)?1:0) == 0) && s2w_active()) {
				if (s2w_switch) {
					exec_count = true;
					barrier = false;
					downx = -1;
				}

				/* DT2W */
				if (scr_suspended && dt2w_switch) {
					cputime64_t now = ktime_to_ns(ktime_get());
					cputime64_t diff = (unsigned int) (now - dt2w_start);
					cputime64_t min = dt2w_min_duration * 1000 * 1000;
					cputime64_t max = dt2w_max_duration * 1000 * 1000;

					if (ts->debug_log_level > 0)
						printk(KERN_INFO "[TP] [dt2w]: s2w_double_tap diff=%lld\n", diff);

					dt2w_start = now;

					if (dt2w_screen && finger_data[loop_i][1] < 1900) {
						if (diff > min && diff < max) {
							printk(KERN_INFO "[TP] [dt2w]: s2w_double_tap ON\n");
							mode = true;
							sweep2wake_syn_pwrtrigger();
						} else {
							if (ts->debug_log_level > 0)
								printk(KERN_INFO "[TP] [dt2w]: s2w_double_tap took too long, %lld\n", diff);
						}
					} else {
						if (ts->debug_log_level > 0)
							printk(KERN_INFO "[TP] [dt2w]: previous tap was outside of the screen");
					}
				}
			}
#endif
			if (ts->debug_log_level & 0x2)
				printk(KERN_INFO "[TP] Finger leave\n");
		}
		if (ts->pre_finger_data[0][0] < 2 || finger_pressed) {
			for (loop_i = 0; loop_i < ts->finger_support; loop_i++) {
				uint32_t flip_flag = SYNAPTICS_FLIP_X;
				if ((((finger_pressed | finger_release_changed) >> loop_i) & 1) == 1) {
					pos_mask = 0x0f;
					for (loop_j = 0; loop_j < 2; loop_j++) {
						finger_data[loop_i][loop_j]
							= (buf[base+2] & pos_mask) >> (loop_j * 4) |
							(uint16_t)buf[base + loop_j] << 4;
						if (ts->flags & flip_flag)
							finger_data[loop_i][loop_j]
								= ts->max[loop_j] - finger_data[loop_i][loop_j];
						flip_flag <<= 1;
						pos_mask <<= 4;
					}
					finger_data[loop_i][2] = (buf[base+3] >> 4 & 0x0F) + (buf[base+3] & 0x0F);
					finger_data[loop_i][3] = buf[base+4];
					if (ts->flags & SYNAPTICS_SWAP_XY)
						swap(finger_data[loop_i][0], finger_data[loop_i][1]);
					if (((finger_release_changed >> loop_i) & 0x1) && ts->pre_finger_data[0][0] < 2) {
						printk(KERN_INFO "[TP] E%d@%d, %d\n", loop_i + 1,
						finger_data[loop_i][0], finger_data[loop_i][1]);
					}
					if (((finger_pressed >> loop_i) & 1) == 1) {
						finger_pressed &= ~(1 << loop_i);
						if (!scr_suspended) {
#ifdef CONFIG_TOUCHSCREEN_COMPATIBLE_REPORT
						input_report_abs(ts->input_dev, ABS_MT_PRESSURE,
							finger_data[loop_i][3]);
						input_report_abs(ts->input_dev, ABS_MT_WIDTH_MAJOR,
							finger_data[loop_i][2]);
						input_report_abs(ts->input_dev, ABS_MT_POSITION_X,
							finger_data[loop_i][0]);
						input_report_abs(ts->input_dev, ABS_MT_POSITION_Y,
							finger_data[loop_i][1]);
						input_report_key(ts->input_dev, BTN_TOUCH,
							finger_data[loop_i][2] ? 1 : 0);
						input_mt_sync(ts->input_dev);
#else
						input_report_abs(ts->input_dev, ABS_MT_AMPLITUDE,
							finger_data[loop_i][3] << 16 | finger_data[loop_i][2]);
						input_report_abs(ts->input_dev, ABS_MT_POSITION,
							(finger_pressed == 0) << 31 |
							finger_data[loop_i][0] << 16 | finger_data[loop_i][1]);
#endif
						}
						if (ts->pre_finger_data[0][0] < 2) {
							if ((finger_press_changed >> loop_i) & 0x1) {
								ts->pre_finger_data[loop_i + 1][0] = finger_data[loop_i][0];
								ts->pre_finger_data[loop_i + 1][1] = finger_data[loop_i][1];
								printk(KERN_INFO "[TP] S%d@%d, %d\n", loop_i + 1,
									ts->pre_finger_data[loop_i + 1][0], ts->pre_finger_data[loop_i + 1][1]);
								if (finger_count == ts->finger_support)
									i2c_smbus_write_byte_data(ts->client, ts->page_table[12].value - 1, 1);
								else if (!ts->pre_finger_data[0][0] && finger_count > 1)
									ts->pre_finger_data[0][0] = 1;
							}
						}
#ifdef CONFIG_TOUCHSCREEN_SYNAPTICS_SWEEP2WAKE
						//printk(KERN_INFO "[sweep2wake]: finger at %d,%d\n",
						//		finger_data[loop_i][0], finger_data[loop_i][1]);
						dt2w_screen = finger_data[loop_i][1] < 1900;
						if (s2w_allow_stroke)
						{
							// stroke2wake - any direction activates
							if ((finger_count == 1) && s2w_switch && (downx != -2))
							{
								if (finger_data[loop_i][1] > 1900)
								{
									if ((downx == -1) || (abs(downx - finger_data[loop_i][0]) > s2w_register_threshold))
									{
										// handle touch down
										if (downx == -1)
										{
											downx = finger_data[loop_i][0];
											break;
										}
										else
										{
											// lock panel to s2w after this distance
											if (abs(downx - finger_data[loop_i][0]) > s2w_register_threshold)
											{
												barrier = true;
											}

											// lock/unlock after distance travelled
											if (abs(downx - finger_data[loop_i][0]) > s2w_min_distance)
											{
												if (exec_count) {
													if (scr_suspended)
													{
														printk(KERN_INFO "[TP] [sweep2wake]: ON\n");
														mode = true;
														sweep2wake_syn_pwrtrigger();
														exec_count = false;
														break;
													}
													else
													{
														printk(KERN_INFO "[TP] [sweep2wake]: OFF\n");
														mode=false;
														sweep2wake_syn_pwrtrigger();
														exec_count = false;
														break;
													}
												}
											}
										}
									}
								}
								else // touch down on screen
								{
									// this prevents swipes originating on screen and then
									// entering button panel to affect s2w (gaming etc.)
									downx = -2;
								}
							}
						}
						else
						{
							// Free swipe - single direction activation
							// left->right
							if ((finger_count == 1) && scr_suspended && s2w_switch && (downx != -2))
							{
								if (finger_data[loop_i][1] > 1900)
								{
									if ((downx == -1) || (finger_data[loop_i][0] > downx))
									{
										// handle touch down
										if (downx == -1)
										{
											downx = finger_data[loop_i][0];
											break;
										}
										else
										{
											// lock panel to s2w after this distance
											if (abs(downx - finger_data[loop_i][0]) > s2w_register_threshold)
											{
												barrier = true;
											}

											// unlock after distance travelled
											if (abs(downx - finger_data[loop_i][0]) > s2w_min_distance)
											{
												if (exec_count) {
													printk(KERN_INFO "[TP] [sweep2wake]: ON\n");
													mode = true;
													sweep2wake_syn_pwrtrigger();
													exec_count = false;
													break;
												}
											}
										}
									}
								}
								else // touch down on screen
								{
									downx = -2;
								}
							}
							// right->left
							else if ((finger_count == 1) && !scr_suspended && s2w_switch && (downx != -2))
							{
								if (finger_data[loop_i][1] > 1900)
								{
									if ((downx == -1) || (finger_data[loop_i][0] < downx))
									{
										// handle touch down
										if (downx == -1)
										{
											downx = finger_data[loop_i][0];
											break;
										}
										else
										{
											// lock panel to s2w after this distance
											if (downx - s2w_register_threshold > finger_data[loop_i][0])
											{
												barrier = true;
											}

											// lock after distance travelled
											if (downx - s2w_min_distance > finger_data[loop_i][0])
											{
												if (exec_count)
												{
													printk(KERN_INFO "[TP] [sweep2wake]: OFF\n");
													mode = false;
													sweep2wake_syn_pwrtrigger();
													exec_count = false;
													break;
												}
											}
										}
									}
								}
								else // touch down on screen
								{
									downx = -2;
								}
							}
						}
#endif
						if (ts->debug_log_level & 0x2)
							printk(KERN_INFO "[TP] Finger %d=> X:%d, Y:%d w:%d, z:%d\n",
								loop_i + 1, finger_data[loop_i][0],
								finger_data[loop_i][1], finger_data[loop_i][2],
								finger_data[loop_i][3]);
					}
					if (((finger_release_changed >> loop_i) & 0x1) && ts->pre_finger_data[0][0] < 2)
						i2c_smbus_write_byte_data(ts->client, ts->page_table[12].value - 1, 1);
						if (!finger_count && !ts->pre_finger_data[0][0]
							&& (jiffies > (ts->timestamp + 15 * HZ) || (finger_data[loop_i][1] > ts->raw_base
							&& abs(ts->pre_finger_data[loop_i + 1][1] - finger_data[loop_i][1]) > ts->raw_ref))) {
							ts->pre_finger_data[0][0] = 2;
							printk(KERN_INFO "[TP] Touch Calibration Confirmed\n");
						} else if (!finger_count)
							ts->pre_finger_data[0][0] = 0;
				}
				base += 5;
			}
		}
	}

#ifdef CONFIG_TOUCHSCREEN_COMPATIBLE_REPORT
	input_sync(ts->input_dev);
#endif

	if (ts->use_irq)
		enable_irq(ts->client->irq);
}

static enum hrtimer_restart synaptics_ts_timer_func(struct hrtimer *timer)
{
	struct synaptics_ts_data *ts = container_of(timer, struct synaptics_ts_data, timer);
	queue_work(ts->syn_wq, &ts->work);
	hrtimer_start(&ts->timer, ktime_set(0, 12500000), HRTIMER_MODE_REL);
	return HRTIMER_NORESTART;
}

static irqreturn_t synaptics_ts_irq_handler(int irq, void *dev_id)
{
	struct synaptics_ts_data *ts = dev_id;
	disable_irq_nosync(ts->client->irq);
	queue_work(ts->syn_wq, &ts->work);
	return IRQ_HANDLED;
}

static int synaptics_ts_probe(
	struct i2c_client *client, const struct i2c_device_id *id)
{
	struct synaptics_ts_data *ts;
	uint8_t loop_i, loop_j;
	int ret = 0;
	uint16_t max_x, max_y;
	struct synaptics_i2c_rmi_platform_data *pdata;
	uint32_t panel_version;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		printk(KERN_ERR "[TP] synaptics_ts_probe: need I2C_FUNC_I2C\n");
		ret = -ENODEV;
		goto err_check_functionality_failed;
	}

	ts = kzalloc(sizeof(*ts), GFP_KERNEL);
	if (ts == NULL) {
		ret = -ENOMEM;
		goto err_alloc_data_failed;
	}
	ts->client = client;
	i2c_set_clientdata(client, ts);
	pdata = client->dev.platform_data;
	if (pdata)
		ts->power = pdata->power;
	if (ts->power)
		ret = ts->power(1);

	for (loop_i = 0; loop_i < 10; loop_i++) {
		if (i2c_smbus_read_byte_data(ts->client, 0x00DD) >= 0)
			break;
		msleep(10);
	}

	if (loop_i == 10) {
		printk(KERN_ERR "[TP] i2c_smbus_read_byte_data failed\n");
		goto err_detect_failed;
	}

	for (loop_i = 0x00DD, loop_j = 0; loop_i <= 0x00EE; loop_i++) {
		ts->page_table[loop_j].addr = loop_i;
		ts->page_table[loop_j].value = i2c_smbus_read_byte_data(ts->client, loop_i);
/*		printk("[0x%2.2X]%2.2X\n", ts->page_table[loop_j].addr, ts->page_table[loop_j].value); */
		while (ts->page_table[loop_j].value != i2c_smbus_read_byte_data(ts->client, loop_i))
			ts->page_table[loop_j].value = i2c_smbus_read_byte_data(ts->client, loop_i);
		loop_j++;
	}

	panel_version =
		i2c_smbus_read_byte_data(ts->client, ts->page_table[6].value + 3) |
		i2c_smbus_read_byte_data(ts->client, ts->page_table[6].value + 2) << 8;
	printk(KERN_INFO "[TP] %s: panel_version: %x\n", __func__, panel_version);
	syn_panel_version = panel_version;

	if (pdata) {
		while (pdata->version > panel_version) {
			printk(KERN_INFO "[TP] synaptics_ts_probe: old tp detected, "
					"panel version = %x\n", panel_version);
			pdata++;
		}
		ts->flags = pdata->flags;
		ts->sensitivity_adjust = pdata->sensitivity_adjust;
		ts->finger_support = pdata->finger_support;
	}
	ts->max[0] = max_x =
		i2c_smbus_read_byte_data(ts->client, ts->page_table[2].value+6) |
		i2c_smbus_read_byte_data(ts->client, ts->page_table[2].value+7) << 8;

	ts->max[1] = max_y =
		i2c_smbus_read_byte_data(ts->client, ts->page_table[2].value+8) |
		i2c_smbus_read_byte_data(ts->client, ts->page_table[2].value+9) << 8;
	printk(KERN_INFO"[TP] max_x: %X, max_y: %X\n", max_x, max_y);

	if (pdata->abs_x_min == pdata->abs_x_max && pdata->abs_y_min == pdata->abs_y_max) {
		pdata->abs_x_min = 0;
		pdata->abs_x_max = max_x;
		pdata->abs_y_min = 0;
		pdata->abs_y_max = max_y;
	}
	if (pdata->display_height) {
		ts->raw_ref = 115 * ((pdata->abs_y_max - pdata->abs_y_min) +
			pdata->abs_y_min) / pdata->display_height;
		ts->raw_base = 650 * ((pdata->abs_y_max - pdata->abs_y_min) +
			pdata->abs_y_min) / pdata->display_height;
		printk(KERN_INFO "[TP] ts->raw_ref: %d, ts->raw_base: %d\n", ts->raw_ref, ts->raw_base);
	} else {
		ts->raw_ref = 0;
		ts->raw_base = 0;
	}

	ret = synaptics_init_panel(ts);
	ts->timestamp = jiffies + 60 * HZ;
	if (ret < 0) {
		printk(KERN_ERR "[TP] synaptics_init_panel failed\n");
		goto err_detect_failed;
	}

	ts->syn_wq = create_singlethread_workqueue("synaptics_wq");
	if (!ts->syn_wq)
		goto err_create_wq_failed;
	INIT_WORK(&ts->work, synaptics_ts_work_func);

	ts->input_dev = input_allocate_device();
	if (ts->input_dev == NULL) {
		ret = -ENOMEM;
		printk(KERN_ERR "[TP] synaptics_ts_probe: Failed to allocate input device\n");
		goto err_input_dev_alloc_failed;
	}
	ts->input_dev->name = "synaptics-rmi-touchscreen";
	set_bit(EV_SYN, ts->input_dev->evbit);
	set_bit(EV_KEY, ts->input_dev->evbit);
	set_bit(BTN_TOUCH, ts->input_dev->keybit);
	set_bit(BTN_2, ts->input_dev->keybit);
	set_bit(EV_ABS, ts->input_dev->evbit);

	set_bit(KEY_BACK, ts->input_dev->keybit);
	set_bit(KEY_HOME, ts->input_dev->keybit);
	set_bit(KEY_MENU, ts->input_dev->keybit);
	set_bit(KEY_SEARCH, ts->input_dev->keybit);

	printk(KERN_INFO "[TP] synaptics_ts_probe: max_x %d, max_y %d\n", max_x, max_y);
	printk(KERN_INFO "[TP] input_set_abs_params: mix_x %d, max_x %d, min_y %d, max_y %d\n",
		pdata->abs_x_min, pdata->abs_x_max, pdata->abs_y_min, pdata->abs_y_max);

	input_set_abs_params(ts->input_dev, ABS_MT_POSITION_X, pdata->abs_x_min, pdata->abs_x_max, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_POSITION_Y, pdata->abs_y_min, pdata->abs_y_max, 0, 0);

	input_set_abs_params(ts->input_dev, ABS_MT_PRESSURE, 0, 255, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_WIDTH_MAJOR, 0, 30, 0, 0);
#ifndef CONFIG_TOUCHSCREEN_COMPATIBLE_REPORT
	input_set_abs_params(ts->input_dev, ABS_MT_AMPLITUDE,
		0, ((255 << 16) | 15), 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_POSITION,
		0, ((1 << 31) | (pdata->abs_x_max << 16) | pdata->abs_y_max), 0, 0);
#endif

	ret = input_register_device(ts->input_dev);
	if (ret) {
		printk(KERN_ERR "[TP] ssynaptics_ts_probe: "
				"Unable to register %s input device\n",
				ts->input_dev->name);
		goto err_input_register_device_failed;
	}

	gl_ts = ts;

	if (client->irq) {
		ret = request_irq(client->irq, synaptics_ts_irq_handler, IRQF_TRIGGER_LOW,
				client->name, ts);
		if (ret == 0) {
			/* enable abs int */
			ret = i2c_smbus_write_byte_data(ts->client, ts->page_table[8].value + 1, 4);
			if (ret)
				free_irq(client->irq, ts);
		}
		if (ret == 0)
			ts->use_irq = 1;
		else
			dev_err(&client->dev, "request_irq failed\n");
	}
	if (!ts->use_irq) {
		hrtimer_init(&ts->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
		ts->timer.function = synaptics_ts_timer_func;
		hrtimer_start(&ts->timer, ktime_set(1, 0), HRTIMER_MODE_REL);
	}

	synaptics_touch_sysfs_init();

	printk(KERN_INFO "[TP] synaptics_ts_probe: Start touchscreen %s in %s mode\n", ts->input_dev->name, ts->use_irq ? "interrupt" : "polling");

	return 0;

err_input_register_device_failed:
	input_free_device(ts->input_dev);

err_input_dev_alloc_failed:
	destroy_workqueue(ts->syn_wq);

err_create_wq_failed:
err_detect_failed:
	kfree(ts);

err_alloc_data_failed:
err_check_functionality_failed:
	return ret;
}

static int synaptics_ts_remove(struct i2c_client *client)
{
	struct synaptics_ts_data *ts = i2c_get_clientdata(client);
	if (ts->use_irq)
		free_irq(client->irq, ts);
	else
		hrtimer_cancel(&ts->timer);
	input_unregister_device(ts->input_dev);

	synaptics_touch_sysfs_remove();

	kfree(ts);
	return 0;
}

static int synaptics_ts_suspend(struct device *dev)
{
	int ret;
	struct synaptics_ts_data *ts;
	struct i2c_client *client = i2c_verify_client(dev);
	if (!client)
		return 0;

	ts = i2c_get_clientdata(client);
	if (ts->debug_log_level > 0)
		printk(KERN_INFO "[TP] %s: enter\n", __func__);

#ifdef CONFIG_TOUCHSCREEN_SYNAPTICS_SWEEP2WAKE
	if (s2w_active()) {
		//screen off, enable_irq_wake
		if (ts->debug_log_level > 0)
			printk(KERN_INFO "[TP] [sweep2wake]: enable_irq_wake\n");
		enable_irq_wake(client->irq);
	}
#endif

	if (ts->use_irq) {
#ifdef CONFIG_TOUCHSCREEN_SYNAPTICS_SWEEP2WAKE
		if (!s2w_active())
#endif
		disable_irq(client->irq);
	} else {
		hrtimer_cancel(&ts->timer);
		ret = cancel_work_sync(&ts->work);
	}

	ts->pre_finger_data[0][0] = 0;
	ts->pre_finger_data[0][1] = 0;

#ifdef CONFIG_TOUCHSCREEN_SYNAPTICS_SWEEP2WAKE
	if (!s2w_active()) {
#endif
	ret = i2c_smbus_write_byte_data(client, ts->page_table[8].value, 0x01); /* sleep */
	if (ret < 0)
		printk(KERN_ERR "[TP] synaptics_ts_suspend: i2c_smbus_write_byte_data failed\n");
#ifdef CONFIG_TOUCHSCREEN_SYNAPTICS_SWEEP2WAKE
	}
#endif

#ifdef CONFIG_TOUCHSCREEN_SYNAPTICS_SWEEP2WAKE
	if (s2w_active()) {
		mode = false;
	}
#endif
	scr_suspended = true;

	if (ts->debug_log_level > 0)
		printk(KERN_INFO "[TP] %s: leave\n", __func__);
	return 0;
}

static int synaptics_ts_resume(struct device *dev)
{
	int ret;
	struct synaptics_ts_data *ts;
	struct i2c_client *client = i2c_verify_client(dev);
	if (!client)
		return 0;

	ts = i2c_get_clientdata(client);
	if (ts->debug_log_level > 0)
		printk(KERN_INFO "[TP] %s: enter\n", __func__);

#ifdef CONFIG_TOUCHSCREEN_SYNAPTICS_SWEEP2WAKE
	if (s2w_active()) {
		ret = i2c_smbus_write_byte_data(client, ts->page_table[8].value, 0x01); /* sleep */
		if (ret < 0)
			printk(KERN_ERR "[TP] synaptics_ts_suspend: i2c_smbus_write_byte_data failed\n");
		msleep(150);
		ret = 0;
		//screen on, disable_irq_wake
		if (ts->debug_log_level > 0)
			printk(KERN_INFO "[TP] [sweep2wake]: disable_irq_wake\n");
		disable_irq_wake(client->irq);
	}
#endif

	ret = i2c_smbus_write_byte_data(client, ts->page_table[8].value, 0x00); /* wake */
	msleep(100);

	synaptics_init_panel(ts);
	ts->timestamp = jiffies;

#ifdef CONFIG_TOUCHSCREEN_COMPATIBLE_REPORT
	input_report_abs(ts->input_dev, ABS_MT_PRESSURE, 0);
	input_report_key(ts->input_dev, BTN_TOUCH, 0);
	input_sync(ts->input_dev);
#else
	input_report_abs(ts->input_dev, ABS_MT_AMPLITUDE, 0);
	input_report_abs(ts->input_dev, ABS_MT_POSITION, 1 << 31);
#endif

#ifdef CONFIG_TOUCHSCREEN_SYNAPTICS_SWEEP2WAKE
	if (!s2w_active()) {
#endif
	if (ts->use_irq)
		enable_irq(client->irq);

	if (!ts->use_irq)
		hrtimer_start(&ts->timer, ktime_set(1, 0), HRTIMER_MODE_REL);
#ifdef CONFIG_TOUCHSCREEN_SYNAPTICS_SWEEP2WAKE
	}
#endif

#ifdef CONFIG_TOUCHSCREEN_SYNAPTICS_SWEEP2WAKE
	if (s2w_active()) {
		mode = true;
	}
#endif
	scr_suspended = false;

#ifdef CONFIG_TOUCHSCREEN_SYNAPTICS_SWEEP2WAKE
	// Set real switches based on temp switches
	s2w_switch = s2w_temp_switch;
	dt2w_switch = dt2w_temp_switch;
#endif

	if (ts->debug_log_level > 0)
		printk(KERN_INFO "[TP] %s: leave\n", __func__);
	return 0;
}

static const struct i2c_device_id synaptics_ts_id[] = {
	{ SYNAPTICS_3K_NAME, 0 },
	{ }
};

static const struct dev_pm_ops synaptics_ts_pm_ops = {
	.suspend = synaptics_ts_suspend,
	.resume = synaptics_ts_resume,
};

static struct i2c_driver synaptics_ts_driver = {
	.probe		= synaptics_ts_probe,
	.remove		= synaptics_ts_remove,
	.id_table	= synaptics_ts_id,
	.driver = {
		.name	= SYNAPTICS_3K_NAME,
		.pm	= &synaptics_ts_pm_ops,
	},
};

static int synaptics_ts_init(void)
{
	return i2c_add_driver(&synaptics_ts_driver);
}

static void __exit synaptics_ts_exit(void)
{
	i2c_del_driver(&synaptics_ts_driver);
}

module_init(synaptics_ts_init);
module_exit(synaptics_ts_exit);

MODULE_DESCRIPTION("Synaptics Touchscreen Driver");
MODULE_LICENSE("GPL");
