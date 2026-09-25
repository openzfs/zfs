/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 2018 Julian Heuking <J.Heuking@beckhoff.com>
 * Copyright (c) 2025 Jorgen Lundman <lundman@lundman.net>
 */

// Get "_daylight: has bad storage class" in time.h
#define	_INC_TIME


#define	_NO_CRT_STDIO_INLINE

#include <sys/debug.h>
#include <stdarg.h>
#include <stdio.h>
#include <Trace.h>
#include <sys/mod.h>

#define	max_line_length 1024

#ifdef windowsStyleLineEndings
	char *endLine = "\r\n";
#else
	char *endLine = "";
#endif

char *endBuf = "-EB-";

KSPIN_LOCK cbuf_spin;
char *cbuf = NULL;

static unsigned long long cbuf_size = 0x100000; // 1MB
static unsigned long long startOff = 0;
void saveBuffer(void);

#define	CBUF_SAVE_LEN 100
#define	CBUF_FILENAME L"\\??\\C:\\Program Files\\OpenZFS On Windows\\cbuf.txt"
uchar_t zfs_cbuf_save[CBUF_SAVE_LEN] = "Set this to 1 to save cbuf";

int
param_cbuf_save(ZFS_MODULE_PARAM_ARGS)
{
	static uchar_t buf[CBUF_SAVE_LEN];

	*type = ZT_TYPE_STRING;

	if (set == B_FALSE) {
		*ptr = (void *)zfs_cbuf_save;
		*len = strlen(zfs_cbuf_save);
		return (0);
	}

	// Skip any string not starting with "1"
	if (!ptr ||
	    !*ptr ||
	    ((char *)*ptr)[0] != '1') {
		// Keep the text if starts with *
		if (((char *)*ptr)[0] != '*')
			snprintf(zfs_cbuf_save, sizeof (zfs_cbuf_save),
			    "Set this to 1 to save cbuf");
		return (0);
	}

	// strlcpy(buf, *ptr, sizeof (buf));
	dprintf("Saving cbuf to %S\n", CBUF_FILENAME);
	((char *)*ptr)[0] = '*'; // Take out the '1'

	saveBuffer();

	return (0);
}

int
initDbgCircularBuffer(void)
{
	cbuf = ExAllocatePoolWithTag(NonPagedPoolNx, cbuf_size, '!GBD');
	ASSERT(cbuf);
	memset(cbuf, '\n', cbuf_size);
	KeInitializeSpinLock(&cbuf_spin);
	return (0);
}

int
finiDbgCircularBuffer(void)
{
	ExFreePoolWithTag(cbuf, '!GBD');
	return (0);
}

/*
 *	Howto: Read the circular buffer with windbg
 *
 *	get address of cbuf buffer:
 *		dt OpenZFS!cbuf --> copy shown address
 *
 *	write memory to file
 *		.writemem [filepath] [cbuf address] L[Length as hex]
 *	e.g. .writemem C:\src\cbuf.txt 0xffff870d`d2000000 L100000
 *
 *	Open in your favourite text editor and
 *	locate -EB-, there's the start/end of the buffer
 *
 */
void
addbuffer(char *buf)
{
	// unsigned long long writtenBytes = 0;
	if (buf) {
		unsigned long long bufLen = strlen(buf);
		unsigned long long endLineLen = strlen(endLine);
		unsigned long long endBufLen = strlen(endBuf);

		if (startOff + bufLen + endLineLen + endBufLen >= cbuf_size) {
			// too long, set reset start offset
			while (startOff < cbuf_size) {
				cbuf[startOff] = 0;
				startOff++;
			}
			startOff = 0;
		}

		unsigned long long endBufOff = startOff + bufLen + endLineLen;
		// print new end buf marker first,
		// before overwriting the old one
		for (int i = 0; i < endBufLen; i++) {
			cbuf[endBufOff + i] = endBuf[i];
		}

		// print buffer
		for (int i = 0; i < bufLen; i++) {
			cbuf[startOff] = buf[i];
			startOff++;
		}

		// print end line marker
		for (int i = 0; i < endLineLen; i++) {
			cbuf[startOff] = endLine[i];
			startOff++;
		}
	}
}

void
printBuffer(const char *fmt, ...)
{
	// DPCs can't block (mutex) - replace this code with spinlocks
	KIRQL level;
	va_list args;

	va_start(args, fmt);
	char buf[max_line_length];
	int buf_used;

	buf_used = _snprintf(buf, sizeof (buf), "%p: ", PsGetCurrentThread());
	if (buf_used < 0) {
		return;
	}

	int tmp = _vsnprintf_s(buf + buf_used, sizeof (buf) - buf_used,
	    _TRUNCATE, fmt, args);

	if (tmp < 0) {
		_snprintf(buf + max_line_length - 7, 7, "TRUNC\n");
	}

	KeAcquireSpinLock(&cbuf_spin, &level);
	addbuffer(buf);
	KeReleaseSpinLock(&cbuf_spin, level);

	va_end(args);
}

// Signalled by userland to write out the kernel buffer.
void
saveBuffer(void)
{
	UNICODE_STRING fileNameUnicode;
	OBJECT_ATTRIBUTES objectAttributes;
	HANDLE fileHandle;
	IO_STATUS_BLOCK ioStatusBlock;
	NTSTATUS status;
	int len;

	RtlInitUnicodeString(&fileNameUnicode, CBUF_FILENAME);

	InitializeObjectAttributes(&objectAttributes, &fileNameUnicode,
	    OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);

	status = ZwCreateFile(&fileHandle, GENERIC_WRITE, &objectAttributes,
	    &ioStatusBlock, NULL, FILE_ATTRIBUTE_NORMAL,
	    0, FILE_OVERWRITE_IF, FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0);

	if (NT_SUCCESS(status))	{
		// To be nice, let's start at the offset, and do two saves.
		// first, startOffset to end of buffer. Special case, if
		// buffer has never wrapped, skip all initial "\n"
		unsigned long long pos;
		pos = startOff + sizeof (endBuf);
		while (cbuf[pos] == '\n' && pos < (cbuf_size - sizeof (endBuf)))
			pos++;

		len = cbuf_size - pos;
		if (len > 0)
			status = ZwWriteFile(fileHandle, NULL, NULL, NULL,
			    &ioStatusBlock, cbuf + pos, (ULONG)len, NULL, NULL);
		// then, start of file, to startOff
		len = startOff;
		if (len > 0)
			status = ZwWriteFile(fileHandle, NULL, NULL, NULL,
			    &ioStatusBlock, cbuf, (ULONG)len, NULL, NULL);

		ZwClose(fileHandle);
		snprintf(zfs_cbuf_save, sizeof (zfs_cbuf_save),
		    "* Saved %S", CBUF_FILENAME);
	} else {
		snprintf(zfs_cbuf_save, sizeof (zfs_cbuf_save),
		    "* Unable to open %S", CBUF_FILENAME);
	}
}
