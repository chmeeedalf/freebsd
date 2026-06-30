/*-
 * Copyright (c) 2015 Ruslan Bukin <br@bsdpad.com>
 * All rights reserved.
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/queue.h>
#include <sys/rman.h>
#include <sys/timeet.h>
#include <sys/timetc.h>

#include <dev/sound/pcm/sound.h>
#include <channel_if.h>
#include <mixer_if.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <machine/bus.h>
#include <machine/cpu.h>
#include <machine/intr_machdep.h>

#include <powerpc/mpc85xx/fsl_dma.h>

#define	SSI_NCHANNELS	2
#define	SSI_CHANNEL_PLAY	0
#define	SSI_CHANNEL_REC	1

#define	SSI_STX0	0x00 /* Transmit Data Register n */
#define	SSI_STX1	0x04 /* Transmit Data Register n */
#define	SSI_SRX0	0x08 /* Receive Data Register n */
#define	SSI_SRX1	0x0C /* Receive Data Register n */
#define	SSI_SCR		0x10 /* Control Register */
#define	  SCR_I2S_MODE_S	  5    /* I2S Mode Select. */
#define	  SCR_I2S_MODE_M	  0x3
#define	  I2S_MASTER		  0x1	/* I2S Master mode */
#define	  I2S_SLAVE		  0x2	/* I2S Slave mode */
#define	 SCR_SYN	(1 << 4)
#define	 SCR_NET       	(1 << 3)  /* Network mode */
#define	 SCR_RE		(1 << 2)  /* Receive Enable. */
#define	 SCR_TE		(1 << 1)  /* Transmit Enable. */
#define	 SCR_SSIEN	(1 << 0)  /* SSI Enable */
#define	SSI_SISR	0x14      /* Interrupt Status Register */
#define	SSI_SIER	0x18      /* Interrupt Enable Register */
#define	 SIER_RDMAE	(1 << 22) /* Receive DMA Enable. */
#define	 SIER_RIE	(1 << 21) /* Receive Interrupt Enable. */
#define	 SIER_TDMAE	(1 << 20) /* Transmit DMA Enable. */
#define	 SIER_TIE	(1 << 19) /* Transmit Interrupt Enable. */
#define	 SIER_TDE0IE	(1 << 12) /* Transmit Data Register Empty 0. */
#define	 SIER_TUE0IE	(1 << 8)  /* Transmitter Underrun Error 0. */
#define	 SIER_TFE0IE	(1 << 0)  /* Transmit FIFO Empty 0 IE. */
#define	SSI_STCR	0x1C	  /* Transmit Configuration Register */
#define	 STCR_TXBIT0	(1 << 9)  /* Transmit Bit 0 shift MSB/LSB */
#define	 STCR_TFEN1	(1 << 8)  /* Transmit FIFO Enable 1. */
#define	 STCR_TFEN0	(1 << 7)  /* Transmit FIFO Enable 0. */
#define	 STCR_TFDIR	(1 << 6)  /* Transmit Frame Direction. */
#define	 STCR_TXDIR	(1 << 5)  /* Transmit Clock Direction. */
#define	 STCR_TSHFD	(1 << 4)  /* Transmit Shift Direction. */
#define	 STCR_TSCKP	(1 << 3)  /* Transmit Clock Polarity. */
#define	 STCR_TFSI	(1 << 2)  /* Transmit Frame Sync Invert. */
#define	 STCR_TFSL	(1 << 1)  /* Transmit Frame Sync Length. */
#define	 STCR_TEFS	(1 << 0)  /* Transmit Early Frame Sync. */
#define	SSI_SRCR	0x20      /* Receive Configuration Register */
#define	 SRCR_RXBIT0	(1 << 9)  /* Receive Bit 0 shift MSB/LSB */
#define	 SRCR_RFEN1	(1 << 8)  /* Receive FIFO Enable 1. */
#define	 SRCR_TFEN0	(1 << 7)  /* Receive FIFO Enable 0. */
#define	 SRCR_RFDIR	(1 << 6)  /* Receive Frame Direction. */
#define	 SRCR_RXDIR	(1 << 5)  /* Receive Clock Direction. */
#define	 SRCR_RSHFD	(1 << 4)  /* Receive Shift Direction. */
#define	 SRCR_RSCKP	(1 << 3)  /* Receive Clock Polarity. */
#define	 SRCR_RFSI	(1 << 2)  /* Receive Frame Sync Invert. */
#define	 SRCR_RFSL	(1 << 1)  /* Receive Frame Sync Length. */
#define	 SRCR_REFS	(1 << 0)  /* Receive Early Frame Sync. */
#define	SSI_STCCR	0x24      /* Transmit Clock Control Register */
#define	 STCCR_DIV2	(1 << 18) /* Divide By 2. */
#define	 STCCR_PSR	(1 << 17) /* Divide clock by 8. */
#define	 WL3_WL0_S	13
#define	 WL3_WL0_M	0xf
#define	 DC4_DC0_S	8
#define	 DC4_DC0_M	0x1f
#define	 PM7_PM0_S	0
#define	 PM7_PM0_M	0xff
#define	SSI_SRCCR	0x28	/* Receive Clock Control Register */
#define	SSI_SFCSR	0x2C	/* FIFO Control/Status Register */
#define	 SFCSR_RFWM1_S	20	/* Receive FIFO Empty WaterMark 1 */
#define	 SFCSR_RFWM1_M	0xf
#define	 SFCSR_TFWM1_S	16	/* Transmit FIFO Empty WaterMark 1 */
#define	 SFCSR_TFWM1_M	0xf
#define	 SFCSR_RFWM0_S	4	/* Receive FIFO Empty WaterMark 0 */
#define	 SFCSR_RFWM0_M	0xf
#define	 SFCSR_TFWM0_S	0	/* Transmit FIFO Empty WaterMark 0 */
#define	 SFCSR_TFWM0_M	0xf
#define	SSI_SACNT	0x38	/* AC97 Control Register */
#define	SSI_SACADD	0x3C	/* AC97 Command Address Register */
#define	SSI_SACDAT	0x40	/* AC97 Command Data Register */
#define	SSI_SATAG	0x44	/* AC97 Tag Register */
#define	SSI_STMSK	0x48	/* Transmit Time Slot Mask Register */
#define	SSI_SRMSK	0x4C	/* Receive Time Slot Mask Register */
#define	SSI_SACCST	0x50	/* AC97 Channel Status Register */
#define	SSI_SACCEN	0x54	/* AC97 Channel Enable Register */
#define	SSI_SACCDIS	0x58	/* AC97 Channel Disable Register */

