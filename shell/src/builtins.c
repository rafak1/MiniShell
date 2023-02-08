#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <signal.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>

#include "builtins.h"

int echo(char *[]);
int undefined(char *[]);

int lkill(char *[]);
int lexit(char *[]);
int lcd(char *[]);
int lls(char *[]);



builtin_pair builtins_table[]={
	{"exit",	&lexit},
	{"lecho",	&echo},
	{"lcd",		&lcd},
	{"lkill",	&lkill},
	{"lls",		&lls},
	{NULL,NULL}
};


int
lexit(char * argv[]){
	exit(0);
	return -1;
}

int
lcd(char * argv[]){
	int error;
	if(argv[1]==NULL)  error= chdir(getenv("HOME"));
	else{
		if(argv[2]!=NULL) return -1;
		error = chdir(argv[1]);
	}

	return error;
}

int
lkill(char * argv[]){
	errno = 0;
	if(argv[1]==NULL) return -1;
	long temp_signal = SIGTERM;
	int signal;
	char * endptr = "not null";
	long temp_pid;
	pid_t pid;

	if(argv[2] == NULL){
		temp_pid = strtol(argv[1], &endptr, 10);
		if(endptr == argv[1] || errno !=0 || *endptr!='\0') return -1;
	}else{
		temp_signal = strtol(argv[1], &endptr, 10);
		if(endptr == argv[1] || errno !=0 || *endptr!='\0') return -1;
		endptr = "not null";
		temp_signal*=-1;
		temp_pid = strtol(argv[2], &endptr, 10);
		if(endptr == argv[2] || errno !=0 || *endptr!='\0') return -1;
	}

	if(temp_pid < INT_MAX && temp_pid > INT_MIN){
		pid = temp_pid;
	}else return -1;

	if(temp_signal < INT_MAX && temp_signal > INT_MIN){
			signal = temp_signal;
	}else return -1;

	return kill(pid, signal);
}

int
lls(char * argv[]){
	DIR * dir;

	if(argv[1]){
		if(argv[2]!=NULL) return -1;
		dir = opendir(argv[1]);
	}else dir = opendir(".");

	struct dirent* a;

	while( (a = readdir(dir)) !=  NULL){
		if(a->d_name[0] != '.') printf("%s\n", a->d_name);
	}
	fflush(stdout);
	closedir(dir);
	return 0;
}


int 
echo( char * argv[])
{
	int i =1;
	if (argv[i]) printf("%s", argv[i++]);
	while  (argv[i])
		printf(" %s", argv[i++]);

	printf("\n");
	fflush(stdout);
	return 0;
}

int 
undefined(char * argv[])
{
	fprintf(stderr, "Command %s undefined.\n", argv[0]);
	return BUILTIN_ERROR;
}
