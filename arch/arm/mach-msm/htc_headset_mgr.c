/*
 *
 * /arch/arm/mach-msm/htc_headset_mgr.c
 *
 * HTC headset manager driver.
 *
 * Copyright (C) 2010 HTC, Inc.
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

#define pr_fmt(fmt) "hs_mgr: " fmt

#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/gpio_event.h>
#include <linux/rtc.h>
#include <linux/slab.h>
#include <linux/module.h>

#include <mach/htc_headset_mgr.h>

static struct workqueue_struct *detect_wq;
static void insert_detect_work_func(struct work_struct *work);
static DECLARE_DELAYED_WORK(insert_detect_work, insert_detect_work_func);
static void remove_detect_work_func(struct work_struct *work);
static DECLARE_DELAYED_WORK(remove_detect_work, remove_detect_work_func);
static void mic_detect_work_func(struct work_struct *work);
static DECLARE_DELAYED_WORK(mic_detect_work, mic_detect_work_func);

static struct workqueue_struct *button_wq;
static void button_35mm_work_func(struct work_struct *work);
static DECLARE_DELAYED_WORK(button_35mm_work, button_35mm_work_func);

static struct workqueue_struct *debug_wq;
static void debug_work_func(struct work_struct *work);
static DECLARE_WORK(debug_work, debug_work_func);

static int hs_mgr_rpc_call(struct msm_rpc_server *server,
			    struct rpc_request_hdr *req, unsigned len);

static struct msm_rpc_server hs_rpc_server = {
	.prog		= HS_RPC_SERVER_PROG,
	.vers		= HS_RPC_SERVER_VERS,
	.rpc_call	= hs_mgr_rpc_call,
};

struct button_work {
	struct delayed_work key_work;
	int key_code;
};

static struct delayed_work *pre_key_work;
static struct htc_headset_mgr_info *hi;
static struct hs_notifier_func hs_mgr_notifier;

static void init_next_driver(void)
{
	int i = hi->driver_init_seq;

	if (!hi->pdata.headset_devices_num)
		return;

	if (i < hi->pdata.headset_devices_num) {
		hi->driver_init_seq++;
		platform_device_register(hi->pdata.headset_devices[i]);
	}
}

int hs_debug_log_state(void)
{
	return (hi->debug_flag & DEBUG_FLAG_LOG) ? 1 : 0;
}

void hs_notify_driver_ready(char *name)
{
	pr_info("%s ready\n", name);
	init_next_driver();
}

void hs_notify_hpin_irq(void)
{
	hi->hpin_jiffies = jiffies;
	if (hs_mgr_notifier.hpin_gpio)
		pr_info("HPIN IRQ (%d)\n", hs_mgr_notifier.hpin_gpio());
	else
		pr_info("HPIN IRQ\n");
}

struct class *hs_get_attribute_class(void)
{
	return hi->htc_accessory_class;
}

int hs_hpin_stable(void)
{
	unsigned long last_hpin_jiffies = 0;
	unsigned long unstable_jiffies = 1.2 * HZ;

	HS_DBG();

	last_hpin_jiffies = hi->hpin_jiffies;

	if (time_before_eq(jiffies, last_hpin_jiffies + unstable_jiffies))
		return 0;

	return 1;
}

static int get_mic_state(void)
{
	HS_DBG();

	switch (hi->hs_35mm_type) {
	case HEADSET_MIC:
	case HEADSET_METRICO:
	case HEADSET_BEATS:
	case HEADSET_BEATS_SOLO:
		return 1;
	default:
		break;
	}

	return 0;
}

static void update_mic_status(int count)
{
	HS_DBG();

	if (hi->is_ext_insert) {
		pr_info("Start MIC status polling (%d)\n", count);
		cancel_delayed_work_sync(&mic_detect_work);
		hi->mic_detect_counter = count;
		queue_delayed_work(detect_wq, &mic_detect_work,
				   HS_JIFFIES_MIC_DETECT);
	}
}

static void headset_notifier_update(int id)
{
	if (!hi) {
		pr_info("HS_MGR driver is not ready\n");
		return;
	}

	switch (id) {
	case HEADSET_REG_HPIN_GPIO:
		break;
	case HEADSET_REG_REMOTE_ADC:
		update_mic_status(HS_DEF_MIC_DETECT_COUNT);
		break;
	case HEADSET_REG_REMOTE_KEYCODE:
	case HEADSET_REG_RPC_KEY:
		break;
	case HEADSET_REG_MIC_STATUS:
		update_mic_status(HS_DEF_MIC_DETECT_COUNT);
		break;
	case HEADSET_REG_MIC_BIAS:
		if (!hi->pdata.headset_power &&
		    hi->hs_35mm_type != HEADSET_UNPLUG) {
			hs_mgr_notifier.mic_bias_enable(1);
			hi->mic_bias_state = 1;
			msleep(HS_DELAY_MIC_BIAS);
			update_mic_status(HS_DEF_MIC_DETECT_COUNT);
		}
		break;
	case HEADSET_REG_MIC_SELECT:
	case HEADSET_REG_KEY_INT_ENABLE:
	case HEADSET_REG_KEY_ENABLE:
	case HEADSET_REG_INDICATOR_ENABLE:
		break;
	default:
		break;
	}
}

int headset_notifier_register(struct headset_notifier *notifier)
{
	if (!notifier->func) {
		pr_info("NULL register function\n");
		return 0;
	}

	switch (notifier->id) {
	case HEADSET_REG_HPIN_GPIO:
		pr_info("Register HPIN_GPIO notifier\n");
		hs_mgr_notifier.hpin_gpio = notifier->func;
		break;
	case HEADSET_REG_REMOTE_ADC:
		pr_info("Register REMOTE_ADC notifier\n");
		hs_mgr_notifier.remote_adc = notifier->func;
		break;
	case HEADSET_REG_REMOTE_KEYCODE:
		pr_info("Register REMOTE_KEYCODE notifier\n");
		hs_mgr_notifier.remote_keycode = notifier->func;
		break;
	case HEADSET_REG_RPC_KEY:
		pr_info("Register RPC_KEY notifier\n");
		hs_mgr_notifier.rpc_key = notifier->func;
		break;
	case HEADSET_REG_MIC_STATUS:
		pr_info("Register MIC_STATUS notifier\n");
		hs_mgr_notifier.mic_status = notifier->func;
		break;
	case HEADSET_REG_MIC_BIAS:
		pr_info("Register MIC_BIAS notifier\n");
		hs_mgr_notifier.mic_bias_enable = notifier->func;
		break;
	case HEADSET_REG_MIC_SELECT:
		pr_info("Register MIC_SELECT notifier\n");
		hs_mgr_notifier.mic_select = notifier->func;
		break;
	case HEADSET_REG_KEY_INT_ENABLE:
		pr_info("Register KEY_INT_ENABLE notifier\n");
		hs_mgr_notifier.key_int_enable = notifier->func;
		break;
	case HEADSET_REG_KEY_ENABLE:
		pr_info("Register KEY_ENABLE notifier\n");
		hs_mgr_notifier.key_enable = notifier->func;
		break;
	case HEADSET_REG_INDICATOR_ENABLE:
		pr_info("Register INDICATOR_ENABLE notifier\n");
		hs_mgr_notifier.indicator_enable = notifier->func;
		break;
	default:
		pr_info("Unknown register ID\n");
		return 0;
	}

	headset_notifier_update(notifier->id);

	return 1;
}

static int hs_mgr_rpc_call(struct msm_rpc_server *server,
			    struct rpc_request_hdr *req, unsigned len)
{
	struct hs_rpc_server_args_key *args_key;

	wake_lock_timeout(&hi->hs_wake_lock, HS_WAKE_LOCK_TIMEOUT);

	HS_DBG();

	switch (req->procedure) {
	case HS_RPC_SERVER_PROC_NULL:
		pr_info("RPC_SERVER_NULL\n");
		break;
	case HS_RPC_SERVER_PROC_KEY:
		args_key = (struct hs_rpc_server_args_key *)(req + 1);
		args_key->adc = be32_to_cpu(args_key->adc);
		pr_info("RPC_SERVER_KEY ADC = %u (0x%X)\n",
			args_key->adc, args_key->adc);
		if (hs_mgr_notifier.rpc_key)
			hs_mgr_notifier.rpc_key(args_key->adc);
		else
			pr_info("RPC_KEY notify function doesn't exist\n");
		break;
	default:
		pr_info("Unknown RPC procedure\n");
		return -EINVAL;
	}

	return 0;
}

static ssize_t h2w_print_name(struct switch_dev *sdev, char *buf)
{
	return sprintf(buf, "Headset\n");
}

static ssize_t usb_audio_print_name(struct switch_dev *sdev, char *buf)
{
	return sprintf(buf, "usb_audio\n");
}

static void get_key_name(int keycode, char *buf)
{
	switch (keycode) {
	case HS_MGR_KEYCODE_END:
		sprintf(buf, "END");
		break;
	case HS_MGR_KEYCODE_MUTE:
		sprintf(buf, "MUTE");
		break;
	case HS_MGR_KEYCODE_VOLDOWN:
		sprintf(buf, "VOLDOWN");
		break;
	case HS_MGR_KEYCODE_VOLUP:
		sprintf(buf, "VOLUP");
		break;
	case HS_MGR_KEYCODE_FORWARD:
		sprintf(buf, "FORWARD");
		break;
	case HS_MGR_KEYCODE_PLAY:
		sprintf(buf, "PLAY");
		break;
	case HS_MGR_KEYCODE_BACKWARD:
		sprintf(buf, "BACKWARD");
		break;
	case HS_MGR_KEYCODE_MEDIA:
		sprintf(buf, "MEDIA");
		break;
	case HS_MGR_KEYCODE_SEND:
		sprintf(buf, "SEND");
		break;
	default:
		sprintf(buf, "%d", keycode);
	}
}

void button_pressed(int type)
{
	char key_name[16];

	get_key_name(type, key_name);
	pr_info("%s (%d) pressed\n", key_name, type);
	atomic_set(&hi->btn_state, type);
	input_report_key(hi->input, type, 1);
	input_sync(hi->input);
}

void button_released(int type)
{
	char key_name[16];

	get_key_name(type, key_name);
	pr_info("%s (%d) released\n", key_name, type);
	atomic_set(&hi->btn_state, 0);
	input_report_key(hi->input, type, 0);
	input_sync(hi->input);
}

void headset_button_event(int is_press, int type)
{
	HS_DBG();

	if (hi->hs_35mm_type == HEADSET_UNPLUG &&
	    hi->h2w_35mm_type == HEADSET_UNPLUG) {
		pr_info("IGNORE key %d (HEADSET_UNPLUG)\n", type);
		return;
	}

	if (!hs_hpin_stable()) {
		pr_info("IGNORE key %d (Unstable HPIN)\n", type);
		return;
	}

	if (!get_mic_state()) {
		pr_info("IGNORE key %d (Not support MIC)\n", type);
		return;
	}

	if (!is_press)
		button_released(type);
	else if (!atomic_read(&hi->btn_state))
		button_pressed(type);
}

void hs_set_mic_select(int state)
{
	HS_DBG();

	if (hs_mgr_notifier.mic_select)
		hs_mgr_notifier.mic_select(state);
}

static int get_mic_status(void)
{
	int i = 0;
	int adc = 0;
	int mic = HEADSET_UNKNOWN_MIC;

	if (hi->pdata.headset_config_num && hs_mgr_notifier.remote_adc) {
		hs_mgr_notifier.remote_adc(&adc);
		for (i = 0; i < hi->pdata.headset_config_num; i++) {
			if (adc >= hi->pdata.headset_config[i].adc_min &&
			    adc <= hi->pdata.headset_config[i].adc_max)
				return hi->pdata.headset_config[i].type;
		}
			if (hi->pdata.driver_flag & DRIVER_HS_MGR_FLOAT_DET) {
				return HEADSET_UNPLUG;
			}
	} else if (hs_mgr_notifier.mic_status) {
		mic = hs_mgr_notifier.mic_status();
	}
	else
		pr_info("Failed to get MIC status\n");
	return mic;
}

int headset_get_type(void)
{
	return hi->hs_35mm_type;
}

int headset_get_type_sync(int count, unsigned int interval)
{
	int current_type = hi->hs_35mm_type;
	int new_type = HEADSET_UNKNOWN_MIC;

	while (count--) {
		new_type = get_mic_status();
		if (new_type != current_type)
			break;
		if (count)
			msleep(interval);
	}

	if (new_type != current_type) {
		update_mic_status(HS_DEF_MIC_DETECT_COUNT);
		return HEADSET_UNKNOWN_MIC;
	}

	return hi->hs_35mm_type;
}

static void set_35mm_hw_state(int state)
{
	HS_DBG();

	if (hi->pdata.headset_power || hs_mgr_notifier.mic_bias_enable) {
		if (hi->mic_bias_state != state) {
			if (hi->pdata.headset_power)
				hi->pdata.headset_power(state);
			if (hs_mgr_notifier.mic_bias_enable)
				hs_mgr_notifier.mic_bias_enable(state);

			hi->mic_bias_state = state;
			if (state) /* Wait for MIC bias stable */
				msleep(HS_DELAY_MIC_BIAS);
		}
	}

	hs_set_mic_select(state);

	if (hs_mgr_notifier.key_enable)
		hs_mgr_notifier.key_enable(state);

	if (hs_mgr_notifier.key_int_enable)
		hs_mgr_notifier.key_int_enable(state);
}

