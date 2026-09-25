
#ifndef _SPL_DKIO_H
#define	_SPL_DKIO_H

struct dk_callback {
	void (*dkc_callback)(void *dkc_cookie, int error);
	void *dkc_cookie;
	int dkc_flag;
};

#define	DKIOC			(0x04 << 8)
#define	DKIOCFLUSHWRITECACHE	(DKIOC | 34)
#define	DKIOCTRIM		(DKIOC | 35)

#endif /* _SPL_DKIO_H */