/* Default 64K buffer */
#define	SSI_DEFAULT_SIZE	65536

static MALLOC_DEFINE(M_SSI, "ssi", "ssi audio");

uint32_t ssi_dma_intr(void *arg);

/*
 * Aligned DMA descriptor with VM address.  Aligned to 64 bytes to ensure
 * malloc_aligned() keeps it all on one page.
 */
struct ssi_dma_desc {
	struct dma_link_descriptor desc;
	SLIST_ENTRY(ssi_dma_desc) link;
};

struct ssi_softc {
	struct resource		*sc_mem;
	struct resource		*sc_irq;
	device_t		sc_dev;
	struct mtx		sc_lock;
	void			*ih;
	int			sc_fifo_depth;
	int			dma_size;
	bus_dma_tag_t		dma_tag;
	bus_dmamap_t		dma_map;
	bus_addr_t		buf_base_phys;
	device_t		sc_playback_dma;
	device_t		sc_capture_dma;
	device_t		sc_codec;
	phandle_t		sc_codec_ph;
	bool			sc_sync_mode;
	int			sc_i2s_mode;
};

/* Channel registers */
struct sc_chinfo {
	struct snd_dbuf		*buffer;
	struct pcm_channel	*channel;
	struct sc_pcminfo	*parent;
	bus_dma_tag_t		dma_tag;
	device_t		dma_dev;
	void			*codec;
	size_t			buf_size;
	int			fifo_watermark;
	struct fsl_dma_ch_conf	dma_conf;
	uint32_t		pos;
	SLIST_HEAD(, ssi_dma_desc) desc_list;

	/* Channel information */
	uint32_t		dir;
	uint32_t		format;

	/* Flags */
	uint32_t		run;
};

/* PCM device private data */
struct sc_pcminfo {
	device_t		dev;
	uint32_t		(*ih)(struct sc_pcminfo *scp);
	uint32_t		chnum;
	struct sc_chinfo	chan[SSI_NCHANNELS];
	struct ssi_softc	*sc;
};

