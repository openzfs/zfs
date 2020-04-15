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
 * Copyright (c) 2016, Brendon Humphrey (brendon.humphrey@mac.com).
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include "disks_private.h"
#include <CoreFoundation/CoreFoundation.h>

static int out_pipe[2], err_pipe[2];
static CFMutableDictionaryRef diskutil_cs_info_cache = NULL;
static CFMutableDictionaryRef diskutil_info_cache = NULL;

static Boolean
CFDictionaryValueIfPresentMatchesSubstring(CFDictionaryRef dict,
    CFStringRef key, CFStringRef substr)
{
	Boolean ret = false;
	CFStringRef existing;
	if (dict &&
	    CFDictionaryGetValueIfPresent(dict, key,
	    (const void **)&existing)) {
		CFRange range = CFStringFind(existing, substr,
		    kCFCompareCaseInsensitive);
		if (range.location != kCFNotFound)
			ret = true;
	}
	return (ret);
}

static int
run_command(char *argv[], int *out_length)
{
	pid_t pid;
	int status = 0;
	struct stat out_stat;

	pipe(out_pipe); // create a pipe
	pipe(err_pipe);
	pid = fork();

	if (pid == 0) {
		close(out_pipe[0]);
		close(err_pipe[0]);
		dup2(out_pipe[1], STDOUT_FILENO);
		dup2(err_pipe[1], STDERR_FILENO);

		execv(argv[0], argv);
	}

	// Parent
	close(out_pipe[1]);
	close(err_pipe[1]);
	waitpid(pid, &status, 0);

	fstat(out_pipe[0], &out_stat);

	*out_length = (int)out_stat.st_size;

	return (status);
}

static void
read_buffers(char *out_buffer, int out_length)
{
	out_buffer[read(out_pipe[0], out_buffer, out_length)] = 0;
}

void
diskutil_init()
{
	diskutil_info_cache = CFDictionaryCreateMutable(NULL, 0,
	    &kCFCopyStringDictionaryKeyCallBacks,
	    &kCFTypeDictionaryValueCallBacks);

	diskutil_cs_info_cache = CFDictionaryCreateMutable(NULL, 0,
	    &kCFCopyStringDictionaryKeyCallBacks,
	    &kCFTypeDictionaryValueCallBacks);
}

void
diskutil_fini()
{
	CFRelease(diskutil_cs_info_cache);
	CFRelease(diskutil_info_cache);
}

void
init_diskutil_info(DU_Info *info)
{
	info = NULL;
}

int
diskutil_info_valid(DU_Info info)
{
	return (info != NULL);
}

void
get_diskutil_cs_info(char *slice, DU_Info *info)
{
	int status = 0;
	int out_length = 0;
	const char *cc[] = { "/usr/sbin/diskutil", "cs", "info", "-plist",
	    slice, NULL};
	char *output = NULL;
	CFPropertyListRef plist = NULL;
	CFStringRef slice_str = CFStringCreateWithCString(NULL,
	    slice, kCFStringEncodingUTF8);

	if (CFDictionaryGetValueIfPresent(diskutil_cs_info_cache, slice_str,
	    (const void **)&plist)) {
		*info = (DU_Info)plist;
	} else {
		*info = NULL;
		status = run_command((char **)cc, &out_length);

		if (WIFEXITED(status) && (WEXITSTATUS(status) == 0)) {
			output = (char *)malloc(out_length);

			if (output) {
				read_buffers(output, out_length);

				CFErrorRef err;
				CFDataRef bytes = CFDataCreate(NULL,
				    (const unsigned char *)(output),
				    strlen(output));

				if (bytes) {
					plist =
					    CFPropertyListCreateWithData(NULL,
					    bytes, kCFPropertyListImmutable,
					    NULL, &err);

					if (plist)
						CFDictionaryAddValue(
						    diskutil_cs_info_cache,
						    slice_str, plist);

					*info = (DU_Info)plist;
				}

				free(output);
			}
		}
	}

	CFRelease(slice_str);
}