static int tv_out_detect(void)
{
	int adc = 0;
	int mic = HEADSET_NO_MIC;

	HS_DBG();

	if (!hs_mgr_notifier.remote_adc)
		return HEADSET_NO_MIC;

	if (!hi->pdata.hptv_det_hp_gpio || !hi->pdata.hptv_det_tv_gpio)
		return HEADSET_NO_MIC;

	gpio_set_value(hi->pdata.hptv_det_hp_gpio, 0);
	gpio_set_value(hi->pdata.hptv_det_tv_gpio, 1);
	msleep(HS_DELAY_MIC_BIAS);

	hs_mgr_notifier.remote_adc(&adc);
	if (adc >= HS_DEF_HPTV_ADC_16_BIT_MIN &&
	    adc <= HS_DEF_HPTV_ADC_16_BIT_MAX)
	mic = HEADSET_TV_OUT;

	gpio_set_value(hi->pdata.hptv_det_hp_gpio, 1);
	gpio_set_value(hi->pdata.hptv_det_tv_gpio, 0);

	return mic;
}

#if 0
static void insert_h2w_35mm(int *state)
{
	int mic = HEADSET_NO_MIC;

	pr_info("Insert H2W 3.5mm headset\n");
	set_35mm_hw_state(1);

	mic = get_mic_status();

	if (mic == HEADSET_NO_MIC) {
		*state |= BIT_HEADSET_NO_MIC;
		hi->h2w_35mm_type = HEADSET_NO_MIC;
		pr_info("H2W 3.5mm without microphone\n");
	} else {
		*state |= BIT_HEADSET;
		hi->h2w_35mm_type = HEADSET_MIC;
		pr_info("H2W 3.5mm with microphone\n");
	}
}

