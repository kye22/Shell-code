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


job * job_list; //Lista de tareas de procesos, para tener información de procesos en background
// -----------------------------------------------------------------------
//                            Manejador de childs          
// -----------------------------------------------------------------------

void child_handler (int s)
{
	block_SIGCHLD();
	int pid_wait;
	int status;
	int info;
	job *current_job;
	enum status status_res; //enum status define valores de posibles estaods de procesos tras waitpid
	while (1){
		pid_wait = waitpid(-1, &status, WNOHANG | WUNTRACED | 8 ); // WCONTINUED se define como 8 en waitflags.h, por algún motivo a mi no me funciona

		if (pid_wait == 0) {
			return; /*no hay cambio del proceso que envia la señal*/
		}
		if (pid_wait == -1) {
			return; /*error*/
		}
		//analizamos estado del proceso
		status_res = analyze_status(status, &info);

		//buscamos el proceso en la lista, por su PID
		current_job= get_item_bypid(job_list, pid_wait);

		if (current_job != NULL){
			switch (status_res){
				case EXITED:
					/*teminó con exit()*/
					printf("\n Background pid: %d, command : %s, Exited, info: %d\n", pid_wait, current_job->command, info); 
					delete_job(job_list, current_job);
					break;
				case SIGNALED:
					printf("\n Background pid: %d, command : %s, Signaled, info: %d\n", pid_wait, current_job->command, info); 
					delete_job(job_list, current_job);
					break;
				case SUSPENDED:
					printf("\n Background pid: %d, command : %s, Suspended, info: %d\n", pid_wait, current_job->command, info); 
					current_job->state = STOPPED;
					break;
				case CONTINUED:
					printf("\n Background pid: %d, command : %s, Continued\n", pid_wait, current_job->command); 
					current_job->state = BACKGROUND;
					break;
			}
		}
	}
	unblock_SIGCHLD();
}

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
	sigset_t mySet;
	job *njob;        /* Creamos variable job para el siguiente*/
	job_list = new_list("Job List");


	while (1)   /* Program terminates normally inside get_command() after ^D is typed*/
	{   		
		ignore_terminal_signals();
		signal(SIGCHLD, child_handler);
		printf("COMMAND-> ");
		fflush(stdout);
		get_command(inputBuffer, MAX_LINE, args, &background);  /* get next command */
		
		if(args[0]==NULL) continue;   // if empty command

		if (strcmp(args[0], "hola")==0){
			printf("que tal?\n");
			continue;
		} 
		if (strcmp(args[0], "quit") == 0){
			break;
		}
		if (strcmp(args[0], "maskCHLD") == 0){
			block_SIGCHLD();
			continue;
		}
		if (strcmp(args[0], "unmaskCHLD") == 0){
			unblock_SIGCHLD();
			continue;
		} 
		if (strcmp(args[0], "jobs")==0){
			print_job_list(job_list); /*mostramos tareas*/
			continue;
		}
		if (strcmp(args[0], "cd") == 0){
			chdir(args[1]);
			if (chdir(args[1]) == -1){
				fprintf(stderr,"cd: %s:", args[1]);
				perror("");
			};
			continue;
		}


		pid_fork = fork();
		switch(pid_fork){
			case -1: 
				perror("fork");
				continue;
			case 0: /*Child*/
				if (!background){
					group = pid_fork = getpid();
					setpgid(pid_fork, group);
					tcsetpgrp(STDIN_FILENO, group);
					restore_terminal_signals();
				}
				execvp(args[0], args);
				fprintf(stderr, "Error, command not found: %s\n", args[0]);
				exit(EXIT_FAILURE);	
				break;
			default:
				if (!background){
					group = pid_fork;
					setpgid(pid_fork, group);//Cambia PID a un nuevo grupo. Como cambiamos al child, para ponerlo al grupo del padre
					tcsetpgrp(STDIN_FILENO, group); //cedemos terminal
					pid_wait = waitpid(pid_fork, &status, WUNTRACED); //esperamos su finalización
					tcsetpgrp(STDIN_FILENO, getpgid(getpid())); //recuperamos terminal
					status_res = analyze_status(status, &info);
					switch (status_res){
						case EXITED:
							if (info != 1){ //Si termina sin error (EXIT_FAILURE es 1, por lo tanto en caso contrario ha terminado correctamente). SI hay error ya se ha tratado en el hijo
							printf("\nForeground pid: %d, command: %s, Finished, info: %d\n", pid_fork, args[0], info);
							}
							break;
						case SIGNALED:
							printf("\nForeground pid: %d, command: %s, Signaled, info: %d\n", pid_fork, args[0], info);
							break;
						case SUSPENDED:
							block_SIGCHLD();
							printf("\nForeground pid: %d, command: %s, Suspended, info: %d\n", pid_fork, args[0], info);
							njob = new_job(pid_fork, args[0], STOPPED);
							add_job(job_list, njob);
							unblock_SIGCHLD();
							break;
					}
				}else{
					block_SIGCHLD();
					njob = new_job(pid_fork, args[0], BACKGROUND);
					add_job(job_list, njob);
					printf("\nBackground job running... pid: %d , command: %s\n", pid_fork, args[0]);
					unblock_SIGCHLD();
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
