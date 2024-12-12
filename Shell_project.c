#include <string.h>   
#include <errno.h>    
#include <fcntl.h>
#include "job_control.h" // remember to compile with module job_control.c
#include "parse_redir.h" 

// Añadimos colores para tarea adicional
#define RESET "\x1b[0m"
#define ROJO "\x1b[31;1;1m"
#define VERDE "\x1b[32;1;1m"
#define AMARILLO "\x1b[33;1;1m"
#define AZUL "\x1b[34;1;1m"
#define PURPURA "\x1b[35;1;1m"
#define CIAN "\x1b[36;1;1m"
#define BLANCO "\x1b[37;1;1m"
#define DEFAULT "\x1b[39;1;1m"

#define MAX_LINE 256 /* 256 chars per line, per command, should be enough. */

// Lista de tareas de procesos, para tener informacion de procesos en background
job *job_list;

//-----------------------------------------------------------------------------
//                            Manejador de childs
//-----------------------------------------------------------------------------
void child_handler(int s) {
    int status;              
    pid_t pid;               
    int info;                
    enum status status_res;  // enum status define valores posibles estados de procesos tras waitpid()

    // Recorremos todos los procesos hijos que han cambiado de estado
    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED | 8)) > 0) { //El 8  sustituye a WCONTINUED (definido supuestamente en waitflags.h pero no se porque no me va)
        // Analizar el estado del proceso hijo
        status_res = analyze_status(status, &info);

        // Usamos la funion getitembypid para rescatar el trabajo y cambiar su estatus o eliminaro
        job *current_job = get_item_bypid(job_list, pid);

        if (current_job) {
            printf(PURPURA "Background process %d: %s\n" RESET, pid, status_strings[status_res]);
            fflush(stdout);

            switch (status_res) {
                case SUSPENDED:
                /* Si está suspendido, cambiamos su estado a stopped*/
                    current_job->state = STOPPED;
                    break;

                case CONTINUED:
                /* Si ha continuado, cambiamos su estado a background*/
                    current_job->state = BACKGROUND;
                    break;

                default:
                /* Si ha terminado, eliminamos la tera de la lista*/
                    delete_job(job_list, current_job);
                    break;
            }
        }
    }
}