static void remove_h2w_35mm(void)
{
	pr_info("Remove H2W 3.5mm headset\n");

	set_35mm_hw_state(0);

	if (atomic_read(&hi->btn_state))
		button_released(atomic_read(&hi->btn_state));
	hi->h2w_35mm_type = HEADSET_UNPLUG;
}
#endif /* #if 0 */

static void enable_metrico_headset(int enable)
{
	HS_DBG();

	if (enable && !hi->metrico_status) {
#if 0
		enable_mos_test(1);
#endif
		hi->metrico_status = 1;
		pr_info("Enable metrico headset\n");
	}

	if (!enable && hi->metrico_status) {
#if 0
		enable_mos_test(0);
#endif
		hi->metrico_status = 0;
		pr_info("Disable metrico headset\n");
	}
}

static void mic_detect_work_func(struct work_struct *work)
{
	int mic = HEADSET_NO_MIC;
	int old_state, new_state;

	wake_lock_timeout(&hi->hs_wake_lock, HS_MIC_DETECT_TIMEOUT);

	HS_DBG();

	if (!hi->pdata.headset_config_num && !hs_mgr_notifier.mic_status) {
		pr_info("Failed to get MIC status\n");
		return;
	}

	mutex_lock(&hi->mutex_lock);

	mic = get_mic_status();

	if (mic == HEADSET_NO_MIC)
		mic = tv_out_detect();

	if (mic == HEADSET_TV_OUT && hi->pdata.hptv_sel_gpio)
		gpio_set_value(hi->pdata.hptv_sel_gpio, 1);

	if (mic == HEADSET_METRICO && !hi->metrico_status)
		enable_metrico_headset(1);

	if (mic == HEADSET_UNKNOWN_MIC || mic == HEADSET_UNPLUG) {
		mutex_unlock(&hi->mutex_lock);
		if (hi->mic_detect_counter--)
			queue_delayed_work(detect_wq, &mic_detect_work,
					   HS_JIFFIES_MIC_DETECT);
		else
			pr_info("MIC polling timeout (UNKNOWN/Floating MIC status)\n");
		return;
	}

	if (hi->hs_35mm_type == HEADSET_UNSTABLE && hi->mic_detect_counter--) {
		mutex_unlock(&hi->mutex_lock);
		queue_delayed_work(detect_wq, &mic_detect_work,
				   HS_JIFFIES_MIC_DETECT);
		return;
	}

	old_state = switch_get_state(&hi->sdev_h2w);
	if (!(old_state & MASK_35MM_HEADSET) && !(hi->is_ext_insert)) {
		pr_info("Headset has been removed\n");
		mutex_unlock(&hi->mutex_lock);
		return;
	}

	new_state = old_state & ~MASK_35MM_HEADSET;
	new_state |= BIT_35MM_HEADSET;

	switch (mic) {
	case HEADSET_UNPLUG:
		new_state &= ~MASK_35MM_HEADSET;
		pr_info("HEADSET_UNPLUG (FLOAT)\n");
		break;
	case HEADSET_NO_MIC:
		new_state |= BIT_HEADSET_NO_MIC;
		pr_info("HEADSET_NO_MIC\n");
		break;
	case HEADSET_MIC:
		new_state |= BIT_HEADSET;
		pr_info("HEADSET_MIC\n");
		break;
	case HEADSET_METRICO:
		new_state |= BIT_HEADSET;
		pr_info("HEADSET_METRICO\n");
		break;
	case HEADSET_TV_OUT:
		new_state |= BIT_TV_OUT;
		pr_info("HEADSET_TV_OUT\n");
		break;
	case HEADSET_BEATS:
		new_state |= BIT_HEADSET;
		pr_info("HEADSET_BEATS\n");
		break;
	case HEADSET_BEATS_SOLO:
		new_state |= BIT_HEADSET;
		pr_info("HEADSET_BEATS_SOLO\n");
		break;
	case HEADSET_INDICATOR:
		pr_info("HEADSET_INDICATOR\n");
		break;
	}

	if (old_state != new_state) {
		if (old_state & new_state & MASK_35MM_HEADSET) {
			if (hi->pdata.driver_flag & DRIVER_HS_MGR_OLD_AJ) {
				new_state |= old_state;
				pr_info("Old audio jack found, use workaround\n");
			} else {
				switch_set_state(&hi->sdev_h2w, old_state & ~MASK_35MM_HEADSET);
				pr_info("Report fake remove event\n");
			}
		}
		hi->hs_35mm_type = mic;
		pr_info("Send uevent for state change, %d => %d\n", old_state, new_state);
		switch_set_state(&hi->sdev_h2w, new_state);
	} else
		pr_info("No state change\n");

	mutex_unlock(&hi->mutex_lock);
}

static void button_35mm_work_func(struct work_struct *work)
{
	int key;
	struct button_work *works;

	wake_lock_timeout(&hi->hs_wake_lock, HS_WAKE_LOCK_TIMEOUT);

	HS_DBG();

	works = container_of(work, struct button_work, key_work.work);
	hi->key_level_flag = works->key_code;

	if (hi->key_level_flag) {
		switch (hi->key_level_flag) {
		case 1:
			key = HS_MGR_KEYCODE_MEDIA;
			break;
		case 2:
			key = HS_MGR_KEYCODE_BACKWARD;
			break;
		case 3:
			key = HS_MGR_KEYCODE_FORWARD;
			break;
		default:
			pr_info("3.5mm RC: WRONG Button Pressed\n");
			kfree(works);
			pre_key_work = NULL;
			return;
		}
		headset_button_event(1, key);
	} else { /* key release */
		if (atomic_read(&hi->btn_state))
			headset_button_event(0, atomic_read(&hi->btn_state));
		else
			pr_info("3.5mm RC: WRONG Button Release\n");
	}

	kfree(works);
	pre_key_work = NULL;
}

static void debug_work_func(struct work_struct *work)
{
	int flag = 0;
	int adc = -EINVAL;
	int hpin_gpio = -EINVAL;

	HS_DBG();

	while (hi->debug_flag & DEBUG_FLAG_ADC) {
		flag = hi->debug_flag;
		if (hs_mgr_notifier.hpin_gpio)
			hpin_gpio = hs_mgr_notifier.hpin_gpio();
		if (hs_mgr_notifier.remote_adc)
			hs_mgr_notifier.remote_adc(&adc);
		pr_info("Debug Flag %d, HP_DET %d, ADC %d\n", flag,
		       hpin_gpio, adc);
		msleep(HS_DELAY_SEC);
	}
}

