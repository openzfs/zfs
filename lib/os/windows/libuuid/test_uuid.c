/*
 * tst_uuid.c --- test program from the UUID library
 *
 * Copyright (C) 1996, 1997, 1998 Theodore Ts'o.
 *
 * %Begin-Header%
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, and the entire permission notice in its entirety,
 *    including the disclaimer of warranties.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE, ALL OF
 * WHICH ARE HEREBY DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF NOT ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 * %End-Header%
 */

#ifdef _WIN32
#define _WIN32_WINNT 0x0500
#include <windows.h>
#define UUID MYUUID
#endif

#include <stdio.h>
#include <stdlib.h>

#include "uuid.h"

static int test_uuid(const char * uuid, int isValid)
{
	static const char * validStr[2] = {"invalid", "valid"};
	uuid_t uuidBits;
	int parsedOk;

	parsedOk = uuid_parse(uuid, uuidBits) == 0;

	printf("%s is %s", uuid, validStr[isValid]);
	if (parsedOk != isValid) {
		printf(" but uuid_parse says %s\n", validStr[parsedOk]);
		return 1;
	}
	printf(", OK\n");
	return 0;
}

#ifdef __GNUC__
#define ATTR(x) __attribute__(x)
#else
#define ATTR(x)
#endif

int
main(int argc ATTR((unused)) , char **argv ATTR((unused)))
{
	uuid_t		buf, tst;
	char		str[100];
	struct timeval	tv;
	time_t		time_reg;
	unsigned char	*cp;
	int i;
	int failed = 0;
	int type, variant;

	uuid_generate(buf);
	uuid_unparse(buf, str);
	printf("UUID generate = %s\n", str);
	printf("UUID: ");
	for (i=0, cp = (unsigned char *) &buf; i < 16; i++) {
		printf("%02x", *cp++);
	}
	printf("\n");
	type = uuid_type(buf); 	variant = uuid_variant(buf);
	printf("UUID type = %d, UUID variant = %d\n", type, variant);
	if (variant != UUID_VARIANT_DCE) {
		printf("Incorrect UUID Variant; was expecting DCE!\n");
		failed++;
	}
	printf("\n");

	uuid_generate_random(buf);
	uuid_unparse(buf, str);
	printf("UUID random string = %s\n", str);
	printf("UUID: ");
	for (i=0, cp = (unsigned char *) &buf; i < 16; i++) {
		printf("%02x", *cp++);
	}
	printf("\n");
	type = uuid_type(buf); 	variant = uuid_variant(buf);
	printf("UUID type = %d, UUID variant = %d\n", type, variant);
	if (variant != UUID_VARIANT_DCE) {
		printf("Incorrect UUID Variant; was expecting DCE!\n");
		failed++;
	}
	if (type != 4) {
		printf("Incorrect UUID type; was expecting "
		       "4 (random type)!\n");
		failed++;
	}
	printf("\n");

	uuid_generate_time(buf);
	uuid_unparse(buf, str);
	printf("UUID string = %s\n", str);
	printf("UUID time: ");
	for (i=0, cp = (unsigned char *) &buf; i < 16; i++) {
		printf("%02x", *cp++);
	}
	printf("\n");
	type = uuid_type(buf); 	variant = uuid_variant(buf);
	printf("UUID type = %d, UUID variant = %d\n", type, variant);
	if (variant != UUID_VARIANT_DCE) {
		printf("Incorrect UUID Variant; was expecting DCE!\n");
		failed++;
	}
	if (type != 1) {
		printf("Incorrect UUID type; was expecting "
		       "1 (time-based type)!\\n");
		failed++;
	}
	tv.tv_sec = 0;
	tv.tv_usec = 0;
	time_reg = uuid_time(buf, &tv);
	printf("UUID time is: (%ld, %ld): %s\n", tv.tv_sec, tv.tv_usec,
	       ctime(&time_reg));
	uuid_parse(str, tst);
	if (!uuid_compare(buf, tst))
		printf("UUID parse and compare succeeded.\n");
	else {
		printf("UUID parse and compare failed!\n");
		failed++;
	}
	uuid_clear(tst);
	if (uuid_is_null(tst))
		printf("UUID clear and is null succeeded.\n");
	else {
		printf("UUID clear and is null failed!\n");
		failed++;
	}
	uuid_copy(buf, tst);
	if (!uuid_compare(buf, tst))
		printf("UUID copy and compare succeeded.\n");
	else {
		printf("UUID copy and compare failed!\n");
		failed++;
	}
	failed += test_uuid("84949cc5-4701-4a84-895b-354c584a981b", 1);
	failed += test_uuid("84949CC5-4701-4A84-895B-354C584A981B", 1);
	failed += test_uuid("84949cc5-4701-4a84-895b-354c584a981bc", 0);
	failed += test_uuid("84949cc5-4701-4a84-895b-354c584a981", 0);
	failed += test_uuid("84949cc5x4701-4a84-895b-354c584a981b", 0);
	failed += test_uuid("84949cc504701-4a84-895b-354c584a981b", 0);
	failed += test_uuid("84949cc5-470104a84-895b-354c584a981b", 0);
	failed += test_uuid("84949cc5-4701-4a840895b-354c584a981b", 0);
	failed += test_uuid("84949cc5-4701-4a84-895b0354c584a981b", 0);
	failed += test_uuid("g4949cc5-4701-4a84-895b-354c584a981b", 0);
	failed += test_uuid("84949cc5-4701-4a84-895b-354c584a981g", 0);

	if (failed) {
		printf("%d failures.\n", failed);
		exit(1);
	}
	return 0;
}
