/*-
 * Copyright (c) 2011-2012 Semihalf.
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/callout.h>
#include <sys/bus.h>
#include <sys/malloc.h>

#include <machine/atomic.h>
#include "bman.h"

#define BME_BUFFER_SIZE	1024

MALLOC_DEFINE(M_BME, "BMAN-Example", "BMAN Usage Example");

static uint8_t in_bpid;
static t_Handle in_pool;
static uint32_t in_pool_depleted;

static uint8_t out_bpid;
static t_Handle out_pool;

static struct callout bme_process_callout;
static struct callout bme_fill_callout;
static struct callout bme_release_callout;

static uint8_t *
bme_get_buffer(t_Handle h_BufferPool, t_Handle *context)
{
	uint8_t *buffer;

	buffer = malloc(BME_BUFFER_SIZE, M_BME, M_NOWAIT);
	printf("%s(): Allocated buffer 0x%08X for %s.\n", __func__, buffer,
	    (char *)h_BufferPool);

	return (buffer);
}

static t_Error
bme_put_buffer(t_Handle h_BufferPool, uint8_t *buffer, t_Handle context)
{

	free(buffer, M_BME);
	printf("%s(): Freed buffer 0x%08X from %s.\n", __func__, buffer,
	    (char *)h_BufferPool);

	return (E_OK);
}

static void
bme_process(void *unused)
{
	uint8_t *buffer;

	buffer = bman_get_buffer(in_pool);
	if (buffer) {
		bman_put_buffer(out_pool, buffer);
		printf("%s(): Processed buffer 0x%08X.\n", __func__, buffer);
	} else
		printf("%s(): IN-Pool empty!\n", __func__);

	callout_reset(&bme_process_callout, hz/10, bme_process, NULL);
}

static void
bme_fill(void *handle)
{
	if (!atomic_load_acq_32(&in_pool_depleted))
		return;

	printf("%s(): Filling %s with 8 buffers.\n", __func__, (char*)handle);
	bman_pool_fill(in_pool, 8);
	callout_reset(&bme_fill_callout, 1, bme_fill, handle);
}

static void
bme_release(void *unused)
{
	unsigned int i;
	void *buffer;

	i = 0;
	while ((buffer = bman_get_buffer(out_pool))) {
		free(buffer, M_BME);
		i += 1;

		printf("%s(): Freed buffer 0x%08X from OUT-Pool.\n", __func__,
		    buffer);
	}

	printf("%s(): Released %u buffers from OUT-Pool.\n", __func__, i);
	callout_reset(&bme_release_callout, hz, bme_release, NULL);
}

static void
bme_depletion_cb(t_Handle h_App, bool in)
{
	printf("%s(): Pool %s %s depletion state!\n", __func__, (char *)h_App,
	    (in) ? "entered" : "exited");

	atomic_store_rel_32(&in_pool_depleted, in);
	if (in)
		callout_reset(&bme_fill_callout, 1, bme_fill, h_App);
}

static void
bme_start(void *unused)
{
	printf("%s(): Starting BMAN Example...\n", __func__);

	in_pool = bman_pool_create(&in_bpid, BME_BUFFER_SIZE, 0, 0, 8,
	    bme_get_buffer, bme_put_buffer, 4, 8, 0, 0, bme_depletion_cb,
	    "IN-Pool", NULL, NULL);

	printf("%s(): Created IN-Pool 0x%08X (ID: %u).\n", __func__, in_pool,
	    in_bpid);


	out_pool = bman_pool_create(&out_bpid, BME_BUFFER_SIZE, 0, 0, 0,
	    bme_get_buffer, bme_put_buffer, 0, 0, 0, 0, NULL, "OUT-Pool",
	    NULL, NULL);

	printf("%s(): Created OUT-Pool 0x%08X (ID: %u).\n", __func__, out_pool,
	    out_bpid);

	callout_init(&bme_process_callout, CALLOUT_MPSAFE);
	callout_init(&bme_fill_callout, CALLOUT_MPSAFE);
	callout_init(&bme_release_callout, CALLOUT_MPSAFE);

	callout_reset(&bme_process_callout, hz, bme_process, NULL);
	callout_reset(&bme_release_callout, hz, bme_release, NULL);
}

SYSINIT(bme_start, SI_SUB_RUN_SCHEDULER, SI_ORDER_MIDDLE, bme_start, NULL);
