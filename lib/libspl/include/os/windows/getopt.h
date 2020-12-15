#ifndef LIBSPL_GETOPT_H_INCLUDED
#define LIBSPL_GETOPT_H_INCLUDED

#define no_argument             0
#define required_argument       1
#define optional_argument       2

struct option
{
	const char*     name;
	int             has_arg;
	int*            flag;
	int             val;
};

extern int	    getopt(int, char* const*, const char*);
extern int      getopt_long(int, char* const*, const char*, const struct option*, int*);
extern int      getopt_long_only(int, char* const*, const char*, const struct option*, int*);
extern int		getsubopt(char** optionsp, char* tokens[], char** valuep);

#endif // LIBSPL_GETOPT_H_INCLUDED