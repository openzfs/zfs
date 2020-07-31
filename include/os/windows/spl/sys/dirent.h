
#ifndef _SPL_DIRENT_H
#define _SPL_DIRENT_H

//#include_next <sys/dirent.h>

#define MAXNAMLEN      255

/*
 * File types
 */
#define DT_UNKNOWN       0
#define DT_FIFO          1
#define DT_CHR           2
#define DT_DIR           4
#define DT_BLK           6
#define DT_REG           8
#define DT_LNK          10
#define DT_SOCK         12
#define DT_WHT          14

struct dirent { 
        uint64_t  d_ino;      /* file number of entry */ 
        uint64_t  d_seekoff;  /* seek offset (optional, used by servers) */ 
        uint16_t  d_reclen;   /* length of this record */ 
        uint16_t  d_namlen;   /* length of string in d_name */ 
        uint8_t   d_type;     /* file type, see below */ 
        char      d_name[MAXPATHLEN]; /* entry name (up to MAXPATHLEN bytes) */ 
};

#define IFTODT(mode)    (((mode) & 0170000) >> 12)
#define DTTOIF(dirtype) ((dirtype) << 12)


#endif /* SPL_DIRENT_H */