void
get_diskutil_info(char *slice, DU_Info *info)
{
	int status = 0;
	int out_length = 0;
	const char *cc[] = {"/usr/sbin/diskutil", "info", "-plist",
	    slice, NULL};
	char *output = NULL;
	CFPropertyListRef plist = NULL;
	CFStringRef slice_str = CFStringCreateWithCString(NULL, slice,
	    kCFStringEncodingUTF8);

	if (CFDictionaryGetValueIfPresent(diskutil_info_cache, slice_str,
	    (const void **)&plist)) {
		*info = (DU_Info)plist;
	} else {
		*info = NULL;
		status = run_command((char **)cc, &out_length);

		if (WIFEXITED(status) && (WEXITSTATUS(status) == 0)) {
			output = (char *)malloc(out_length);

			if (output) {
				read_buffers(output, out_length);

				CFErrorRef err;
				CFDataRef bytes = CFDataCreate(NULL,
				    (const unsigned char *)(output),
				    strlen(output));

				if (bytes) {
					plist =
					    CFPropertyListCreateWithData(NULL,
					    bytes, kCFPropertyListImmutable,
					    NULL, &err);

					if (plist)
						CFDictionaryAddValue(
						    diskutil_info_cache,
						    slice_str, plist);

					*info = (DU_Info)plist;
				}

				free(output);
			}
		} else {
			*info = NULL;
		}

	}

	CFRelease(slice_str);
}

int
is_cs_converted(DU_Info *info)
{
	return CFDictionaryValueIfPresentMatchesSubstring(
	    (CFDictionaryRef)info,
	    CFSTR("CoreStorageLogicalVolumeConversionState"),
	    CFSTR("Complete"));
}

int
is_cs_locked(DU_Info *info)
{
	return CFDictionaryValueIfPresentMatchesSubstring(
	    (CFDictionaryRef)info,
	    CFSTR("CoreStorageLogicalVolumeStatus"),
	    CFSTR("Locked"));
}

int
is_cs_online(DU_Info *info)
{
	return CFDictionaryValueIfPresentMatchesSubstring(
	    (CFDictionaryRef)info,
	    CFSTR("CoreStorageLogicalVolumeStatus"),
	    CFSTR("Online"));
}

CFStringRef
get_cs_LV_status(DU_Info *info)
{
	CFStringRef existing = NULL;

	if (info &&
	    CFDictionaryGetValueIfPresent((CFDictionaryRef)info,
	    CFSTR("CoreStorageLogicalVolumeStatus"),
	    (const void **)&existing)) {
		return (existing);
	}

	return (NULL);
}

int
is_cs_logical_volume(DU_Info *info)
{
	return CFDictionaryValueIfPresentMatchesSubstring(
	    (CFDictionaryRef)info,
	    CFSTR("CoreStorageRole"),
	    CFSTR("LV"));
}

int
is_cs_physical_volume(DU_Info *info)
{
	return CFDictionaryValueIfPresentMatchesSubstring(
	    (CFDictionaryRef)info,
	    CFSTR("CoreStorageRole"),
	    CFSTR("PV"));
}

int
is_cs_disk(DU_Info *info)
{
	return (is_cs_logical_volume(info) || is_cs_physical_volume(info));
}

int
is_efi_partition(DU_Info info)
{
	return CFDictionaryValueIfPresentMatchesSubstring(
	    (CFDictionaryRef)info,
	    CFSTR("Content"),
	    CFSTR("EFI"));
}

int
is_recovery_partition(DU_Info info)
{
	return CFDictionaryValueIfPresentMatchesSubstring(
	    (CFDictionaryRef)info,
	    CFSTR("Content"),
	    CFSTR("Apple_Boot"));
}

int
is_APFS_partition(DU_Info info)
{
	return CFDictionaryValueIfPresentMatchesSubstring(
	    (CFDictionaryRef)info,
	    CFSTR("Content"),
	    CFSTR("Apple_APFS"));
}

int
is_HFS_partition(DU_Info info)
{
	return CFDictionaryValueIfPresentMatchesSubstring(
	    (CFDictionaryRef)info,
	    CFSTR("Content"),
	    CFSTR("Apple_HFS"));
}

int
is_MSDOS_partition(DU_Info info)
{
	return CFDictionaryValueIfPresentMatchesSubstring(
	    (CFDictionaryRef)info,
	    CFSTR("Content"),
	    CFSTR("Microsoft Basic Data"));
}

int
is_whole_disk(DU_Info info)
{
	int ret = 0;
	Boolean is_whole = false;
	if (info &&
	    CFDictionaryGetValueIfPresent((CFDictionaryRef)info,
	    CFSTR("WholeDisk"),
	    (const void **)&is_whole)) {
		ret = is_whole;
	}

	return (ret);
}

int
has_filesystem_type(DU_Info info)
{
	return (info &&
	    CFDictionaryContainsKey((CFDictionaryRef)info,
	    CFSTR("FilesystemType")));
}

CFStringRef
get_filesystem_type(DU_Info info)
{
	CFStringRef existing = NULL;

	if (info &&
	    CFDictionaryGetValueIfPresent((CFDictionaryRef)info,
	    CFSTR("FilesystemType"),
	    (const void **)&existing)) {
		return (existing);
	}

	return (NULL);
}