static int setup_dma(struct sc_chinfo *scp);
static void setup_ssi(struct ssi_softc *);


/*
 * Channel interface.
 */

static void *
ssichan_init(kobj_t obj, void *devinfo, struct snd_dbuf *b,
    struct pcm_channel *c, int dir)
{
	struct sc_pcminfo *scp;
	struct sc_chinfo *ch;
	struct ssi_softc *sc;
	device_t dma_dev;

	scp = (struct sc_pcminfo *)devinfo;
	sc = scp->sc;

	mtx_lock(&sc->sc_lock);
	if (dir == PCMDIR_PLAY) {
		ch = &scp->chan[SSI_CHANNEL_PLAY];
		dma_dev = sc->sc_playback_dma;
	
	} else {
		ch = &scp->chan[SSI_CHANNEL_REC];
		dma_dev = sc->sc_capture_dma;
	}

	ch->dir = dir;
	ch->run = 0;
	ch->buffer = b;
	ch->channel = c;
	ch->parent = scp;
	ch->dma_dev = dma_dev;
#ifdef notyet
	ch->codec = CHANNEL_INIT(sc->sc_codec, sc->sc_codec, b, c, dir);
#endif
	ch->buf_size = pcm_getbuffersize(sc->sc_dev,
	    FSL_DMA_MINSIZE, SSI_DEFAULT_SIZE, FSL_DMA_MAXSIZE);
	mtx_unlock(&sc->sc_lock);
	/* For DMA purposes, set watermark to ~1/2 total. */
	ch->fifo_watermark = (sc->sc_fifo_depth + 1) / 2;

	if (sndbuf_alloc(ch->buffer, sc->dma_tag, 0, ch->buf_size) != 0) {
		device_printf(sc->sc_dev, "Can't setup sndbuf.\n");
		return NULL;
	}

	return ch;
}

static int
ssichan_free(kobj_t obj, void *data)
{
	struct sc_chinfo *ch = data;
	struct sc_pcminfo *scp = ch->parent;
	struct ssi_softc *sc = scp->sc;

	mtx_lock(&sc->sc_lock);
	sndbuf_free(ch->buffer);
	mtx_unlock(&sc->sc_lock);

	return (0);
}

static int
ssichan_setformat(kobj_t obj, void *data, uint32_t format)
{
	struct sc_chinfo *ch = data;
	int reg;
	uint32_t val;
	int width;

#if 0
	if (CHANNEL_SETFORMAT(ch->codec, ch->codec, format) != 0)
		return (ENXIO);
#endif
	ch->format = format;
	if (ch->dir == PCMDIR_PLAY)
		reg = SSI_STCCR;
	else
		reg = SSI_SRCCR;

	val = bus_read_4(ch->parent->sc->sc_mem, reg);
	val &= ~(WL3_WL0_M << WL3_WL0_S);

	if (format & AFMT_16BIT)
		width = 16;
	else if (format & AFMT_24BIT)
		width = 24;
	else if (format & AFMT_32BIT)
		width = 32;
	else
		return (EINVAL);

	val |= (width / 2 - 1) << WL3_WL0_S;
	bus_write_4(ch->parent->sc->sc_mem, reg, val);

	return (0);
}

static uint32_t
ssichan_setspeed(kobj_t obj, void *data, uint32_t speed)
{
#if 0
	struct sc_chinfo *ch = data;

	return (CHANNEL_SETSPEED(ch->codec, ch->codec, speed));
#endif
	return (speed);
}

static uint32_t
ssichan_setblocksize(kobj_t obj, void *data, uint32_t blocksize)
{
	struct sc_chinfo *ch = data;
	struct sc_pcminfo *scp = ch->parent;
	struct ssi_softc *sc = scp->sc;

	sndbuf_resize(ch->buffer, sc->dma_size / blocksize, blocksize);

	setup_dma(ch);

	return (ch->buffer->blksz);
}

uint32_t
ssi_dma_intr(void *arg)
{
	struct sc_chinfo *ch;
	struct fsl_dma_ch_conf *conf;
	int bufsize;

	ch = arg;
	conf = &ch->dma_conf;

	bufsize = ch->buffer->bufsize;

	ch->pos += conf->period;
	if (ch->pos >= bufsize)
		ch->pos -= bufsize;

	if (ch->run)
		chn_intr(ch->channel);

	return (0);
}