static void remove_detect_work_func(struct work_struct *work)
{
	int state;

	wake_lock_timeout(&hi->hs_wake_lock, HS_WAKE_LOCK_TIMEOUT);

	HS_DBG();

	if (time_before_eq(jiffies, hi->insert_jiffies + HZ)) {
		pr_info("Waiting for HPIN stable\n");
		if (hi->pdata.driver_flag & DRIVER_HS_MGR_OLD_AJ)
			msleep(HS_DELAY_SEC - HS_DELAY_REMOVE_LONG);
		else
			msleep(HS_DELAY_SEC - HS_DELAY_REMOVE_SHORT);
	}

	if (hi->is_ext_insert) {
		pr_info("Headset has been reinserted during debounce time\n");
		return;
	}

	if (hi->hs_35mm_type == HEADSET_INDICATOR &&
	    hs_mgr_notifier.indicator_enable)
		hs_mgr_notifier.indicator_enable(0);

	set_35mm_hw_state(0);
	if (hi->metrico_status)
		enable_metrico_headset(0);

	if (atomic_read(&hi->btn_state))
		button_released(atomic_read(&hi->btn_state));
	hi->hs_35mm_type = HEADSET_UNPLUG;

	mutex_lock(&hi->mutex_lock);

	state = switch_get_state(&hi->sdev_h2w);
	if (!(state & MASK_35MM_HEADSET)) {
		pr_info("Headset has been removed\n");
		mutex_unlock(&hi->mutex_lock);
		return;
	}

	state &= ~(MASK_35MM_HEADSET | MASK_FM_ATTRIBUTE);
	switch_set_state(&hi->sdev_h2w, state);

	pr_info("Remove 3.5mm accessory\n");

	mutex_unlock(&hi->mutex_lock);

#ifdef HTC_HEADSET_CONFIG_QUICK_BOOT
	if (gpio_event_get_quickboot_status())
		pr_info("quick_boot_status = 1\n");
#endif
}

static void insert_detect_work_func(struct work_struct *work)
{
	int state,old_state;
	int mic = HEADSET_NO_MIC;

	wake_lock_timeout(&hi->hs_wake_lock, HS_WAKE_LOCK_TIMEOUT);

	HS_DBG();

	if (!hi->is_ext_insert) {
		pr_info("Headset has been removed\n");
		return;
	}

	hi->insert_jiffies = jiffies;
	set_35mm_hw_state(1);

	mutex_lock(&hi->mutex_lock);

	mic = get_mic_status();
	if (hi->pdata.driver_flag & DRIVER_HS_MGR_FLOAT_DET) {
		pr_info("Headset float detect enable\n");
		if (mic == HEADSET_UNPLUG) {
			mutex_unlock(&hi->mutex_lock);
			update_mic_status(HS_DEF_MIC_DETECT_COUNT);
			return;
		}
	}

	if (mic == HEADSET_NO_MIC)
		mic = tv_out_detect();

	if (mic == HEADSET_TV_OUT && hi->pdata.hptv_sel_gpio)
		gpio_set_value(hi->pdata.hptv_sel_gpio, 1);

	if (mic == HEADSET_METRICO && !hi->metrico_status)
		enable_metrico_headset(1);

	state = switch_get_state(&hi->sdev_h2w);
	old_state = state;
	state &= ~MASK_35MM_HEADSET;
	state |= BIT_35MM_HEADSET;

	switch (mic) {

	case HEADSET_NO_MIC:
		state |= BIT_HEADSET_NO_MIC;
		pr_info("HEADSET_NO_MIC\n");
		break;
	case HEADSET_MIC:
		state |= BIT_HEADSET;
		pr_info("HEADSET_MIC\n");
		break;
	case HEADSET_METRICO:
		mic = HEADSET_UNSTABLE;
		pr_info("HEADSET_METRICO (UNSTABLE)\n");
		break;
	case HEADSET_UNKNOWN_MIC:
		state |= BIT_HEADSET_NO_MIC;
		pr_info("HEADSET_UNKNOWN_MIC\n");
		break;
	case HEADSET_TV_OUT:
		state |= BIT_TV_OUT;
		pr_info("HEADSET_TV_OUT\n");
		break;
	case HEADSET_BEATS:
		state |= BIT_HEADSET;
		pr_info("HEADSET_BEATS\n");
		break;
	case HEADSET_BEATS_SOLO:
		state |= BIT_HEADSET;
		pr_info("HEADSET_BEATS_SOLO\n");
		break;
	case HEADSET_INDICATOR:
		pr_info("HEADSET_INDICATOR\n");
		break;
	}

	if (old_state != state) {
		if (old_state & state & MASK_35MM_HEADSET) {
			if (hi->pdata.driver_flag & DRIVER_HS_MGR_OLD_AJ) {
				state |= old_state;
				pr_info("Old audio jack found, use workaround\n");
			} else {
				switch_set_state(&hi->sdev_h2w, old_state & ~MASK_35MM_HEADSET);
				pr_info("Report fake remove event\n");
			}
		}
		hi->hs_35mm_type = mic;
		pr_info("Send uevent for state change, %d => %d\n", old_state, state);
		switch_set_state(&hi->sdev_h2w, state);
	} else
		pr_info("No state change");

	mutex_unlock(&hi->mutex_lock);

#ifdef HTC_HEADSET_CONFIG_QUICK_BOOT
	if (gpio_event_get_quickboot_status())
		pr_info("quick_boot_status = 1\n");
#endif

	if (mic == HEADSET_UNKNOWN_MIC)
		update_mic_status(HS_DEF_MIC_DETECT_COUNT);
	else if (mic == HEADSET_UNSTABLE)
		update_mic_status(0);
	else if (mic == HEADSET_INDICATOR) {
		if (headset_get_type_sync(3, HS_DELAY_SEC) == HEADSET_INDICATOR)
			pr_info("Delay check: HEADSET_INDICATOR\n");
		else
			pr_info("Delay check: HEADSET_UNKNOWN_MIC\n");
	}
}

int hs_notify_plug_event(int insert)
{
	int ret = 0;
	HS_DBG("Headset status %d", insert);

	mutex_lock(&hi->mutex_lock);
	hi->is_ext_insert = insert;
	mutex_unlock(&hi->mutex_lock);

	cancel_delayed_work_sync(&mic_detect_work);
	ret = cancel_delayed_work_sync(&insert_detect_work);
	if (ret && hs_mgr_notifier.key_int_enable)
		hs_mgr_notifier.key_int_enable(1);
	ret = cancel_delayed_work_sync(&remove_detect_work);
	if (ret && hs_mgr_notifier.key_int_enable)
		hs_mgr_notifier.key_int_enable(0);

	if (hi->is_ext_insert)
		queue_delayed_work(detect_wq, &insert_detect_work,
				   HS_JIFFIES_INSERT);
	else {
		if (hi->pdata.driver_flag & DRIVER_HS_MGR_OLD_AJ) {
			queue_delayed_work(detect_wq, &remove_detect_work,
					HS_JIFFIES_REMOVE_LONG);
		} else {
			queue_delayed_work(detect_wq, &remove_detect_work,
					HS_JIFFIES_REMOVE_SHORT);
		}
	}
	return 1;
}

