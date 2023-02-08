#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include "config.h"
#include "siparse.h"
#include "utils.h"
#include "builtins.h"
#include <stdbool.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>

void
print_errors(int error, const command* com);

int
execute_command(const command* com, char** arguments);

bool 
move_buffer(char* buf, char* current, int *max);

void
parse_command(const command* com ,char** arguments);

int
number_of_arguments(const command* com);

bool 
check_and_exec_builtins(char** arguments);

int
change_io(command* com);

void 
change_pipes(commandseq* curr, commandseq* start, int * pipefd, int * pipe_to_close);

bool 
check_for_pipe_errors(pipelineseq* curr_pipeseq);

bool
exec_pipeline(pipelineseq* curr_pipeseq);

void 
sig_handler(int signum);

int 
safe_read(int start, char* buf, int end);

bool 
is_fg(int pid);

void
print_ended_bg_processes();

void
block_signal(int singum);

void 
unblock_signal(int signum);


volatile int fg_count = 0;
volatile int fg_proc[MAX_LINE_LENGTH];
volatile int bg_tw_count = 0;
volatile int bg_to_write[NOPBACKGROUND][2];

//unblock sigchild na readzie

int
main(int argc, char *argv[])
{
	fg_count = 0;
	bg_tw_count = 0;
	block_signal(SIGCHLD);
	/* set up a handler */
	struct sigaction act;
	act.sa_handler = sig_handler;
	sigemptyset(&(act.sa_mask));
	act.sa_flags = 0;
	sigaction(SIGCHLD, &act, NULL);
	signal(SIGINT, SIG_IGN);


	pipelineseq * ln;

	char buf[MAX_LINE_LENGTH+3];

	bool resized_buff = false;
	bool syntax_error_flag = false;
	bool read_error_flag = false;

	int max = 1;
	char* current;

	struct stat status;	
	fstat(0, &status);

	while (max && !read_error_flag) {
		max = 1;
		current=buf;
		if(S_ISCHR(status.st_mode)){
			print_ended_bg_processes();
			write(1, PROMPT_STR, 2);
		}

		resized_buff = false;

		max = safe_read(0, buf, MAX_LINE_LENGTH+1);
		
		while(current < buf + max && !read_error_flag){
			buf[max+1] = '\0';
			if((max > MAX_LINE_LENGTH && S_ISCHR(status.st_mode)) || max == -1 ){
				ln = NULL;
			}else{
				char* eol;
				if(*current == '\0') {
					eol = NULL; 
					current+=1;
				}
				else{
					eol = strchr(current, '\n');
				}
				/*if eol not found in an outside file*/
				if(eol == NULL && !S_ISCHR(status.st_mode)){
					if(max < MAX_LINE_LENGTH ){
						
						int add_max;

						add_max = safe_read(0, buf + max, MAX_LINE_LENGTH+1 - max );

						if(add_max == -1){
							read_error_flag = true;
							break;
						}
						max += add_max;
						continue;
					}else if(resized_buff){
						/*eol not found even after filling up the whole buffer*/
						ln = NULL;
						syntax_error_flag = true;
						break;
					}else{
						read_error_flag = move_buffer(buf, current, &max);
						current=buf;
						if(read_error_flag) break;
						resized_buff = true;
						continue;
					}
				}else{
					if(syntax_error_flag){
						current = (char*) (eol + 1);
						syntax_error_flag = false;
						fputs(SYNTAX_ERROR_STR, stderr);
						fputc('\n', stderr);
						continue;
					}
				 	*(eol) = '\0';
					resized_buff = false;
				}

				if(!syntax_error_flag) ln = parseline(current);
				current = (char*) (eol + 1);
			}
		
			if (ln == NULL) {
				fputs(SYNTAX_ERROR_STR, stderr);
				fputc('\n', stderr);
			} else {

				pipelineseq* curr_pipeseq = ln;
				pipelineseq* start_pipeseq = ln;

				bool is_pipeseq = true;

				do{	
					
					is_pipeseq = exec_pipeline(curr_pipeseq);

					curr_pipeseq = curr_pipeseq->next;
				}while(is_pipeseq &&  curr_pipeseq != start_pipeseq);
			}
		}
	}

	return 0;
}

/**
 * @brief cheks if user tries to execute a built in command
 * 
 * @param arguments 
 * @return true 
 * @return false 
 */
bool 
check_and_exec_builtins(char** arguments){
	int i=0;
	while(true){
		if(builtins_table[i].name == NULL) break;
		if(strcmp(arguments[0], builtins_table[i].name) == 0){
			int error = builtins_table[i].fun(arguments);
			if(error!=0){
				fprintf(stderr ,"Builtin %s error.\n" , arguments[0]);
			}
			return true;
		}
		i++;
	}
	return false;
}


/**
 * @brief moves buffer to the beginnig and reads the rest
 * 
 * @param buf buffer in question
 * @param current pointer to a place in the buffer after the last executed command
 * @param max current size of the buffer
 * @return true if a read error occured
 */
