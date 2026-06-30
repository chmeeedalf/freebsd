/*-
 * Copyright (c) 2016 Justin Hibbits <jhibbits@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>

#include <dev/fdt/simplebus.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <machine/bus.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <machine/pmap.h>

#include "fsl_dma.h"

/*
 * The Freescale DMA driver is composed of two parts: the controller and the
 * channels.  There are 4 channels to a controller, each represented by a device
 * tree node, and therefore each represented by a newbus device.
 *
 * The DMA channel driver is also a base class, so that other compatible strings
 * can be used to make private use of the channel.  The SSI makes use of this in
 * the device tree, so that no other driver can use the DMA controllers in use
 * by the SSI controller.
 */
static int	fsl_dma__probe(device_t);
static int	fsl_dma__attach(device_t);

#define	DMA_MR			0x00
#define	  MR_BWC_M		  0x0f000000
#define	  MR_BWC_S		  24
#define	  MR_DAHTS_M		  0x00030000
#define	  MR_DAHTS_S		  16
#define	  MR_SAHTS_M		  0x0000c000
#define	  MR_SAHTS_S		  14
#define	  MR_DAHE		  0x00002000
#define	  MR_SAHE		  0x00001000
#define	  MR_CA			  0x00000008	/* Channel Abort */
#define	  MR_CC			  0x00000002	/* Channel Continue */
#define	  MR_CS			  0x00000001	/* Channel Start */
#define	DMA_SR			0x04
#define	  SR_TE			  0x00000080	/* Transfer Error */
#define	  SR_CH		  	  0x00000020	/* Channel Halted */
#define	  SR_PE			  0x00000010	/* Programming Error */
#define	  SR_EOLNI		  0x00000008	/* End-of-Links */
#define	  SR_CB			  0x00000004	/* Channel Busy */
#define	  SR_EOSI		  0x00000002	/* End-of-Segment */
#define	  SR_EOLSI		  0x00000001	/* End-of-List */
#define	DMA_ECLNDAR		0x08
#define	DMA_CLNDAR		0x0c
#define	DMA_SATR		0x10
#define	DMA_SAR			0x14
#define	DMA_DATR		0x18
#define	DMA_DAR			0x1c
#define	DMA_BCR			0x20
#define	DMA_ENLNDAR		0x24
#define	DMA_NLDNDAR		0x28
#define	DMA_ECLSDAR		0x30
#define	DMA_CLSDAR		0x34
#define	DMA_ENLSDAR		0x38
#define	DMA_NLSDAR		0x3c
#define	DMA_SSR			0x40
#define	DMA_DSR			0x44

struct fsl_dma__softc {
	struct simplebus_softc	sc_base;
};

static int
fsl_dma__probe(device_t dev)
{
	if (!ofw_bus_is_compatible(dev, "fsl,eloplus-dma"))
		return (ENXIO);

	device_set_desc(dev, "Freescale DMA");

	return (BUS_PROBE_DEFAULT);
}

static int
fsl_dma__attach(device_t dev)
{
	return (simplebus_attach(dev));
}

static device_method_t fsl_dma__methods[] = {
	/* Device methods */
	DEVMETHOD(device_probe,		fsl_dma__probe),
	DEVMETHOD(device_attach,	fsl_dma__attach),

	DEVMETHOD_END
};

DEFINE_CLASS_1(dma, fsl_dma__driver, fsl_dma__methods, sizeof(struct fsl_dma__softc),
    simplebus_driver);
DRIVER_MODULE(dma, simplebus, fsl_dma__driver, 0, 0);
MODULE_VERSION(dma, 1);

struct fsl_dma_ch_softc {
	struct resource *sc_mem;
	struct resource	*sc_irq;
	void		*sc_cookie;

	uint32_t	(*ih)(void *);
	void		*ih_user;
};

static int
fsl_dma_ch_probe(device_t dev)
{
	if (!ofw_bus_is_compatible(dev, "fsl,eloplus-dma-channel") &&
	    !ofw_bus_is_compatible(dev, "fsl,ssi-dma-channel"))
		return (ENXIO);

	device_set_desc(dev, "Freescale DMA channel");

	return (BUS_PROBE_DEFAULT);
}

static void
fsl_dma_ch_intr(void *arg)
{
	struct fsl_dma_ch_softc *sc = arg;
	uint32_t reg;

	reg = bus_read_4(sc->sc_mem, DMA_SR);
	/* Clear all interrupts */
	bus_write_4(sc->sc_mem, DMA_SR, reg);

	if (sc->ih != NULL)
		sc->ih(sc->ih_user);
}

