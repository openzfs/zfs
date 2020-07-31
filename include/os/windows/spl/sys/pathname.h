
#ifndef _SPL_PATHNAME_H
#define _SPL_PATHNAME_H

typedef struct pathname {
	char	*pn_buf;		/* underlying storage */
	char	*pn_path;		/* remaining pathname */
	size_t	pn_pathlen;		/* remaining length */
	size_t	pn_bufsize;		/* total size of pn_buf */
} pathname_t;

#endif /* SPL_PATHNAME_H */
