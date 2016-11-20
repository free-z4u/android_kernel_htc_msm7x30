/* linux/arch/arm/mach-msm/devices.c
 *
 * Copyright (C) 2008 Google, Inc.
 * Copyright (C) 2007-2009 HTC Corporation.
 * Author: Thomas Tsai <thomas_tsai@htc.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <mach/board.h>
#include <asm/setup.h>
#include <linux/mtd/nand.h>
#include <linux/export.h>

#define MFG_GPIO_TABLE_MAX_SIZE        0x400
static unsigned char mfg_gpio_table[MFG_GPIO_TABLE_MAX_SIZE];

/* setup calls mach->fixup, then parse_tags, parse_cmdline
 * We need to setup meminfo in mach->fixup, so this function
 * will need to traverse each tag to find smi tag.
 */
static int smi_sz = 0;

void __init early_init_dt_setup_smi(unsigned long value) {
	pr_info("[dt]smi = 0x%lx\n", value);
	smi_sz = value;
}

unsigned int board_get_smi_sz(void)
{
	return smi_sz;
}

int __init parse_tag_smi(const struct tag *tags)
{
	smi_sz = tags->u.mem.size;
	pr_info("[atag]smi_size = %d\n", smi_sz);
	return smi_sz;
}
__tagtable(ATAG_SMI, parse_tag_smi);


static int hwid = 0;

void __init early_init_dt_setup_hwid(unsigned long value) {
	pr_info("[dt]hwid = 0x%lx\n", value);
	smi_sz = value;
}

unsigned int board_get_hwid(void)
{
	return hwid;
}

int __init parse_tag_hwid(const struct tag *tags)
{
	hwid = tags->u.revision.rev;
	pr_info("[atag]hwid = 0x%x\n", hwid);
	return hwid;
}
__tagtable(ATAG_HWID, parse_tag_hwid);

static char *keycap_tag = NULL;
static int __init board_keycaps_tag(char *get_keypads)
{
	if(strlen(get_keypads))
		keycap_tag = get_keypads;
	else
		keycap_tag = NULL;
	return 1;
}
__setup("androidboot.keycaps=", board_keycaps_tag);

void board_get_keycaps_tag(char **ret_data)
{
	*ret_data = keycap_tag;
}
EXPORT_SYMBOL(board_get_keycaps_tag);

static char *cid_tag = NULL;
static int __init board_set_cid_tag(char *get_hboot_cid)
{
	if(strlen(get_hboot_cid))
		cid_tag = get_hboot_cid;
	else
		cid_tag = NULL;
	return 1;
}
__setup("androidboot.cid=", board_set_cid_tag);

void board_get_cid_tag(char **ret_data)
{
	*ret_data = cid_tag;
}
EXPORT_SYMBOL(board_get_cid_tag);

static char *carrier_tag = NULL;
static int __init board_set_carrier_tag(char *get_hboot_carrier)
{
	if(strlen(get_hboot_carrier))
		carrier_tag = get_hboot_carrier;
	else
		carrier_tag = NULL;
	return 1;
}
__setup("androidboot.carrier=", board_set_carrier_tag);

void board_get_carrier_tag(char **ret_data)
{
	*ret_data = carrier_tag;
}
EXPORT_SYMBOL(board_get_carrier_tag);

static int skuid = 0;

void __init early_init_dt_setup_skuid(unsigned long value) {
	pr_info("[dt]skuid = 0x%lx\n", value);
	smi_sz = value;
}

unsigned int board_get_skuid(void)
{
	return skuid;
}

int __init parse_tag_skuid(const struct tag *tags)
{
	skuid = tags->u.revision.rev;
	pr_info("[atag]skuid = 0x%x\n", skuid);
	return skuid;
}
__tagtable(ATAG_SKUID, parse_tag_skuid);

/* Proximity sensor calibration values */
unsigned int als_kadc;
EXPORT_SYMBOL(als_kadc);

void __init early_init_dt_setup_als_calibration(unsigned long value) {
	pr_info("[dt]als calibration = 0x%lx\n", value);
	als_kadc = value;
}

static int __init parse_tag_als_calibration(const struct tag *tag)
{
	als_kadc = tag->u.als_kadc.kadc;
	pr_info("[atag]als calibration = 0x%x\n", skuid);
	return 0;
}

__tagtable(ATAG_ALS, parse_tag_als_calibration);

