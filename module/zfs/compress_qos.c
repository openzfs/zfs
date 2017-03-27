#include <sys/zio.h>
#include <sys/compress_qos.h>
#include <sys/zio_compress.h>
#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/vdev_impl.h>
#include <sys/dmu_objset.h>

enum zio_compress qos_compression[QOS_COMPESS_LEVELS] = {
	ZIO_COMPRESS_LZ4,
	ZIO_COMPRESS_GZIP_1,
	ZIO_COMPRESS_GZIP_2,
	ZIO_COMPRESS_GZIP_3,
	ZIO_COMPRESS_GZIP_4,
	ZIO_COMPRESS_GZIP_5,
	ZIO_COMPRESS_GZIP_6,
	ZIO_COMPRESS_GZIP_7,
	ZIO_COMPRESS_GZIP_8,
	ZIO_COMPRESS_GZIP_9 };

enum zio_compress qos_compress_select(zio_t *zio, int wanted_throughput) {
	enum zio_compress res = ZIO_COMPRESS_LZ4;
	wanted_throughput += 5;
	zio_t *pio = DMU_META_DNODE(zio->io_prop.zp_os)->dn_zio;

	if (pio != NULL) {
		uint64_t trans = 1000; // transform to mb/s
		uint8_t next_level = pio->io_compress_level;
		uint64_t compress_time = (gethrtime() - pio->io_qos_timestamp);
		uint64_t exp_pipespeed_avg = (pio->io_qos_lsize * trans)
			/ compress_time;
		zio->io_temp_parent = pio;

		if (exp_pipespeed_avg) {
			if (exp_pipespeed_avg < wanted_throughput) {
				if (next_level > 0) {
					next_level--;
				}
			} else if (exp_pipespeed_avg > wanted_throughput) {
				if (next_level < QOS_COMPESS_LEVELS - 1) {
					next_level++;
				}
			}
			zio->io_compress_level = next_level;
		}
		res = qos_compression[next_level];
	}
	return (res);
}

void qos_update(zio_t *zio, uint64_t psize) {

	zio_t *pio = zio->io_temp_parent;

	if (pio != NULL) {
		mutex_enter(&pio->io_lock);
		pio->io_qos_size += psize;
		pio->io_qos_lsize += zio->io_lsize;
		pio->io_compress_level = zio->io_compress_level;
		mutex_exit(&pio->io_lock);
	}
}

size_t qos_compress(zio_t *zio, enum zio_compress *c, abd_t *src, void *dst,
    size_t s_len) {
	size_t psize;

	switch (*c) {
	case ZIO_COMPRESS_QOS_10:
		*c = qos_compress_select(zio, 10);
		break;
	case ZIO_COMPRESS_QOS_20:
		*c = qos_compress_select(zio, 20);
		break;
	case ZIO_COMPRESS_QOS_30:
		*c = qos_compress_select(zio, 30);
		break;
	case ZIO_COMPRESS_QOS_40:
		*c = qos_compress_select(zio, 40);
		break;
	case ZIO_COMPRESS_QOS_50:
		*c = qos_compress_select(zio, 50);
		break;
	case ZIO_COMPRESS_QOS_100:
		*c = qos_compress_select(zio, 100);
		break;
	case ZIO_COMPRESS_QOS_150:
		*c = qos_compress_select(zio, 150);
		break;
	case ZIO_COMPRESS_QOS_200:
		*c = qos_compress_select(zio, 200);
		break;
	case ZIO_COMPRESS_QOS_250:
		*c = qos_compress_select(zio, 250);
		break;
	case ZIO_COMPRESS_QOS_300:
		*c = qos_compress_select(zio, 300);
		break;
	case ZIO_COMPRESS_QOS_350:
		*c = qos_compress_select(zio, 350);
		break;
	case ZIO_COMPRESS_QOS_400:
		*c = qos_compress_select(zio, 400);
		break;
	case ZIO_COMPRESS_QOS_450:
		*c = qos_compress_select(zio, 450);
		break;
	case ZIO_COMPRESS_QOS_500:
		*c = qos_compress_select(zio, 500);
		break;
	case ZIO_COMPRESS_QOS_550:
		*c = qos_compress_select(zio, 550);
		break;
	case ZIO_COMPRESS_QOS_600:
		*c = qos_compress_select(zio, 600);
		break;
	case ZIO_COMPRESS_QOS_650:
		*c = qos_compress_select(zio, 650);
		break;
	case ZIO_COMPRESS_QOS_700:
		*c = qos_compress_select(zio, 700);
		break;
	case ZIO_COMPRESS_QOS_750:
		*c = qos_compress_select(zio, 750);
		break;
	case ZIO_COMPRESS_QOS_800:
		*c = qos_compress_select(zio, 800);
		break;
	case ZIO_COMPRESS_QOS_850:
		*c = qos_compress_select(zio, 850);
		break;
	case ZIO_COMPRESS_QOS_900:
		*c = qos_compress_select(zio, 900);
		break;
	case ZIO_COMPRESS_QOS_950:
		*c = qos_compress_select(zio, 950);
		break;
	case ZIO_COMPRESS_QOS_1000:
		*c = qos_compress_select(zio, 1000);
		break;
	default:
		break; // error
	}

	psize = zio_compress_data(*c, src, dst, s_len);
	qos_update(zio, psize);
	return (psize);
}
