#include <stdio.h>
#include <stdlib.h>
#include <sys/shm.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <string.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <signal.h>
#include <errno.h>
#include <time.h>

// Firme delle funzioni
void printTable(int *matrix, int mode);
int putPawn(int column, int player, int *matrix);
int convertPos(int row, int column);
void printIntroGame();
void ctrlcHandler(int sig);
void libera_posto_occupato(void);
void handlerVittoriaTavolino(int sig);

// Variabili dimensioni tabellone
int RIGHE = 0;
int COLONNE = 0;

int giocatore;
int startMatch = 0;
int dopo_menu = 0;
int primo_menu = 0;

int sem_mutex; // Semaforo Mutex
int sem_array; // Array di Semafori - controllo turno giocata
int sem_id; // semaforo 1 --> i player arrivano e lo decrementano
int sem_id2; // semaforo 2 --> per attesa dei due player nella lobby

//variabili globali necessarie all´esecuzione del programma
int shmid_pid;
int shmid;
int shmid_dimensione;
int shmid_tabellone;
int shmid_vittoria;
int shmid_player1;
int shmid_player2;
size_t size_tabellone;
int *array_pid;
int *value;
int *dimensione;
int *tabellone;
int *vittoria;
char *player1;
char *player2;
const char botName[] = "BOT"; //nome giocatore automatico
pid_t pidFiglio = 1; //valore fittizio


/**
 * E´ letteralmente il main
 */
