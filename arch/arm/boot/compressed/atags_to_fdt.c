#include <asm/setup.h>
#include <libfdt.h>

#if defined(CONFIG_ARM_ATAG_DTB_COMPAT_CMDLINE_EXTEND)
#define do_extend_cmdline 1
#else
#define do_extend_cmdline 0
#endif

#define NR_BANKS 16

static int node_offset(void *fdt, const char *node_path)
{
	int offset = fdt_path_offset(fdt, node_path);
	if (offset == -FDT_ERR_NOTFOUND)
		offset = fdt_add_subnode(fdt, 0, node_path);
	return offset;
}

static int setprop(void *fdt, const char *node_path, const char *property,
		   uint32_t *val_array, int size)
{
	int offset = node_offset(fdt, node_path);
	if (offset < 0)
		return offset;
	return fdt_setprop(fdt, offset, property, val_array, size);
}

static int setprop_string(void *fdt, const char *node_path,
			  const char *property, const char *string)
{
	int offset = node_offset(fdt, node_path);
	if (offset < 0)
		return offset;
	return fdt_setprop_string(fdt, offset, property, string);
}

static int setprop_values(void *fdt, const char *node_path,
			  const char *property, const void *val, int len)
{
	int offset = node_offset(fdt, node_path);
	if (offset < 0)
		return offset;
	return fdt_setprop(fdt, offset, property, val, len);
}

static int setprop_cell(void *fdt, const char *node_path,
			const char *property, uint32_t val)
{
	int offset = node_offset(fdt, node_path);
	if (offset < 0)
		return offset;
	return fdt_setprop_cell(fdt, offset, property, val);
}

static const void *getprop(const void *fdt, const char *node_path,
			   const char *property, int *len)
{
	int offset = fdt_path_offset(fdt, node_path);

	if (offset == -FDT_ERR_NOTFOUND)
		return NULL;

	return fdt_getprop(fdt, offset, property, len);
}

static uint32_t get_cell_size(const void *fdt)
{
	int len;
	uint32_t cell_size = 1;
	const uint32_t *size_len =  getprop(fdt, "/", "#size-cells", &len);

	if (size_len)
		cell_size = fdt32_to_cpu(*size_len);
	return cell_size;
}

static void merge_fdt_bootargs(void *fdt, const char *fdt_cmdline)
{
	char cmdline[COMMAND_LINE_SIZE];
	const char *fdt_bootargs;
	char *ptr = cmdline;
	int len = 0;

	/* copy the fdt command line into the buffer */
	fdt_bootargs = getprop(fdt, "/chosen", "bootargs", &len);
	if (fdt_bootargs)
		if (len < COMMAND_LINE_SIZE) {
			memcpy(ptr, fdt_bootargs, len);
			/* len is the length of the string
			 * including the NULL terminator */
			ptr += len - 1;
		}

	/* and append the ATAG_CMDLINE */
	if (fdt_cmdline) {
		len = strlen(fdt_cmdline);
		if (ptr - cmdline + len + 2 < COMMAND_LINE_SIZE) {
			*ptr++ = ' ';
			memcpy(ptr, fdt_cmdline, len);
			ptr += len;
		}
	}
	*ptr = '\0';

	setprop_string(fdt, "/chosen", "bootargs", cmdline);
}

/*
 * Convert and fold provided ATAGs into the provided FDT.
 *
 * REturn values:
 *    = 0 -> pretend success
 *    = 1 -> bad ATAG (may retry with another possible ATAG pointer)
 *    < 0 -> error from libfdt
 */
