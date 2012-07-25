#include <stdio.h>

int main ( int argc, char ** argv) {

	if (argc != 3) {
		fprintf(stderr,"usage: <command> --escape <string>\n");
		return 0;
	}

	const char * parm = argv[2];
	char character;
	int counter;

	counter = 0;
	character = parm[counter];
	while (character != '\0') {
		if (character == '/' && counter == 0) printf("");
		else if (character == '/' && counter != 0) printf("-");
		else if (character <= 32 || character == '-') printf("\\x%x",character);
		else printf("%c",character);
		counter++;
		character = parm[counter];
	}
	return 0;

}
