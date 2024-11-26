/**
UNIX Shell Project

Sistemas Operativos
Grados I. Informatica, Computadores & Software
Dept. Arquitectura de Computadores - UMA

Some code adapted from "Fundamentos de Sistemas Operativos", Silberschatz et al.

To compile and run the program:
   $ gcc Shell_project.c job_control.c -o Shell
   $ ./Shell          
	(then type ^D to exit program)

**/

#include "job_control.h"   // remember to compile with module job_control.c 
#include <string.h>

#define MAX_LINE 256 /* 256 chars per line, per command, should be enough. */

// -----------------------------------------------------------------------
//                            MAIN          
// -----------------------------------------------------------------------

int main(int argc, char *argv[], char *env[])
{
	char inputBuffer[MAX_LINE]; /* buffer to hold the command entered */
	int background;             /* equals 1 if a command is followed by '&' */
	char *args[MAX_LINE/2];     /* command line (of 256) has max of 128 arguments */
	// probably useful variables:
	int group; //grupo para añadir procesos
	int pid_fork, pid_wait; /* pid for created and waited process */
	int status;             /* status returned by wait */
	enum status status_res; /* status processed by analyze_status() */
	int info;				/* info processed by analyze_status() */

	while (1)   /* Program terminates normally inside get_command() after ^D is typed*/
	{   		
		ignore_terminal_signals();
		printf("COMMAND->");
		fflush(stdout);
		get_command(inputBuffer, MAX_LINE, args, &background);  /* get next command */
		
		if(args[0]==NULL) continue;   // if empty command

		if (strcmp(args[0], "hola")==0){
			printf("que tal?\n");
			continue;
		}

		printf("Argumento 0 : %s\n", args[0]);

		pid_fork = fork();
		switch(pid_fork){
			case -1: perror("fork");
					 continue;
			case 0: /*Child*/
				if (!background){
					group = pid_fork = getpid();
					setpgid(pid_fork, group);
					tcsetpgrp(STDIN_FILENO, group);
				}
				restore_terminal_signals();
				execvp(args[0], args);
				perror(args[0]);
				exit(EXIT_FAILURE);	
				break;
			default:
				if (!background){
					group = pid_fork;
					setpgid(pid_fork, group);//Cambia PID a un nuevo grupo. Como cambiamos al child, para ponerlo al grupo del padre
					tcsetpgrp(STDIN_FILENO, group);
					pid_wait = waitpid(pid_fork, &status, WUNTRACED);
					tcsetpgrp(STDIN_FILENO, getpgid(getpid()));
					if (WIFEXITED(status)){
						/*teminó con exit()*/
						/*WEXITSTATUS(status)*/
						printf("\nForeground pid: %d, command: %s, %d\n", pid_fork, args[0], WEXITSTATUS(status));
					}else if(WIFSIGNALED(status)){
						/*terminó por una señal*/
						/*WTERMSIG(status)*/
						printf("\nForeground pid: %d, command: %s, %d\n", pid_fork, args[0], WTERMSIG(status));
					}else if (WCOREDUMP(status)){
						
					}else if(WIFSTOPPED(status)){
						/*stopped*/
						/*WSTOPSIG(status)*/

					}else if (WIFCONTINUED(status)){
						/*never reached*/

					}else{
						/*pid_wait == -1*/
					}
					
					
				}else{
					printf("\nBackground job running... pid: %d , command: %s\n", pid_fork, args[0]);
				}
				
				break;


		}
		/* the steps are:
			 (1) fork a child process using fork()
			 (2) the child process will invoke execvp()
			 (3) if background == 0, the parent will wait, otherwise continue 
			 (4) Shell shows a status message for processed command 
			 (5) loop returns to get_commnad() function
		*/

	} // end while
}
