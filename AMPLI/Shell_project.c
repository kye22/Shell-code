/*---------------------Shell de Cayetano López Moreno -------------------------*/



#include <string.h>   
#include <errno.h>    
#include "job_control.h" // remember to compile with module job_control.c
#include "parse_redir.h" 
#include <pthread.h>

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
int dead_signaled;
int dead_exited;
pthread_t alarm_thread;
//-----------------------------------------------------------------------------
//                            Funcion alarm-thread
//-----------------------------------------------------------------------------
typedef struct datos_alarma{
    int delay;
    pid_t pid_alarma;
    int action;
}data_alarm;

void *alarm_timer(void *datos){
    data_alarm * data = (data_alarm *)datos;
    sleep(data->delay);
    if (get_item_bypid(job_list, data->pid_alarma)!=NULL){
        kill(data->pid_alarma, data->action);
        data->action==SIGKILL ? printf(AZUL"Killing"RESET): printf(AZUL"Starting" RESET);
        printf(AZUL" pid:%d after waiting for %d seconds"RESET, data->pid_alarma, data->delay);
        puts("");
    }
    pthread_exit(NULL);
}

//-----------------------------------------------------------------------------
//                              Manejador SIGHUP
//-----------------------------------------------------------------------------

void sighup_handler(int signal){
    FILE *fp;
    fp = fopen("hup.txt", "a");
    fprintf(fp,"SIGHUP recibido.\n");
    fclose(fp);
}
//-----------------------------------------------------------------------------
//                            Manejador de childs
//-----------------------------------------------------------------------------
void child_handler(int s) {
    int status;              
    pid_t pid;               
    int info;               
    enum status status_res;  // enum status define valores posibles estados de procesos tras waitpid()
    // Recorremos todos los procesos hijos que han cambiado de estado
    block_SIGCHLD();
    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0) { //El 8  sustituye a WCONTINUED (definido supuestamente en waitflags.h pero no se porque no me va)
        // Analizar el estado del proceso hijo
        status_res = analyze_status(status, &info);


        // Usamos la funion getitembypid para rescatar el trabajo y cambiar su estatus o eliminaro
        job *current_job = get_item_bypid(job_list, pid);

        if (current_job) {
            if (info == 1){ // si el programa termina con error devuelve info = 1.
                printf(ROJO"Background process %d: %s, info: %d\n" RESET, pid, status_strings[status_res], info);                
            }else{
                printf(PURPURA "Background process %d: %s, info: %d\n" RESET, pid, status_strings[status_res], info);
            }
            fflush(stdout);
            switch (status_res) {
                case SUSPENDED:
                /* Si está suspendido, cambiamos su estado a stopped*/
                    current_job->state = (current_job->state == RESPAWNED) ? STOP_RESPAWN : STOPPED;
                    break;

                case CONTINUED:
                /* Si ha continuado, cambiamos su estado a background*/
                    current_job->state = (current_job->state == STOP_RESPAWN) ? RESPAWNED : BACKGROUND;
                    break;

                default:
                /* Si ha terminado, y no es respawn eliminamos la tera de la lista*/
                    if (current_job->state!= RESPAWNED && current_job->state != STOP_RESPAWN || info ==1){ // Si termina con error, para evitar bucles, no revivimos.
                        delete_job(job_list, current_job);
                        //Añadimos contador
                        if (status_res == SIGNALED) dead_signaled++;
                        else dead_exited++;
                    }else{//tenemos que revivir los comandos con la flag respawn
                        pid = fork();
                        switch(pid){
                            case -1:
                            /*error*/
                            case 0:
                                new_process_group(getpid());
                                restore_terminal_signals();
                                execvp(current_job->args[0], current_job->args); 
                                perror(ROJO "Error: execvp falló" RESET);
                                exit(EXIT_FAILURE);
                                break;
                            default:
                                printf(CIAN "Reviving process; old pid: %d, new pid: %d, Command: %s\n" RESET, current_job->pgid, pid, current_job->command);
                                current_job->state = RESPAWNED; //Cambiamos a RESPAWNED para que pueda volver a revivir en la siguiente finalización
                                current_job->pgid = pid; //Actualizamos pid del proceso por el nuevo.
                                break;
                        }

                    }
                    //Añadimos contador dead
                    break;
                
            }
        }
    }
    unblock_SIGCHLD();
}

