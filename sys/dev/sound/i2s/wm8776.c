/*-
 * Copyright 2016 Justin Hibbits <jhibbits@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include "opt_snd.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/malloc.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <machine/dbdma.h>
#include <machine/intr_machdep.h>
#include <machine/resource.h>
#include <machine/bus.h>
#include <machine/pio.h>
#include <sys/rman.h>

#include <dev/iicbus/iicbus.h>
#include <dev/iicbus/iiconf.h>
#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <dev/sound/pcm/sound.h>

#include "channel_if.h"
#include "mixer_if.h"

#define	WM8776_HEADPHONE_LEFT	0x0
#define	WM8776_HEADPHONE_RIGHT	0x1
#define	WM8776_HEADPHONE_MASTER	0x2
#define	WM8776_DIGI_ATTN_DACL	0x3
#define	WM8776_DIGI_ATTN_DACR	0x4
#define	WM8776_DIGI_ATTN_DACA	0x5
#define	  WM8776_UPDATE		  0x100
#define	WM8776_DAC_IC		0xa
#define	  DAC_IC_I2S		  0x02
#define	  DAC_IC_DACWL_M	  0x30
#define	  DAC_IC_DACWL_16	  0x00
#define	  DAC_IC_DACWL_20	  0x10
#define	  DAC_IC_DACWL_24	  0x20
#define	  DAC_IC_DACWL_32	  0x30
#define	WM8776_ADC_IC		0xb
#define	WM8776_MASTER_CTL	0xc
#define	  MASTER_CTL_ADCRATE_S	  0
#define	  MASTER_CTL_DACRATE_S	  4
#define	  MASTER_CTL_RATE_M	  0x7
#define	  MASTER_CTL_DACMS	  0x80
#define	  MASTER_CTL_ADCMS	  0x100
#define	WM8776_PWR_DOWN		0xd
#define	WM8776_ATTN_ADCL	0xe
#define	WM8776_ATTN_ADCR	0xf
#define	WM8776_ADCMUX		0x15
#define	WM8776_RESET		0x17
#define	WM8776_MAXREGS		0x18

struct wm8776_softc
{
	device_t sc_dev;
	uint32_t sc_addr;
	uint16_t sc_regs[WM8776_MAXREGS];
	uint32_t sc_sysclk;
};

struct wm8776_chan {
	struct wm8776_softc *sc;
	int direction;
};

static int	wm8776_probe(device_t);
static int 	wm8776_attach(device_t);
static int	wm8776_init(struct snd_mixer *m);
static int	wm8776_uninit(struct snd_mixer *m);
static int	wm8776_reinit(struct snd_mixer *m);
static int	wm8776_set(struct snd_mixer *m, unsigned dev, unsigned left,
		    unsigned right);
static u_int32_t	wm8776_setrecsrc(struct snd_mixer *m, u_int32_t src);

static void *wm8776_chan_init(kobj_t obj, void *devinfo, struct snd_dbuf *b,
    struct pcm_channel *c, int dir);
static int	wm8776_chan_free(kobj_t obj, void *data);
static int	wm8776_chan_setformat(kobj_t obj, void *data, u_int32_t format);
static int	wm8776_set_format_int(struct wm8776_softc *sc, int reg, uint32_t fmt);
static uint32_t	wm8776_chan_setspeed(kobj_t obj, void *data, u_int32_t speed);
static uint32_t wm8776_setspeed_int(struct wm8776_softc *sc, int dir, u_int32_t speed);
static uint32_t	wm8776_chan_setblocksize(kobj_t obj, void *data, u_int32_t blocksz);
static int	wm8776_chan_trigger(kobj_t obj, void *data, int go);
static uint32_t	wm8776_chan_getptr(kobj_t obj, void *data);
static struct pcmchan_caps *wm8776_chan_getcaps(kobj_t obj, void *data);

static device_method_t wm8776_methods[] = {
	/* Device interface. */
	DEVMETHOD(device_probe,		wm8776_probe),
	DEVMETHOD(device_attach,	wm8776_attach),

	{ 0, 0 }
};

static DEFINE_CLASS_0(wm8776, wm8776_driver, wm8776_methods,
    sizeof(struct wm8776_softc));
DRIVER_MODULE(wm8776, iicbus, wm8776_driver, 0, 0);
MODULE_VERSION(wm8776, 1);
MODULE_DEPEND(wm8776, iicbus, 1, 1, 1);

static kobj_method_t wm8776_mixer_methods[] = {
	KOBJMETHOD(mixer_init, 		wm8776_init),
	KOBJMETHOD(mixer_uninit, 	wm8776_uninit),
	KOBJMETHOD(mixer_reinit, 	wm8776_reinit),
	KOBJMETHOD(mixer_set, 		wm8776_set),
	KOBJMETHOD(mixer_setrecsrc, 	wm8776_setrecsrc),
	KOBJMETHOD_END
};

MIXER_DECLARE(wm8776_mixer);

