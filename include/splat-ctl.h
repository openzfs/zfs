#ifndef _SPLAT_CTL_H
#define _SPLAY_CTL_H

#ifdef _KERNEL
#include <asm/ioctls.h>
#include <asm/uaccess.h>
#include <linux/list.h>
#endif /* _KERNEL */

#define KZT_VERSION			"v1.0"
#define KZT_VERSION_SIZE		64

#define KZT_MAJOR			229 /* XXX - Arbitrary */
#define KZT_MINORS                      1
#define KZT_DEV				"/dev/kztctl"

#define KZT_NAME_SIZE			12
#define KZT_DESC_SIZE			60

typedef struct kzt_user {
	char name[KZT_NAME_SIZE];	/* short name */
	char desc[KZT_DESC_SIZE];	/* short description */
	int id;				/* unique numeric id */
} kzt_user_t;

#define	KZT_CFG_MAGIC			0x15263748U
typedef struct kzt_cfg {
	unsigned int cfg_magic;		/* Unique magic */
	int cfg_cmd;			/* Config command */
	int cfg_arg1;			/* Config command arg 1 */
	int cfg_rc1;			/* Config response 1 */
	union {
		struct {
			int size;
			kzt_user_t descs[0];
		} kzt_subsystems;
		struct {
			int size;
			kzt_user_t descs[0];
		} kzt_tests;
	} cfg_data;
} kzt_cfg_t;

#define	KZT_CMD_MAGIC			0x9daebfc0U
typedef struct kzt_cmd {
	unsigned int cmd_magic;		/* Unique magic */
	int cmd_subsystem;		/* Target subsystem */
	int cmd_test;			/* Subsystem test */
	int cmd_data_size;		/* Extra opaque data */
	char cmd_data_str[0];		/* Opaque data region */
} kzt_cmd_t;

/* Valid ioctls */
#define KZT_CFG				_IOWR('f', 101, long)
#define KZT_CMD				_IOWR('f', 102, long)

/* Valid configuration commands */
#define KZT_CFG_BUFFER_CLEAR		0x001	/* Clear text buffer */
#define KZT_CFG_BUFFER_SIZE		0x002	/* Resize text buffer */
#define KZT_CFG_SUBSYSTEM_COUNT		0x101	/* Number of subsystem */
#define KZT_CFG_SUBSYSTEM_LIST		0x102	/* List of N subsystems */
#define KZT_CFG_TEST_COUNT		0x201	/* Number of tests */
#define KZT_CFG_TEST_LIST		0x202	/* List of N tests */

/* Valid subsystem and test commands defined in each subsystem, we do
 * need to be careful to avoid colisions.  That alone may argue to define
 * them all here, for now we just define the global error codes.
 */
#define KZT_SUBSYSTEM_UNKNOWN		0xF00
#define KZT_TEST_UNKNOWN		0xFFF