static int
fsl_dma_ch_attach(device_t dev)
{
	struct fsl_dma_ch_softc *sc = device_get_softc(dev);

	sc->sc_mem = bus_alloc_resource_any(dev, SYS_RES_MEMORY, 0,
	    RF_ACTIVE | RF_SHAREABLE);
	if (sc->sc_mem == NULL)
		return (ENXIO);

	sc->sc_irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, 0, RF_ACTIVE);
	if (sc->sc_irq == NULL)
		goto err;

	if (bus_setup_intr(dev, sc->sc_irq, INTR_TYPE_MISC, NULL,
	    fsl_dma_ch_intr, sc, &sc->sc_cookie)) {
		device_printf(dev, "Can't setup interrupt\n");
		goto err;
	}

	return (0);

err:
	if (sc->sc_mem != NULL)
		bus_release_resource(dev, sc->sc_mem);
	if (sc->sc_irq != NULL)
		bus_release_resource(dev, sc->sc_irq);

	return (ENXIO);
}

int
fsl_dma_ch_configure(device_t dev, struct fsl_dma_ch_conf *conf)
{
	struct fsl_dma_ch_softc	*sc;
	uint32_t		 mr;
	int			 period;

	/* Basic sanity checks */
	if (conf->dest_hold_count > conf->period ||
	    conf->src_hold_count > conf->period)
	    	return (EINVAL);

	/* Configure MR */
	sc = device_get_softc(dev);
	mr = bus_read_4(sc->sc_mem, DMA_MR);
	mr &= ~(MR_DAHTS_M | MR_SAHTS_M | MR_DAHE | MR_SAHE);

	if (conf->dest_hold_count != 0) {
		if (conf->dest_hold_count > 8)
			return (EINVAL);
		mr |= (ffs(conf->dest_hold_count) - 1) << MR_DAHTS_S;
		mr |= MR_DAHE;
	}
	if (conf->src_hold_count != 0) {
		if (conf->src_hold_count > 8)
			return (EINVAL);
		mr |= (ffs(conf->src_hold_count) - 1) << MR_SAHTS_S;
		mr |= MR_SAHE;
	}
	if (conf->period != 0) {
		period = ffsl(conf->period) - 1;
		if (period <= 10)
			mr |= period << MR_BWC_S;
		else
			mr |= MR_BWC_M;
	}
	bus_write_4(sc->sc_mem, DMA_MR, mr);

	return (0);
}

int
fsl_dma_ch_stop(device_t dev)
{
	struct fsl_dma_ch_softc *sc = device_get_softc(dev);
	uint32_t reg;

	reg = bus_read_4(sc->sc_mem, DMA_MR);
	reg |= MR_CA;
	bus_write_4(sc->sc_mem, DMA_MR, reg);

	return (0);
}

/* Start DMA basic-chainning, single-write mode */
int
fsl_dma_ch_start(device_t dev, struct dma_link_descriptor *desc)
{
	struct fsl_dma_ch_softc *sc = device_get_softc(dev);
	vm_paddr_t desc_pa;
	uint32_t reg;

	/* If this is a single descriptor, set registers directly */
	if (desc->next == 0) {
		bus_write_4(sc->sc_mem, DMA_SATR, desc->src_attr);
		bus_write_4(sc->sc_mem, DMA_SAR, desc->src_addr);
		bus_write_4(sc->sc_mem, DMA_DATR, desc->dst_attr);
		bus_write_4(sc->sc_mem, DMA_DAR, desc->dst_addr);
		bus_write_4(sc->sc_mem, DMA_BCR, desc->byte_cnt);
	} else {
		desc_pa = pmap_kextract((uintptr_t)desc);
		bus_write_4(sc->sc_mem, DMA_ECLNDAR, desc_pa >> 32);
		bus_write_4(sc->sc_mem, DMA_CLNDAR, desc_pa & 0xffffffff);
	}

	reg = bus_read_4(sc->sc_mem, DMA_MR);
	reg |= MR_CS;
	bus_write_4(sc->sc_mem, DMA_MR, reg);

	return (0);
}

static device_method_t fsl_dma_ch_methods[] = {
	DEVMETHOD(device_probe,		fsl_dma_ch_probe),
	DEVMETHOD(device_attach,	fsl_dma_ch_attach),

	DEVMETHOD_END
};

DEFINE_CLASS_0(fsl_dma_ch, fsl_dma_ch_driver, fsl_dma_ch_methods, sizeof(struct fsl_dma_ch_softc));
DRIVER_MODULE(fsl_dma_ch, dma, fsl_dma_ch_driver, 0, 0);
MODULE_VERSION(fsl_dma_ch, 1);