/* CSA sensor calibration values */
#define ATAG_CSA	0x5441001f

unsigned int csa_kvalue1;
EXPORT_SYMBOL(csa_kvalue1);

unsigned int csa_kvalue2;
EXPORT_SYMBOL(csa_kvalue2);

unsigned int csa_kvalue3;
EXPORT_SYMBOL(csa_kvalue3);

static int __init parse_tag_csa_calibration(const struct tag *tag)
{
	unsigned int *ptr = (unsigned int *)&tag->u;
	csa_kvalue1 = ptr[0];
	csa_kvalue2 = ptr[1];
	csa_kvalue3 = ptr[2];

	printk(KERN_DEBUG "csa_kvalue1 = 0x%x, csa_kvalue2 = 0x%x, "
	"csa_kvalue3 = 0x%x\n", csa_kvalue1, csa_kvalue2, csa_kvalue3);

	return 0;
}
__tagtable(ATAG_CSA, parse_tag_csa_calibration);

static unsigned memory_size;

void __init early_init_dt_setup_memsize(unsigned long value) {
	pr_info("[dt]memsize = 0x%lx\n", value);
	memory_size = value;
}

unsigned int board_get_memsize(void)
{
	return memory_size;
}

int __init parse_tag_memsize(const struct tag *tags)
{
	memory_size = tags->u.revision.rev;
	pr_info("[atag]memsize: %d\n", memory_size);
	return memory_size;
}
__tagtable(ATAG_MEMSIZE, parse_tag_memsize);

#define ATAG_DDR_ID 0x54410030
int __init parse_tag_ddr_id(const struct tag *tags)
{
	int ddr_id = -1, find = 0;
	struct tag *t = (struct tag *)tags;

	for (; t->hdr.size; t = tag_next(t)) {
		if (t->hdr.tag == ATAG_DDR_ID) {
			printk(KERN_DEBUG "find the DDR-ID tag\n");
			find = 1;
			break;
		}
	}

	if (find)
		ddr_id = t->u.revision.rev;

	printk(KERN_DEBUG "parse_tag_ddr_id: %d\n", ddr_id);
	return ddr_id;
}
__tagtable(ATAG_DDR_ID, parse_tag_ddr_id);

static unsigned engineerid;
EXPORT_SYMBOL(engineerid);

void __init early_init_dt_setup_engineerid(unsigned long value) {
	pr_info("[dt]engineerid: hwid = 0x%lx\n", value);
	engineerid = value;
}

unsigned int board_get_engineerid(void)
{
	return engineerid;
}

int __init parse_tag_engineerid(const struct tag *tags)
{
	engineerid = tags->u.revision.rev;
	pr_info("[atag]engineerid = 0x%x\n", engineerid);
	return engineerid;
}
__tagtable(ATAG_ENGINEERID, parse_tag_engineerid);


unsigned int gs_kvalue;
EXPORT_SYMBOL(gs_kvalue);

void __init early_init_dt_setup_gs_calibration(unsigned long value){
	pr_info("[dt]gs_calibration = 0x%lx\n", value);
	gs_kvalue = value;
}

static int __init parse_tag_gs_calibration(const struct tag *tag)
{
	gs_kvalue = tag->u.revision.rev;
	pr_info("[atag]gs_kvalue = 0x%x\n", gs_kvalue);
	return 0;
}

__tagtable(ATAG_GS, parse_tag_gs_calibration);

unsigned int ps_kparam1;
EXPORT_SYMBOL(ps_kparam1);

unsigned int ps_kparam2;
EXPORT_SYMBOL(ps_kparam2);

void __init early_init_dt_setup_ps_calibration(unsigned long ps_low, unsigned long ps_high) {
	ps_kparam1 = ps_low;
	ps_kparam2 = ps_high;
	pr_info("[dt]ps_low = 0x%x, ps_high = 0x%x\n",
		ps_kparam1, ps_kparam2);
}

static int __init parse_tag_ps_calibration(const struct tag *tag)
{
	ps_kparam1 = tag->u.serialnr.low;
	ps_kparam2 = tag->u.serialnr.high;

	pr_info("[atag]ps_kparam1 = 0x%x, ps_kparam2 = 0x%x\n",
		ps_kparam1, ps_kparam2);

	return 0;
}

__tagtable(ATAG_PS, parse_tag_ps_calibration);

