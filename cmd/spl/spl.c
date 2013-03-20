/*****************************************************************************\
 *  Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 *  UCRL-CODE-235197
 *
 *  This file is part of the SPL, Solaris Porting Layer.
 *  For details, see <http://zfsonlinux.org/>.
 *
 *  The SPL is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  The SPL is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with the SPL.  If not, see <http://www.gnu.org/licenses/>.
 *****************************************************************************
 *  Solaris Porting Layer (SPL) User Space Interface.
\*****************************************************************************/

#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include "../include/spl-ctl.h"

static int spl_debug_mask = ~0;
static int spl_debug_subsystem = ~0;

/* all strings nul-terminated; only the struct and hdr need to be freed */
struct dbg_line {
        struct spl_debug_header *hdr;
        char *file;
        char *fn;
        char *text;
};

static int
cmp_rec(const void *p1, const void *p2)
{
        struct dbg_line *d1 = *(struct dbg_line **)p1;
        struct dbg_line *d2 = *(struct dbg_line **)p2;

        if (d1->hdr->ph_sec < d2->hdr->ph_sec)
                return -1;

        if (d1->hdr->ph_sec == d2->hdr->ph_sec &&
            d1->hdr->ph_usec < d2->hdr->ph_usec)
                return -1;

        if (d1->hdr->ph_sec == d2->hdr->ph_sec &&
            d1->hdr->ph_usec == d2->hdr->ph_usec)
                return 0;

        return 1;
}

static void
print_rec(struct dbg_line **linev, int used, FILE *out)
{
        int i;

        for (i = 0; i < used; i++) {
                struct dbg_line *line = linev[i];
                struct spl_debug_header *hdr = line->hdr;

                fprintf(out, "%08x:%08x:%u:%u.%06llu:%u:%u:%u:(%s:%u:%s()) %s",
                        hdr->ph_subsys, hdr->ph_mask, hdr->ph_cpu_id,
                        hdr->ph_sec, (unsigned long long)hdr->ph_usec,
                        hdr->ph_stack, hdr->ph_pid, hdr->ph_stack, line->file,
                        hdr->ph_line_num, line->fn, line->text);
                free(line->hdr);
                free(line);
        }

        free(linev);
}

static int
add_rec(struct dbg_line *line, struct dbg_line ***linevp, int *lenp, int used)
{
        struct dbg_line **linev = *linevp;

        if (used == *lenp) {
                int nlen = *lenp + 512;
                int nsize = nlen * sizeof(struct dbg_line *);

                linev = *linevp ? realloc(*linevp, nsize) : malloc(nsize);
                if (!linev)
                        return 0;
                *linevp = linev;
                *lenp = nlen;
        }
        linev[used] = line;
        return 1;
}

static int
parse_buffer(FILE *in, FILE *out)
{
        struct dbg_line *line;
        struct spl_debug_header *hdr;
        char buf[4097], *p;
        unsigned long dropped = 0, kept = 0;
        struct dbg_line **linev = NULL;
        const int phl = sizeof(hdr->ph_len);
        const int phf = sizeof(hdr->ph_flags);
        int rc, linev_len = 0;

        while (1) {
                rc = fread(buf, phl + phf, 1, in);
                if (rc <= 0)
                        break;

                hdr = (void *)buf;
                if (hdr->ph_len == 0)
                        break;
                if (hdr->ph_len > 4094) {
                        fprintf(stderr, "unexpected large record: %d bytes.  "
                                "aborting.\n", hdr->ph_len);
                        break;
                }

                rc = fread(buf + phl + phf, 1, hdr->ph_len - phl - phf, in);
                if (rc <= 0)
                        break;

                if (hdr->ph_mask &&
                    (!(spl_debug_subsystem & hdr->ph_subsys) ||
                     (!(spl_debug_mask & hdr->ph_mask)))) {
                        dropped++;
                        continue;
                }

                line = malloc(sizeof(*line));
                if (line == NULL) {
                        fprintf(stderr, "malloc failed; printing accumulated "
                                "records and exiting.\n");
                        break;
                }

                line->hdr = malloc(hdr->ph_len + 1);
                if (line->hdr == NULL) {
                        free(line);
                        fprintf(stderr, "malloc failed; printing accumulated "
                                "records and exiting.\n");
                        break;
                }

                p = (void *)line->hdr;
                memcpy(line->hdr, buf, hdr->ph_len);
                p[hdr->ph_len] = '\0';

                p += sizeof(*hdr);
                line->file = p;
                p += strlen(line->file) + 1;
                line->fn = p;
                p += strlen(line->fn) + 1;
                line->text = p;

                if (!add_rec(line, &linev, &linev_len, kept)) {
                        fprintf(stderr, "malloc failed; printing accumulated "
                                "records and exiting.\n");
                        break;
                }
                kept++;
        }

        if (linev) {
                qsort(linev, kept, sizeof(struct dbg_line *), cmp_rec);
                print_rec(linev, kept, out);
        }

        printf("Debug log: %lu lines, %lu kept, %lu dropped.\n",
                dropped + kept, kept, dropped);
        return 0;
}

int
main(int argc, char *argv[])
{
        int fdin, fdout;
        FILE *in, *out = stdout;
        int rc, o_lf = 0;

        if (argc > 3 || argc < 2) {
                fprintf(stderr, "usage: %s <input> [output]\n", argv[0]);
                return 0;
        }

#ifdef __USE_LARGEFILE64
        o_lf = O_LARGEFILE;
#endif

        fdin = open(argv[1], O_RDONLY | o_lf);
        if (fdin == -1) {
                fprintf(stderr, "open(%s) failed: %s\n", argv[1],
                        strerror(errno));
                return 1;
        }
        in = fdopen(fdin, "r");
        if (in == NULL) {
                fprintf(stderr, "fopen(%s) failed: %s\n", argv[1],
                        strerror(errno));
                close(fdin);
                return 1;
        }
        if (argc > 2) {
                fdout = open(argv[2], O_CREAT | O_TRUNC | O_WRONLY | o_lf, 0600);
                if (fdout == -1) {
                        fprintf(stderr, "open(%s) failed: %s\n", argv[2],
                                strerror(errno));
                        fclose(in);
                        return 1;
                }
                out = fdopen(fdout, "w");
                if (out == NULL) {
                        fprintf(stderr, "fopen(%s) failed: %s\n", argv[2],
                                strerror(errno));
                        fclose(in);
                        close(fdout);
                        return 1;
                }
        }

        rc = parse_buffer(in, out);

        fclose(in);
        if (out != stdout)
                fclose(out);

        return rc;
}
