/*-
 * Copyright (c) 2012 Semihalf.
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
#include <sys/kdb.h>

#include <machine/atomic.h>
#include "qman.h"

#define QME_BUFFER_SIZE		1024
#define	QME_TEST_FRAMES_NUM	1000

MALLOC_DEFINE(M_QME, "QMAN-Example", "QMAN Usage Example");


static struct callout qme_callout;
static struct callout qme_end_callout;

static void
qme_end(t_Handle fqr)
{
	printf("QME: Freeing FQR ID = %u\n", qman_fqr_get_base_fqid(fqr));
	qman_fqr_free(fqr);
}

static e_RxStoreResponse
qme_recieved_frame_callback(t_Handle app, t_Handle qm_fqr, t_Handle qm_portal,
    uint32_t fqid_offset, t_DpaaFD *frame)
{

	if (frame->id != ((uint32_t *)frame->addrl)[0])
		printf("QME: corrupted frame received\n");

	if (frame->id == QME_TEST_FRAMES_NUM) {
		printf("QME: All frames received successfully on FQ ID = %u\n",
		    qman_fqr_get_base_fqid(qm_fqr));
		callout_reset(&qme_end_callout, hz, qme_end, qm_fqr);
	}

	free((void *)frame->addrl, M_QME);

	return (e_RX_STORE_RESPONSE_CONTINUE);
}

t_Handle qq2;

static void
qme_enqueue(t_Handle qq, uint32_t i)
{
	t_DpaaFD fd;
	t_Error error;

	fd.addrl = (uint32_t) malloc(QME_BUFFER_SIZE, M_QME, M_NOWAIT);
	fd.id = i;
	fd.length = QME_BUFFER_SIZE;
	fd.status = 0;

	((uint32_t *)fd.addrl)[0] = i;

	error = qman_fqr_enqueue(qq, 0, &fd);
	if (error != E_OK) {
		printf("enqueue error\n");
	}

	++i;
}

static void
qme_test_fqr(t_Handle fqr)
{
	int i;

	for (i = 0; i < QME_TEST_FRAMES_NUM; ++i)
		qme_enqueue(fqr, i + 1);

	printf("QME: enqueued %u frames to FQID = %u\n", QME_TEST_FRAMES_NUM,
	    qman_fqr_get_base_fqid(fqr));
}

static void
qme_test(void *ptr)
{
	t_Handle fqr, fqr2;

	fqr = qman_fqr_create(1, e_QM_FQ_CHANNEL_POOL1, 1, FALSE, 0, FALSE,
	    FALSE, TRUE, FALSE, 0, 0, 0);
	if (fqr == NULL) {
		printf("could not create the queue\n");
	}
	qman_fqr_register_cb(fqr, qme_recieved_frame_callback, fqr);

	fqr2 = qman_fqr_create(1, e_QM_FQ_CHANNEL_POOL1, 1, TRUE, 2, FALSE,
	    FALSE, TRUE, FALSE, 0, 0, 0);
	if (fqr2 == NULL) {
		printf("could not create the queue\n");
	}
	qman_fqr_register_cb(fqr2, qme_recieved_frame_callback, fqr2);

	qme_test_fqr(fqr);
	qme_test_fqr(fqr2);
}

static void
qme_start(void *unused)
{

	printf("Starting QMAN Example...\n");

	callout_init(&qme_callout, CALLOUT_MPSAFE);
	callout_init(&qme_end_callout, CALLOUT_MPSAFE);
	callout_reset(&qme_callout, hz, qme_test, NULL);

	return;
}

SYSINIT(qme_start, SI_SUB_RUN_SCHEDULER, SI_ORDER_MIDDLE, qme_start, NULL);