int main(void)
{
    char inputBuffer[MAX_LINE]; /* Bbuffer to hold the command entered*/
    int background;             /* equals 1 if a command is followed by '&' */
    char *args[MAX_LINE / 2];   /* command line (of 256) has max of 128 arguments */

    // probably useful variables
    int pid_fork, pid_wait; /* pid for created and waited process */
    int status;             /* status returned by wait */
    enum status status_res; /* status procesed by analyze_status() */
    int info;               /* info processed by analyze_status() */

    job *tarea; /* Creamos variable job para manejar tareas creadas.*/
    job_list = new_list("Job List");


    signal(SIGCHLD, child_handler);


    ignore_terminal_signals(); /*Macro para ignorar todas las señales menos ctrl + D durante la ejecución del shell*/

    printf(AZUL "Shell de Cayetano López Moreno:\n" RESET);

    while (1) {  /* Bucle principal del shell */
        printf(VERDE "COMMAND->" RESET);
        fflush(stdout); 
        get_command(inputBuffer, MAX_LINE, args, &background); /*get next command*/

        if (args[0] == NULL) continue; /* Ignorar comandos vacíos */

        // Obtenemos , si hay, ficheros  de entrada y/o salida
        char *fich_entrada, *fich_salida;
        parse_redirections(args, &fich_entrada, &fich_salida); /*funcion de parse_redir.h*/

        /* _______________________    COMANDOS INTERNOS     _______________________*/

        // Comando interno: (cd)
        if (strcmp(args[0], "cd") == 0) {
            if (args[1] == NULL) {
                chdir(getenv("HOME")); 
            } else {
                if (chdir(args[1]) == -1) { 
                    printf(ROJO "Error: Directorio no encontrado\n" RESET);
                }
            }
            continue; // Volver al inicio del bucle principal
        }

        // Comando interno: mostar los jobs
        if (strcmp(args[0], "jobs") == 0) {
            block_SIGCHLD(); // Bloqueamos las señales SIGCHLD para evitar race conditions
            if (empty_list(job_list)) { 
                printf(ROJO "No hay tareas en background o suspendidas\n" RESET);
            } else {
                print_job_list(job_list); 
            }
            unblock_SIGCHLD(); 
            continue; 
        }

        // Comando interno: poner en primer plano un trabajo (fg)
        if (strcmp(args[0], "fg") == 0) {
            int idx = (args[1] == NULL) ? 1 : atoi(args[1]);  /*si no hay mas, idx = 1, si hay mas es el valor en entero*/
            block_SIGCHLD(); /* race conditions */
            job * tarea_fg = get_item_bypos(job_list, idx);
            if (tarea_fg == NULL) {
                printf(ROJO "fg: no existe un trabajo en esa posición\n" RESET);
                // Desbloqueamos SIGCHLD si no encontramos el trabajo
                unblock_SIGCHLD();
                continue; // Volver al bucle principal
            }

            // Cambiamos el estado del trabajo a FOREGROUND
            printf(PURPURA"Background process pid: %d, command: %s is now running in Foreground\n" RESET, tarea_fg->pgid, tarea_fg->command);
            if (tarea_fg->state == STOPPED) killpg(tarea_fg->pgid, SIGCONT);
            tarea_fg->state = FOREGROUND;
            unblock_SIGCHLD();

            // Cedemos el terminal al grupo de procesos del trabajo
            tcsetpgrp(STDIN_FILENO, tarea_fg->pgid);


            int status;
            int info;
            enum status status_res;

            // Esperamos al proceso en primer plano (puede finalizar o suspenderse)
            pid_t pid_wait = waitpid(tarea_fg->pgid, &status, WUNTRACED);
            // Analizamos el estado en que terminó o cambió el proceso
            status_res = analyze_status(status, &info);

            switch (status_res) {
                case SUSPENDED:
                    block_SIGCHLD();
                    tarea_fg->state = STOPPED;
                    unblock_SIGCHLD();
                    printf(CIAN "Foreground pid: %d, command: %s, Suspended in FG, info: %d.\n" RESET, tarea_fg->pgid, tarea_fg->command, info);
                    break;

                case EXITED:

                case SIGNALED:
                    block_SIGCHLD();
                    printf(CIAN "Foreground pid: %d, command: %s, %s, info: %d\n" RESET, 
                        pid_wait, tarea_fg->command, status_strings[status_res], info);
                    delete_job(job_list, tarea_fg);
                    unblock_SIGCHLD();
                    break;

                default:
                    break;
            }

            // Devolvemos el terminal al shell
            tcsetpgrp(STDIN_FILENO, getpid());

            continue; // Volver al inicio del bucle principal
        }

        // Comando interno: poner en segundo plano un trabajo suspendido (bg)
        if (strcmp(args[0], "bg") == 0) {
            int n = 1; 
            // Si el usuario especifica un número, lo convertimos a entero.
            // Si no se especifica, n=1 se aplicará al primer trabajo de la lista.
            if (args[1] != NULL) {
                n = atoi(args[1]);
                if (n <= 0) {
                    printf(ROJO "bg: Argumento inválido\n" RESET);
                    continue; // Argumento inválido, volvemos al bucle principal.
                }
            }

            // Bloqueamos SIGCHLD antes de acceder a la lista de trabajos.
            block_SIGCHLD();
            job *tarea_bg = get_item_bypos(job_list, n);
            if (tarea_bg == NULL) {
                // Si no encontramos un trabajo en esa posición, informamos y continuamos.
                printf(ROJO "bg: no existe un trabajo en esa posición\n" RESET);
                unblock_SIGCHLD();
                continue;
            }

            // Verificamos que el trabajo esté suspendido (STOPPED).
            if (tarea_bg->state != STOPPED) {
                // Si no está suspendido, no podemos ponerlo en bg.
                printf(ROJO "bg: el trabajo seleccionado no está suspendido\n" RESET);
                unblock_SIGCHLD();
                continue;
            }

            // Cambiamos el estado a BACKGROUND
            tarea_bg->state = BACKGROUND;
            // Ya no necesitamos acceso exclusivo a la lista
            unblock_SIGCHLD();

            // Enviamos SIGCONT al grupo de procesos del trabajo para reanudarlo en segundo plano.
            killpg(tarea_bg->pgid, SIGCONT);

            // Indicamos al usuario que el trabajo se ha reanudado en segundo plano.
            printf(VERDE "Tarea %d reanudada en segundo plano: PID: %d, Command: %s\n" RESET, 
                   n, tarea_bg->pgid, tarea_bg->command);

            continue; // Volver al inicio del bucle principal
        }

        /* =========================    EJECUCIÓN DE PROCESOS    ========================= */

        pid_fork = fork();

        switch (pid_fork) {
            case -1:
                perror(ROJO "Error: fork() failed\n" RESET);
                exit(-1);

            case 0: /* Proceso hijo */
                restore_terminal_signals();
                new_process_group(getpid());

                // Redirección de entrada
                if (fich_entrada != NULL) {
                    int fd_in = open(fich_entrada, O_RDONLY);
                    if (fd_in < 0) {
                        perror(ROJO "Error abriendo fichero de entrada" RESET);
                        exit(1);
                    }
                    dup2(fd_in, STDIN_FILENO);
                    close(fd_in);
                }

                // Redirección de salida
                if (fich_salida != NULL) {
                    int fd_out = open(fich_salida, O_WRONLY | O_CREAT | O_TRUNC, 0666);
                    if (fd_out < 0) {
                        perror(ROJO "Error abriendo fichero de salida" RESET);
                        exit(1);
                    }
                    dup2(fd_out, STDOUT_FILENO);
                    close(fd_out);
                }

                execvp(args[0], args);
                perror(ROJO "Error: execvp falló" RESET);
                exit(EXIT_FAILURE);

            default: /* Proceso padre */
                if (background == 0) {
                    tcsetpgrp(STDIN_FILENO, pid_fork);
                    pid_wait = waitpid(pid_fork, &status, WUNTRACED);
                    tcsetpgrp(STDIN_FILENO, getpid());

                    status_res = analyze_status(status, &info);

                    switch (status_res) {
                        case SUSPENDED:
                            block_SIGCHLD();
                            tarea = new_job(pid_fork, args[0], STOPPED);
                            add_job(job_list, tarea);
                            unblock_SIGCHLD();
                            printf(CIAN "\nForeground pid: %d, Command: %s, Status: %s, Info: %d\n" RESET,
                                   pid_wait, args[0], status_strings[status_res], info);
                            break;
                        case EXITED:
                        case SIGNALED:
                            printf(CIAN "Foreground pid: %d, Command: %s, Status: %s, Info: %d\n" RESET,
                                   pid_wait, args[0], status_strings[status_res], info);
                            break;

                        default:
                            break;
                    }

                } else {
                    block_SIGCHLD();
                    tarea = new_job(pid_fork, args[0], BACKGROUND);
                    add_job(job_list, tarea);
                    unblock_SIGCHLD();
                    printf(CIAN "Background process running pid: %d, Command: %s\n" RESET,
                           pid_fork, args[0]);
                }

                break;
        }
    }
}