static int
setup_dma(struct sc_chinfo *ch)
{
	struct fsl_dma_ch_conf *conf;
	struct sc_pcminfo *scp = ch->parent;
	struct ssi_softc *sc;
	struct ssi_dma_desc *desc;
	vm_paddr_t pa;
	int fmt;
	int word_bytes;

	sc = scp->sc;
	conf = &ch->dma_conf;

	conf->ih = ssi_dma_intr;
	conf->ih_user = ch;
	conf->period = ch->buffer->blksz;

	fmt = ch->buffer->fmt;

	if (fmt & AFMT_16BIT) {
		word_bytes = 2;
	} else if (fmt & AFMT_32BIT) {
		word_bytes = 4;
	} else {
		device_printf(sc->sc_dev, "Unknown format\n");
		return (-1);
	}
	if (ch->dir == PCMDIR_PLAY)
		conf->dest_hold_count = word_bytes;
	else
		conf->src_hold_count = word_bytes;

	while (!SLIST_EMPTY(&ch->desc_list)) {
		desc = SLIST_FIRST(&ch->desc_list);
		SLIST_REMOVE(&ch->desc_list, desc, ssi_dma_desc, link);
		free(desc, M_SSI);
	}

	for (int i = 0; i < ch->buffer->blkcnt; i++) {
		desc = malloc_aligned(sizeof(struct ssi_dma_desc), 64,
		    M_SSI, M_ZERO | M_NOWAIT);
		if (ch->dir == PCMDIR_PLAY) {
			pa = rman_get_start(sc->sc_mem) + SSI_STX0;
			desc->desc.dst_attr = (pa >> 32);
			desc->desc.dst_addr = pa;
			desc->desc.src_attr = (ch->buffer->buf_addr >> 32);
			desc->desc.src_addr = ch->buffer->buf_addr;
		} else {
			pa = rman_get_start(sc->sc_mem) + SSI_SRX0;
			desc->desc.src_attr = (pa >> 32);
			desc->desc.src_addr = pa;
			desc->desc.dst_attr = (ch->buffer->buf_addr >> 32);
			desc->desc.dst_addr = ch->buffer->buf_addr;
		}
		SLIST_INSERT_HEAD(&ch->desc_list, desc, link);
	}

	return (0);
}

static int
ssi_start(struct sc_chinfo *ch)
{
	struct ssi_softc *sc;
	int reg;

	sc = ch->parent->sc;

	reg = bus_read_4(sc->sc_mem, SSI_SIER);
	/* Enable DMA interrupt */
	if (ch->dir == PCMDIR_PLAY)
		reg |= SIER_TDMAE;
	else
		reg |= SIER_RDMAE;

	fsl_dma_ch_configure(ch->dma_dev, &ch->dma_conf);
	fsl_dma_ch_start(ch->dma_dev, &SLIST_FIRST(&ch->desc_list)->desc);
	bus_write_4(sc->sc_mem, SSI_SIER, reg);

	return (0);
}

static int
ssi_stop(struct sc_chinfo *ch)
{
	struct ssi_softc *sc;
	int reg;

	sc = ch->parent->sc;

	reg = bus_read_4(sc->sc_mem, SSI_SIER);
	/* Enable DMA interrupt */
	if (ch->dir == PCMDIR_PLAY)
		reg &= ~SIER_TDMAE;
	else
		reg &= ~SIER_RDMAE;

	bus_write_4(sc->sc_mem, SSI_SIER, reg);

	fsl_dma_ch_stop(ch->dma_dev);

	return (0);
}

static int
ssichan_trigger(kobj_t obj, void *data, int go)
{
	struct sc_pcminfo *scp;
	struct sc_chinfo *ch;
	struct ssi_softc *sc;

	ch = data;
	scp = ch->parent;
	sc = scp->sc;

	mtx_lock(&sc->sc_lock);

	switch (go) {
	case PCMTRIG_START:
		ch->run = 1;

		ssi_start(ch);

		break;

	case PCMTRIG_STOP:
	case PCMTRIG_ABORT:
		ch->run = 0;

		ssi_stop(ch);

		break;
	}

	mtx_unlock(&sc->sc_lock);

	return (0);
}

static uint32_t
ssichan_getptr(kobj_t obj, void *data)
{
	struct sc_chinfo *ch;

	ch = data;

	return (ch->pos);
}