int hs_notify_key_event(int key_code)
{
	struct button_work *work;
	int ret;

	HS_DBG();

	if (pre_key_work) {
		ret = cancel_delayed_work_sync(pre_key_work);
		if (ret)
			pr_info("Previous key code cancelled\n");
	}
	if (hi->hs_35mm_type == HEADSET_INDICATOR) {
		pr_info("Not support remote control\n");
		return 1;
	}

	if (hi->hs_35mm_type == HEADSET_UNKNOWN_MIC ||
	    hi->hs_35mm_type == HEADSET_NO_MIC ||
	    hi->h2w_35mm_type == HEADSET_NO_MIC)
		update_mic_status(HS_DEF_MIC_DETECT_COUNT);
	else if (hi->hs_35mm_type == HEADSET_UNSTABLE)
		update_mic_status(0);
	else if (!hs_hpin_stable()) {
		pr_info("IGNORE key %d (Unstable HPIN)\n", key_code);
		return 1;
	} else if (hi->hs_35mm_type == HEADSET_UNPLUG && hi->is_ext_insert == 1) {
		pr_info("MIC status is changed from float, re-polling to decide accessory type\n");
		update_mic_status(HS_DEF_MIC_DETECT_COUNT);
		return 1;
	} else {
		work = kzalloc(sizeof(struct button_work), GFP_KERNEL);
		if (!work) {
			pr_info("Failed to allocate button memory\n");
			return 1;
		}
		work->key_code = key_code;
		INIT_DELAYED_WORK(&work->key_work, button_35mm_work_func);
		pre_key_work = &work->key_work;
		if (hi->pdata.driver_flag & DRIVER_HS_MGR_OLD_AJ) {
			queue_delayed_work(button_wq, &work->key_work,
					   HS_JIFFIES_BUTTON_LONG);
		} else {
			queue_delayed_work(button_wq, &work->key_work,
					   HS_JIFFIES_BUTTON);
		}
	}

	return 1;
}

int hs_notify_key_irq(void)
{
	int adc = 0;
	int key_code = HS_MGR_KEY_INVALID;

	if (hi->hs_35mm_type == HEADSET_INDICATOR) {
		pr_info("Not support remote control\n");
		return 1;
	}

	if (!hs_mgr_notifier.remote_adc || !hs_mgr_notifier.remote_keycode) {
		pr_info("Failed to get remote key code\n");
		return 1;
	}

	if (hs_hpin_stable()) {
		mdelay(40);
		hs_mgr_notifier.remote_adc(&adc);
		key_code = hs_mgr_notifier.remote_keycode(adc);
		hs_notify_key_event(key_code);
	} else if (hi->hs_35mm_type == HEADSET_NO_MIC ||
		   hi->hs_35mm_type == HEADSET_UNKNOWN_MIC) {
		pr_info("IGNORE key IRQ (Unstable HPIN)\n");
		update_mic_status(HS_DEF_MIC_DETECT_COUNT);
	}

	return 1;
}

static void usb_headset_detect(int type)
{
	int state_h2w = 0;
	int state_usb = 0;

	HS_DBG();

	mutex_lock(&hi->mutex_lock);
	state_h2w = switch_get_state(&hi->sdev_h2w);

	switch (type) {
	case USB_NO_HEADSET:
		hi->usb_headset.type = USB_NO_HEADSET;
		hi->usb_headset.status = STATUS_DISCONNECTED;
		state_h2w &= ~MASK_USB_HEADSET;
		state_usb = GOOGLE_USB_AUDIO_UNPLUG;
		pr_info("Remove USB_HEADSET (state %d, %d)\n",
			    state_h2w, state_usb);
		break;
	case USB_AUDIO_OUT:
		hi->usb_headset.type = USB_AUDIO_OUT;
		hi->usb_headset.status = STATUS_CONNECTED_ENABLED;
		state_h2w |= BIT_USB_AUDIO_OUT;
		state_usb = GOOGLE_USB_AUDIO_ANLG;
		pr_info("Insert USB_AUDIO_OUT (state %d, %d)\n",
			    state_h2w, state_usb);
		break;
	default:
		pr_info("Unknown headset type\n");
	}

	switch_set_state(&hi->sdev_h2w, state_h2w);
	switch_set_state(&hi->sdev_usb_audio, state_usb);
	mutex_unlock(&hi->mutex_lock);
}

void headset_ext_detect(int type)
{
	HS_DBG();

	switch (type) {
	case H2W_NO_HEADSET:
		/* Release Key */
	case H2W_HEADSET:
	case H2W_35MM_HEADSET:
	case H2W_REMOTE_CONTROL:
	case H2W_USB_CRADLE:
	case H2W_UART_DEBUG:
	case H2W_TVOUT:
		break;
	case USB_NO_HEADSET:
		/* Release Key */
	case USB_AUDIO_OUT:
		usb_headset_detect(type);
		break;
	default:
		pr_info("Unknown headset type\n");
	}
}

void headset_ext_button(int headset_type, int key_code, int press)
{
	pr_info("Headset %d, Key %d, Press %d\n", headset_type, key_code, press);
	headset_button_event(press, key_code);
}

int switch_send_event(unsigned int bit, int on)
{
	unsigned long state;

	HS_DBG();

	mutex_lock(&hi->mutex_lock);
	state = switch_get_state(&hi->sdev_h2w);
	state &= ~(bit);

	if (on)
		state |= bit;

	switch_set_state(&hi->sdev_h2w, state);
	mutex_unlock(&hi->mutex_lock);
	return 0;
}

static ssize_t headset_state_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int length = 0;
	char *state;

	HS_DBG();

	switch (hi->hs_35mm_type) {
	case HEADSET_UNPLUG:
		state = "headset_unplug";
		break;
	case HEADSET_NO_MIC:
		state = "headset_no_mic";
		break;
	case HEADSET_MIC:
		state = "headset_mic";
		break;
	case HEADSET_METRICO:
		state = "headset_metrico";
		break;
	case HEADSET_UNKNOWN_MIC:
		state = "headset_unknown_mic";
		break;
	case HEADSET_TV_OUT:
		state = "headset_tv_out";
		break;
	case HEADSET_UNSTABLE:
		state = "headset_unstable";
		break;
	case HEADSET_BEATS:
		state = "headset_beats";
		break;
	case HEADSET_BEATS_SOLO:
		state = "headset_beats_solo";
		break;
	case HEADSET_INDICATOR:
		state = "headset_indicator";
		break;
	default:
		state = "error_state";
	}

	length = sprintf(buf, "%s\n", state);

	return length;
}

static ssize_t headset_state_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	HS_DBG();
	return 0;
}

static DEVICE_HEADSET_ATTR(state, 0644, headset_state_show,
			   headset_state_store);

static ssize_t headset_simulate_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	HS_DBG();
	return sprintf(buf, "Command is not supported\n");
}

