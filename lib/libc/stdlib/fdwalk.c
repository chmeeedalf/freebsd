/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2019 Justin Hibbits
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/filedesc.h>
#include <sys/sysctl.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

#define FD(slot, bit)	((slot * sizeof(NDSLOTTYPE) * NBBY) + bit)

int
fdwalk(int (*cb)(void *, int), void *cbd)
{
	int mib[4];
	int error;
	size_t len, i, j;
	NDSLOTTYPE *buf, tmp;
	int retries;

	len = 0;
	mib[0] = CTL_KERN;
	mib[1] = KERN_PROC;
	mib[2] = KERN_PROC_FDMAP;
	mib[3] = 0;

	buf = NULL;
	retries = 5;	/* Arbitrary retry count. */

	/*
	 * Try a few times to get a stable buffer.  The buffer size may change
	 * if file descriptors are being created in other threads.
	 */
	for (;;) {
		error = sysctl(mib, nitems(mib), NULL, &len, NULL, 0);
		if (error == -1)
			return (-1);
		/*
		 * Add some headroom in case more descriptors are added before
		 * the next call.
		 */
		len = len * 4 / 3;
		buf = reallocf(buf, len);
		if (buf == NULL)
			return (-1);
		error = sysctl(mib, nitems(mib), buf, &len, NULL, 0);
		if (error == 0)
			break;
		if (errno != ENOMEM) {
			free(buf);
			return (-1);
		}
	}
	/*
	 * Go through the full file list.  The fdmap is an integral multiple of
	 * sizeof(NDSLOTTYPE).
	 */
	len = howmany(len, sizeof(NDSLOTTYPE));
	
	for (i = 0; i < len; i++) {
		/*
		 * Iterate over each bit in the slot, short-circuting when there
		 * are no more file descriptors in use in this slot.
		 */
		for (j = 0, tmp = buf[i];
		    j < NBBY * sizeof(NDSLOTTYPE) && tmp != 0;
		    j++, tmp >>= 1) {
			if (tmp & 1) {
				error = cb(cbd, FD(i, j));
				if (error != 0)
					goto done;
			}
		}
	}
done:
	free(buf);

	return (error);
}