bool 
move_buffer(char* buf, char* current, int *max){
	int len_curr = (buf + MAX_LINE_LENGTH+1 - current);

	memmove(buf, current, len_curr);
	do{
		errno = 0;
		*max = safe_read(0, buf+len_curr, MAX_LINE_LENGTH+1 - len_curr);
	}while(*max == -1 && errno == EINTR);
	if(*max==-1) return true;
	*max+=len_curr;
	if(*max < MAX_LINE_LENGTH+1) buf[*max] = 0;
	return false;
}



/**
 * @brief parses input and executes command
 * 
 * @param com command in question
 * @param arguments arguments for command
 * @return int returns the return value of execvp
 */
int
execute_command(const command* com , char** arguments){
	unblock_signal(SIGCHLD);
 	int retv = execvp(com->args->arg, arguments); 
	block_signal(SIGCHLD);
	return retv;
}

/**
 * @brief parses command into an array of strings
 * 
 * @param com command in question
 * @param arguments where the arguments will be stored
 * @return char* arguments of the command
 */
void 
parse_command(const command* com , char** arguments){				

	/* change command arguements into an array of strings */
	argseq* curr = com->args->next;
	argseq* start = com->args;
	arguments[0] = start->arg;
	int i = 1;
	while (curr != start) {
		arguments[i++] = curr->arg;
		curr = curr->next;
	}			
	arguments[i] = NULL;

	return;
}

/**
 * @brief calulates number of arguments
 * 
 * @param com command
 * @return int number of arguments
 */
int
number_of_arguments(const command* com){
	int arg_n = 1;
	argseq* curr ;
	curr = com->args->next;
	argseq* start = com->args;
	while (curr != start){
		arg_n++;
		curr = curr->next;
	}
	return arg_n+2;
}


/**
 * @brief prints error from erno of execvp
 * 
 * @param error return value of execvp
 * @param com command which caused an error
 */
void
print_errors(int error, const command* com){
	if (error!=0){
		if (errno == EACCES) {
			fprintf(stderr,"%s: permission denied\n",  com->args->arg);
		}
		else if (errno == ENOENT) {
			fprintf(stderr,"%s: no such file or directory\n",  com->args->arg);
		} else {
			fprintf(stderr,"%s: exec error\n",  com->args->arg);
		}
		exit(EXEC_FAILURE);
	}
}

/**
 * @brief changes input/output/error according to command
 * 
 * @param com command
 */
int
change_io(command* com){
	errno = 0;
	if(com->redirs == NULL) return 0;
	redirseq* curr; 
	curr = com->redirs;
	redirseq* start; 
	start = com->redirs;
	do{
		FILE* error_f;
		if(IS_RIN(curr->r->flags)){
			error_f = freopen(curr->r->filename, "r", stdin);
		}else if(IS_ROUT(curr->r->flags)){
			error_f = freopen(curr->r->filename, "w", stdout);
		}else if(IS_RAPPEND(curr->r->flags)){
			error_f = freopen(curr->r->filename, "a", stdout);
		}
		if(error_f == NULL){
			if (errno == EACCES) {
				fprintf(stderr,"%s: permission denied\n",  curr->r->filename);
			}
			else if (errno == ENOENT) {
				fprintf(stderr,"%s: no such file or directory\n",  curr->r->filename);
			}
			return -1;
		}
		curr = curr->next;
	}while(curr != start);
	return 0;
}


void 
change_pipes(commandseq* curr, commandseq* start, int *pipefd, int* pipe_to_close){
	if(curr == start){	//first in pipe
		int dup_err = dup2( pipefd[1], STDOUT_FILENO);
		if(dup_err == -1) perror("");
	}else if (curr->next == start){	//last in pipe
		int dup_err = dup2( pipefd[0], STDIN_FILENO);
		if(dup_err == -1) perror("");
	}else{	//int the middle of the pipe
		int dup_err = dup2(pipefd[1], STDOUT_FILENO);
		if(dup_err == -1) perror("");
		dup_err = dup2(pipe_to_close[0], STDIN_FILENO);
		if(dup_err == -1) perror("");
	}
	close(pipefd[0]);
	close(pipefd[1]);
}


bool 
check_for_pipe_errors(pipelineseq* curr_pipeseq){
	commandseq* curr = curr_pipeseq->pipeline->commands;
	commandseq* start = curr_pipeseq->pipeline->commands;

	do{
		if(curr->com == NULL) return false;
		curr = curr->next;
	}while(curr != start);
	return true;
}


/**
 * @brief executes a given pipeline
 * 
 * @param curr_pipeseq 
 * @return true on succes
 * @return false on error
 */