static kobj_method_t wm8776_chan_methods[] = {
	KOBJMETHOD(channel_init, 	wm8776_chan_init),
	KOBJMETHOD(channel_free, 	wm8776_chan_free),
	KOBJMETHOD(channel_setformat,	wm8776_chan_setformat),
	KOBJMETHOD(channel_setspeed,	wm8776_chan_setspeed),
	KOBJMETHOD(channel_setblocksize, wm8776_chan_setblocksize),
	KOBJMETHOD(channel_trigger, 	wm8776_chan_trigger),
	KOBJMETHOD(channel_getcaps, 	wm8776_chan_getcaps),
	KOBJMETHOD(channel_getptr, 	wm8776_chan_getptr),
	KOBJMETHOD_END
};

CHANNEL_DECLARE(wm8776_chan);

static int
wm8776_write(struct wm8776_softc *sc, uint8_t reg, uint16_t data)
{
	uint8_t buf[3];

	struct iic_msg msg[] = {
		{ sc->sc_addr, IIC_M_WR, 0, buf }
	};
		
	msg[0].len = 2;
	buf[0] = (reg << 1) | ((data & 0x100) >> 8);
	buf[1] = data & 0xff;

	sc->sc_regs[reg] = data;
	iicbus_transfer(sc->sc_dev, msg, 1);

	return (0);
}

static int
wm8776_probe(device_t dev)
{

	if (!ofw_bus_is_compatible(dev, "wlf,wm8776"))
		return (ENXIO);

	device_set_desc(dev, "Wolfson WM8776 Audio Codec");
	return (0);
}

static int
wm8776_attach(device_t dev)
{
	struct wm8776_softc *sc;
	cell_t cell;

	/* Driver only works in master mode. */
	if (OF_getencprop(ofw_bus_get_node(dev), "clock-frequency",
	    &cell, sizeof(cell)) <= 0)
		return (ENXIO);

	sc = device_get_softc(dev);
	sc->sc_dev = dev;
	sc->sc_addr = iicbus_get_addr(dev);
	sc->sc_sysclk = cell;

	/*
	 * Initialize register cache.  These values are taken straight from the
	 * datasheet.
	 */
	sc->sc_regs[0] = 0x79;
	sc->sc_regs[1] = 0x79;
	sc->sc_regs[2] = 0x79;
	sc->sc_regs[3] = 0xff;
	sc->sc_regs[4] = 0xff;
	sc->sc_regs[5] = 0xff;
	sc->sc_regs[6] = 0x00;
	sc->sc_regs[7] = 0x00;
	sc->sc_regs[8] = 0x00;
	sc->sc_regs[9] = 0x00;
	sc->sc_regs[10] = 0x22;
	sc->sc_regs[11] = 0x22;
	sc->sc_regs[12] = 0x22;
	sc->sc_regs[13] = 0x08;
	sc->sc_regs[14] = 0xcf;
	sc->sc_regs[15] = 0xcf;
	sc->sc_regs[16] = 0x7b;
	sc->sc_regs[17] = 0x00;
	sc->sc_regs[18] = 0x32;
	sc->sc_regs[19] = 0x00;
	sc->sc_regs[20] = 0xa6;
	sc->sc_regs[21] = 0x01;
	sc->sc_regs[22] = 0x01;
	sc->sc_regs[23] = 0x00;

	mixer_init(dev, &wm8776_mixer_class, sc);

	/* Set static format */
	wm8776_set_format_int(sc, WM8776_DAC_IC,
	    SND_FORMAT(AFMT_S16_BE, 2, 0));
	wm8776_set_format_int(sc, WM8776_ADC_IC,
	    SND_FORMAT(AFMT_S16_BE, 2, 0));
	wm8776_setspeed_int(sc, PCMDIR_PLAY, 44100);
	wm8776_setspeed_int(sc, PCMDIR_REC, 44100);

	(void)&wm8776_chan_class;
#if 0
	pcm_setflags(dev, pcm_getflags(dev) | SD_F_MPSAFE);

	pcm_init(dev, scp);

	pcm_addchan(dev, PCMDIR_PLAY, &wm8776_chan_class, sc);
	pcm_addchan(dev, PCMDIR_REC, &wm8776_chan_class, sc);
#endif

	return (0);
}

static int
wm8776_init(struct snd_mixer *m)
{
	u_int		x = 0;

	x |= SOUND_MASK_VOLUME | SOUND_MIXER_PCM;
	x |= SOUND_MIXER_IGAIN | SOUND_MIXER_OGAIN;
	mix_setdevs(m, x);

	return (0);
}

static int
wm8776_uninit(struct snd_mixer *m)
{

	return (0);
}

static int
wm8776_reinit(struct snd_mixer *m)
{
	struct wm8776_softc *sc = mix_getdevinfo(m);

	wm8776_write(sc, WM8776_RESET, 0);
	return (0);
}