/* camera values */
#define ATAG_CAM	0x54410021
int __init parse_tag_cam(const struct tag *tags)
{
    int mem_size = 0, find = 0;
    struct tag *t = (struct tag *)tags;

    for (; t->hdr.size; t = tag_next(t)) {
	    if (t->hdr.tag == ATAG_CAM) {
		    printk(KERN_DEBUG "find the memsize tag\n");
		    find = 1;
		    break;
	    }
    }

    if (find) {
	    mem_size = t->u.revision.rev;
    }
    printk(KERN_DEBUG "parse_tag_memsize: %d\n", mem_size);
    return mem_size;
}
__tagtable(ATAG_CAM, parse_tag_cam);

/* Gyro/G-senosr calibration values */
#define ATAG_GRYO_GSENSOR	0x54410020
unsigned char gyro_gsensor_kvalue[37];
EXPORT_SYMBOL(gyro_gsensor_kvalue);

static int __init parse_tag_gyro_gsensor_calibration(const struct tag *tag)
{
	int i;
	unsigned char *ptr = (unsigned char *)&tag->u;
	memcpy(&gyro_gsensor_kvalue[0], ptr, sizeof(gyro_gsensor_kvalue));

	printk(KERN_DEBUG "gyro_gs data\n");
	for (i = 0; i < sizeof(gyro_gsensor_kvalue); i++)
		printk(KERN_DEBUG "[%d]:0x%x", i, gyro_gsensor_kvalue[i]);

	return 0;
}
__tagtable(ATAG_GRYO_GSENSOR, parse_tag_gyro_gsensor_calibration);

BLOCKING_NOTIFIER_HEAD(psensor_notifier_list);
int register_notifier_by_psensor(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&psensor_notifier_list, nb);
}

int unregister_notifier_by_psensor(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&psensor_notifier_list, nb);
}

static int panel_type = 0;

void __init early_init_dt_setup_panel_type(unsigned long value) {
	pr_info("[dt]panel_type = 0x%lx\n", value);
	panel_type = value;
}

unsigned int board_get_panel_type(void)
{
	return panel_type;
}

int __init tag_panel_parsing(const struct tag *tags)
{
	panel_type = tags->u.revision.rev;

	pr_info("[atag]panel type = %d\n", panel_type);

	return panel_type;
}
__tagtable(ATAG_HERO_PANEL_TYPE, tag_panel_parsing);

void __init early_init_dt_setup_gpio_table(char * data, size_t len) {
	unsigned size;

	size = min(len, (__u32)MFG_GPIO_TABLE_MAX_SIZE);
	memcpy(mfg_gpio_table, data, size);

	pr_info("[dt]GPIO table size = %d\n", len / 4);
}

int __init parse_tag_mfg_gpio_table(const struct tag *tags)
{
	unsigned char *dptr = (unsigned char *)(&tags->u);
	__u32 size;

	size = min((__u32)(tags->hdr.size - 2) * sizeof(__u32), (__u32)MFG_GPIO_TABLE_MAX_SIZE);
	memcpy(mfg_gpio_table, dptr, size);
	pr_info("[atag]GPIO table size = %d\n", tags->hdr.size);
	return 0;
}
__tagtable(ATAG_MFG_GPIO_TABLE, parse_tag_mfg_gpio_table);

static char *emmc_tag;
static int __init board_set_emmc_tag(char *get_hboot_emmc)
{
	if (strlen(get_hboot_emmc))
		emmc_tag = get_hboot_emmc;
	else
		emmc_tag = NULL;
	return 1;
}
__setup("androidboot.emmc=", board_set_emmc_tag);

int board_emmc_boot(void)
{
	if (emmc_tag) {
		if (!strcmp(emmc_tag, "true"))
			return 1;
	}

	return 0;
}

char *board_get_mfg_sleep_gpio_table(void)
{
	return mfg_gpio_table;
}
EXPORT_SYMBOL(board_get_mfg_sleep_gpio_table);
static int mfg_mode;
int __init board_mfg_mode_init(char *s)
{
	if (!strcmp(s, "normal"))
		mfg_mode = 0;
	else if (!strcmp(s, "factory2"))
		mfg_mode = 1;
	else if (!strcmp(s, "recovery"))
		mfg_mode = 2;
	else if (!strcmp(s, "charge"))
		mfg_mode = 3;
	else if (!strcmp(s, "power_test"))
		mfg_mode = 4;
	else if (!strcmp(s, "offmode_charging"))
		mfg_mode = 5;
	else if (!strcmp(s, "mfgkernel:diag58"))
		mfg_mode = 6;
	else if (!strcmp(s, "gift_mode"))
		mfg_mode = 7;
	else if (!strcmp(s, "mfgkernel"))
		mfg_mode = 8;
	return 1;
}
__setup("androidboot.mode=", board_mfg_mode_init);