int main(int argc, char* argv[])
{

    //CONTROLLO PARAMETRI DA INPUT
    if(argc<2)
    {
        printf("Numero parametri inseriti invalido! %d\n",argc);
        return EXIT_FAILURE;
    }
    if(argc>=4)
    {
        printf("Attenzione, numero parametri eccessivo (%d)\nSe vuoi giocare contro il bot usa il flag  \\*  !\n",argc);
        return EXIT_FAILURE;
    }
    if(strlen(argv[1])>200){
        printf("Nome giocatore troppo lungo\n");
        return EXIT_FAILURE;
    }
    if(argc==3 && !(argv[2][0]=='*' && argv[2][1]=='\0')){ //controllo gioco automatico
        printf("Parametro giocatore automatico invalido\n");
        return EXIT_FAILURE;
    }
    else if(argc==3 && argv[2][0]=='*' && argv[2][1]=='\0' ){
        pidFiglio = fork();
    }


    //BINDING SEGNALI-FUNZIONI
    if (signal(SIGINT, ctrlcHandler) == SIG_ERR)   //gestisco i segnali di CTRLC
    {
        exit(EXIT_FAILURE);
    }
    if (signal(SIGHUP, ctrlcHandler) == SIG_ERR)   //gestisco i segnali di chiusura finestra terminale
    {
        exit(EXIT_FAILURE);
    }
    if (signal(SIGUSR1, handlerVittoriaTavolino) == SIG_ERR || signal(SIGUSR2, handlerVittoriaTavolino) == SIG_ERR)   //gestisco i segnali per la vittoria a tavolino (SIGUSR1 e SIGUSR2)
    {
        exit(EXIT_FAILURE);
    }


    //GENERAZIONI CHIAVI
    // Genero key per segmento di memoria condivisa -- Value
    char path[255] = "./key.txt";
    key_t key = ftok(path, 1);
    // Genero key per segmento di memoria condivisa - ARRAY PER PID
    key_t key_pid = ftok(path, 9);
    // Genero key per segmento di memoria condivisa -- Tabellone
    key_t key_tabellone = ftok(path, 6);
    // Genero key per segmento di memoria condivisa - DIMENSIONI TABELLONE
    key_t key_dimensione = ftok(path, 8);
    // Genero key per segmento di memoria condivisa - INTERO PER VITTORIA
    key_t key_vittoria = ftok(path, 7);
    // Genero key per semaforo
    key_t key_sem = ftok(path, 2);
    // Genero key per semaforo 2
    key_t key_sem2 = ftok(path, 3);
    // Genero key per array di semafori
    key_t key_sem_array = ftok(path, 4);
    // Genero key per mutex
    key_t key_sem_mutex = ftok(path, 5);
    // Genero key per mutex
    key_t key_arrayNomi = ftok(path, 10);
    // Genero key per mutex
    key_t key_arrayNomi1 = ftok(path, 11);

    // Controllo se errore nella generazione della chiave
    if (key_arrayNomi1 == -1 || key_arrayNomi == -1 || key == -1 || key_sem == -1 || key_sem2 == -1 || key_sem_array == -1 || key_sem_mutex == -1 || key_tabellone == -1 || key_vittoria == -1 || key_dimensione == -1 || key_pid == -1)
    {   
        if(pidFiglio)
            printf("Errore nella generazione della chiave");
        return EXIT_FAILURE;
    }

    //APERTURA SEMAFORI & MEMORIE CONDIVISE
    if ((sem_id = semget(key_sem, 1, 0666 | IPC_CREAT)) == -1)
    {
        exit(EXIT_FAILURE);
    }
    if ((sem_id2 = semget(key_sem2, 1, 0666 | IPC_CREAT)) == -1)
    {
        exit(EXIT_FAILURE);
    }
    if ((sem_mutex = semget(key_sem_mutex, 1, 0666 | IPC_CREAT)) == -1)
    {   
        if(pidFiglio)
            printf("suca0_1");
        exit(EXIT_FAILURE);
    }
    if ((sem_array = semget(key_sem_array, 2, 0666 | IPC_CREAT)) == -1)
    {   
        if(pidFiglio)
            printf("Errore creazione Array di semafori ");
        exit(EXIT_FAILURE);
    }

    //prendo segmento di memoria condivisa - ARRAY PID 
    size_t size_pid = sizeof(int)*3;
    shmid_pid = shmget(key_pid, size_pid, IPC_CREAT | S_IRUSR | S_IWUSR);
    array_pid = (int *)shmat(shmid_pid, NULL, 0);

    if (array_pid == (void *)-1)
    {   
        if(pidFiglio)
            printf("Errore nell'attach della memoria condivisa");
        exit(EXIT_FAILURE);
    }

    if(!array_pid[0]){ //check server off
        if(pidFiglio)
            printf("Server offline, quitto!\n");
        if (shmdt(array_pid) == -1)
        {   
            if(pidFiglio)
                printf("Errore detach memory value");
            exit(EXIT_FAILURE);
        }
        exit(EXIT_FAILURE);
    }

    //operazioni wait e signal che useremo spesso
    struct sembuf wait_op = {0, -1, 0};
    struct sembuf signal_op = {0, 1, 0};

    if (semop(sem_id, &wait_op, 1) == -1){
        if(errno == 4){
            if (semop(sem_id, &wait_op, 1) == -1){
                
                if(pidFiglio)
                    printf("Primissima wait errore\n");
                exit(EXIT_FAILURE);
            }
        }
        else{
            if(pidFiglio)
                printf("Primissima wait errore\n");
            exit(EXIT_FAILURE);
        }
    }

    // prendo segmento di memoria condivisa per Value
    size_t size = sizeof(int);
    shmid = shmget(key, size, IPC_CREAT | S_IRUSR | S_IWUSR);
    value = (int *)shmat(shmid, NULL, 0);

    // Checcko se attach riuscito
    if (value == (void *)-1)
    {   
        if(pidFiglio)
            printf("Errore nell'attach della memoria condivisa");
        exit(EXIT_FAILURE);
    }

    // prendo segmento di memoria condivisa - INTERO PER VERIFICARE VITTORIA
    size_t size_vittoria = sizeof(int);
    shmid_vittoria = shmget(key_vittoria, size_vittoria, IPC_CREAT | S_IRUSR | S_IWUSR);
    vittoria = (int *)shmat(shmid_vittoria, NULL, 0);

    // Checcko se attach riuscito
    if (vittoria == (void *)-1)
    {   
        if(pidFiglio)
            printf("Errore nell'attach della memoria condivisa");
        exit(EXIT_FAILURE);
    }

    // Genero segmento di memoria condivisa - ARRAY DI 4 INTERI PER DIMENSIONI DEL TABELLONE + PEDINE
    size_t size_dimensione = sizeof(int)*4;
    shmid_dimensione = shmget(key_dimensione, size_dimensione, IPC_CREAT | S_IRUSR | S_IWUSR);
    dimensione = (int *)shmat(shmid_dimensione, NULL, 0);

    // Checcko se attach riuscito
    if (dimensione == (void *)-1)
    {   
        if(pidFiglio)
            printf("Errore nell'attach della memoria condivisa");
        exit(EXIT_FAILURE);
    }

    //genero segmento di memoria array per i nomi
    size_t size_nomi = sizeof(char)*200;
    shmid_player1 = shmget(key_arrayNomi, size_nomi, IPC_CREAT | S_IRUSR | S_IWUSR);
    player1 = (char *)shmat(shmid_player1, NULL, 0);
    shmid_player2 = shmget(key_arrayNomi1, size_nomi, IPC_CREAT | S_IRUSR | S_IWUSR);
    player2 = (char *)shmat(shmid_player2, NULL, 0);

    int gameMode = 0; //modalita di gioco, se 1 gioca, se 2 esci dal menu´
    int rigioco;
    char choice_str[9] = " ";
    int choice = 0;

    printIntroGame();
    do{ 
            if(pidFiglio)
            {
                printf("SCEGLI: ");
                scanf(" %s: ", choice_str);
                choice = atoi(choice_str);
                gameMode = choice;
            }
            else
            {
                gameMode = 1;
            }

    }while(gameMode!=1 && gameMode!=2 && !choice);

    giocatore = *value; // salvo che giocatore sono
    *value += 1;

    if(pidFiglio){
        //metto il nome del giocatore nel rispettivo slot
        (giocatore == 0) ? strcpy(player1,argv[1]) : strcpy(player2,argv[1]);
    }
    else
        (giocatore == 0) ? strcpy(player1,botName) : strcpy(player2,botName);

    dopo_menu = 1;
    primo_menu = 1;
    rigioco = 0;
    
    if(gameMode == 2)
    {
        libera_posto_occupato();
        *value -= 1;
        if(pidFiglio)
            kill(array_pid[0], SIGUSR1);
        exit(EXIT_FAILURE);
    }

    if(pidFiglio)
        printf("\n\nIn attesa di giocatore ... \n\n\n");

    // Manda un singolo ack al Server (1/2)
    if (semop(sem_id2, &signal_op, 1) == -1)
    { 
        printf("Tavolo offline\n");
        exit(EXIT_FAILURE);
    }

    // decremento mutex
    // leggo che giocatore sono
    // incremento mutex
    if (semop(sem_mutex, &wait_op, 1) == -1)
    {
        if(errno == 4){
            if (semop(sem_mutex, &wait_op, 1) == -1){
                if(pidFiglio)
                    printf("Errore wait secondo semaforo\n");
                exit(EXIT_FAILURE);
            }
        } //per colpa della ctrlc
        else if(errno == 43)
        {
            if(pidFiglio)
                printf("Tavolo chiuso\n");
            exit(EXIT_FAILURE);
        }
        else{
            if(pidFiglio)
                printf("Errore wait secondo semaforo\n");
            exit(EXIT_FAILURE);
        }
    }

    startMatch = 1;
    giocatore++;
    array_pid[giocatore] = getpid();

    if (semop(sem_mutex, &signal_op, 1) == -1)
    {
        exit(EXIT_FAILURE);
    }

    RIGHE = dimensione[0];
    COLONNE = dimensione[1];

    // prendo segmento di memoria condivisa per Tabellone
    size_t size_tabellone = sizeof(int) * RIGHE * COLONNE;
    int shmid_tabellone = shmget(key_tabellone, size_tabellone, IPC_CREAT | S_IRUSR | S_IWUSR);
    int *tabellone = (int *)shmat(shmid_tabellone, NULL, 0);

    // Checcko se attach riuscito
    if (tabellone == (void *)-1)
    {   
        if(pidFiglio)
            printf("Errore nell'attach della memoria condivisa");
        exit(EXIT_FAILURE);
    }

    while (gameMode != 2)
    {
        if(pidFiglio){
            printf("%s sei giocatore #%i\n", argv[1], giocatore);
            printf("Pedina utilizzata: %c",(giocatore == 1) ? dimensione[2] : dimensione[3]);
        }

        struct sembuf wait_player = {giocatore - 1, -1, 0};
        int chosenColumn = 0;

        if(pidFiglio)
            printf("\n\nDUE PLAYER: %s & %s",player1,player2);

        do
        {
            if (semop(sem_array, &wait_player, 1) == -1)
            {
                if(errno == 4){
                    if (semop(sem_array, &wait_player, 1) == -1)
                    {
                        if(pidFiglio)
                            printf("exit 130 sono qua");
                        exit(EXIT_FAILURE);
                    }
                }
                else if(errno == 43)
                {   
                    if(pidFiglio)
                        printf(" ");
                }
                else{
                    if(pidFiglio){
                        printf("%i",errno);
                        printf("exit 130");
                    }    
                    exit(EXIT_FAILURE);
                }   
            }

            if (*vittoria != 1)
            {
                if (startMatch)
                {
                    if(pidFiglio)
                        printTable(tabellone, 2);
                    startMatch = 0;
                }
                else
                {   
                    if(pidFiglio)
                        printTable(tabellone, 0);
                }

                do
                {
                    do
                    {
                        time_t t;
                        srand((unsigned) time(&t));
                        char temp_str[9] = " ";
                        if(pidFiglio){
                            printf("%s usa pedina: %c\nSCEGLI COLONNA: ", argv[1], (giocatore == 1) ? dimensione[2] : dimensione[3]);
                            scanf("%s",temp_str);
                        }
                        else
                        {
                            int temp_int = rand() % COLONNE + 1; 
                            sprintf(temp_str,"%d",temp_int);
                        }
                        chosenColumn = atoi(temp_str);
                        
                    } while (!chosenColumn);
                } while (!putPawn(chosenColumn, (giocatore==1) ? dimensione[2] : dimensione[3], tabellone));

                if(pidFiglio)
                    printTable(tabellone, 1);

                if (semop(sem_id2, &signal_op, 1) == -1)
                {
                    if(pidFiglio)
                        printf("exit 140");
                    exit(EXIT_FAILURE);
                }

                if (semop(sem_array, &wait_player, 1) == -1){
                    if(errno == 4){
                        if (semop(sem_array, &wait_player, 1) == -1){
                            if(pidFiglio)
                                printf("Exit 130 maso\n");
                            exit(EXIT_FAILURE);
                        }
                    }
                    else{
                        if(pidFiglio)
                            printf("Exit 130 vero\n");
                        exit(EXIT_FAILURE);
                    }
                }
            }

        } while (*vittoria == 0);

        //controllo vittoria/sconfitta/pareggio
        if(*value == -1){ //in caso di pareggio
            if(pidFiglio)
                printf("Pareggio!\n\n\n\n\n");
        }
        else if (giocatore == *value)
        {
            if(pidFiglio){
                printf("Complimenti hai vinto il match!\n");
                printTable(tabellone, 1);
            }
        }
        else
        {
            if(pidFiglio){
                printf("Il tuo avversario ha vinto il match!\n");
                printTable(tabellone, 1);
            }
        }
        
        startMatch = 1;
        rigioco = 1;
        char choice_str1[9] = " ";
        int choice1 = 0;
        if(rigioco)
        {
            printIntroGame();
            dopo_menu = 0;
            do{
                if(pidFiglio){
                    printf("SCEGLI: ");
                    scanf(" %s: ", choice_str1);
                    choice1 = atoi(choice_str1);
                    gameMode = choice1;
                }
                else{
                    gameMode = 1;
                }
            }while(gameMode!=1 && gameMode!=2 && !choice1);
            // Manda un singolo ack al Server (1/2)
            if (semop(sem_id2, &signal_op, 1) == -1)
            {
                exit(1);
            }
            dopo_menu = 1;
            startMatch = 1;
        }
        if(gameMode == 2)
        {
            //AVVISIAMO il server che sto quittando
            kill(array_pid[0], SIGUSR1); //mando sigUSER1 a server
            if(pidFiglio)
                printf("Abbandono il tavolo!\n");
            exit(EXIT_SUCCESS);
        }
    }
    return 0;
}