#ifdef _KERNEL
#define KZT_SUBSYSTEM_INIT(type)                                        \
({      kzt_subsystem_t *_sub_;                                         \
                                                                        \
        _sub_ = (kzt_subsystem_t *)kzt_##type##_init();                 \
        if (_sub_ == NULL) {                                            \
                printk(KERN_ERR "Error initializing: " #type "\n");     \
        } else {                                                        \
                spin_lock(&kzt_module_lock);                            \
                list_add_tail(&(_sub_->subsystem_list), 		\
		              &kzt_module_list);			\
                spin_unlock(&kzt_module_lock);                          \
        }                                                               \
})

#define KZT_SUBSYSTEM_FINI(type)                                        \
({      kzt_subsystem_t *_sub_, *_tmp_;                                 \
        int _id_, _flag_ = 0;                                           \
                                                                        \
	_id_ = kzt_##type##_id();                                       \
        spin_lock(&kzt_module_lock);                                    \
        list_for_each_entry_safe(_sub_, _tmp_,  &kzt_module_list,	\
		                 subsystem_list) { 			\
                if (_sub_->desc.id == _id_) {                           \
                        list_del_init(&(_sub_->subsystem_list));        \
        		spin_unlock(&kzt_module_lock);                  \
                        kzt_##type##_fini(_sub_);                       \
			spin_lock(&kzt_module_lock);			\
                        _flag_ = 1;                                     \
                }                                                       \
        }                                                               \
        spin_unlock(&kzt_module_lock);                                  \
                                                                        \
	if (!_flag_)                                                    \
                printk(KERN_ERR "Error finalizing: " #type "\n");       \
})

#define KZT_TEST_INIT(sub, n, d, tid, func)				\
({      kzt_test_t *_test_;                                             \
                                                                        \
	_test_ = (kzt_test_t *)kmalloc(sizeof(*_test_), GFP_KERNEL);    \
        if (_test_ == NULL) {						\
		printk(KERN_ERR "Error initializing: " n "/" #tid" \n");\
	} else {							\
		memset(_test_, 0, sizeof(*_test_));			\
		strncpy(_test_->desc.name, n, KZT_NAME_SIZE);		\
		strncpy(_test_->desc.desc, d, KZT_DESC_SIZE);		\
		_test_->desc.id = tid;					\
	        _test_->test = func;					\
		INIT_LIST_HEAD(&(_test_->test_list));			\
                spin_lock(&((sub)->test_lock));				\
                list_add_tail(&(_test_->test_list),&((sub)->test_list));\
                spin_unlock(&((sub)->test_lock));			\
        }								\
})

#define KZT_TEST_FINI(sub, tid)						\
({      kzt_test_t *_test_, *_tmp_;                                     \
        int _flag_ = 0;                                          	\
                                                                        \
        spin_lock(&((sub)->test_lock));					\
        list_for_each_entry_safe(_test_, _tmp_,				\
		                 &((sub)->test_list), test_list) {	\
                if (_test_->desc.id == tid) {                           \
                        list_del_init(&(_test_->test_list));		\
                        _flag_ = 1;                                     \
                }                                                       \
        }                                                               \
        spin_unlock(&((sub)->test_lock));				\
                                                                        \
	if (!_flag_)                                                    \
                printk(KERN_ERR "Error finalizing: " #tid "\n");       	\
})

typedef int (*kzt_test_func_t)(struct file *, void *);

typedef struct kzt_test {
	struct list_head test_list;
	kzt_user_t desc;
	kzt_test_func_t test;
} kzt_test_t;

typedef struct kzt_subsystem {
	struct list_head subsystem_list;/* List had to chain entries */
	kzt_user_t desc;
	spinlock_t test_lock;
	struct list_head test_list;
} kzt_subsystem_t;

#define KZT_INFO_BUFFER_SIZE		65536
#define KZT_INFO_BUFFER_REDZONE		256

typedef struct kzt_info {
	spinlock_t info_lock;
	int info_size;
	char *info_buffer;
	char *info_head;	/* Internal kernel use only */
} kzt_info_t;

#define sym2str(sym)			(char *)(#sym)

#define kzt_print(file, format, args...)				\
({	kzt_info_t *_info_ = (kzt_info_t *)file->private_data;		\
	int _rc_;							\
									\
	ASSERT(_info_);							\
	ASSERT(_info_->info_buffer);					\
									\
	spin_lock(&_info_->info_lock);					\
									\
	/* Don't allow the kernel to start a write in the red zone */	\
	if ((int)(_info_->info_head - _info_->info_buffer) > 		\
	    (KZT_INFO_BUFFER_SIZE -KZT_INFO_BUFFER_REDZONE)) {		\
		_rc_ = -EOVERFLOW;					\
	} else {							\
		_rc_ = sprintf(_info_->info_head, format, args);	\
		if (_rc_ >= 0)						\
			_info_->info_head += _rc_;			\
	}								\
									\
	spin_unlock(&_info_->info_lock);				\
	_rc_;								\
})

#define kzt_vprint(file, test, format, args...)				\
	kzt_print(file, "%*s: " format, KZT_NAME_SIZE, test, args)

kzt_subsystem_t * kzt_condvar_init(void);
kzt_subsystem_t * kzt_kmem_init(void);
kzt_subsystem_t * kzt_mutex_init(void);
kzt_subsystem_t * kzt_krng_init(void);
kzt_subsystem_t * kzt_rwlock_init(void);
kzt_subsystem_t * kzt_taskq_init(void);
kzt_subsystem_t * kzt_thread_init(void);
kzt_subsystem_t * kzt_time_init(void);

#endif /* _KERNEL */

#endif /* _SPLAY_CTL_H */
