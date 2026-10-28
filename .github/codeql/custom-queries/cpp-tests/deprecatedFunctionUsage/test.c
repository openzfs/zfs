typedef unsigned long size_t;
typedef long time_t;

struct tm;

char *strtok(char *, const char *);
char *__xpg_basename(char *);
char *basename(char *);
char *dirname(char *);
void bcopy(const void *, void *, size_t);
int bcmp(const void *, const void *, size_t);
void bzero(void *, size_t);
char *asctime(const struct tm *);
char *asctime_r(const struct tm *, char *);
struct tm *gmtime(const time_t *);
struct tm *localtime(const time_t *);
char *strncpy(char *, const char *, size_t);

char *strtok_r(char *, const char *, char **);
char *zfs_basename(char *);
size_t zfs_dirnamelen(const char *);
void *memcpy(void *, const void *, size_t);
void *memmove(void *, const void *, size_t);
int memcmp(const void *, const void *, size_t);
void *memset(void *, int, size_t);
size_t strftime(char *, size_t, const char *, const struct tm *);
struct tm *gmtime_r(const time_t *, struct tm *);
struct tm *localtime_r(const time_t *, struct tm *);
size_t strlcpy(char *, const char *, size_t);
void strtok_copy(void);
void localtime_safe(void);

static char *
tokenize(char *text, const char *separators)
{
	return (strtok(text, separators));
}

void
deprecated_calls(char *text, char *path, void *dst, const void *src,
    size_t size, const struct tm *tm, time_t *time, char *buffer)
{
	(void) strtok(text, ":");
	(void) __xpg_basename(path);
	(void) basename(path);
	(void) dirname(path);
	bcopy(src, dst, size);
	(void) bcmp(src, dst, size);
	bzero(dst, size);
	(void) asctime(tm);
	(void) asctime_r(tm, buffer);
	(void) gmtime(time);
	(void) localtime(time);
	(void) strncpy(buffer, text, size);
}

void
replacement_calls(char *text, char *path, void *dst, const void *src,
    size_t size, const struct tm *tm, time_t *time, char *buffer,
    char **state, struct tm *result)
{
	(void) strtok_r(text, ":", state);
	(void) zfs_basename(path);
	(void) zfs_dirnamelen(path);
	(void) memcpy(dst, src, size);
	(void) memmove(dst, src, size);
	(void) memcmp(src, dst, size);
	(void) memset(dst, 0, size);
	(void) strftime(buffer, size, "%s", tm);
	(void) gmtime_r(time, result);
	(void) localtime_r(time, result);
	(void) strlcpy(buffer, text, size);
	(void) tokenize(text, ":");
	strtok_copy();
	localtime_safe();
}