static ssize_t headset_simulate_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned long state = 0;

	HS_DBG();

	state = MASK_35MM_HEADSET | MASK_USB_HEADSET;
	switch_send_event(state, 0);

	if (strncmp(buf, "headset_unplug", count - 1) == 0) {
		pr_info("Headset simulation: headset_unplug\n");
		set_35mm_hw_state(0);
		hi->hs_35mm_type = HEADSET_UNPLUG;
		return count;
	}

	set_35mm_hw_state(1);
	state = BIT_35MM_HEADSET;

	if (strncmp(buf, "headset_no_mic", count - 1) == 0) {
		pr_info("Headset simulation: headset_no_mic\n");
		hi->hs_35mm_type = HEADSET_NO_MIC;
		state |= BIT_HEADSET_NO_MIC;
	} else if (strncmp(buf, "headset_mic", count - 1) == 0) {
		pr_info("Headset simulation: headset_mic\n");
		hi->hs_35mm_type = HEADSET_MIC;
		state |= BIT_HEADSET;
	} else if (strncmp(buf, "headset_metrico", count - 1) == 0) {
		pr_info("Headset simulation: headset_metrico\n");
		hi->hs_35mm_type = HEADSET_METRICO;
		state |= BIT_HEADSET;
	} else if (strncmp(buf, "headset_unknown_mic", count - 1) == 0) {
		pr_info("Headset simulation: headset_unknown_mic\n");
		hi->hs_35mm_type = HEADSET_UNKNOWN_MIC;
		state |= BIT_HEADSET_NO_MIC;
	} else if (strncmp(buf, "headset_tv_out", count - 1) == 0) {
		pr_info("Headset simulation: headset_tv_out\n");
		hi->hs_35mm_type = HEADSET_TV_OUT;
		state |= BIT_TV_OUT;
	} else if (strncmp(buf, "headset_indicator", count - 1) == 0) {
		pr_info("Headset simulation: headset_indicator\n");
		hi->hs_35mm_type = HEADSET_INDICATOR;
	} else if (strncmp(buf, "headset_beats", count - 1) == 0) {
		pr_info("Headset simulation: headset_beats\n");
		hi->hs_35mm_type = HEADSET_BEATS;
		state |= BIT_HEADSET;
	} else if (strncmp(buf, "headset_beats_solo", count - 1) == 0) {
		pr_info("Headset simulation: headset_beats_solo\n");
		hi->hs_35mm_type = HEADSET_BEATS_SOLO;
		state |= BIT_HEADSET;
	} else {
		pr_info("Invalid parameter\n");
		return count;
	}

	switch_send_event(state, 1);

	return count;
}

static DEVICE_HEADSET_ATTR(simulate, 0644, headset_simulate_show,
			   headset_simulate_store);

static ssize_t tty_flag_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	char *s = buf;

	HS_DBG();

	mutex_lock(&hi->mutex_lock);
	s += sprintf(s, "%d\n", hi->tty_enable_flag);
	mutex_unlock(&hi->mutex_lock);
	return (s - buf);
}

static ssize_t tty_flag_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int state;

	HS_DBG();

	mutex_lock(&hi->mutex_lock);
	state = switch_get_state(&hi->sdev_h2w);
	state &= ~(BIT_TTY_FULL | BIT_TTY_VCO | BIT_TTY_HCO);

	if (count == (strlen("enable") + 1) &&
	   strncmp(buf, "enable", strlen("enable")) == 0) {
		hi->tty_enable_flag = 1;
		switch_set_state(&hi->sdev_h2w, state | BIT_TTY_FULL);
		mutex_unlock(&hi->mutex_lock);
		pr_info("Enable TTY FULL\n");
		return count;
	}
	if (count == (strlen("vco_enable") + 1) &&
	   strncmp(buf, "vco_enable", strlen("vco_enable")) == 0) {
		hi->tty_enable_flag = 2;
		switch_set_state(&hi->sdev_h2w, state | BIT_TTY_VCO);
		mutex_unlock(&hi->mutex_lock);
		pr_info("Enable TTY VCO\n");
		return count;
	}
	if (count == (strlen("hco_enable") + 1) &&
	   strncmp(buf, "hco_enable", strlen("hco_enable")) == 0) {
		hi->tty_enable_flag = 3;
		switch_set_state(&hi->sdev_h2w, state | BIT_TTY_HCO);
		mutex_unlock(&hi->mutex_lock);
		pr_info("Enable TTY HCO\n");
		return count;
	}
	if (count == (strlen("disable") + 1) &&
	   strncmp(buf, "disable", strlen("disable")) == 0) {
		hi->tty_enable_flag = 0;
		switch_set_state(&hi->sdev_h2w, state);
		mutex_unlock(&hi->mutex_lock);
		pr_info("Disable TTY\n");
		return count;
	}

	mutex_unlock(&hi->mutex_lock);
	pr_info("Invalid TTY argument\n");

	return -EINVAL;
}

static DEVICE_ACCESSORY_ATTR(tty, 0644, tty_flag_show, tty_flag_store);

static ssize_t fm_flag_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	char *s = buf;
	char *state;

	HS_DBG();

	mutex_lock(&hi->mutex_lock);
	switch (hi->fm_flag) {
	case 0:
		state = "disable";
		break;
	case 1:
		state = "fm_headset";
		break;
	case 2:
		state = "fm_speaker";
		break;
	default:
		state = "unknown_fm_status";
	}

	s += sprintf(s, "%s\n", state);
	mutex_unlock(&hi->mutex_lock);
	return (s - buf);
}

static ssize_t fm_flag_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int state;

	HS_DBG();

	mutex_lock(&hi->mutex_lock);
	state = switch_get_state(&hi->sdev_h2w);
	state &= ~(BIT_FM_HEADSET | BIT_FM_SPEAKER);

	if (count == (strlen("fm_headset") + 1) &&
	   strncmp(buf, "fm_headset", strlen("fm_headset")) == 0) {
		hi->fm_flag = 1;
		state |= BIT_FM_HEADSET;
		pr_info("Enable FM HEADSET\n");
	} else if (count == (strlen("fm_speaker") + 1) &&
	   strncmp(buf, "fm_speaker", strlen("fm_speaker")) == 0) {
		hi->fm_flag = 2;
		state |= BIT_FM_SPEAKER;
		pr_info("Enable FM SPEAKER\n");
	} else if (count == (strlen("disable") + 1) &&
	   strncmp(buf, "disable", strlen("disable")) == 0) {
		hi->fm_flag = 0 ;
		pr_info("Disable FM\n");
	} else {
		mutex_unlock(&hi->mutex_lock);
		pr_info("Invalid FM argument\n");
		return -EINVAL;
	}

	switch_set_state(&hi->sdev_h2w, state);
	mutex_unlock(&hi->mutex_lock);

	return count;
}

static DEVICE_ACCESSORY_ATTR(fm, 0644, fm_flag_show, fm_flag_store);

static ssize_t debug_flag_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	int flag = hi->debug_flag;
	int adc = -EINVAL;
	int hpin_gpio = -EINVAL;

	HS_DBG();

	if (hs_mgr_notifier.hpin_gpio)
		hpin_gpio = hs_mgr_notifier.hpin_gpio();
	if (hs_mgr_notifier.remote_adc)
		hs_mgr_notifier.remote_adc(&adc);

	return sprintf(buf, "Debug Flag %d, HP_DET %d, ADC %d\n", flag,
		       hpin_gpio, adc);
}