int atags_to_fdt(void *atag_list, void *fdt, int total_space)
{
	struct tag *atag = atag_list;
	/* In the case of 64 bits memory size, need to reserve 2 cells for
	 * address and size for each bank */
	uint32_t mem_reg_property[2 * 2 * NR_BANKS];
	int memcount = 0;
	int ret, memsize;

	/* make sure we've got an aligned pointer */
	if ((u32)atag_list & 0x3)
		return 1;

	/* if we get a DTB here we're done already */
	if (*(u32 *)atag_list == fdt32_to_cpu(FDT_MAGIC))
	       return 0;

	/* validate the ATAG */
	if (atag->hdr.tag != ATAG_CORE ||
	    (atag->hdr.size != tag_size(tag_core) &&
	     atag->hdr.size != 2))
		return 1;

	/* let's give it all the room it could need */
	ret = fdt_open_into(fdt, fdt, total_space);
	if (ret < 0)
		return ret;

	for_each_tag(atag, atag_list) {
		if (atag->hdr.tag == ATAG_CMDLINE) {
			/* Append the ATAGS command line to the device tree
			 * command line.
			 * NB: This means that if the same parameter is set in
			 * the device tree and in the tags, the one from the
			 * tags will be chosen.
			 */
			if (do_extend_cmdline)
				merge_fdt_bootargs(fdt,
						   atag->u.cmdline.cmdline);
			else
				setprop_string(fdt, "/chosen", "bootargs",
					       atag->u.cmdline.cmdline);
		} else if (atag->hdr.tag == ATAG_MEM) {
			if (memcount >= sizeof(mem_reg_property)/4)
				continue;
			if (!atag->u.mem.size)
				continue;
			memsize = get_cell_size(fdt);

			if (memsize == 2) {
				/* if memsize is 2, that means that
				 * each data needs 2 cells of 32 bits,
				 * so the data are 64 bits */
				uint64_t *mem_reg_prop64 =
					(uint64_t *)mem_reg_property;
				mem_reg_prop64[memcount++] =
					cpu_to_fdt64(atag->u.mem.start);
				mem_reg_prop64[memcount++] =
					cpu_to_fdt64(atag->u.mem.size);
			} else {
				mem_reg_property[memcount++] =
					cpu_to_fdt32(atag->u.mem.start);
				mem_reg_property[memcount++] =
					cpu_to_fdt32(atag->u.mem.size);
			}

		} else if (atag->hdr.tag == ATAG_INITRD2) {
			uint32_t initrd_start, initrd_size;
			initrd_start = atag->u.initrd.start;
			initrd_size = atag->u.initrd.size;
			setprop_cell(fdt, "/chosen", "linux,initrd-start",
					initrd_start);
			setprop_cell(fdt, "/chosen", "linux,initrd-end",
					initrd_start + initrd_size);
		} else if (atag->hdr.tag == ATAG_BLUETOOTH) {
			setprop_values(fdt, "/chosen", "linux,bt_mac",
				(unsigned char *)(&atag->u), (atag->hdr.size-2)*sizeof(__u32));
		} else if (atag->hdr.tag == ATAG_MSM_WIFI) {
			#define NVS_MAX_SIZE	0x800U
			#define NVS_LEN_OFFSET	0x0C
			#define NVS_DATA_OFFSET	0x40
			char append[] = "\nsd_oobonly=1\nbtc_params80=0\nbtc_params6=30\n";
			__u32 len = 0;
			__u32 full_len = (atag->hdr.size-2)*sizeof(__u32);
			// check that we have enought space for get len
			if (full_len > NVS_LEN_OFFSET)
				memcpy(&len, (unsigned char *)(&atag->u) + NVS_LEN_OFFSET, sizeof(len));
			// len is less than full block size
			if (len > (NVS_MAX_SIZE - NVS_DATA_OFFSET))
				len = (NVS_MAX_SIZE - NVS_DATA_OFFSET);
			// len is less than atag block size
			if (len > full_len)
				len = full_len;
			// we have enought space for add additional params
			if ((len + strlen(append) + 1) <= full_len) {
				// block is finished by zero
				if (((unsigned char *)(&atag->u))[NVS_DATA_OFFSET + len] == 0)
					len --;
				//copy additional params
				memcpy(
					(unsigned char *)(&atag->u) + NVS_DATA_OFFSET + len,
					append,
					strlen(append) + 1
				);
				len += strlen(append);
				len ++;
			}
			// finaly save new wifi calibration
			setprop_values(fdt, "/chosen", "linux,wifi-calibration",
				(unsigned char *)(&atag->u) + NVS_DATA_OFFSET, len);
		} else if (atag->hdr.tag == ATAG_MSM_AWB_CAL) {
			setprop_values(fdt, "/chosen", "linux,awb_cal",
				(unsigned char *)(&atag->u), (atag->hdr.size-2)*sizeof(__u32));
		} else if (atag->hdr.tag == ATAG_MFG_GPIO_TABLE) {
			setprop_values(fdt, "/chosen", "linux,gpio_table",
				(unsigned char *)(&atag->u), (atag->hdr.size-2)*sizeof(__u32));
		} else if (atag->hdr.tag == ATAG_MSM_PARTITION) {
			setprop_values(fdt, "/chosen", "linux,msm_partitions",
				(unsigned char *)(&atag->u), (atag->hdr.size-2)*sizeof(__u32));
		} else if (atag->hdr.tag == ATAG_MEMSIZE) {
			setprop_cell(fdt, "/chosen", "linux,memsize",
					atag->u.revision.rev);
		} else if (atag->hdr.tag == ATAG_ALS) {
			setprop_cell(fdt, "/chosen", "linux,als_calibration",
					atag->u.als_kadc.kadc);
		} else if (atag->hdr.tag == ATAG_ENGINEERID) {
			setprop_cell(fdt, "/chosen", "linux,engineerid",
					atag->u.revision.rev);
		} else if (atag->hdr.tag == ATAG_SMI) {
			setprop_cell(fdt, "/chosen", "linux,smi",
					atag->u.mem.size);
		} else if (atag->hdr.tag == ATAG_HWID) {
			setprop_cell(fdt, "/chosen", "linux,hwid",
					atag->u.revision.rev);
		} else if (atag->hdr.tag == ATAG_SKUID) {
			setprop_cell(fdt, "/chosen", "linux,skuid",
					atag->u.revision.rev);
		} else if (atag->hdr.tag == ATAG_HERO_PANEL_TYPE) {
			setprop_cell(fdt, "/chosen", "linux,panel_type",
					atag->u.revision.rev);
		} else if (atag->hdr.tag == ATAG_GS) {
			setprop_cell(fdt, "/chosen", "linux,gs_calibration",
					atag->u.revision.rev);
		} else if (atag->hdr.tag == ATAG_REVISION) {
			__u32 revision[2];
			revision[0] = cpu_to_fdt32(atag->u.revision.rev);
			revision[1] = cpu_to_fdt32(atag->u.revision.rev);
			if (atag->hdr.size > 3) {
				revision[1] = cpu_to_fdt32(atag->u.revision.rev2);
			}
			setprop_values(fdt, "/chosen", "linux,revision",
					revision, sizeof(revision));
		} else if (atag->hdr.tag == ATAG_PS) {
			__u32 ps_settings[2];
			ps_settings[0] = cpu_to_fdt32(atag->u.serialnr.low);
			ps_settings[1] = cpu_to_fdt32(atag->u.serialnr.high);
			setprop_values(fdt, "/chosen", "linux,ps_calibration",
					ps_settings, sizeof(ps_settings));
		} else if (atag->hdr.tag == ATAG_PS_TYPE) {
			setprop_cell(fdt, "/chosen", "linux,ps_type",
					atag->u.revision.rev);
		}
	}

	if (memcount) {
		setprop(fdt, "/memory", "reg", mem_reg_property,
			4 * memcount * memsize);
	}

	return fdt_pack(fdt);
}
