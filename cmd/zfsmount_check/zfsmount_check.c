#include <stdio.h>
#include <string.h>
#include <stdlib.h>


	/*
	 * A side-effect free way of reading /proc/mounts and reporting if
	 * directory argument is mounted.  For use with zfs snapshots,
	 * since the commands mountpoint and sync, force automount behaviour.
	 */



int main(int argc, char ** argv) {

char * mounts = "/proc/mounts";
FILE * in = NULL;
char * targ = NULL;
char * line = NULL;
size_t read, len;
char dev[2048], path[2048], vfs[2048];


/*
 * We take one argument, if no arg, report failed (0).
 * and fail silently.
 * If mounted report 1.
 */

if (argc > 1) {
	targ = (char *)malloc(strlen(argv[1])+1);
	strncpy(targ, argv[1], strlen(argv[1]));

	in = fopen(mounts, "r");
	if (in == NULL)
		exit(0);

	while ((read = getline(&line, &len, in)) != -1) {
	/* fprintf(stderr,"Retrieved line of length %zu :\n", read); */
		sscanf(line, "%s%s%s", dev, path, vfs);
		/*  fprintf(stderr,"dev=%s path=%s vfs=%s \n",dev,path,vfs); */
		if (strcmp(vfs, "zfs") == 0)
			if (strncmp(path, targ, strlen(path)) == 0) {
	/* fprintf(stderr,"found match to %s with %s \n", targ, path); */
			exit(1);
		}
	} /* end while */
	fclose(in);
	exit(0);
} else {
exit(0);
}
/* end main */
}