static ssize_t debug_flag_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	unsigned long state = 0;

	HS_DBG();

	if (strncmp(buf, "enable", count - 1) == 0) {
		if (hi->debug_flag & DEBUG_FLAG_ADC) {
			pr_info("Debug work is already running\n");
			return count;
		}
		if (!debug_wq) {
			debug_wq = create_workqueue("debug");
			if (!debug_wq) {
				pr_info("Failed to create debug workqueue\n");
				return count;
			}
		}
		pr_info("Enable headset debug\n");
		mutex_lock(&hi->mutex_lock);
		hi->debug_flag |= DEBUG_FLAG_ADC;
		mutex_unlock(&hi->mutex_lock);
		queue_work(debug_wq, &debug_work);
	} else if (strncmp(buf, "disable", count - 1) == 0) {
		if (!(hi->debug_flag & DEBUG_FLAG_ADC)) {
			pr_info("Debug work has been stopped\n");
			return count;
		}
		pr_info("Disable headset debug\n");
		mutex_lock(&hi->mutex_lock);
		hi->debug_flag &= ~DEBUG_FLAG_ADC;
		mutex_unlock(&hi->mutex_lock);
		if (debug_wq) {
			flush_workqueue(debug_wq);
			destroy_workqueue(debug_wq);
			debug_wq = NULL;
		}
	} else if (strncmp(buf, "debug_log_enable", count - 1) == 0) {
		pr_info("Enable headset debug log\n");
		hi->debug_flag |= DEBUG_FLAG_LOG;
	} else if (strncmp(buf, "debug_log_disable", count - 1) == 0) {
		pr_info("Disable headset debug log\n");
		hi->debug_flag &= ~DEBUG_FLAG_LOG;
	} else if (strncmp(buf, "no_headset", count - 1) == 0) {
		pr_info("Headset simulation: no_headset\n");
		state = BIT_HEADSET | BIT_HEADSET_NO_MIC | BIT_35MM_HEADSET |
			BIT_TV_OUT | BIT_USB_AUDIO_OUT;
		switch_send_event(state, 0);
	} else if (strncmp(buf, "35mm_mic", count - 1) == 0) {
		pr_info("Headset simulation: 35mm_mic\n");
		state = BIT_HEADSET | BIT_35MM_HEADSET;
		switch_send_event(state, 1);
	} else if (strncmp(buf, "35mm_no_mic", count - 1) == 0) {
		pr_info("Headset simulation: 35mm_no_mic\n");
		state = BIT_HEADSET_NO_MIC | BIT_35MM_HEADSET;
		switch_send_event(state, 1);
	} else if (strncmp(buf, "35mm_tv_out", count - 1) == 0) {
		pr_info("Headset simulation: 35mm_tv_out\n");
		state = BIT_TV_OUT | BIT_35MM_HEADSET;
		switch_send_event(state, 1);
	} else if (strncmp(buf, "usb_audio", count - 1) == 0) {
		pr_info("Headset simulation: usb_audio\n");
		state = BIT_USB_AUDIO_OUT;
		switch_send_event(state, 1);
	} else {
		pr_info("Invalid parameter\n");
		return count;
	}

	return count;
}

static DEVICE_ACCESSORY_ATTR(debug, 0644, debug_flag_show, debug_flag_store);

static int register_attributes(void)
{
	int ret = 0;

	hi->htc_accessory_class = class_create(THIS_MODULE, "htc_accessory");
	if (IS_ERR(hi->htc_accessory_class)) {
		ret = PTR_ERR(hi->htc_accessory_class);
		hi->htc_accessory_class = NULL;
		goto err_create_class;
	}

	/* Register headset attributes */
	hi->headset_dev = device_create(hi->htc_accessory_class,
					NULL, 0, "%s", "headset");
	if (unlikely(IS_ERR(hi->headset_dev))) {
		ret = PTR_ERR(hi->headset_dev);
		hi->headset_dev = NULL;
		goto err_create_headset_device;
	}

	ret = device_create_file(hi->headset_dev, &dev_attr_headset_state);
	if (ret)
		goto err_create_headset_state_device_file;

	ret = device_create_file(hi->headset_dev, &dev_attr_headset_simulate);
	if (ret)
		goto err_create_headset_simulate_device_file;

	/* Register TTY attributes */
	hi->tty_dev = device_create(hi->htc_accessory_class,
				    NULL, 0, "%s", "tty");
	if (unlikely(IS_ERR(hi->tty_dev))) {
		ret = PTR_ERR(hi->tty_dev);
		hi->tty_dev = NULL;
		goto err_create_tty_device;
	}

	ret = device_create_file(hi->tty_dev, &dev_attr_tty);
	if (ret)
		goto err_create_tty_device_file;

	/* Register FM attributes */
	hi->fm_dev = device_create(hi->htc_accessory_class,
				   NULL, 0, "%s", "fm");
	if (unlikely(IS_ERR(hi->fm_dev))) {
		ret = PTR_ERR(hi->fm_dev);
		hi->fm_dev = NULL;
		goto err_create_fm_device;
	}

	ret = device_create_file(hi->fm_dev, &dev_attr_fm);
	if (ret)
		goto err_create_fm_device_file;

	/* Register debug attributes */
	hi->debug_dev = device_create(hi->htc_accessory_class,
				      NULL, 0, "%s", "debug");
	if (unlikely(IS_ERR(hi->debug_dev))) {
		ret = PTR_ERR(hi->debug_dev);
		hi->debug_dev = NULL;
		goto err_create_debug_device;
	}

	/* register the attributes */
	ret = device_create_file(hi->debug_dev, &dev_attr_debug);
	if (ret)
		goto err_create_debug_device_file;

	return 0;

err_create_debug_device_file:
	device_unregister(hi->debug_dev);

err_create_debug_device:
	device_remove_file(hi->fm_dev, &dev_attr_fm);

err_create_fm_device_file:
	device_unregister(hi->fm_dev);

err_create_fm_device:
	device_remove_file(hi->tty_dev, &dev_attr_tty);

err_create_tty_device_file:
	device_unregister(hi->tty_dev);

err_create_tty_device:
	device_remove_file(hi->headset_dev, &dev_attr_headset_simulate);

err_create_headset_simulate_device_file:
	device_remove_file(hi->headset_dev, &dev_attr_headset_state);

err_create_headset_state_device_file:
	device_unregister(hi->headset_dev);

err_create_headset_device:
	class_destroy(hi->htc_accessory_class);

err_create_class:

	return ret;
}

static void unregister_attributes(void)
{
	device_remove_file(hi->debug_dev, &dev_attr_debug);
	device_unregister(hi->debug_dev);
	device_remove_file(hi->fm_dev, &dev_attr_fm);
	device_unregister(hi->fm_dev);
	device_remove_file(hi->tty_dev, &dev_attr_tty);
	device_unregister(hi->tty_dev);
	device_remove_file(hi->headset_dev, &dev_attr_headset_simulate);
	device_remove_file(hi->headset_dev, &dev_attr_headset_state);
	device_unregister(hi->headset_dev);
	class_destroy(hi->htc_accessory_class);
}

static void headset_mgr_init(void)
{
	if (hi->pdata.hptv_det_hp_gpio)
		gpio_set_value(hi->pdata.hptv_det_hp_gpio, 1);
	if (hi->pdata.hptv_det_tv_gpio)
		gpio_set_value(hi->pdata.hptv_det_tv_gpio, 0);
	if (hi->pdata.hptv_sel_gpio)
		gpio_set_value(hi->pdata.hptv_sel_gpio, 0);
}

