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
  */

#include <sys/debug.h>
#include <stdarg.h>
#include <stdio.h>

#define max_line_length 1024
//#define windowsStyleLineEndings

#ifdef windowsStyleLineEndings
	char* endLine = "\r\n";
#else
	char* endLine = "";
#endif

char* endBuf = "-EB-";

KSPIN_LOCK cbuf_spin;
char *cbuf = NULL;

static unsigned long long cbuf_size = 0x100000; //1MB 
static unsigned long long startOff = 0;


int initDbgCircularBuffer(void)
{
	cbuf = ExAllocatePoolWithTag(NonPagedPoolNx, cbuf_size, '!GBD');
	ASSERT(cbuf);
	KeInitializeSpinLock(&cbuf_spin);
	return 0;
}

int finiDbgCircularBuffer(void)
{
	ExFreePoolWithTag(cbuf, '!GBD');
	return 0;
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

void addbuffer(char* buf)
{
	// unsigned long long writtenBytes = 0;
	if (buf) {
		unsigned long long bufLen = strlen(buf);
		unsigned long long endLineLen = strlen(endLine);
		unsigned long long endBufLen = strlen(endBuf);

		if (startOff + bufLen + endLineLen + endBufLen >= cbuf_size) {
			// too long, set reset start offset 
			while (startOff < cbuf_size) { // reset the rest of the buffer
				cbuf[startOff] = 0;
				startOff++;
			}
			startOff = 0;
		}

		unsigned long long endBufOff = startOff + bufLen + endLineLen;
		// print new end buf marker first, before overwriting the old one
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

void printBuffer(const char *fmt, ...)
{
	// DPCs can't block (mutex) - replace this code with spinlocks
	KIRQL level;
	va_list args;
	va_start(args, fmt);
	char buf[max_line_length];
	_snprintf(buf, 18, "%p: ", PsGetCurrentThread());

	int tmp = _vsnprintf_s(&buf[17], sizeof(buf), max_line_length, fmt, args);
	if (tmp >= max_line_length) {
		_snprintf(&buf[17], 17, "buffer too small");
	}

	KeAcquireSpinLock(&cbuf_spin,&level);
	addbuffer(buf);
	KeReleaseSpinLock(&cbuf_spin, level);
	
	va_end(args);
}

// Signalled by userland to write out the kernel buffer.
void saveBuffer(void)
{
	UNICODE_STRING      UnicodeFilespec;
	OBJECT_ATTRIBUTES   ObjectAttributes;
	NTSTATUS status;
	HANDLE h;

	printBuffer("saving buffer to disk\n");

	RtlInitUnicodeString(&UnicodeFilespec, L"\\??\\C:\\Windows\\debug\\ZFSin.txt");

	// Attempt to create file, make a weak attempt, give up easily.
	ObjectAttributes.Length = sizeof(OBJECT_ATTRIBUTES);
	ObjectAttributes.RootDirectory = NULL;
	ObjectAttributes.Attributes = /*OBJ_CASE_INSENSITIVE |*/ OBJ_KERNEL_HANDLE;
	ObjectAttributes.ObjectName = &UnicodeFilespec;
	ObjectAttributes.SecurityDescriptor = NULL;
	ObjectAttributes.SecurityQualityOfService = NULL;
	IO_STATUS_BLOCK iostatus;

	status = ZwCreateFile(&h,
		GENERIC_READ | GENERIC_WRITE | SYNCHRONIZE,
		&ObjectAttributes,
		&iostatus,
		0,
		FILE_ATTRIBUTE_NORMAL,
		/* FILE_SHARE_WRITE | */ FILE_SHARE_READ,
		FILE_OVERWRITE_IF,
		FILE_SYNCHRONOUS_IO_NONALERT | FILE_NO_INTERMEDIATE_BUFFERING,
		NULL,
		0);

	if (status != STATUS_SUCCESS) {
		printBuffer("failed to save buffer: 0x%lx\n", status);
		return;
	}

	ZwWriteFile(h, 0, NULL, NULL, &iostatus, cbuf, (ULONG)cbuf_size, NULL, NULL);
	ZwClose(h);
}
