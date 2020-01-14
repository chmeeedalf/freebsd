/*-
 * SPDX-License-Identifier: BSD-4-Clause AND BSD-2-Clause-FreeBSD
 * Copyright (C) 2019 Justin Hibbits
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *	$NetBSD: machdep.c,v 1.74.2.1 2000/11/01 16:13:48 tv Exp $
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/rman.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <machine/bus.h>

#include <vm/vm.h>
#include <vm/pmap.h>

/* Names chosen to be consistent with Linux and SkiBoot */
#define	VAS_HVWC_SIZE	512
#define	VAS_UWC_SIZE	PAGE_SIZE

/* MMIO registers */
#define	VAS_LPID			0x010
#define	VAS_PID				0x018
#define	VAS_XLATE_MSR			0x020
#define	VAS_XLATE_LPCR			0x028
#define	  XLATE_LPCR_PAGE_SIZE_M	  0xe000000000000000
#define	  XLATE_LPCR_PAGE_SIZE_4k	  0x0000000000000000
#define	  XLATE_LPCR_PAGE_SIZE_64k	  0xa000000000000000
#define	  XLATE_LPCR_ISL		  0x1000000000000000
#define	  XLATE_LPCR_TC			  0x0800000000000000
#define	  XLATE_LPCR_SC			  0x0400000000000000
#define	VAS_XLATE_CTL			0x030
#define	VAS_AMR				0x040
#define	VAS_SEIDR			0x048
#define	VAS_FAULT_TX_WIN		0x050
#define	VAS_OSU_INTR_SRC_RA		0x060
#define	VAS_HV_INTR_SRC_RA		0x070
#define	VAS_PSWID			0x078
#define	VAS_LFIFO_BAR			0x0a0
#define	VAS_LDATA_STAMP_CTL		0x0a8
#define	VAS_LDMA_CACHE_CTL		0x0b0
#define	VAS_LRFIFO_PUSH			0x0b8
#define	VAS_CURR_MSG_COUNT		0x0c0
#define	VAS_LNOTIFY_AFTER_COUNT		0x0c8
#define	VAS_LRX_WCRED			0x0e0
#define	VAS_TX_WCRED			0x0f0
#define	VAS_LFIFO_SIZE			0x100
#define	VAS_WIN_CTL			0x108
#define	VAS_WIN_STATUS			0x110
#define	VAS_WIN_CTX_CACHING_CTL		0x118
#define	VAS_TX_RSVD_BUF_COUNT		0x120
#define	VAS_LRFIFO_WIN_PTR		0x128
#define	VAS_LNOTIFY_CTL			0x138
#define	VAS_LNOTIFY_PID			0x140
#define	VAS_LNOTIFY_LPID		0x148
#define	VAS_LNOTIFY_TID			0x150
#define	VAS_NX_UTIL_ADDER		0x180
#define	VAS_LNOTIFY_SCOPE		0x158
#define	VAS_NX_UTIL			0x1b0
#define	VAS_NX_UTIL_SE			0x1b8
#define	VAS_LRX_WCRED_ADDER		0x190
#define	VAS_TX_WCRED_ADDER		0x1a0

static int vas_probe(device_t);
static int vas_attach(device_t);

#define	sc_hvmem	sc_res[0]
#define	sc_umem		sc_res[1]

struct vas_window {
	int	 win_id;
	void	*hvwc_win;
	void	*uwc_win;
	void	*paste_win;
};

struct vas_win_ctx {
	int	lpid;
	int	pid;
};

struct vas_softc {
	struct resource *sc_res[4];
	uint64_t	 sc_paste_bar;
	uint64_t	 sc_win_id_shift;
	uint64_t	 sc_win_id_width;
	int		 sc_vas_id;
	int		 sc_chip_id;
	struct unrhdr	*sc_win_id_pool;
};

static struct resource_spec vas_resources[] = {
	{ SYS_RES_MEMORY, 0, RF_ACTIVE | RF_SHAREABLE },
	{ SYS_RES_MEMORY, 1, RF_ACTIVE | RF_SHAREABLE },
	{ SYS_RES_MEMORY, 2, 0 },
	{ SYS_RES_MEMORY, 3, 0 },
	RESOURCE_SPEC_END
};

static device_method_t  vas_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,	 vas_probe),
	DEVMETHOD(device_attach, vas_attach),
	{ 0, 0 },
};

static driver_t vas_driver = {
	"vas",
	vas_methods,
	sizeof(struct vas_softc)
};

static devclass_t vas_devclass;

DRIVER_MODULE(vas, ofwbus, vas_driver, vas_devclass, 0, 0);
static MALLOC_DEFINE(M_PPCVAS, "powervas",
    "POWER Virtual Accelerator Switchboard");

static int
vas_probe(device_t dev)
{
	if (!ofw_bus_is_compatible(dev, "ibm,vas"))
		return (ENXIO);

	device_set_desc(dev, "POWER Virtual Accelerator Switchboard");

	return (BUS_PROBE_GENERIC);
}

static int
vas_attach(device_t dev)
{
	struct vas_softc *sc;
	uint64_t win_size;

	sc = device_get_softc(dev);
	if (bus_alloc_resources(dev, vas_resources, sc->sc_res) != 0) {
		device_printf(dev, "Failed allocating resources.\n");
		return (ENXIO);
	}
	sc->sc_paste_bar = rman_get_start(sc->sc_res[2]);
	sc->sc_win_id_shift = 63 - rman_get_end(sc->sc_res[3]);
	sc->sc_win_id_width = rman_get_size(sc->sc_res[3]);
	win_size = (sc->sc_win_id_shift + sc->sc_win_id_width);
	if ((1ULL << win_size) > rman_get_size(sc->sc_res[2])) {
		device_printf(dev, "Inconsistent paste window and shift.\n");
		bus_release_resources(dev, vas_resources, sc->sc_res);
		return (ENXIO);
	}
	/* Setup vmem region for window IDs */
	sc->sc_win_id_pool = new_unrhdr(0, (1 << sc->sc_win_id_width) - 1, NULL);

	return (0);
}

