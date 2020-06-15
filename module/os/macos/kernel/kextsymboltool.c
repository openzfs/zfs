/*
 * Copyright (c) 2006 Apple Computer, Inc. All rights reserved.
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
#include <libc.h>
#include <errno.h>
#include <ctype.h>

#include <sys/stat.h>
#include <sys/file.h>
#include <sys/mman.h>

#include <mach-o/arch.h>
#include <mach-o/fat.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <mach-o/swap.h>

#include <uuid/uuid.h>

#include <IOKit/IOTypes.h>

#pragma mark Typedefs, Enums, Constants
/*********************************************************************
* Typedefs, Enums, Constants
*********************************************************************/
typedef enum {
    kErrorNone = 0,
    kError,
    kErrorFileAccess,
    kErrorDiskFull,
    kErrorDuplicate
} ToolError;

#pragma mark Function Protos
/*********************************************************************
* Function Protos
*********************************************************************/
__private_extern__ ToolError
readFile(const char *path, vm_offset_t * objAddr, vm_size_t * objSize);

__private_extern__ ToolError
writeFile(int fd, const void * data, size_t length);

extern char* __cxa_demangle (const char* mangled_name,
				   char* buf,
				   size_t* n,
				   int* status);

#pragma mark Functions
/*********************************************************************
*********************************************************************/
__private_extern__ ToolError
writeFile(int fd, const void * data, size_t length)
{
    ToolError err;

    if (length != (size_t)write(fd, data, length))
        err = kErrorDiskFull;
    else
        err = kErrorNone;

    if (kErrorNone != err)
        perror("couldn't write output");

    return( err );
}

/*********************************************************************
*********************************************************************/
__private_extern__ ToolError
readFile(const char *path, vm_offset_t * objAddr, vm_size_t * objSize)
{
    ToolError err = kErrorFileAccess;
    int fd;
    struct stat stat_buf;

    *objAddr = 0;
    *objSize = 0;

    do
    {
        if((fd = open(path, O_RDONLY)) == -1)
	    continue;

	if(fstat(fd, &stat_buf) == -1)
	    continue;

        if (0 == (stat_buf.st_mode & S_IFREG))
            continue;

       /* Don't try to map an empty file, it fails now due to conformance
        * stuff (PR 4611502).
        */
        if (0 == stat_buf.st_size) {
            err = kErrorNone;
            continue;
        }

	*objSize = stat_buf.st_size;

        *objAddr = (vm_offset_t)mmap(NULL /* address */, *objSize,
            PROT_READ|PROT_WRITE, MAP_FILE|MAP_PRIVATE /* flags */,
            fd, 0 /* offset */);

	if ((void *)*objAddr == MAP_FAILED) {
            *objAddr = 0;
            *objSize = 0;
	    continue;
	}

	err = kErrorNone;

    } while( false );

    if (-1 != fd)
    {
        close(fd);
    }
    if (kErrorNone != err)
    {
        fprintf(stderr, "couldn't read %s: %s\n", path, strerror(errno));
    }

    return( err );
}


enum { kExported = 0x00000001, kObsolete = 0x00000002 };

struct symbol {
    char * name;
    unsigned int name_len;
    char * indirect;
    unsigned int indirect_len;
    unsigned int flags;
    struct symbol * list;
    unsigned int list_count;
};

static bool issymchar( char c )
{
    return ((c > ' ') && (c <= '~') && (c != ':') && (c != '#'));
}

static bool iswhitespace( char c )
{
    return ((c == ' ') || (c == '\t'));
}

/*
 * Function for qsort for comparing symbol list names.
 */
static int
qsort_cmp(const void * _left, const void * _right)
{
    struct symbol * left  = (struct symbol *) _left;
    struct symbol * right = (struct symbol *) _right;

    return (strcmp(left->name, right->name));
}

/*
 * Function for bsearch for finding a symbol name.
 */

static int
bsearch_cmp( const void * _key, const void * _cmp)
{
    char * key = (char *)_key;
    struct symbol * cmp = (struct symbol *) _cmp;

    return(strcmp(key, cmp->name));
}

