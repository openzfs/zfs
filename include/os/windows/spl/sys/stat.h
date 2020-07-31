#ifndef _SPL_STAT_H
#define _SPL_STAT_H

//#include_next <sys/stat.h>

#ifndef S_IFMT
/* File type */
#define S_IFMT          0170000         /* [XSI] type of file mask */
#define S_IFIFO         0010000         /* [XSI] named pipe (fifo) */
#define S_IFCHR         0020000         /* [XSI] character special */
#define S_IFDIR         0040000         /* [XSI] directory */
#define S_IFBLK         0060000         /* [XSI] block special */
#define S_IFREG         0100000         /* [XSI] regular */
#define S_IFLNK         0120000         /* [XSI] symbolic link */
#define S_IFSOCK        0140000         /* [XSI] socket */
#if !defined(_POSIX_C_SOURCE) 
#define S_IFWHT         0160000         /* OBSOLETE: whiteout */
#endif
/* File mode */
/* Read, write, execute/search by owner */
#define S_IRWXU         0000700         /* [XSI] RWX mask for owner */
#define S_IRUSR         0000400         /* [XSI] R for owner */
#define S_IWUSR         0000200         /* [XSI] W for owner */
#define S_IXUSR         0000100         /* [XSI] X for owner */
/* Read, write, execute/search by group */
#define S_IRWXG         0000070         /* [XSI] RWX mask for group */
#define S_IRGRP         0000040         /* [XSI] R for group */
#define S_IWGRP         0000020         /* [XSI] W for group */
#define S_IXGRP         0000010         /* [XSI] X for group */
/* Read, write, execute/search by others */
#define S_IRWXO         0000007         /* [XSI] RWX mask for other */
#define S_IROTH         0000004         /* [XSI] R for other */
#define S_IWOTH         0000002         /* [XSI] W for other */
#define S_IXOTH         0000001         /* [XSI] X for other */

#define S_ISUID         0004000         /* [XSI] set user id on execution */
#define S_ISGID         0002000         /* [XSI] set group id on execution */
#define S_ISVTX         0001000         /* [XSI] directory restrcted delete */

#if !defined(_POSIX_C_SOURCE) 
#define S_ISTXT         S_ISVTX         /* sticky bit: not supported */
#define S_IREAD         S_IRUSR         /* backward compatability */
#define S_IWRITE        S_IWUSR         /* backward compatability */
#define S_IEXEC         S_IXUSR         /* backward compatability */
#endif
#endif  /* !S_IFMT */

/*
 * [XSI] The following macros shall be provided to test whether a file is
 * of the specified type.  The value m supplied to the macros is the value
 * of st_mode from a stat structure.  The macro shall evaluate to a non-zero
 * value if the test is true; 0 if the test is false.
 */
#define S_ISBLK(m)      (((m)& S_IFMT) == S_IFBLK)     /* block special */
#define S_ISCHR(m)      (((m)& S_IFMT) == S_IFCHR)     /* char special */
#define S_ISDIR(m)      (((m)& S_IFMT) == S_IFDIR)     /* directory */
#define S_ISFIFO(m)     (((m)& S_IFMT) == S_IFIFO)     /* fifo or socket */
#define S_ISREG(m)      (((m)& S_IFMT) == S_IFREG)     /* regular file */
#define S_ISLNK(m)      (((m)& S_IFMT) == S_IFLNK)     /* symbolic link */
#define S_ISSOCK(m)     (((m)& S_IFMT) == S_IFSOCK)    /* socket */
#if !defined(_POSIX_C_SOURCE) || defined(_DARWIN_C_SOURCE)
#define S_ISWHT(m)      (((m)& S_IFMT) == S_IFWHT)     /* OBSOLETE: whiteout */
#endif

#endif /* SPL_STAT_H */