bool
exec_pipeline(pipelineseq* curr_pipeseq){

	command* com;

	commandseq* curr = curr_pipeseq->pipeline->commands;
	commandseq* start = curr_pipeseq->pipeline->commands;

	if ( curr->next != start && !check_for_pipe_errors(curr_pipeseq)) {
		fputs(SYNTAX_ERROR_STR, stderr);
		fputc('\n', stderr);
	}
	bool pipe_loop = false;

	if(start->next != start){
		com = curr->com;
		pipe_loop = true;
	}else{
		pipe_loop = false;
		com = pickfirstcommand(curr_pipeseq);
	}

	bool is_comseq = true;
	int pipefd[2];
	int pipe_to_close[2];
	bool is_pipe_init =false;
	bool to_close_pipes = false;
	bool to_change_pipes = false;

	do{
		if(pipe_loop) com = curr->com;

		if(is_pipe_init){
			pipe_to_close[0] = pipefd[0];
			pipe_to_close[1] = pipefd[1];
			to_close_pipes = true;
		}

		if(pipe_loop && curr->next!=start){

			int pipe_err = pipe(pipefd);
			to_change_pipes = true;
			is_pipe_init = true;
			if(pipe_err !=0){
				is_comseq = false;
				return false;
			}
		}else if(pipe_loop && curr->next ==start) to_change_pipes = true;

		if(com==NULL) {
			is_comseq = false;
			return false;
		}

		int size = number_of_arguments(com);

		char* arguments[size];
		parse_command(com, arguments);
		
		bool is_built_in = false;
		if( arguments[0]!=NULL ) is_built_in = check_and_exec_builtins(arguments);
		if(is_built_in) {
			is_comseq = false;
			return false;
		}


		pid_t fork_value = fork();

		if(fork_value == 0){

			if(curr_pipeseq->pipeline->flags != INBACKGROUND){
				signal(SIGINT, SIG_DFL);
			}

			if(to_change_pipes) change_pipes(curr, start, pipefd, pipe_to_close);
			if(pipe_loop && to_close_pipes){
				close(pipe_to_close[0]); 
				close(pipe_to_close[1]);
			}

			errno = 0;

			if(change_io(com) == -1) exit(-1);

			errno = 0;
			int error = 0;

			error = execute_command(com, arguments);

			print_errors(error,com);

			fflush(stdout);
			fflush(stderr);

			exit(0);
		} else {

			if(curr_pipeseq->pipeline->flags != INBACKGROUND){
				fg_proc[fg_count++] = fork_value;
			}

			int status = 0;

			/* wait for children to die*/
			if(!pipe_loop){
				sigset_t empty_ss;
				if(sigemptyset(&empty_ss) == -1) return false;
				while(fg_count != 0) {
					sigsuspend(&empty_ss);
				}
			}
			else if(to_close_pipes){
				close(pipe_to_close[0]); 
				close(pipe_to_close[1]);
			}
		}
		
		curr = curr->next;
	}while(is_comseq && curr != start);

	if(pipe_loop){
		sigset_t empty_ss;
		if(sigemptyset(&empty_ss) == -1) return false;
		sigemptyset(&empty_ss);
		while(fg_count != 0) {
			sigsuspend(&empty_ss);
		}
	}
	return true;
}

void 
sig_handler(int signum){
	pid_t pid = 1;
	while(pid!=-1 && pid!=0){
		int wstatus = 0;
		pid = waitpid(-1, &wstatus, WNOHANG);
		if(pid!=-1 && pid!=0 && !is_fg(pid)){
			bg_to_write[bg_tw_count][0] = pid;
			bg_to_write[bg_tw_count++][1] = wstatus;
		}
	}
}

/**
 * @brief safley reades into buffer
 * 
 * @param start file descriptor 
 * @param buf buffer
 * @param end size of buffer
 * @return int number of read charachters
 */
int 
safe_read(int start, char* buf, int end){
	unblock_signal(SIGCHLD);
	int max;
	do{
		errno = 0;
		max = read(start, buf, end);
	}while(max == -1 && errno == EINTR);
	return max;
	block_signal(SIGCHLD);
}

/**
 * @brief check if given process is running in foregorund, or background
 * 
 * @param pid pid of given process
 * @return true if is foreground
 * @return false else
 */
bool
is_fg(int pid){
	for(int i=0;i<fg_count;i++){
		if(pid == fg_proc[i]){
			for(int j = i; j < fg_count -1; j++) fg_proc[j] = fg_proc[j + 1];
			fg_count--;
			return true;
		}
	}
	return false;
}

/**
 * @brief prints all the procceses that has ended in the background
 * 
 */
void
print_ended_bg_processes(){
	for(int i=0;i<bg_tw_count;i++){
		if(WIFSIGNALED(bg_to_write[i][1])){
			printf("Background process %d terminated. (killed by signal %d)\n", bg_to_write[i][0], WTERMSIG(bg_to_write[i][1]));
		}else{
			printf("Background process %d terminated. (exited with status %d)\n", bg_to_write[i][0], WEXITSTATUS(bg_to_write[i][1]));
		}
	}
	bg_tw_count = 0;
}

/**
 * @brief adds the signum to the set of blocked signals
 * 
 * @param signum 
 */
void
block_signal(int signum){
	sigset_t signal_set;
	sigemptyset(&signal_set);
	sigaddset(&signal_set, signum);
	sigprocmask(SIG_BLOCK, &signal_set, NULL);
}

/**
 * @brief removes signum from the set of blocked signals
 * 
 * @param signum 
 */
void 
unblock_signal(int signum){
	sigset_t signal_set;
	sigemptyset(&signal_set);
	sigaddset(&signal_set, signum);
	sigprocmask(SIG_UNBLOCK, &signal_set, NULL );
}