struct bsearch_key
{
    char * name;
    unsigned int name_len;
};

static int
bsearch_cmp_prefix( const void * _key, const void * _cmp)
{
    struct bsearch_key * key = (struct bsearch_key *)_key;
    struct symbol *      cmp = (struct symbol *) _cmp;

    return(strncmp(key->name, cmp->name, key->name_len));
}

static uint32_t
count_symbols(char * file, vm_size_t file_size)
{
    uint32_t nsyms = 0;
    char *   scan;
    char *   eol;
    char *   next;

    for (scan = file; true; scan = next) {

        eol = memchr(scan, '\n', file_size - (scan - file));
        if (eol == NULL) {
            break;
        }
        next = eol + 1;

       /* Skip empty lines.
        */
        if (eol == scan) {
            continue;
        }

       /* Skip comment lines.
        */
        if (scan[0] == '#') {
            continue;
        }

       /* Scan past any non-symbol characters at the beginning of the line. */
        while ((scan < eol) && !issymchar(*scan)) {
            scan++;
        }

       /* No symbol on line? Move along.
        */
        if (scan == eol) {
            continue;
        }

       /* Skip symbols starting with '.'.
        */
        if (scan[0] == '.') {
            continue;
        }
        nsyms++;
    }

    return nsyms;
}

static uint32_t
store_symbols(char * file, vm_size_t file_size, struct symbol * symbols, uint32_t idx, uint32_t max_symbols)
{
    char *   scan;
    char *   line;
    char *   eol;
    char *   next;

    uint32_t strtabsize;

    strtabsize = 0;

    for (scan = file, line = file; true; scan = next, line = next) {

        char *       name = NULL;
        char *       name_term = NULL;
        unsigned int name_len = 0;
        char *       indirect = NULL;
        char *       indirect_term = NULL;
        unsigned int indirect_len = 0;
        char *       option = NULL;
        char *       option_term = NULL;
        unsigned int option_len = 0;
        char         optionstr[256];
        boolean_t    obsolete = 0;

        eol = memchr(scan, '\n', file_size - (scan - file));
        if (eol == NULL) {
            break;
        }
        next = eol + 1;

       /* Skip empty lines.
        */
        if (eol == scan) {
            continue;
        }

        *eol = '\0';

       /* Skip comment lines.
        */
        if (scan[0] == '#') {
            continue;
        }

       /* Scan past any non-symbol characters at the beginning of the line. */
        while ((scan < eol) && !issymchar(*scan)) {
            scan++;
        }

       /* No symbol on line? Move along.
        */
        if (scan == eol) {
            continue;
        }

       /* Skip symbols starting with '.'.
        */
        if (scan[0] == '.') {
            continue;
        }

        name = scan;

       /* Find the end of the symbol.
        */
        while ((*scan != '\0') && issymchar(*scan)) {
            scan++;
        }

       /* Note char past end of symbol.
        */
        name_term = scan;

       /* Stored length must include the terminating nul char.
        */
        name_len = name_term - name + 1;

       /* Now look for an indirect.
        */
        if (*scan != '\0') {
            while ((*scan != '\0') && iswhitespace(*scan)) {
                scan++;
            }
            if (*scan == ':') {
                scan++;
                while ((*scan != '\0') && iswhitespace(*scan)) {
                    scan++;
                }
                if (issymchar(*scan)) {
                    indirect = scan;

                   /* Find the end of the symbol.
                    */
                    while ((*scan != '\0') && issymchar(*scan)) {
                        scan++;
                    }

                   /* Note char past end of symbol.
                    */
                    indirect_term = scan;

                   /* Stored length must include the terminating nul char.
                    */
                    indirect_len = indirect_term - indirect + 1;

                } else if (*scan == '\0') {
		    fprintf(stderr, "bad format in symbol line: %s\n", line);
		    exit(1);
		}
            } else if (*scan != '\0' && *scan != '-') {
                fprintf(stderr, "bad format in symbol line: %s\n", line);
                exit(1);
            }
        }

        /* Look for options.
         */
        if (*scan != '\0') {
            while ((*scan != '\0') && iswhitespace(*scan)) {
                scan++;
            }

            if (*scan == '-') {
                scan++;

                if (isalpha(*scan)) {
                    option = scan;

                   /* Find the end of the option.
                    */
                    while ((*scan != '\0') && isalpha(*scan)) {
                        scan++;
                    }

                   /* Note char past end of option.
                    */
                    option_term = scan;
                    option_len = option_term - option;

                    if (option_len >= sizeof(optionstr)) {
                        fprintf(stderr, "option too long in symbol line: %s\n", line);
                        exit(1);
                    }
                    memcpy(optionstr, option, option_len);
                    optionstr[option_len] = '\0';

                    /* Find the option.
                     */
                    if (!strncmp(optionstr, "obsolete", option_len)) {
                        obsolete = TRUE;
                    }

                } else if (*scan == '\0') {
		    fprintf(stderr, "bad format in symbol line: %s\n", line);
		    exit(1);
		}

            }

        }

        if(idx >= max_symbols) {
            fprintf(stderr, "symbol[%d/%d] overflow: %s\n", idx, max_symbols, line);
            exit(1);
        }

        *name_term = '\0';
        if (indirect_term) {
            *indirect_term = '\0';
        }

        symbols[idx].name = name;
        symbols[idx].name_len = name_len;
        symbols[idx].indirect = indirect;
        symbols[idx].indirect_len = indirect_len;
        symbols[idx].flags = (obsolete) ? kObsolete : 0;

        strtabsize += symbols[idx].name_len + symbols[idx].indirect_len;
        idx++;
    }

    return strtabsize;
}

