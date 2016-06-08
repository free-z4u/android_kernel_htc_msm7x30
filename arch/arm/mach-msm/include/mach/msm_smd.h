/* linux/include/asm-arm/arch-msm/msm_smd.h
 *
 * Copyright (C) 2007 Google, Inc.
 * Copyright (c) 2009-2011, Code Aurora Forum. All rights reserved.
 * Author: Brian Swetland <swetland@google.com>
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

#ifndef __ASM_ARCH_MSM_SMD_H
#define __ASM_ARCH_MSM_SMD_H

typedef struct smd_channel smd_channel_t;

extern int (*msm_check_for_modem_crash)(void);

/* warning: notify() may be called before open returns */
int smd_open(const char *name, smd_channel_t **ch, void *priv,
	     void (*notify)(void *priv, unsigned event));

#define SMD_EVENT_DATA 1
#define SMD_EVENT_OPEN 2
#define SMD_EVENT_CLOSE 3
#define SMD_EVENT_STATUS 4
#define SMD_EVENT_REOPEN_READY 5
#define SMD_MAX_CH_NAME_LEN 20 /* includes null char at end */

enum {
	SMD_APPS_MODEM = 0,
	SMD_APPS_QDSP,
	SMD_MODEM_QDSP,
	SMD_APPS_DSPS,
	SMD_MODEM_DSPS,
	SMD_QDSP_DSPS,
	SMD_APPS_WCNSS,
	SMD_MODEM_WCNSS,
	SMD_QDSP_WCNSS,
	SMD_DSPS_WCNSS,
	SMD_APPS_Q6FW,
	SMD_MODEM_Q6FW,
	SMD_QDSP_Q6FW,
	SMD_DSPS_Q6FW,
	SMD_WCNSS_Q6FW,
	SMD_NUM_TYPE,
	SMD_LOOPBACK_TYPE = 100,

};

/* warning: notify() may be called before open returns */
int smd_open(const char *name, smd_channel_t **ch, void *priv,
	     void (*notify)(void *priv, unsigned event));

int smd_close(smd_channel_t *ch);

/* passing a null pointer for data reads and discards */
int smd_read(smd_channel_t *ch, void *data, int len);
int smd_read_from_cb(smd_channel_t *ch, void *data, int len);
/* Same as smd_read() but takes a data buffer from userspace
 * The function might sleep.  Only safe to call from user context
 */
int smd_read_user_buffer(smd_channel_t *ch, void *data, int len);

/* Write to stream channels may do a partial write and return
** the length actually written.
** Write to packet channels will never do a partial write --
** it will return the requested length written or an error.
*/
int smd_write(smd_channel_t *ch, const void *data, int len);
int smd_write_atomic(smd_channel_t *ch, const void *data, int len);
/* Same as smd_write() but takes a data buffer from userspace
 * The function might sleep.  Only safe to call from user context
 */
int smd_write_user_buffer(smd_channel_t *ch, const void *data, int len);

int smd_write_avail(smd_channel_t *ch);
int smd_read_avail(smd_channel_t *ch);

/* Returns the total size of the current packet being read.
** Returns 0 if no packets available or a stream channel.
*/
int smd_cur_packet_size(smd_channel_t *ch);

/* used for tty unthrottling and the like -- causes the notify()
** callback to be called from the same lock context as is used
** when it is called from channel updates
*/
void smd_kick(smd_channel_t *ch);


#if 0
/* these are interruptable waits which will block you until the specified
** number of bytes are readable or writable.
*/
int smd_wait_until_readable(smd_channel_t *ch, int bytes);
int smd_wait_until_writable(smd_channel_t *ch, int bytes);
#endif

typedef enum {
	SMD_PORT_DS = 0,
	SMD_PORT_DIAG,
	SMD_PORT_RPC_CALL,
	SMD_PORT_RPC_REPLY,
	SMD_PORT_BT,
	SMD_PORT_CONTROL,
	SMD_PORT_MEMCPY_SPARE1,
	SMD_PORT_DATA1,
	SMD_PORT_DATA2,
	SMD_PORT_DATA3,
	SMD_PORT_DATA4,
	SMD_PORT_DATA5,
	SMD_PORT_DATA6,
	SMD_PORT_DATA7,
	SMD_PORT_DATA8,
	SMD_PORT_DATA9,
	SMD_PORT_DATA10,
	SMD_PORT_DATA11,
	SMD_PORT_DATA12,
	SMD_PORT_DATA13,
	SMD_PORT_DATA14,
	SMD_PORT_DATA15,
	SMD_PORT_DATA16,
	SMD_PORT_DATA17,
	SMD_PORT_DATA18,
	SMD_PORT_DATA19,
	SMD_PORT_DATA20,
	SMD_PORT_GPS_NMEA,
	SMD_PORT_BRIDGE_1,
	SMD_PORT_BRIDGE_2,
	SMD_PORT_BRIDGE_3,
	SMD_PORT_BRIDGE_4,
	SMD_PORT_BRIDGE_5,
	SMD_PORT_LOOPBACK,
	SMD_PORT_CS_APPS_MODEM,
	SMD_PORT_CS_APPS_DSP,
	SMD_PORT_CS_MODEM_DSP,
	SMD_NUM_PORTS,
} smd_port_id_type;

/* these are used to get and set the IF sigs of a channel.
 * DTR and RTS can be set; DSR, CTS, CD and RI can be read.
 */
int smd_tiocmget(smd_channel_t *ch);
int smd_tiocmset(smd_channel_t *ch, unsigned int set, unsigned int clear);
int smd_tiocmset_from_cb(smd_channel_t *ch, unsigned int set, unsigned int clear);
int smd_named_open_on_edge(const char *name, uint32_t edge, smd_channel_t **_ch,
			   void *priv, void (*notify)(void *, unsigned));

#endif
