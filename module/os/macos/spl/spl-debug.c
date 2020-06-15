#include <sys/sysmacros.h>



/* Debug log support enabled */
__attribute__((noinline)) int assfail(const char *str, const char *file,
	unsigned int line) __attribute__((optnone))
{
	return (1); /* Must return true for ASSERT macro */
}