/**
 * Funzione che accetta una colonna nella quale mettere la pedina
 * 
 * @param int column colonna scelta per giocare
 * @param int player paramatro passato controllare il campo di gioco
 * @param int *matrix matrice sulla quale giocare
 * @return status operazione 0 se fallita o 1 se success
 */
int putPawn(int column, int player, int *matrix)
{
    int check = 0;
    //check se la colonna esiste
    if(column<=0 || column>COLONNE){
        if(pidFiglio)
            printf("Colonna non esistente! Scegline un'altra!\n");
        return 0;
        
    }
    // check se c'è spazio
    for (int i = RIGHE -1; i >= 0 && !check; i--)
    {
        if (matrix[convertPos(i, column)] == 0) // controllo se spazio libero
        {
            matrix[convertPos(i, column)] = player; // piazzo pedina
            check = 1;
        }
    }
    
    if (!check) // se non ho piazzato nessuna pedina perchè non c'è spazio ritorno 0
    {
        if(pidFiglio)
            printf("Colonna piena! Scegline un'altra!\n");
        return 0;
    }
    return 1;
}


/**
 * Removes all data used by the GIF handler
 * 
 * @param int *matrix e´ la matrice da stampare
 * @param int mode modalita´ di stampata
 * @return void
 */
void printTable(int *matrix, int mode)
{
    if (mode == 2)
    {
        if(pidFiglio)
            printf("\nMatch iniziato!\n");
    }
    else if (mode == 1)
    {   
        if(pidFiglio)
            printf("Mossa eseguita!\n");
    }
    else
    {   
        if(pidFiglio)
        {printf("\n\n\n");
        printf("Mossa dell'aversario!\n");}
    }
    int pos = 0;
    for (int row = 0; row < RIGHE; row++)
    {
        for (int clm = 1; clm <= COLONNE; clm++)
        {
            pos = convertPos(row, clm);

            if(matrix[pos] == 0){
                if(pidFiglio)
                    printf("▢ ");
            }
            else{
                if(pidFiglio)
                    printf("%c ",(char)matrix[pos]);
            }
        }
        if(pidFiglio)
            printf("\n");
    }
    if(pidFiglio)
        printf("\n\n");
}