int board_mfg_mode(void)
{
	return mfg_mode;
}

EXPORT_SYMBOL(board_mfg_mode);

static int build_flag;

static int __init board_bootloader_setup(char *str)
{
	char temp[strlen(str) + 1];
	char *p = NULL;
	char *build = NULL;
	char *args = temp;

	printk(KERN_INFO "%s: %s\n", __func__, str);

	strcpy(temp, str);

	/*parse the last parameter*/
	while ((p = strsep(&args, ".")) != NULL) build = p;

	/* Sometime hboot version would change from .X000 to .X001, .X002,...
	 * So compare the first character to avoid unnecessary error.
	 */
	if (build) {
		if (build[0] == '0') {
			printk(KERN_INFO "%s: SHIP BUILD\n", __func__);
			build_flag = SHIP_BUILD;
		} else if (build[0] == '2') {
			printk(KERN_INFO "%s: ENG BUILD\n", __func__);
			build_flag = ENG_BUILD;
		} else if (build[0] == '1') {
			printk(KERN_INFO "%s: MFG BUILD\n", __func__);
			build_flag = MFG_BUILD;
		} else {
			printk(KERN_INFO "%s: default ENG BUILD\n", __func__);
			build_flag = ENG_BUILD;
		}
	}
	return 1;
}
__setup("androidboot.bootloader=", board_bootloader_setup);

int board_build_flag(void)
{
	return build_flag;
}

EXPORT_SYMBOL(board_build_flag);

/* ISL29028 ID values */
#define ATAG_PS_TYPE 0x4d534D77
int ps_type;
EXPORT_SYMBOL(ps_type);
void __init early_init_dt_setup_ps_type(unsigned long value) {
	pr_info("[dt]ps_type = 0x%lx\n", value);
	ps_type = value;
}

int __init tag_ps_parsing(const struct tag *tags)
{
	ps_type = tags->u.revision.rev;

	printk("[atag]ps_type = 0x%x\n", ps_type);

	return ps_type;
}
__tagtable(ATAG_PS_TYPE, tag_ps_parsing);

static unsigned int radio_flag;
int __init radio_flag_init(char *s)
{
	radio_flag = simple_strtoul(s, 0, 16);
	return 1;
}
__setup("radioflag=", radio_flag_init);

unsigned int get_radio_flag(void)
{
	return radio_flag;
}

static unsigned long kernel_flag;
int __init kernel_flag_init(char *s)
{
	int ret;
	ret = strict_strtoul(s, 16, &kernel_flag);
	return 1;
}
__setup("kernelflag=", kernel_flag_init);

unsigned long get_kernel_flag(void)
{
	return kernel_flag;
}

#define MID_LEN	(9)
static char modelid[MID_LEN+1];
int __init model_id_init(char *s)
{
	memset(modelid, 0, sizeof(modelid));
	strncpy(modelid, s, MID_LEN);
	return 1;
}
__setup("androidboot.mid=", model_id_init);

char *get_model_id(void)
{
	return modelid;
}

static char *sku_color_tag = NULL;
static int __init board_set_qwerty_color_tag(char *get_sku_color)
{
	if (strlen(get_sku_color))
		sku_color_tag = get_sku_color;
	else
		sku_color_tag = NULL;
	return 1;
}
__setup("androidboot.qwerty_color=", board_set_qwerty_color_tag);

void board_get_sku_color_tag(char **ret_data)
{
	*ret_data = sku_color_tag;
}
EXPORT_SYMBOL(board_get_sku_color_tag);

static int usb_ats;
int __init board_ats_init(char *s)
{
	usb_ats = simple_strtoul(s, 0, 10);
	return 1;
}
__setup("ats=", board_ats_init);


int board_get_usb_ats(void)
{
	return usb_ats;
}

EXPORT_SYMBOL(board_get_usb_ats);