static inline void
vas_win_hvwc_write(struct vas_softc *sc, int win, int reg, uint64_t data)
{
	bus_write_8(sc->sc_hvmem, win * VAS_HVWC_SIZE + reg, data);
}

static inline uint64_t
vas_win_hvwc_read(struct vas_softc *sc, int win, int reg)
{

	return (bus_read_8(sc->sc_hvmem, win * VAS_HVWC_SIZE + reg));
}

static int
vas_alloc_win(device_t dev, struct vas_window *win, struct vas_win_ctx *ctx)
{
	struct vas_softc *sc;
	vm_paddr_t paste_win_pa;
	int win_id;

	sc = device_get_softc(dev);
	win_id = alloc_unr(sc->sc_win_id_pool);

	if (win_id == -1)
		return (ENOMEM);

	win->win_id = win_id;

	win->hvwc_win = sc->sc_hvmem + win_id * VAS_HVWC_SIZE;
	win->uwc_win = sc->sc_umem + win_id * VAS_UWC_SIZE;

	paste_win_pa = sc->sc_paste_bar + (win_id << sc->sc_win_id_shift);

	/* Paste windows must be allocated cacheable. */
	win->paste_win = pmap_mapdev_attr(paste_win_pa, PAGE_SIZE,
	    VM_MEMATTR_CACHEABLE);

	/* Initialize the window's MMIO registers. */
	vas_win_hvwc_write(sc, win_id, VAS_LPID, ctx->lpid);
	vas_win_hvwc_write(sc, win_id, VAS_PID, ctx->pid);
	vas_win_hvwc_write(sc, win_id, VAS_XLATE_MSR, 0);
	vas_win_hvwc_write(sc, win_id, VAS_XLATE_LPCR, 0);
	vas_win_hvwc_write(sc, win_id, VAS_XLATE_CTL, 0);
	vas_win_hvwc_write(sc, win_id, VAS_AMR, 0);
	vas_win_hvwc_write(sc, win_id, VAS_SEIDR, 0);
	vas_win_hvwc_write(sc, win_id, VAS_FAULT_TX_WIN, 0);
	vas_win_hvwc_write(sc, win_id, VAS_OSU_INTR_SRC_RA, 0);
	vas_win_hvwc_write(sc, win_id, VAS_HV_INTR_SRC_RA, 0);
	vas_win_hvwc_write(sc, win_id, VAS_PSWID, 0);
	vas_win_hvwc_write(sc, win_id, VAS_LFIFO_BAR, 0);
	vas_win_hvwc_write(sc, win_id, VAS_LDATA_STAMP_CTL, 0);
	vas_win_hvwc_write(sc, win_id, VAS_LDMA_CACHE_CTL, 0);
	vas_win_hvwc_write(sc, win_id, VAS_LRFIFO_PUSH, 0);
	vas_win_hvwc_write(sc, win_id, VAS_CURR_MSG_COUNT, 0);
	vas_win_hvwc_write(sc, win_id, VAS_LNOTIFY_AFTER_COUNT, 0);
	vas_win_hvwc_write(sc, win_id, VAS_LRX_WCRED, 0);
	vas_win_hvwc_write(sc, win_id, VAS_TX_WCRED, 0);
	vas_win_hvwc_write(sc, win_id, VAS_LFIFO_SIZE, 0);
	vas_win_hvwc_write(sc, win_id, VAS_WIN_CTL, 0);
	vas_win_hvwc_write(sc, win_id, VAS_WIN_STATUS, 0);
	vas_win_hvwc_write(sc, win_id, VAS_WIN_CTX_CACHING_CTL, 0);
	vas_win_hvwc_write(sc, win_id, VAS_TX_RSVD_BUF_COUNT, 0);
	vas_win_hvwc_write(sc, win_id, VAS_LRFIFO_WIN_PTR, 0);
	vas_win_hvwc_write(sc, win_id, VAS_LNOTIFY_CTL, 0);
	vas_win_hvwc_write(sc, win_id, VAS_LNOTIFY_PID, 0);
	vas_win_hvwc_write(sc, win_id, VAS_LNOTIFY_LPID, 0);
	vas_win_hvwc_write(sc, win_id, VAS_LNOTIFY_TID, 0);
	vas_win_hvwc_write(sc, win_id, VAS_NX_UTIL_ADDER, 0);
	vas_win_hvwc_write(sc, win_id, VAS_LNOTIFY_SCOPE, 0);
	vas_win_hvwc_write(sc, win_id, VAS_NX_UTIL, 0);
	vas_win_hvwc_write(sc, win_id, VAS_NX_UTIL_SE, 0);
	vas_win_hvwc_write(sc, win_id, VAS_LRX_WCRED_ADDER, 0);
	vas_win_hvwc_write(sc, win_id, VAS_TX_WCRED_ADDER, 0);

	return (0);
}

static int
vas_release_win(device_t dev, struct vas_window *win)
{
	struct vas_softc *sc;

	sc = device_get_softc(dev);

	free_unr(sc->sc_win_id_pool, win->win_id);
	pmap_unmapdev((vm_offset_t)win->paste_win, PAGE_SIZE);

	return (0);
}