/*********************************************************************
*********************************************************************/
int main(int argc, char * argv[])
{
    ToolError	err;
    int			i, fd;
    const char *	output_name = NULL;
    uint32_t		zero = 0, num_files = 0;
    uint32_t		filenum;
    uint32_t		strx, strtabsize, strtabpad;
    struct symbol *	import_symbols;
    struct symbol *	export_symbols;
    uint32_t		num_import_syms, num_export_syms;
    uint32_t		result_count, num_removed_syms;
    uint32_t		import_idx, export_idx;
    const NXArchInfo *	host_arch;
    const NXArchInfo *	target_arch;
    boolean_t		require_imports = true;
    boolean_t		diff = false;


    struct file {
        vm_offset_t  mapped;
        vm_size_t    mapped_size;
	uint32_t     nsyms;
	boolean_t    import;
	const char * path;
    };
    struct file files[64];

    host_arch = NXGetLocalArchInfo();
    target_arch = host_arch;

    for( i = 1; i < argc; i += 2)
    {
	boolean_t import;

        if (!strcmp("-sect", argv[i]))
        {
	    require_imports = false;
	    i--;
	    continue;
        }
        if (!strcmp("-diff", argv[i]))
        {
	    require_imports = false;
	    diff = true;
	    i--;
	    continue;
        }

	if (i == (argc - 1))
	{
	    fprintf(stderr, "bad arguments: %s\n", argv[i]);
	    exit(1);
	}

        if (!strcmp("-arch", argv[i]))
        {
            target_arch = NXGetArchInfoFromName(argv[i + 1]);
	    if (!target_arch)
	    {
		fprintf(stderr, "unknown architecture name: %s\n", argv[i+1]);
		exit(1);
	    }
            continue;
        }
        if (!strcmp("-output", argv[i]))
        {
	    output_name = argv[i+1];
            continue;
        }

        if (!strcmp("-import", argv[i]))
	    import = true;
	else if (!strcmp("-export", argv[i]))
	    import = false;
	else
	{
	    fprintf(stderr, "unknown option: %s\n", argv[i]);
	    exit(1);
	}

        err = readFile(argv[i+1], &files[num_files].mapped, &files[num_files].mapped_size);
        if (kErrorNone != err)
            exit(1);

        if (files[num_files].mapped && files[num_files].mapped_size)
	{
	    files[num_files].import = import;
	    files[num_files].path   = argv[i+1];
            num_files++;
	}
    }

    if (!output_name)
    {
	fprintf(stderr, "no output file\n");
	exit(1);
    }

    num_import_syms = 0;
    num_export_syms = 0;
    for (filenum = 0; filenum < num_files; filenum++)
    {
        files[filenum].nsyms = count_symbols((char *) files[filenum].mapped, files[filenum].mapped_size);
	if (files[filenum].import)
	    num_import_syms += files[filenum].nsyms;
	else
	    num_export_syms += files[filenum].nsyms;
    }
    if (!num_export_syms)
    {
	fprintf(stderr, "no export names\n");
	exit(1);
    }

    import_symbols = calloc(num_import_syms, sizeof(struct symbol));
    export_symbols = calloc(num_export_syms, sizeof(struct symbol));

    import_idx = 0;
    export_idx = 0;

    for (filenum = 0; filenum < num_files; filenum++)
    {
	if (files[filenum].import)
	{
	    store_symbols((char *) files[filenum].mapped, files[filenum].mapped_size,
					import_symbols, import_idx, num_import_syms);
	    import_idx += files[filenum].nsyms;
	}
	else
	{
	    store_symbols((char *) files[filenum].mapped, files[filenum].mapped_size,
					export_symbols, export_idx, num_export_syms);
	    export_idx += files[filenum].nsyms;
	}
	if (false && !files[filenum].nsyms)
	{
	    fprintf(stderr, "warning: file %s contains no names\n", files[filenum].path);
	}
    }


    qsort(import_symbols, num_import_syms, sizeof(struct symbol), &qsort_cmp);
    qsort(export_symbols, num_export_syms, sizeof(struct symbol), &qsort_cmp);

    result_count = 0;
    num_removed_syms = 0;
    strtabsize = 4;
    if (num_import_syms)
    {
	for (export_idx = 0; export_idx < num_export_syms; export_idx++)
	{
	    struct symbol * result;
	    char * name;
	    size_t len;
	    boolean_t wild;

	    name = export_symbols[export_idx].indirect;
	    len  = export_symbols[export_idx].indirect_len;
	    if (!name)
	    {
		name = export_symbols[export_idx].name;
		len  = export_symbols[export_idx].name_len;
	    }
	    wild = ((len > 2) && ('*' == name[len-=2]));
	    if (wild)
	    {
		struct bsearch_key key;
		key.name = name;
		key.name_len = len;
		result = bsearch(&key, import_symbols,
				    num_import_syms, sizeof(struct symbol), &bsearch_cmp_prefix);

		if (result)
		{
		    struct symbol * first;
		    struct symbol * last;

		    strtabsize += (result->name_len + result->indirect_len);

		    first = result;
		    while (--first >= &import_symbols[0])
		    {
			if (bsearch_cmp_prefix(&key, first))
			    break;
			strtabsize += (first->name_len + first->indirect_len);
		    }
		    first++;

		    last = result;
		    while (++last < (&import_symbols[0] + num_import_syms))
		    {
			if (bsearch_cmp_prefix(&key, last))
			    break;
			strtabsize += (last->name_len + last->indirect_len);
		    }
		    result_count += last - first;
		    result = first;
		    export_symbols[export_idx].list = first;
		    export_symbols[export_idx].list_count = last - first;
		    export_symbols[export_idx].flags |= kExported;
		}
	    }
	    else
		result = bsearch(name, import_symbols,
				    num_import_syms, sizeof(struct symbol), &bsearch_cmp);

	    if (!result && require_imports)
	    {
		int status;
		char * demangled_result =
			__cxa_demangle(export_symbols[export_idx].name + 1, NULL, NULL, &status);
		fprintf(stderr, "exported name not in import list: %s\n",
					demangled_result ? demangled_result : export_symbols[export_idx].name);
//		fprintf(stderr, "                                : %s\n", export_symbols[export_idx].name);
		if (demangled_result) {
			free(demangled_result);
		}
		num_removed_syms++;
	    }
	    if (diff)
	    {
		if (!result)
		    result = &export_symbols[export_idx];
		else
		    result = NULL;
	    }
	    if (result && !wild)
	    {
		export_symbols[export_idx].flags |= kExported;
		strtabsize += (export_symbols[export_idx].name_len + export_symbols[export_idx].indirect_len);
		result_count++;
		export_symbols[export_idx].list = &export_symbols[export_idx];
		export_symbols[export_idx].list_count = 1;
	    }
	}
    }
    strtabpad = (strtabsize + 3) & ~3;

    if (require_imports && num_removed_syms)
    {
	err = kError;
	goto finish;
    }

    fd = open(output_name, O_WRONLY|O_CREAT|O_TRUNC, 0755);
    if (-1 == fd)
    {
	perror("couldn't write output");
	err = kErrorFileAccess;
	goto finish;
    }

    struct symtab_command symcmd;
    struct uuid_command uuidcmd;

    symcmd.cmd		= LC_SYMTAB;
    symcmd.cmdsize	= sizeof(symcmd);
    symcmd.symoff	= sizeof(symcmd) + sizeof(uuidcmd);
    symcmd.nsyms	= result_count;
    symcmd.strsize	= strtabpad;

    uuidcmd.cmd         = LC_UUID;
    uuidcmd.cmdsize     = sizeof(uuidcmd);
    uuid_generate(uuidcmd.uuid);

    if (CPU_ARCH_ABI64 & target_arch->cputype)
    {
	struct mach_header_64 hdr;
	hdr.magic	= MH_MAGIC_64;
	hdr.cputype	= target_arch->cputype;
	hdr.cpusubtype	= target_arch->cpusubtype;
	hdr.filetype	= MH_KEXT_BUNDLE;
	hdr.ncmds	= 2;
	hdr.sizeofcmds	= sizeof(symcmd) + sizeof(uuidcmd);
	hdr.flags	= MH_INCRLINK;

	symcmd.symoff	+= sizeof(hdr);
	symcmd.stroff	= result_count * sizeof(struct nlist_64)
				+ symcmd.symoff;

	if (target_arch->byteorder != host_arch->byteorder)
	    swap_mach_header_64(&hdr, target_arch->byteorder);
	err = writeFile(fd, &hdr, sizeof(hdr));
    }
    else
    {
	struct mach_header    hdr;
	hdr.magic	= MH_MAGIC;
	hdr.cputype	= target_arch->cputype;
	hdr.cpusubtype	= target_arch->cpusubtype;
	hdr.filetype	= (target_arch->cputype == CPU_TYPE_I386) ? MH_OBJECT : MH_KEXT_BUNDLE;
	hdr.ncmds	= 2;
	hdr.sizeofcmds	= sizeof(symcmd) + sizeof(uuidcmd);
	hdr.flags	= MH_INCRLINK;

	symcmd.symoff	+= sizeof(hdr);
	symcmd.stroff	= result_count * sizeof(struct nlist)
				+ symcmd.symoff;

	if (target_arch->byteorder != host_arch->byteorder)
	    swap_mach_header(&hdr, target_arch->byteorder);
	err = writeFile(fd, &hdr, sizeof(hdr));
    }

    if (kErrorNone != err)
	goto finish;

    if (target_arch->byteorder != host_arch->byteorder) {
        swap_symtab_command(&symcmd, target_arch->byteorder);
        swap_uuid_command(&uuidcmd, target_arch->byteorder);
    }
    err = writeFile(fd, &symcmd, sizeof(symcmd));
    if (kErrorNone != err)
	goto finish;
    err = writeFile(fd, &uuidcmd, sizeof(uuidcmd));
    if (kErrorNone != err)
        goto finish;

    strx = 4;
    for (export_idx = 0; export_idx < num_export_syms; export_idx++)
    {
	if (!export_symbols[export_idx].name)
	    continue;
	if (!(kExported & export_symbols[export_idx].flags))
	    continue;

	if (export_idx
	  && export_symbols[export_idx - 1].name
	  && !strcmp(export_symbols[export_idx - 1].name, export_symbols[export_idx].name))
	{
	    fprintf(stderr, "duplicate export: %s\n", export_symbols[export_idx - 1].name);
	    err = kErrorDuplicate;
	    goto finish;
	}

	for (import_idx = 0; import_idx < export_symbols[export_idx].list_count; import_idx++)
	{

	    if (export_symbols[export_idx].list != &export_symbols[export_idx])
	    {
		printf("wild: %s, %s\n", export_symbols[export_idx].name,
			export_symbols[export_idx].list[import_idx].name);
	    }
	    if (CPU_ARCH_ABI64 & target_arch->cputype)
	    {
		struct nlist_64 nl;

		nl.n_sect  = 0;
                nl.n_desc  = 0;
		nl.n_un.n_strx = strx;
		strx += export_symbols[export_idx].list[import_idx].name_len;

                if (export_symbols[export_idx].flags & kObsolete) {
                    nl.n_desc |= N_DESC_DISCARDED;
                }

		if (export_symbols[export_idx].list[import_idx].indirect)
		{
		    nl.n_type  = N_INDR | N_EXT;
		    nl.n_value = strx;
		    strx += export_symbols[export_idx].list[import_idx].indirect_len;
		}
		else
		{
		    nl.n_type  = N_UNDF | N_EXT;
		    nl.n_value = 0;
		}

		if (target_arch->byteorder != host_arch->byteorder)
		    swap_nlist_64(&nl, 1, target_arch->byteorder);

		err = writeFile(fd, &nl, sizeof(nl));
	    }
	    else
	    {
		struct nlist nl;

		nl.n_sect  = 0;
		nl.n_desc  = 0;
		nl.n_un.n_strx = strx;
		strx += export_symbols[export_idx].list[import_idx].name_len;

                if (export_symbols[export_idx].flags & kObsolete) {
                    nl.n_desc |= N_DESC_DISCARDED;
                }

		if (export_symbols[export_idx].list[import_idx].indirect)
		{
		    nl.n_type  = N_INDR | N_EXT;
		    nl.n_value = strx;
		    strx += export_symbols[export_idx].list[import_idx].indirect_len;
		}
		else
		{
		    nl.n_type  = N_UNDF | N_EXT;
		    nl.n_value = 0;
		}

		if (target_arch->byteorder != host_arch->byteorder)
		    swap_nlist(&nl, 1, target_arch->byteorder);

		err = writeFile(fd, &nl, sizeof(nl));
	    }
	}

	if (kErrorNone != err)
	    goto finish;
    }

    strx = sizeof(uint32_t);
    err = writeFile(fd, &zero, strx);
    if (kErrorNone != err)
	goto finish;

    for (export_idx = 0; export_idx < num_export_syms; export_idx++)
    {
	if (!export_symbols[export_idx].name)
	    continue;

	for (import_idx = 0; import_idx < export_symbols[export_idx].list_count; import_idx++)
	{
	    err = writeFile(fd, export_symbols[export_idx].list[import_idx].name,
			export_symbols[export_idx].list[import_idx].name_len);
	    if (kErrorNone != err)
		goto finish;
	    if (export_symbols[export_idx].list[import_idx].indirect)
	    {
		err = writeFile(fd, export_symbols[export_idx].list[import_idx].indirect,
			    export_symbols[export_idx].list[import_idx].indirect_len);
		if (kErrorNone != err)
		    goto finish;
	    }
	}
    }

    err = writeFile(fd, &zero, strtabpad - strtabsize);
    if (kErrorNone != err)
	goto finish;

    close(fd);


finish:
    for (filenum = 0; filenum < num_files; filenum++) {
        // unmap file
        if (files[filenum].mapped_size)
        {
            munmap((caddr_t)files[filenum].mapped, files[filenum].mapped_size);
            files[filenum].mapped     = 0;
            files[filenum].mapped_size = 0;
        }

    }

    if (kErrorNone != err)
    {
	if (output_name)
	    unlink(output_name);
        exit(1);
    }
    else
        exit(0);
    return(0);
}