/**
 * Funzione di conversione di riga/colonna di forma bidimensionale di matrice a forma lineare di indice di array
 * @param int row indica la posizione della riga della matrice
 * @param int column indica la posizione della colonna della matrice
 * @return int che inidcherà la posizione rispettiva al nostro array
 */
int convertPos(int row, int column)
{
    column--;
    return column + (COLONNE * (row));
}


/**
 * Funzione di stampa del logo e del menù di scelta della modalità di gioco
 * @return void
 */
void printIntroGame(void)
{
    if(pidFiglio)
    {
        printf("    ______                          __ __    \n");
        printf("   / ____/____ _________  ____ _   / // /  ◉ \n");
        printf("  / /_  / __  / ___/_  / / __ `/  / // /_  ◉ \n");
        printf(" / __/ / /_/ / /    / /_/ /_/ /  /__  __/  ◉ \n");
        printf("/_/   /_____/_/    /___/___,_/     /_/     ◉ \n\n");
        printf("MENU:\n1) GIOCA\n2) QUIT\n\n");
    }
}


/**
 * Funzione usata per segnalare al server che un utente ha lasciato il game
 * 
 * @param void
 * @return void
 */
void libera_posto_occupato(void)
{
    struct sembuf signal_op = {0, 1, 0};
    if(pidFiglio){ //se ho figliato
        if (semop(sem_id, &signal_op, 1) == -1)
            exit(EXIT_FAILURE);
    }   
    if (semop(sem_id, &signal_op, 1) == -1)
    {
        exit(EXIT_FAILURE);
    }
}