static uint32_t ssi_pfmt[] = {
	SND_FORMAT(AFMT_S16_BE, 2, 0),
#if 0
	SND_FORMAT(AFMT_S32_BE, 2, 0),
#endif
	0
};

static struct pcmchan_caps ssi_pcaps = {44100, 44100, ssi_pfmt, 0};

static struct pcmchan_caps *
ssichan_getcaps(kobj_t obj, void *data)
{

	return (&ssi_pcaps);
}

static kobj_method_t ssichan_methods[] = {
	KOBJMETHOD(channel_init,         ssichan_init),
	KOBJMETHOD(channel_free,         ssichan_free),
	KOBJMETHOD(channel_setformat,    ssichan_setformat),
	KOBJMETHOD(channel_setspeed,     ssichan_setspeed),
	KOBJMETHOD(channel_setblocksize, ssichan_setblocksize),
	KOBJMETHOD(channel_trigger,      ssichan_trigger),
	KOBJMETHOD(channel_getptr,       ssichan_getptr),
	KOBJMETHOD(channel_getcaps,      ssichan_getcaps),
	KOBJMETHOD_END
};
CHANNEL_DECLARE(ssichan);

static int
ssi_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "fsl,mpc8610-ssi"))
		return (ENXIO);

	device_set_desc(dev,
	    "Freescale MPC8xxx Synchronous Serial Interface (SSI)");

	return (BUS_PROBE_DEFAULT);
}

static void
ssi_intr(void *arg)
{
#if 0
	struct sc_pcminfo *scp;
	struct sc_chinfo *ch;
	struct ssi_softc *sc;

	scp = arg;
	sc = scp->sc;
	ch = &scp->chan[0];
#endif

	/* We don't use SSI interrupt */
}

static void
setup_ssi(struct ssi_softc *sc)
{
	int reg;

	reg = bus_read_4(sc->sc_mem, SSI_SCR);
	reg &= ~(SCR_I2S_MODE_M << SCR_I2S_MODE_S); /* Not master */
	reg |= (SCR_SSIEN | SCR_TE);
	reg |= (SCR_NET);
	if (sc->sc_sync_mode)
		reg |= SCR_SYN;
	else
		reg &= ~SCR_SYN;
	if (sc->sc_i2s_mode != 0)
		reg |= (sc->sc_i2s_mode << SCR_I2S_MODE_S);
	bus_write_4(sc->sc_mem, SSI_SCR, reg);

}

