/*
 *  Copyright (C) 2006, Jon Gettler
 *  http://www.mvpmc.org/
 *
 * Password code from busybox 1.1.3:
 *  Copyright (C) 1999-2004 by Erik Andersen <andersen@codepoet.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <crypt.h>
#include <time.h>
#include <fcntl.h>
#include <signal.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>

#define PWD_BUFFER_SIZE 256

static int i64c(int i)
{
	if (i <= 0)
		return ('.');
	if (i == 1)
		return ('/');
	if (i >= 2 && i < 12)
		return ('0' - 2 + i);
	if (i >= 12 && i < 38)
		return ('A' - 12 + i);
	if (i >= 38 && i < 63)
		return ('a' - 38 + i);
	return ('z');
}

static char *crypt_make_salt(void)
{
	time_t now;
	static unsigned long x;
	static char result[3];

	time(&now);
	x += now + getpid() + clock();
	result[0] = i64c(((x >> 18) ^ (x >> 6)) & 077);
	result[1] = i64c(((x >> 12) ^ x) & 077);
	result[2] = '\0';
	return result;
}

char *pw_encrypt(const char *clear, const char *salt)
{
	static char cipher[128];
	char *cp;

	cp = (char *) crypt(clear, salt);
	strncpy(cipher, cp, sizeof(cipher));
	return cipher;
}

char *
askpass(const char * prompt)
{
	char *ret;
	int i, size;
	struct sigaction sa;
	struct termios old, new;
	static char passwd[PWD_BUFFER_SIZE];

	tcgetattr(STDIN_FILENO, &old);
	tcflush(STDIN_FILENO, TCIFLUSH);

	size = sizeof(passwd);
	ret = passwd;
	memset(passwd, 0, size);

	fputs(prompt, stdout);
	fflush(stdout);

	tcgetattr(STDIN_FILENO, &new);
	new.c_iflag &= ~(IUCLC|IXON|IXOFF|IXANY);
	new.c_lflag &= ~(ECHO|ECHOE|ECHOK|ECHONL|TOSTOP);
	tcsetattr(STDIN_FILENO, TCSANOW, &new);

	if (read(STDIN_FILENO, passwd, size-1) <= 0) {
		ret = NULL;
	} else {
		for(i = 0; i < size && passwd[i]; i++) {
			if (passwd[i]== '\r' || passwd[i] == '\n') {
				passwd[i]= 0;
				break;
			}
		}
	}

	tcsetattr(STDIN_FILENO, TCSANOW, &old);
	fputs("\n", stdout);
	fflush(stdout);
	return ret;
}

static char *
findstr(char *haystack, char *needle, int size)
{
	int n = 0;
	int len = strlen(needle);

	while ((n+len) < size) {
		if (strncmp(haystack+n, needle, len) == 0)
			return haystack+n;
		n++;
	}

	return NULL;
}

static void
print_help(char *prog)
{
	printf("%s [-i file] [-o file]\n", prog);
}

int
main(int argc, char **argv)
{
	char salt[12];
	char *pass1, *pass2;
	char *cp;
	char c;
	char *input = NULL, *output = NULL;
	char *map;
	char *pw;
	int fd, n;
	struct stat sb;

	while ((c=getopt(argc, argv, "hi:o:")) != -1) {
		switch (c) {
		case 'h':
			print_help(argv[0]);
			exit(0);
			break;
		case 'i':
			input = strdup(optarg);
			break;
		case 'o':
			output = strdup(optarg);
			break;
		default:
			print_help(argv[0]);
			exit(1);
			break;
		}
	}

	if ((output == NULL) && (input == NULL)) {
		fprintf(stderr, "No files specified!\n");
		exit(1);
	}
	if (input == NULL) {
		fprintf(stderr, "No input file specified!\n");
		exit(1);
	}

	if (output && (output != input)) {
		char cmd[1024];

		snprintf(cmd, sizeof(cmd), "cp %s %s", input, output);
		if (system(cmd) != 0) {
			fprintf(stderr, "Copy failed!\n");
			exit(1);
		}
		input = output;
	}

	stat(input, &sb);
	if ((fd=open(input, O_RDWR)) < 0) {
		perror(input);
	}
	if ((map=mmap(NULL, sb.st_size, PROT_READ|PROT_WRITE, MAP_SHARED,
		      fd, 0)) == MAP_FAILED) {
		perror("mmap()");
		exit(1);
	}

	if ((pw=findstr(map, "mvpmc_root=", sb.st_size)) == NULL) {
		printf("Password not found in input file!\n");
		exit(1);
	}
	pw += 11;
	n = 0;
	while (pw[n] == 'X') {
		n++;
	}

	memset(salt, 0, sizeof(salt));
	strcpy(salt, "$1$");
	strcat(salt, crypt_make_salt());
	strcat(salt, crypt_make_salt());
	strcat(salt, crypt_make_salt());

	strcat(salt, crypt_make_salt());
	if ((pass1=askpass("Type root password: ")) == NULL) {
		fprintf(stderr, "Invalid password!\n");
		exit(1);
	}
	pass1 = strdup(pass1);
	if ((pass2=askpass("Type root password again: ")) == NULL) {
		fprintf(stderr, "Invalid password!\n");
		exit(1);
	}
	pass2 = strdup(pass2);

	if (strcmp(pass1, pass2) != 0) {
		fprintf(stderr, "Passwords do not match!\n");
		exit(1);
	}

	cp = pw_encrypt(pass1, salt);

	if (strlen(cp) > n) {
		printf("Not enough space to store password!\n");
		exit(1);
	}

	memset(pw, ' ', n);
	memcpy(pw, cp, strlen(cp));

	printf("Done\n");

	return 0;
}