/**
 * Funzione usata per gestire il CTRL C
 * 
 * @param int sig parametro che inidica il segnale preso in ingresso
 * @return void
 */
void ctrlcHandler(int sig) { 
    if(primo_menu){
        if(!(array_pid[1] && array_pid[2])){
            if(pidFiglio)
                printf("\n\nUtente ritirato!\n"); 
        }
        else{
            if(pidFiglio)
                printf("\n\n\nMi ritiro\n");
        }
        if(startMatch){
            array_pid[giocatore] = 0;
        }
        kill(array_pid[0], SIGUSR1); //mando sigUSER1 a server
        exit(EXIT_SUCCESS);
    }
    else{
        libera_posto_occupato();
        exit(EXIT_SUCCESS);
    }   
}


/**
 * Funzione per gestire SIGUSR1 e SIGUSR2
 * 
 * @param int parametro che inidica il segnale preso in ingresso
 * @return void
 */
void handlerVittoriaTavolino(int sig){
    if(sig == SIGUSR2){
        printf("\n\nAbbandono il tavolo\n");
        exit(EXIT_SUCCESS);
    }
    if(array_pid[1]==-1 && array_pid[2]==-1){
        if(pidFiglio)
            printf("\n\nIl tavolo è chiuso per chiusura del server\n");
        exit(EXIT_SUCCESS);
    }
    if(array_pid[1] && array_pid[2]){
        if(pidFiglio)
            printf("Il tavolo è chiuso per abbandono\n");
        exit(EXIT_SUCCESS);
    }
    else{
        if(pidFiglio)
            printf("\n\nHo vinto a tavolino\n");
        exit(EXIT_SUCCESS);    
    }
}

/************************************
* VR471343 - VR471337 - VR471487
* Alex Gaiga - Michele Cipriani -Tommaso Vilotto
* 24/04/2023
*************************************/