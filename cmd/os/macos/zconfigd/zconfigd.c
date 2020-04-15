/*
 * Copyright © 2003-2012 Apple Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 *
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this
 * file.
 *
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 *
 * @APPLE_LICENSE_HEADER_END@
 */
/*
 * © Copyright 2001-2002 Apple Inc.  All rights reserved.
 *
 * IMPORTANT:
 * This Apple software is supplied to you by Apple Computer, Inc. (“Apple”)
 * in consideration of your agreement to the following terms, and your use,
 * installation, modification or redistribution of this Apple software
 * constitutes acceptance of these terms.  If you do not agree with these
 * terms, please do not use, install, modify or redistribute this
 * Apple software.
 *
 * In consideration of your agreement to abide by the following terms,
 * and subject to these terms, Apple grants you a personal, non exclusive
 * license, under Apple’s copyrights in this original Apple software
 * (the “Apple Software”), to use, reproduce, modify and redistribute
 * the Apple Software, with or without modifications, in source and/or
 * binary forms; provided that if you redistribute the Apple Software
 * in its entirety and without modifications, you must retain this notice
 * and the following text and disclaimers in all such redistributions
 * of the Apple Software.  Neither the name, trademarks, service marks
 * or logos of Apple Computer, Inc. may be used to endorse or promote
 * products derived from the Apple Software without specific prior written
 * permission from Apple. Except as expressly stated in this notice, no other
 * rights or licenses, express or implied, are granted by Apple herein,
 * including but not limited to any patent rights that may be infringed
 * by your derivative works or by other works in which the Apple Software
 * may be incorporated.
 *
 * The Apple Software is provided by Apple on an "AS IS" basis.
 * APPLE MAKES NO WARRANTIES, EXPRESS OR IMPLIED, INCLUDING WITHOUT
 * LIMITATION THE IMPLIED WARRANTIES OF NON-INFRINGEMENT, MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE, REGARDING THE APPLE SOFTWARE OR
 * ITS USE AND OPERATION ALONE OR IN COMBINATION WITH YOUR PRODUCTS.
 *
 * IN NO EVENT SHALL APPLE BE LIABLE FOR ANY SPECIAL, INDIRECT, INCIDENTAL
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) ARISING IN ANY WAY OUT OF THE USE,
 * REPRODUCTION, MODIFICATION AND/OR DISTRIBUTION OF THE APPLE SOFTWARE,
 * HOWEVER CAUSED AND WHETHER UNDER THEORY OF CONTRACT, TORT (INCLUDING
 * NEGLIGENCE), STRICT LIABILITY OR OTHERWISE, EVEN IF APPLE HAS BEEN
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Much of this file is a modified version of Apple's USB Notification Example:
 * http://www.opensource.apple.com/source/IOUSBFamily/IOUSB
 * Family-630.4.5/Examples/USBNotification%20Example/main.c
 */

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
 * Copyright 2015 OpenZFS on OS X. All rights reserved.
 */

#include <unistd.h>
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <mach/mach.h>
#include <sys/stat.h>

#include "zconfigd.h"

// globals
static IONotificationPortRef gNotifyPort;
static io_iterator_t gKextLoadedIter;

static void
SignalHandler(int sigraised)
{
	(void) sigraised;
	fprintf(stderr, "\nInterrupted\n");

	// Clean up here
	IONotificationPortDestroy(gNotifyPort);

	if (gKextLoadedIter) {
		IOObjectRelease(gKextLoadedIter);
		gKextLoadedIter = 0;
	}

	fflush(stdout);
	fflush(stderr);

	// exit(0) should not be called from a signal handler. Use _exit(0)
	// instead.
	//
	_exit(0);
}

static void
ZFSKextLoaded(void *refCon, io_iterator_t iterator)
{
	io_service_t myservice;
	Boolean doAction = FALSE;
	struct stat sbuf;
	(void) refCon;

	while ((myservice = IOIteratorNext(iterator))) {
		fprintf(stderr, "Found match\n");
		doAction = TRUE;
		IOObjectRelease(myservice);
	}
	if (doAction && stat(ZSYSCTL_CONF_FILE, &sbuf) == 0) {
		fprintf(stderr, "Running "ZSYSCTL_CMD_WITH_ARGS"\n");
		system(ZSYSCTL_CMD_WITH_ARGS);
	}

	fflush(stdout);
	fflush(stderr);
}

int
main(int argc, const char *argv[])
{
	mach_port_t masterPort;
	CFMutableDictionaryRef matchingDict;
	CFRunLoopSourceRef runLoopSource;
	kern_return_t kr;
	sig_t oldHandler;

	(void) argc;
	(void) argv;

	// Set up a signal handler so we can clean up when we're interrupted
	// from the command line. Otherwise we stay in our run loop forever.
	oldHandler = signal(SIGINT, SignalHandler);
	if (oldHandler == SIG_ERR)
		fprintf(stderr, "Could not establish new signal handler");

	// first create a master_port for my task
	kr = IOMasterPort(MACH_PORT_NULL, &masterPort);
	if (kr || !masterPort) {
		fprintf(stderr, "ERR: Couldn't create a master IOKit "
		    "Port(%08x)\n", kr);
		return (-1);
	}

	fprintf(stderr, "Looking for service matching %s\n",
	    kNetLundmanZfsZvol);

	// Set up the matching criteria for the service we're interested in
	matchingDict = IOServiceNameMatching(kNetLundmanZfsZvol);
	if (!matchingDict) {
		fprintf(stderr, "Can't create a %s matching dictionary\n",
		    kNetLundmanZfsZvol);
		mach_port_deallocate(mach_task_self(), masterPort);
		return (-1);
	}

	// Create a notification port and add its run loop event source to our
	// run loop. This is how async notifications get set up.
	gNotifyPort = IONotificationPortCreate(masterPort);
	runLoopSource = IONotificationPortGetRunLoopSource(gNotifyPort);

	CFRunLoopAddSource(CFRunLoopGetCurrent(), runLoopSource,
	    kCFRunLoopDefaultMode);

	// Now set up a notification to be called when zfs.kext loads
	kr = IOServiceAddMatchingNotification(gNotifyPort,
	    kIOFirstMatchNotification, matchingDict, ZFSKextLoaded, NULL,
	    &gKextLoadedIter);

	// Iterate once to get already-present services and arm the notification
	ZFSKextLoaded(NULL, gKextLoadedIter);

	// Now done with the master_port
	mach_port_deallocate(mach_task_self(), masterPort);
	masterPort = 0;

	fprintf(stderr, "Starting the run loop\n");

	fflush(stdout);
	fflush(stderr);

	// Start the run loop. Now we'll receive notifications.
	CFRunLoopRun();

	// We should never get here
	return (0);
}