static int
ssi_attach(device_t dev)
{
	char status[SND_STATUSLEN];
	char mode[20];
	struct sc_pcminfo *scp;
	struct ssi_softc *sc;
	phandle_t codec, dma_handle, node;
	cell_t data;
	int err;
	uint32_t reg;

	sc = malloc(sizeof(struct ssi_softc), M_DEVBUF, M_WAITOK | M_ZERO);
	sc->sc_dev = dev;

	node = ofw_bus_get_node(dev);
	if (OF_getprop(node, "fsl,mode", mode, sizeof(mode)) <= 0) {
		device_printf(dev, "Missing 'fsl,mode' property.\n");
		return (ENXIO);
	}
	if (strcmp(mode, "i2s-slave") == 0) {
		sc->sc_i2s_mode = I2S_SLAVE;
		if (OF_getencprop(node, "codec-handle",
		    &codec, sizeof(codec)) <= 0) {
			device_printf(dev, "Missing codec handle.\n");
			return (ENXIO);
		}
		codec = OF_node_from_xref(codec);
		sc->sc_codec_ph = codec;
		if (OF_getencprop(codec, "clock-frequency",
		    &data, sizeof(data)) < 0) {
			device_printf(dev, "Codec missing frequency property.\n");
			return (ENXIO);
		}
	} else {
		device_printf(dev, "Unknown mode '%s'\n", mode);
		return (ENXIO);
	}

	mtx_init(&sc->sc_lock, device_get_nameunit(dev), "ssi softc", MTX_DEF);

	sc->sc_mem = bus_alloc_resource_any(dev, SYS_RES_MEMORY, 0,
	    RF_ACTIVE | RF_SHAREABLE);
	if (sc->sc_mem == NULL) {
		device_printf(dev, "could not allocate memory resource\n");
		return (ENXIO);
	}

	if (sc->sc_i2s_mode == I2S_SLAVE) {
		reg = bus_read_4(sc->sc_mem, SSI_STCR);
		reg &= ~(STCR_TXDIR);
		bus_write_4(sc->sc_mem, SSI_STCR, reg);
		reg = bus_read_4(sc->sc_mem, SSI_SRCR);
		reg &= ~(SRCR_RXDIR);
		bus_write_4(sc->sc_mem, SSI_SRCR, reg);
	}

	sc->sc_irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, 0,
	    RF_ACTIVE | RF_SHAREABLE);
	if (sc->sc_irq == NULL) {
		device_printf(dev, "could not allocate IRQ resource\n");
		return (ENXIO);
	}

	node = ofw_bus_get_node(dev);
	if (!OF_hasprop(node, "fsl,ssi-asynchronous"))
		sc->sc_sync_mode = true;

	if (OF_getencprop(node, "fsl,fifo-depth", &data, sizeof(data)) <= 0)
		sc->sc_fifo_depth = 8;
	else
		sc->sc_fifo_depth = data + 1;

	if (OF_getencprop(node, "fsl,playback-dma", &dma_handle,
	    sizeof(dma_handle)) <= 0) {
		device_printf(dev, "No playback DMA\n");
		goto err;
	}
	sc->sc_playback_dma = OF_device_from_xref(dma_handle);

	if (OF_getencprop(node, "fsl,capture-dma", &dma_handle,
	    sizeof(dma_handle)) <= 0) {
		device_printf(dev, "No capture DMA\n");
		goto err;
	}
	sc->sc_capture_dma = OF_device_from_xref(dma_handle);

	/* Setup PCM */
	scp = malloc(sizeof(struct sc_pcminfo), M_DEVBUF, M_NOWAIT | M_ZERO);
	scp->sc = sc;
	scp->dev = dev;

	/*
	 * Maximum possible DMA buffer.
	 * Will be used partially to match 24 bit word.
	 */
	sc->dma_size = 131072;

	/*
	 * Must use dma_size boundary as modulo feature required.
	 * Modulo feature allows setup circular buffer.
	 */

	err = bus_dma_tag_create(
	    bus_get_dma_tag(sc->sc_dev),
	    4, 0,			/* alignment, boundary */
	    BUS_SPACE_MAXADDR_32BIT,	/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    FSL_DMA_MAXSIZE,		/* maxsize */
	    BUS_SPACE_UNRESTRICTED,	/* nsegments */
	    FSL_DMA_MAXSEG, 0,		/* maxsegsize, flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->dma_tag);

	/* Setup interrupt handler */
	err = bus_setup_intr(dev, sc->sc_irq, INTR_MPSAFE | INTR_TYPE_AV,
	    NULL, ssi_intr, scp, &sc->ih);
	if (err) {
		device_printf(dev, "Unable to alloc interrupt resource.\n");
		goto err;
	}

	pcm_init(dev, scp);

	pcm_setflags(dev, pcm_getflags(dev) | SD_F_MPSAFE);

	if (err) {
		device_printf(dev, "Can't register pcm.\n");
		goto err;
	}

	scp->chnum = 0;
	pcm_addchan(dev, PCMDIR_PLAY, &ssichan_class, scp);
	scp->chnum++;
	pcm_addchan(dev, PCMDIR_REC, &ssichan_class, scp);
	scp->chnum++;

	snprintf(status, SND_STATUSLEN, "at simplebus");
	err = pcm_register(dev, status);

	setup_ssi(sc);

	return (0);

err:
	if (sc->sc_mem != NULL)
		bus_release_resource(dev, sc->sc_mem);
	if (sc->sc_irq != NULL)
		bus_release_resource(dev, sc->sc_irq);

	return (ENXIO);
}

static device_method_t ssi_pcm_methods[] = {
	DEVMETHOD(device_probe,		ssi_probe),
	DEVMETHOD(device_attach,	ssi_attach),
	{ 0, 0 }
};

static driver_t ssi_pcm_driver = {
	"pcm",
	ssi_pcm_methods,
	PCM_SOFTC_SIZE,
};

DRIVER_MODULE(ssi, simplebus, ssi_pcm_driver, 0, 0);
MODULE_DEPEND(ssi, sound, SOUND_MINVER, SOUND_PREFVER, SOUND_MAXVER);
MODULE_VERSION(ssi, 1);
