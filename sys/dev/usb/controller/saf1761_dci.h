/* $FreeBSD$ */
/*-
 * Copyright (c) 2014 Hans Petter Selasky <hselasky@FreeBSD.org>
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract (FA8750-10-C-0237)
 * ("CTSRD"), as part of the DARPA CRASH research programme.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _SAF1761_DCI_H_
#define	_SAF1761_DCI_H_

#define	SOTG_MAX_DEVICES (USB_MIN_DEVICES + 1)
#define	SOTG_FS_MAX_PACKET_SIZE 64
#define	SOTG_HS_MAX_PACKET_SIZE 512

#define	SAF1761_READ_1(sc, reg)	\
  bus_space_read_1((sc)->sc_io_tag, (sc)->sc_io_hdl, reg)
#define	SAF1761_READ_2(sc, reg)	\
  bus_space_read_2((sc)->sc_io_tag, (sc)->sc_io_hdl, reg)
#define	SAF1761_READ_4(sc, reg)	\
  bus_space_read_4((sc)->sc_io_tag, (sc)->sc_io_hdl, reg)

#define	SAF1761_WRITE_1(sc, reg, data)	\
  bus_space_write_1((sc)->sc_io_tag, (sc)->sc_io_hdl, reg, data)
#define	SAF1761_WRITE_2(sc, reg, data)	\
  bus_space_write_2((sc)->sc_io_tag, (sc)->sc_io_hdl, reg, data)
#define	SAF1761_WRITE_4(sc, reg, data)	\
  bus_space_write_4((sc)->sc_io_tag, (sc)->sc_io_hdl, reg, data)

struct saf1761_dci_softc;
struct saf1761_dci_td;

typedef uint8_t (saf1761_dci_cmd_t)(struct saf1761_dci_softc *, struct saf1761_dci_td *td);

struct saf1761_dci_td {
	struct saf1761_dci_td *obj_next;
	saf1761_dci_cmd_t *func;
	struct usb_page_cache *pc;
	uint32_t offset;
	uint32_t remainder;
	uint16_t max_packet_size;
	uint8_t	ep_index;
	uint8_t	error:1;
	uint8_t	alt_next:1;
	uint8_t	short_pkt:1;
	uint8_t	did_stall:1;
};

struct saf1761_dci_std_temp {
	saf1761_dci_cmd_t *func;
	struct usb_page_cache *pc;
	struct saf1761_dci_td *td;
	struct saf1761_dci_td *td_next;
	uint32_t len;
	uint32_t offset;
	uint16_t max_frame_size;
	uint8_t	short_pkt;
	/*
         * short_pkt = 0: transfer should be short terminated
         * short_pkt = 1: transfer should not be short terminated
         */
	uint8_t	setup_alt_next;
	uint8_t	did_stall;
};

struct saf1761_dci_config_desc {
	struct usb_config_descriptor confd;
	struct usb_interface_descriptor ifcd;
	struct usb_endpoint_descriptor endpd;
} __packed;

union saf1761_dci_hub_temp {
	uWord	wValue;
	struct usb_port_status ps;
};

struct saf1761_dci_flags {
	uint8_t	change_connect:1;
	uint8_t	change_suspend:1;
	uint8_t	status_suspend:1;	/* set if suspended */
	uint8_t	status_vbus:1;		/* set if present */
	uint8_t	status_bus_reset:1;	/* set if reset complete */
	uint8_t	clocks_off:1;
	uint8_t	port_powered:1;
	uint8_t	port_enabled:1;
	uint8_t	d_pulled_up:1;
	uint8_t	mcsr_feat:1;
};

struct saf1761_dci_softc {
	struct usb_bus sc_bus;
	union saf1761_dci_hub_temp sc_hub_temp;

	struct usb_device *sc_devices[SOTG_MAX_DEVICES];
	struct resource *sc_io_res;
	struct resource *sc_irq_res;
	void   *sc_intr_hdl;
	bus_size_t sc_io_size;
	bus_space_tag_t sc_io_tag;
	bus_space_handle_t sc_io_hdl;

	uint32_t sc_intr_enable;	/* enabled interrupts */

	uint8_t	sc_rt_addr;		/* root HUB address */
	uint8_t	sc_dv_addr;		/* device address */
	uint8_t	sc_conf;		/* root HUB config */

	uint8_t	sc_hub_idata[1];

	struct saf1761_dci_flags sc_flags;
};

/* prototypes */

usb_error_t saf1761_dci_init(struct saf1761_dci_softc *sc);
void	saf1761_dci_uninit(struct saf1761_dci_softc *sc);
void	saf1761_dci_interrupt(struct saf1761_dci_softc *sc);

#endif					/* _SAF1761_DCI_H_ */