static int htc_headset_mgr_suspend(struct platform_device *pdev,
				   pm_message_t mesg)
{
	HS_DBG();

#ifdef HTC_HEADSET_CONFIG_QUICK_BOOT
	if (gpio_event_get_quickboot_status())
		hi->quick_boot_status = 1;
#endif

	return 0;
}

static int htc_headset_mgr_resume(struct platform_device *pdev)
{
	HS_DBG();

	return 0;
}

static int htc_headset_mgr_probe(struct platform_device *pdev)
{
	int ret;

	struct htc_headset_mgr_platform_data *pdata = pdev->dev.platform_data;

	pr_info("++++++++++++++++++++\n");

	hi = kzalloc(sizeof(struct htc_headset_mgr_info), GFP_KERNEL);
	if (!hi)
		return -ENOMEM;

	hi->pdata.driver_flag = pdata->driver_flag;
	hi->pdata.headset_devices_num = pdata->headset_devices_num;
	hi->pdata.headset_devices = pdata->headset_devices;
	hi->pdata.headset_config_num = pdata->headset_config_num;
	hi->pdata.headset_config = pdata->headset_config;

	hi->pdata.hptv_det_hp_gpio = pdata->hptv_det_hp_gpio;
	hi->pdata.hptv_det_tv_gpio = pdata->hptv_det_tv_gpio;
	hi->pdata.hptv_sel_gpio = pdata->hptv_sel_gpio;

	hi->pdata.headset_init = pdata->headset_init;
	hi->pdata.headset_power = pdata->headset_power;

	if (hi->pdata.headset_init)
		hi->pdata.headset_init();

	hi->driver_init_seq = 0;

	wake_lock_init(&hi->hs_wake_lock, WAKE_LOCK_SUSPEND, "hs_mgr");

	hi->hpin_jiffies = jiffies;
	hi->usb_headset.type = USB_NO_HEADSET;
	hi->usb_headset.status = STATUS_DISCONNECTED;

	hi->hs_35mm_type = HEADSET_UNPLUG;
	hi->h2w_35mm_type = HEADSET_UNPLUG;
	hi->is_ext_insert = 0;
	hi->mic_bias_state = 0;
	hi->mic_detect_counter = 0;
	hi->key_level_flag = -1;
	hi->quick_boot_status = 0;

	atomic_set(&hi->btn_state, 0);

	hi->tty_enable_flag = 0;
	hi->fm_flag = 0;
	hi->debug_flag = 0;

	mutex_init(&hi->mutex_lock);

	hi->sdev_h2w.name = "h2w";
	hi->sdev_h2w.print_name = h2w_print_name;

	ret = switch_dev_register(&hi->sdev_h2w);
	if (ret < 0)
		goto err_h2w_switch_dev_register;

	hi->sdev_usb_audio.name = "usb_audio";
	hi->sdev_usb_audio.print_name = usb_audio_print_name;

	ret = switch_dev_register(&hi->sdev_usb_audio);
	if (ret < 0)
		goto err_usb_audio_switch_dev_register;

	detect_wq = create_workqueue("detect");
	if (detect_wq == NULL) {
		ret = -ENOMEM;
		pr_info("Failed to create detect workqueue\n");
		goto err_create_detect_work_queue;
	}

	button_wq = create_workqueue("button");
	if (button_wq  == NULL) {
		ret = -ENOMEM;
		pr_info("Failed to create button workqueue\n");
		goto err_create_button_work_queue;
	}

	hi->input = input_allocate_device();
	if (!hi->input) {
		ret = -ENOMEM;
		goto err_request_input_dev;
	}

	hi->input->name = "h2w headset";
	set_bit(EV_SYN, hi->input->evbit);
	set_bit(EV_KEY, hi->input->evbit);
	set_bit(KEY_END, hi->input->keybit);
	set_bit(KEY_MUTE, hi->input->keybit);
	set_bit(KEY_VOLUMEDOWN, hi->input->keybit);
	set_bit(KEY_VOLUMEUP, hi->input->keybit);
	set_bit(KEY_NEXTSONG, hi->input->keybit);
	set_bit(KEY_PLAYPAUSE, hi->input->keybit);
	set_bit(KEY_PREVIOUSSONG, hi->input->keybit);
	set_bit(KEY_MEDIA, hi->input->keybit);
	set_bit(KEY_SEND, hi->input->keybit);

	ret = input_register_device(hi->input);
	if (ret < 0)
	goto err_register_input_dev;

	ret = register_attributes();
	if (ret)
		goto err_register_attributes;

#ifdef HTC_HEADSET_CONFIG_MSM_RPC
	if (hi->pdata.driver_flag & DRIVER_HS_MGR_RPC_SERVER) {
		/* Create RPC server */
		ret = msm_rpc_create_server(&hs_rpc_server);
		if (ret < 0) {
			pr_info("Failed to create RPC server\n");
			goto err_create_rpc_server;
		}
		pr_info("Create RPC server successfully\n");
	}
#else
	HS_DBG("NOT support RPC (%du, %du)", hs_rpc_server.prog,
	       hs_rpc_server.vers);
#endif

	headset_mgr_init();
	hs_notify_driver_ready("hs_mgr");

	pr_info("--------------------\n");

	return 0;

#ifdef HTC_HEADSET_CONFIG_MSM_RPC
err_create_rpc_server:
#endif

err_register_attributes:
	input_unregister_device(hi->input);

err_register_input_dev:
	input_free_device(hi->input);

err_request_input_dev:
	destroy_workqueue(button_wq);

err_create_button_work_queue:
	destroy_workqueue(detect_wq);

err_create_detect_work_queue:
	switch_dev_unregister(&hi->sdev_usb_audio);

err_usb_audio_switch_dev_register:
	switch_dev_unregister(&hi->sdev_h2w);

err_h2w_switch_dev_register:
	mutex_destroy(&hi->mutex_lock);
	wake_lock_destroy(&hi->hs_wake_lock);
	kfree(hi);

	pr_info("Failed to register driver\n");

	return ret;
}

static int htc_headset_mgr_remove(struct platform_device *pdev)
{
#if 0
	if ((switch_get_state(&hi->sdev_h2w) & MASK_HEADSET) != 0)
		remove_headset();
#endif

	unregister_attributes();
	input_unregister_device(hi->input);
	destroy_workqueue(button_wq);
	destroy_workqueue(detect_wq);
	switch_dev_unregister(&hi->sdev_usb_audio);
	switch_dev_unregister(&hi->sdev_h2w);
	mutex_destroy(&hi->mutex_lock);
	wake_lock_destroy(&hi->hs_wake_lock);
	kfree(hi);

	return 0;
}

static struct platform_driver htc_headset_mgr_driver = {
	.probe		= htc_headset_mgr_probe,
	.remove		= htc_headset_mgr_remove,
	.suspend	= htc_headset_mgr_suspend,
	.resume		= htc_headset_mgr_resume,
	.driver		= {
		.name		= "HTC_HEADSET_MGR",
		.owner		= THIS_MODULE,
	},
};


static int __init htc_headset_mgr_init(void)
{
	return platform_driver_register(&htc_headset_mgr_driver);
}

static void __exit htc_headset_mgr_exit(void)
{
	platform_driver_unregister(&htc_headset_mgr_driver);
}

module_init(htc_headset_mgr_init);
module_exit(htc_headset_mgr_exit);

MODULE_DESCRIPTION("HTC headset manager driver");
MODULE_LICENSE("GPL");