static int
wm8776_set(struct snd_mixer *m, unsigned dev, unsigned left, unsigned right)
{
	struct wm8776_softc *sc;
	u_int l, r;

	sc = mix_getdevinfo(m);

	if (left > 100 || right > 100)
		return (0);

	switch (dev) {
	case SOUND_MIXER_VOLUME:
		/* Scale goes from -127dB to 0dB, 0.5dB increments */
		l = (left * 255) / 100;
		r = (right * 255) / 100;

		wm8776_write(sc, WM8776_DIGI_ATTN_DACL, l | WM8776_UPDATE);
		wm8776_write(sc, WM8776_DIGI_ATTN_DACR, r | WM8776_UPDATE);

		return (left | (right << 8));
	case SOUND_MIXER_PCM:
		/* Scale goes from -73dB to +6dB, in 1dB increments */
		l = (left == 0) ? 0 : 0x30 + (left * 80) / 100;
		r = (right == 0) ? 0 : 0x30 + (right * 80) / 100;

		wm8776_write(sc, WM8776_HEADPHONE_LEFT, l | WM8776_UPDATE);
		wm8776_write(sc, WM8776_HEADPHONE_RIGHT, r | WM8776_UPDATE);

		return (left | (right << 8));
	case SOUND_MIXER_IGAIN:
	case SOUND_MIXER_OGAIN:
		/* TODO */
		break;
	}

	return (0);
}

static u_int32_t
wm8776_setrecsrc(struct snd_mixer *m, u_int32_t src)
{
	return (0);
}

static void *
wm8776_chan_init(kobj_t kobj, void *devinfo, struct snd_dbuf *dbuf,
    struct pcm_channel *chan, int dir)
{

	return (NULL);
}

static int
wm8776_chan_free(kobj_t kobj, void *devinfo)
{
	return (0);
}

static int
wm8776_set_format_int(struct wm8776_softc *sc, int reg, uint32_t format)
{
	uint8_t regval = sc->sc_regs[reg];
	uint8_t fmt;

	switch (AFMT_ENCODING(format)) {
	case AFMT_S16_BE:
		fmt = DAC_IC_DACWL_16;
		break;
	case AFMT_S24_BE:
		fmt = DAC_IC_DACWL_24;
		break;
	case AFMT_S32_BE:
		fmt = DAC_IC_DACWL_32;
		break;
	default:
		return (EINVAL);
	}

	regval &= ~DAC_IC_DACWL_M;
	regval |= fmt;

	wm8776_write(sc, reg, regval);

	return (0);
}

static int
wm8776_chan_setformat(kobj_t kobj, void *devinfo, u_int32_t format)
{
	struct wm8776_chan *ch = devinfo;
	uint8_t reg;

	if (ch->direction == PCMDIR_PLAY)
		reg = WM8776_DAC_IC;
	else
		reg = WM8776_ADC_IC;

	return (wm8776_set_format_int(ch->sc, reg, format));
}

static int divisors[] = {
	128, /* DAC only */
	192, /* DAC only */
	256,
	384,
	512,
	768
};

static uint32_t
wm8776_setspeed_int(struct wm8776_softc *sc, int dir, u_int32_t speed)
{
	int i = 0;
	uint8_t reg_val;
	int shift;

	if (dir != PCMDIR_PLAY)
		i = 2;

	for (; i < nitems(divisors); i++)
		if (sc->sc_sysclk / divisors[i] <= speed)
			break;

	if (dir == PCMDIR_PLAY)
		shift = MASTER_CTL_DACRATE_S;
	else
		shift = MASTER_CTL_ADCRATE_S;

	reg_val = sc->sc_regs[WM8776_MASTER_CTL];
	reg_val &= ~(MASTER_CTL_RATE_M << shift);
	reg_val |= (i << shift);
	wm8776_write(sc, WM8776_MASTER_CTL, reg_val);

	return (sc->sc_sysclk / divisors[i]);
}

static uint32_t
wm8776_chan_setspeed(kobj_t kobj, void *devinfo, u_int32_t speed)
{
	struct wm8776_chan *chan = devinfo;
	struct wm8776_softc *sc = chan->sc;

	return (wm8776_setspeed_int(sc, chan->direction, speed));
}

static uint32_t
wm8776_chan_setblocksize(kobj_t kobj, void *devinfo, u_int32_t blocksize)
{
	/* Nothing to do here */

	return (blocksize);
}

static int
wm8776_chan_trigger(kobj_t kobj, void *devinfo, int trigger)
{
	/* Nothing to do here */

	return (0);
}

static uint32_t
wm8776_chan_getptr(kobj_t kobj, void *devinfo)
{
	/* Nothing to do here */

	return (0);
}

#ifdef notyet
static uint32_t sc_fmt[] = {
	SND_FORMAT(AFMT_S16_BE, 2, 0),
	SND_FORMAT(AFMT_S24_BE, 2, 0),
	SND_FORMAT(AFMT_S32_BE, 2, 0),
	0
};

static struct pcmchan_caps caps = {
	32000, 192000, sc_fmt, 0
};
#endif
static uint32_t sc_fmt[] = {
	SND_FORMAT(AFMT_S16_BE, 2, 0),
};

static struct pcmchan_caps caps = {
	44100, 44100, sc_fmt, 0
};

static struct pcmchan_caps *
wm8776_chan_getcaps(kobj_t kobj, void *devinfo)
{
	return (&caps);
}
