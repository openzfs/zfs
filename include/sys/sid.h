#ifndef _SPL_SID_H
#define _SPL_SID_H

typedef struct ksiddomain {
	uint_t		kd_ref;
	uint_t		kd_len;
	char		*kd_name;
	avl_node_t	kd_link;
} ksiddomain_t;

static inline ksiddomain_t *
ksid_lookupdomain(const char *dom)
{
        ksiddomain_t *kd;
	int len = strlen(dom);

        kd = kmem_zalloc(sizeof(ksiddomain_t), KM_SLEEP);
        kd->kd_name = kmem_zalloc(len + 1, KM_SLEEP);
	memcpy(kd->kd_name, dom, len);

        return (kd);
}

static inline void
ksiddomain_rele(ksiddomain_t *ksid)
{
	kmem_free(ksid->kd_name, strlen(ksid->kd_name) + 1);
        kmem_free(ksid, sizeof(ksiddomain_t));
}

#endif /* _SPL_SID_H */
