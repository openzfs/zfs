// SPDX-License-Identifier: 0BSD

#undef NDEBUG
#include <assert.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef RENAME_NOREPLACE
static void makeff(int in, size_t sz, struct stat *madeff) {
	int ff = openat(in, "from",
	    O_WRONLY | O_CREAT | O_TRUNC | O_EXCL | O_CLOEXEC, 0644);
	assert(write(ff, "from", sz) == sz);
	fstat(ff, madeff);
	close(ff);
}

int main(int argc, const char **argv) {
	int from = argc > 1 ?
	    open(argv[1], O_PATH | O_DIRECTORY | O_CLOEXEC) : AT_FDCWD;
	int to   = argc > 2 ?
	    open(argv[2], O_PATH | O_DIRECTORY | O_CLOEXEC) : AT_FDCWD;
	assert(from != -1 && to != -1);

	struct stat ffbuf, frombuf, tobuf;
	makeff(from, 3, &ffbuf);
	fstatat(from, "from", &frombuf, 0);
	assert(!memcmp(&ffbuf, &frombuf, sizeof (struct stat)));
	assert(!renameat2(from, "from", to, "to", RENAME_NOREPLACE));
	assert(!fstatat(to, "to", &tobuf, 0));
	assert(!memcmp(&ffbuf, &tobuf, sizeof (struct stat)));

	struct stat ffbuf2, frombuf2, from2buf2, tobuf2;
	makeff(from, 4, &ffbuf2);
	fstatat(from, "from", &frombuf2, 0);
	assert(!memcmp(&ffbuf2, &frombuf2, sizeof (struct stat)));
	assert(memcmp(&ffbuf, &ffbuf2, sizeof (struct stat)));
	assert(renameat2(from, "from", to, "to", RENAME_NOREPLACE) == -1 &&
	    errno == EEXIST);
	assert(!fstatat(from, "from", &from2buf2, 0));
	assert(!memcmp(&frombuf2, &from2buf2, sizeof (struct stat)));
	assert(!fstatat(to, "to", &tobuf2, 0));
	assert(!memcmp(&tobuf, &tobuf2, sizeof (struct stat)));
}
#else
int main(void) {
	abort();
}
#endif