int main(void)
{
    char inputBuffer[MAX_LINE]; /* Bbuffer to hold the command entered*/
    int background;             /* equals 1 if a command is followed by '&' */
    char *args[MAX_LINE / 2];   /* command line (of 256) has max of 128 arguments */

/*           VARIABLES PROPIAS        */
    int respawn;                /* comprobamos si tiene el argumento '+' */
    int alarmar;                /* variable para informar si debemos alarmar*/
    int esperar;                /* variable para ver si tenemos que esperar para lanzar*/
    job *tarea; /* Creamos variable job para manejar tareas creadas.*/
    job_list = new_list("Job List");
    dead_signaled = 0; /*contador de procesos señalados*/
    dead_exited = 0;   /* contador de procesos finalizados*/
    data_alarm *alarma = malloc(sizeof(data_alarm));

    // probably useful variables
    int pid_fork, pid_wait; /* pid for created and waited process */
    int status;             /* status returned by wait */
    enum status status_res; /* status procesed by analyze_status() */
    int info;               /* info processed by analyze_status() */



    signal(SIGCHLD, child_handler);
    signal(SIGHUP, sighup_handler);

    ignore_terminal_signals(); /*Macro para ignorar todas las señales menos ctrl + D durante la ejecución del shell*/

    printf(AZUL "Shell de Cayetano López Moreno:\n" RESET);

    while (1) {  /* Bucle principal del shell */

        /*Reiniciamos variables de control*/
        respawn = 0;
        alarmar  = 0;
        esperar = 0;
        int contador = 1;
        /*                                */
        printf(BLANCO "COMMAND->" RESET);
        fflush(stdout); 
        get_command(inputBuffer, MAX_LINE, args, &background); /*get next command*/

        if (args[0] == NULL) continue; /* Ignorar comandos vacíos */

        // Obtenemos , si hay, ficheros  de entrada y/o salida
        char *fich_entrada, *fich_salida;
        parse_redirections(args, &fich_entrada, &fich_salida); /*funcion de parse_redir.h*/

        /* Contamos el numero de argumentos del comando de entrada*/
        int nargs = 0;
        while (args[nargs]!= NULL){
            nargs++;
        }

        /* _______________________    COMANDOS INTERNOS     _______________________*/
        //Comprobamos si el proceso es respawneable
        if (strcmp(args[nargs-1], "+")==0){
            respawn = 1;
            args[nargs-1] = NULL;
            nargs--;
        }


        //Comando interno: currjob
        if (strcmp(args[0], "currjob") == 0) {
            tarea = get_item_bypos(job_list, 1);
            printf(VERDE"Trabajo actual: PID= %d command=%s\n"RESET, tarea->pgid, tarea->command);
            continue;
        }


        // COmando interno: dead
        if (strcmp(args[0], "dead") == 0) {
            printf(ROJO "Dead jobs: Signaled: %d, Exited: %d\n"RESET, dead_signaled, dead_exited);
            continue;
        }


        //Comando interno bgteam
        if (strcmp(args[0], "bgteam") == 0) {  
            if (args[1] == NULL || args[2] == NULL){
                printf(ROJO"EL comando bgteam requiere dos argumentos\n"RESET);
                continue;
            }
            background = 1;
            contador = atoi(args[1]);
            //comprobamos valor de contador
            if (contador < 0){
                printf(ROJO "Valor erróneo de número de procesos\n"RESET);
                continue;
            }
            //eliminamos el comando bg y el contador 
            for (int i = 2; args[i - 2]!=NULL;i++){
                args[i-2]=args[i];
            }
            nargs -= 2;
        }  

        //Comando interno: alarm-thread
        if (strcmp(args[0], "alarm-thread") == 0) {
            alarma = malloc(sizeof(data_alarm));
                if (args[1] == NULL || args[2] == NULL){
                printf(ROJO"alarm-thread necesita dos argumentos minimo\n"RESET);
                continue;
            }
            int delay = atoi(args[1]);
            if (delay < 0){
                printf(ROJO "Valor erróneo de temporizador\n"RESET);
                continue;
            }
            alarma->delay = delay; 
            alarma->action = SIGKILL;
            for (int i = 2; args[i - 2]!=NULL;i++){
                args[i-2]=args[i];
            }
            nargs -= 2;
            alarmar = 1;
        }
        //Comando interno delay-thread
        if (strcmp(args[0], "delay-thread") == 0) {
            alarma = malloc(sizeof(data_alarm));
            if (args[1] == NULL || args[2] == NULL){
                printf(ROJO"delay-thread necesita dos argumentos minimo\n"RESET);
                continue;
            }
            int delay = atoi(args[1]);
            if (delay < 0){
                printf(ROJO "Valor erróneo de temporizador\n"RESET);
                continue;
            }
            alarma->delay = delay; 
            alarma->action = SIGCONT;
            for (int i = 2; args[i - 2]!=NULL;i++){
                args[i-2]=args[i];
            }
            nargs -= 2;
            esperar = 1;
            background = 1;
        }
        // Comando interno: (cd)
        if (strcmp(args[0], "cd") == 0) {
            if (args[1] == NULL) {
                chdir(getenv("HOME")); 
            } else {
                if (chdir(args[1]) == -1) { 
                    printf(ROJO "Error: cd: %s:"RESET" %s\n", args[1], strerror(errno));
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
            printf(VERDE"La tarea pid: %d, command: %s pasará ahora a Foreground" RESET, tarea_fg->pgid, tarea_fg->command);
            puts("");
            if (tarea_fg->state == STOPPED || tarea_fg->state == STOP_RESPAWN ) killpg(tarea_fg->pgid, SIGCONT);
            tarea_fg->state = FOREGROUND;
            // Cedemos el terminal al proceso
            tcsetpgrp(STDIN_FILENO, tarea_fg->pgid);
            int status;
            int info;
            enum status status_res;
            // Esperamos al proceso en fg
            pid_t pid_wait = waitpid(tarea_fg->pgid, &status, WUNTRACED);
            unblock_SIGCHLD();
            // Analizamos el estado en que terminó o cambió el proceso
            status_res = analyze_status(status, &info);
            // Devolvemos el terminal al shell
            tcsetpgrp(STDIN_FILENO, getpid());
            block_SIGCHLD();
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
                    printf(CIAN "Foreground pid: %d, command: %s, %s, info: %d\n" RESET, pid_wait, tarea_fg->command, status_strings[status_res], info);
                    delete_job(job_list, tarea_fg);
                    unblock_SIGCHLD();
                    break;

                default:
                    break;
            }
            unblock_SIGCHLD();
            continue; // Volver al inicio del bucle principal
        }

        // Comando interno: poner en segundo plano un trabajo suspendido (bg)
        if (strcmp(args[0], "bg") == 0) {
            int idx = (args[1] == NULL) ? 1 : atoi(args[1]);  /*si no hay mas, idx = 1, si hay mas es el valor en entero*/  

            // Bloqueamos SIGCHLD antes de acceder a la lista de trabajos.
            block_SIGCHLD();
            job *tarea_bg = get_item_bypos(job_list, idx);
            if (tarea_bg == NULL) {
                // Si no encontramos un trabajo en esa posición, informamos y continuamos.
                printf(ROJO "bg: no existe un trabajo en esa posición\n" RESET);
                unblock_SIGCHLD();
                continue;
            }

            // Verificamos que el trabajo esté suspendido (STOPPED).
            if (tarea_bg->state != STOPPED && tarea_bg->state != STOP_RESPAWN) {
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
                   idx, tarea_bg->pgid, tarea_bg->command);

            continue; // Volver al inicio del bucle principal
        }

        /* =========================    EJECUCIÓN DE PROCESOS    ========================= */
        for (int i = 0; i < contador; i++){ //creamos bucle por el tema de bgteam
            pid_fork = fork();

            switch (pid_fork) {
                case -1:
                    perror(ROJO "Error: fork() failed\n" RESET);
                    exit(-1);

                case 0: /* Proceso hijo */
                    restore_terminal_signals();
                    new_process_group(getpid());
                    //Comprobamos si tenemos que esperar, para hacernos killpg SIGSTOP a nosotros mismosç
                    block_SIGCHLD();
                    if (esperar){
                        killpg(getpid(), SIGSTOP);
                    }
                    unblock_SIGCHLD();
                    if (background == 0 && respawn == 0){
                        tcsetpgrp(STDIN_FILENO, getpid());
                    }
                    // Redirección de entrada
                    if (fich_entrada != NULL) {
                        FILE * fd_in = fopen(fich_entrada, "r");
                        if (fd_in == NULL) {
                            perror(ROJO "Error abriendo fichero de entrada" RESET);
                            exit(1);
                        }
                        dup2(fileno(fd_in), STDIN_FILENO);
                        fclose(fd_in);
                    }

                    // Redirección de salida
                    if (fich_salida != NULL) {
                        FILE* fd_out = fopen(fich_salida, "w");
                        if (fd_out == NULL) {
                            perror(ROJO "Error abriendo fichero de salida" RESET);
                            exit(1);
                        }
                        dup2(fileno(fd_out), STDOUT_FILENO);
                        fclose(fd_out);
                    }
                    execvp(args[0], args);
                    perror(ROJO "Error: execvp falló" RESET);
                    exit(EXIT_FAILURE);

                default: /* Proceso padre */

                    if (alarmar || esperar){
                        //creamos el thread que va a "dormir"
                        alarma->pid_alarma = pid_fork; /*añadimos el valor del fork del hijo a los datos de la alarma*/
                        printf(PURPURA "Empezando un temporizador de %d segundos para %s\n"RESET, alarma->delay, args[0]);
                        pthread_create(&alarm_thread, NULL, &alarm_timer, alarma);
                        pthread_detach(alarm_thread);
                    }
                    if (background == 0 && respawn == 0) { /*Comprobamos que el proceso se ejecuta en*/
                        tcsetpgrp(STDIN_FILENO, pid_fork);
                        pid_wait = waitpid(pid_fork, &status, WUNTRACED);
                        tcsetpgrp(STDIN_FILENO, getpid());

                        status_res = analyze_status(status, &info);

                        switch (status_res) {
                            case SUSPENDED:
                                block_SIGCHLD();
                                tarea = new_job(pid_fork, args[0], nargs, args, STOPPED);
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
                        tarea = new_job(pid_fork, args[0], nargs, args, respawn ? RESPAWNED : BACKGROUND);
                        add_job(job_list, tarea);
                        unblock_SIGCHLD();
                        printf(CIAN "Background process running pid: %d, Command: %s\n" RESET,
                            pid_fork, args[0]);
                    }

                    break;
            }
         }
    }
}