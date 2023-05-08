/// @file Server.c
/// @brief Server gestore del match.
/// @author Alex, Michele, Tommaso

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


// Firme delle funzioni
int checkWin(int *matrix, int player);
int convertPos(int row, int column);
void reset_tabellone(int *tabellone, int size_tabellone);
int checkInput(char* input);
void ctrlcHandler(int sig);
void handlerVittoriaTavolino(int sig);
void clear_ipcs(void);
void alarmHandler(int sig);

// Variabili inerenti alle pedine e al tabellone
int RIGHE = 0;
int COLONNE = 0;
char pown1 = ' ';
char pown2 = ' ';

// Variabile di controllo relativo al doppio CTRL-C
int shutDown = 0;

// Variabli globali inerenti al IPCS

int sem_mutex; // Semaforo per accesso alla variabile value in mutua esclusione
int sem_array; // Array di Semafori - controllo turno giocata
int sem_id; // Semaforo 1 --> i player arrivano e lo decrementano
int sem_id2; // Semaforo 2 --> per attesa dei due player nella lobby

// Memoria condivisa
int shmid;
int shmid_pid;
int shmid_dimensione;
int shmid_tabellone;
int shmid_vittoria;
int shmid_player1;
int shmid_player2;
size_t size_tabellone;
int *value;
int *dimensione;
int *tabellone;
int *vittoria;
int *array_pid;
char *player1;
char *player2;

// Allarme per la scadenza del turno 
int allarme = 0;

// Turno di gioco
int turn;

union semun
{
    int val;
    struct semid_ds *buf;
    unsigned short *array;
};

int main(int argc, char* argv[])
{
    // Gestione del segnale relativo al CTRL-C
    if (signal(SIGINT, ctrlcHandler) == SIG_ERR)
    {
        exit(EXIT_FAILURE);
    }

    //  Gestione del segnale relativo all' exit mediante "x"
    if (signal(SIGHUP, ctrlcHandler) == SIG_ERR)
    {
        exit(EXIT_FAILURE);
    }

    // Gestione del segnale relativo al SIGUSR1
    if (signal(SIGUSR1, handlerVittoriaTavolino) == SIG_ERR)
    {
        exit(EXIT_FAILURE);
    }

    // Gestione del segnale SIGALRM
    if (signal(SIGALRM,alarmHandler) == SIG_ERR)
    {
        exit(EXIT_FAILURE);
    }

    // Fase di controllo parametri
    if(argc!=5)
    {
        printf("Numero parametri errati!\n");
        return EXIT_FAILURE;
    }

    // Controllo che i primi due parametri siano numeri
    if(!checkInput(argv[1]) || !checkInput(argv[2]) )
    {
        printf("Parametri tabellone errati!\n");
        return EXIT_FAILURE;
    }

    // Controllo dimensioni tabellone
    if(!(atoi(argv[1])>=5 && atoi(argv[2])>=5))
    {
        printf("Dimensione tabellone troppo piccole!\n");
        return EXIT_FAILURE;
    }

    // Controllo che pedine siano a singolo char
    if(argv[3][1]!='\0' || argv[4][1]!='\0' || argv[3][0]==argv[4][0])
    {
        printf("Errore in fase di inserimento delle pedine\n");
        return EXIT_FAILURE;
    }

    // Salvo le pedine
    pown1 = argv[3][0];
    pown2 = argv[4][0];

    // Path del file utilizzato dalla ftok
    char path[255] = "./key.txt";

    // Genero key per segmento di memoria condivisa - VALUE
    key_t key = ftok(path, 1);

    // Genero key per segmento di memoria condivisa - ARRAY PER PID
    key_t key_pid = ftok(path, 9);

    // Genero key per segmento di memoria condivisa - TABELLONE
    key_t key_tabellone = ftok(path, 6);

    // Genero key per segmento di memoria condivisa - DIMENSIONI TABELLONE
    key_t key_dimensione = ftok(path, 8);

    // Genero key per segmento di memoria condivisa - INTERO PER VITTORIA
    key_t key_vittoria = ftok(path, 7);

    // Genero key per semaforo 1
    key_t key_sem = ftok(path, 2);

    // Genero key per semaforo 2
    key_t key_sem2 = ftok(path, 3);

    // Genero key per array di semafori
    key_t key_sem_array = ftok(path, 4);

    // Genero key per mutex
    key_t key_sem_mutex = ftok(path, 5);

    // Genero key per player1
    key_t key_player1 = ftok(path, 10);

    // Genero key per player2
    key_t key_player2 = ftok(path, 11);

    // Controllo se errore nella generazione della chiave
    if ( key_player2 == -1 || key_player1 == -1 || key == -1 || key_sem == -1 || key_sem2 == -1 || key_sem_array == -1 || key_sem_mutex == -1 || key_tabellone == -1 || key_vittoria == -1 || key_dimensione == -1 || key_pid == -1)
    {
        printf("Errore nella generazione della chiave");
        return EXIT_FAILURE;
    }

    // Creazione del mutex
    if ((sem_mutex = semget(key_sem_mutex, 1, 0666 | IPC_CREAT)) == -1)
    {
        printf("Errore creazione Mutex");
        exit(EXIT_FAILURE);
    }

    // Inizializzazione del mutex
    if (semctl(sem_mutex, 0, SETVAL, 0) == -1)
    {
        printf("Errore inizializzazione Mutex");
        exit(EXIT_FAILURE);
    }

    // Creazione del array di semafori
    if ((sem_array = semget(key_sem_array, 2, 0666 | IPC_CREAT)) == -1)
    {
        printf("Errore creazione Array di semafori ");
        exit(EXIT_FAILURE);
    }

    unsigned short values[2] = {0, 0};
    union semun arg;
    arg.array = values;

    // Inizializzazione array di semafori
    if (semctl(sem_array, 0, SETALL, arg) == -1)
    {
        printf("Errore inizializzazione Array di semafori");
        exit(EXIT_FAILURE);
    }

    // Creazione del semaforo
    if ((sem_id = semget(key_sem, 1, 0666 | IPC_CREAT)) == -1)
    {
        printf("Errore creazione primo semaforo");
        exit(EXIT_FAILURE);
    }

    // Inizializzazione del semaforo a 2 --> quando vale zero e quindi sono passati i 2 player sblocca semaforo 2 (sem_id2)
    if (semctl(sem_id, 0, SETVAL, 2) == -1)
    {
        printf("Errore inizializzazione primo semaforo");
        exit(EXIT_FAILURE);
    }

    // Creazione del semaforo
    if ((sem_id2 = semget(key_sem2, 1, 0666 | IPC_CREAT)) == -1)
    {
        printf("Errore creazione secondo semaforo");
        exit(EXIT_FAILURE);
    }

    // Inizializzazione del semaforo a 0
    if (semctl(sem_id2, 0, SETVAL, 0) == -1)
    {
        printf("Errore inizializzazione secondo semaforo");
        exit(EXIT_FAILURE);
    }

    // Struct per le operazioni signal/wait dei vari semafori
    struct sembuf wait_op = {0, -1, 0};
    struct sembuf wait2_op = {0, -2, 0};
    struct sembuf signal_op = {0, 1, 0};

    // Genero segmento di memoria condivisa - ARRAY PID 
    size_t size_pid = sizeof(int)*3;
    shmid_pid = shmget(key_pid, size_pid, IPC_CREAT | S_IRUSR | S_IWUSR);
    array_pid = (int *)shmat(shmid_pid, NULL, 0);

    if (array_pid == (void *)-1)
    {
        printf("Errore nell'attach della memoria condivisa");
        exit(EXIT_FAILURE);
    }

    // Salvo nell'array pid il pid del Server
    array_pid[0] = getpid();
    
    // Genero segmento di memoria - NOMI GIOCATORI
    size_t size_nomi = sizeof(char)*200;
    shmid_player1 = shmget(key_player1, size_nomi, IPC_CREAT | S_IRUSR | S_IWUSR);
    player1 = (char *)shmat(shmid_player1, NULL, 0);
    shmid_player2 = shmget(key_player2, size_nomi, IPC_CREAT | S_IRUSR | S_IWUSR);
    player2 = (char *)shmat(shmid_player2, NULL, 0);

    if (player1 == (void *)-1 || player2 == (void *)-1)
    {
        printf("Errore nell'attach della memoria condivisa per i nomi dei player");
        exit(EXIT_FAILURE);
    }


    // Genero segmento di memoria condivisa - VALUE
    size_t size = sizeof(int);
    shmid = shmget(key, size, IPC_CREAT | S_IRUSR | S_IWUSR);
    value = (int *)shmat(shmid, NULL, 0);

    if (value == (void *)-1)
    {
        printf("Errore nell'attach della memoria condivisa");
        exit(EXIT_FAILURE);
    }

    // Genero segmento di memoria condivisa - ARRAY DI 4 INTERI PER DIMENSIONI DEL TABELLONE + PEDINE
    size_t size_dimensione = sizeof(int)*4;
    shmid_dimensione = shmget(key_dimensione, size_dimensione, IPC_CREAT | S_IRUSR | S_IWUSR);
    dimensione = (int *)shmat(shmid_dimensione, NULL, 0);

    if (dimensione == (void *)-1)
    {
        printf("Errore nell'attach della memoria condivisa");
        exit(EXIT_FAILURE);
    }

    // Setto dimensioni tabellone con argomenti passati da bash
    dimensione[0] = atoi(argv[1]); // Righe
    RIGHE = dimensione[0];
    dimensione[1] = atoi(argv[2]); // Colonne
    COLONNE = dimensione[1];

    // Inserisco nella stessa memoria le pedine scelte
    dimensione[2] = (int)argv[3][0]; //prima pedina
    dimensione[3] = (int)argv[4][0]; //seconda pedina

    // Genero segmento di memoria condivisa - TABELLONE
    size_tabellone = sizeof(int) * RIGHE * COLONNE;
    shmid_tabellone = shmget(key_tabellone, size_tabellone, IPC_CREAT | S_IRUSR | S_IWUSR);
    tabellone = (int *)shmat(shmid_tabellone, NULL, 0);

    if (tabellone == (void *)-1)
    {
        printf("Errore nell'attach della memoria condivisaaaa");
        exit(EXIT_FAILURE);
    }

    // Pulizia del tabellone di gioco
    reset_tabellone(tabellone, size_tabellone);

    // Genero segmento di memoria condivisa - INTERO PER VERIFICARE VITTORIA
    size_t size_vittoria = sizeof(int);
    shmid_vittoria = shmget(key_vittoria, size_vittoria, IPC_CREAT | S_IRUSR | S_IWUSR);
    vittoria = (int *)shmat(shmid_vittoria, NULL, 0);

    if (vittoria == (void *)-1)
    {
        printf("Errore nell'attach della memoria condivisa");
        exit(EXIT_FAILURE);
    }

    
    // Avviso che il campo di gioco è stato impostato
    printf("Campo di gioco impostato a %s x %s\n",argv[1],argv[2]);


    // Aspetto che ci siano 2 client alive
    printf("Attendo gicatori... \n");

    // Wait doppio per l'attesa dei 2 client
    if (semop(sem_id2, &wait2_op, 1) == -1)
    {
        // Errno relativo ad eventuali interrupt
        if(errno == 4){
            if (semop(sem_id2, &wait2_op, 1) == -1)
            {
                printf("Errore wait secondo semaforo");
                exit(EXIT_FAILURE);
            }
        } 
        else
        {
            printf("Errore wait secondo semaforo");
            exit(EXIT_FAILURE);
        }
        
    }
    

    // Qua Server arriva SSE si sono presentati due Client
    printf("\nRilevati 2 client\n");


    // I Client sono bloccati in semaforo attesa per evento (nostro nominato mutex) in attesa di essere numerati
    // Ne sblocco uno

    *value = 0;
    printf("\n\n\nPuo iniziare la partita\n\n");
    if (semop(sem_mutex, &signal_op, 1) == -1)
    {
        printf("Errore signal del mutex");
        exit(EXIT_FAILURE);
    }

    // Struct per le operazioni signal dei nostri player
    struct sembuf signal_players[2] = 
    {
        {0, 1, 0},
        {1, 1, 0}

    };

    // Set variabili relative alla vittoria
    *vittoria = 0;
    int checkWinBro = 0;

    // Funzione principale di gioco
    while (1)
    {
        // Variabile per set turno player
        turn = 0;

        // Reset vittoria
        *vittoria = 0;

        // Variabile per conteggio pedine inserite
        int count_par = 1;

        // Reset del tabellone
        reset_tabellone(tabellone, size_tabellone);

        // Finche vittoria non raggiunta
        while (*vittoria == 0)
        {
            printf("Tocca a Giocatore %i - (%s)\n", (turn % 2) + 1,(((turn % 2)+1)==1)?player1:player2);

            // Sblocco giocatore 1 o 2
            semop(sem_array, &signal_players[turn % 2], 1);

            // Aspetto  giocata giocatore
            allarme = 1;

            // Lancio allarme
            alarm(20); 

            printf("Attendo giocata ...\n\n");
            
            // Aspetto mosssa giocatore
            if(semop(sem_id2, &wait_op, 1) == -1)
            {
                if(errno == 4)
                {
                    // Errno relativo ad eventuali interrupt
                    if (semop(sem_id2, &wait_op, 1) == -1)
                    {
                        printf("Errore wait secondo semaforo\n");
                        exit(EXIT_FAILURE);
                    }

                }
                else
                {
                    printf("Errore wait secondo semaforo\n");
                    exit(EXIT_FAILURE);
                }
            }
            allarme = 0;

            // Controllo esito tabellone
            checkWinBro = checkWin(tabellone, (turn%2)==0 ? pown1 : pown2);

            //Controllo stato partita
            if (checkWinBro == 1)
            {
                printf("Vittoria del giocatore #%i, complimenti!!\n\n", (turn % 2) + 1);
                *value = (turn % 2) + 1;
            }
            else if(checkWinBro == 0 && count_par == RIGHE*COLONNE)
            {
                printf("Complimenti ad entrambi, avete pareggiato!\n\n\n\n\n\n");
                *value = -1;
                checkWinBro = 1;
            }
            
            // Aggiorno variabile vittoria per eventuale uscita dal while-loop
            *vittoria = checkWinBro;

            // Sblocco altro player 
            semop(sem_array, &signal_players[turn % 2], 1);

            // Cambio turno
            turn++; 
            
            // Incremento counter relativo al conteggio delle pedine inserite
            count_par++;
        }

        // Questo serve per sbloccare player che non ha vinto ma che era rimasto bloccato
        if (*vittoria == 1)
        {
            semop(sem_array, &signal_players[turn % 2], 1);
        }
        printf("Partita terminata\n\n\n");
        printf("Setto la stanza per nuovo match....\n\n");

        // Mi metto in attesa che i player siano pronti per eventuale rigioco
        if (semop(sem_id2, &wait2_op, 1) == -1)
        {
            // Errno relativo al interrupt della CTRL-C (Interrupted system call)
            if(errno == 4)
            {
                if (semop(sem_id2, &wait2_op, 1) == -1)
                {
                    printf("Errore wait secondo semaforo\n");
                    exit(EXIT_FAILURE);
                }
            } 
            else
            {
                printf("Errore wait secondo semaforo\n");
                exit(EXIT_FAILURE);
            }
        }
    }

    // Fase finale pulizia memoria
    clear_ipcs();

    return EXIT_SUCCESS;
}

/**
 * Funzione di controllo e di riscotro in caso di vittoria
 * @param int campo di gioco in shared memory
 * @param int giocatore corrente
 * @return int che inidcherà ad 1 se vinto, 0 no
 */
int checkWin(int *matrix, int player)
{
    
    // Conta le pedina di fila
    int counter = 0;

    // CONTROLLO ORRIZONTALE - righe
    for (int righe = RIGHE - 1; righe >= 0; righe--)
    {
        counter = 0;

        // Controllo se le pedine gia prese e le pedine probabili rimanenti sono maggiori di 4
        for (int clm = 0; clm < COLONNE && (counter + (COLONNE - clm) >= 4); clm++)
        {
            if (matrix[convertPos(righe, clm)] == player)
            {
                counter++; // Aumento la streak
            }
            else
            {
                counter = 0; // Resetto la streak
            }
            // Guardo se ho vinto
            if (counter == 4)
                return 1;
        }
    }

    // CONTROLLO VERTICALE - colonne
    for (int clm = 0; clm < COLONNE; clm++)
    {
        counter = 0;

        // Controllo se le pedine gia prese e le pedine probabili rimanenti sono maggiori di 4
        for (int row = RIGHE - 1; row >= 0 ; row--)
        {
            if (matrix[convertPos(row, clm)] == player)
            {
                counter++; // Aumento la streak
            }
            else
            {
                counter = 0; // Resetto la streak
            }
            // Guardo se ho vinto
            if (counter == 4)
                return 1;
        }
    }

    //  CONTROLLO DIAGONALE SX-DX DAL BASSO
    for (int righe = RIGHE - 1; righe + 1 >= 4; righe--)
    {
        // ciclo le colonne
        for (int clm = 0; COLONNE - clm >= 4; clm++)
        {
            // QUI PARTE IL CONTROLLO DIAGONALE 
            counter = 0;
            int righe2 = righe;
            int clm2 = clm;
            while (righe2 >= 0 && clm2 < COLONNE)
            {
                if (matrix[convertPos(righe2, clm2)] != player)
                {
                    counter = 0; // Azzero la streak
                }
                else
                {
                    counter++; // Aumento la streak
                }
                if (counter == 4)
                    return 1; 

                // Aumento in diagonale gli indici da controllare
                righe2--;
                clm2++;
            }
        }
    }

    //  CONTROLLO DIAGONALE DX-SX DAL BASSO
    for (int righe = RIGHE - 1; righe + 1 >= 4; righe--)
    {
        // ciclo le colonne
        for (int clm = COLONNE - 1; clm >= 0; clm--)
        {
            // QUI PARTE IL CONTROLLO DIAGONALE 
            counter = 0;
            int righe2 = righe;
            int clm2 = clm;
            while (righe2 >= 0 && clm2 >= 0)
            {
                if (matrix[convertPos(righe2, clm2)] != player)
                {
                    counter = 0; // aAzzero la streak
                }
                else
                {
                    counter++; // Aumento la streak
                }
                if (counter == 4)
                    return 1;

                // Aumento in diagonale gli indici da controllare
                righe2--;
                clm2--;
            }
        }
    }

    return 0; // nessuna vittoria
}

/**
 * Funzione di conversione di riga/colonna di forma bidimensionale di matrice a forma lineare di indice di array
 * @param int row indica la posizione della riga della matrice
 * @param int column indica la posizione della colonna della matrice
 * @return int che inidcherà la posizione rispettiva al nostro array
 */
int convertPos(int row, int column)
{
    return column + (COLONNE * (row));
}

/**
 * Funzione usata per gestire il CTRL C
 * 
 * @param int campo di gioco in shared 
 * @param int size del tabellone
 * @return void
 */
void reset_tabellone(int *tabellone, int size_tabellone)
{
    for (int i = 0; i < size_tabellone; i++)
    {
        tabellone[i] = 0;
    }
}

/**
 * Funzione di controllo relativo ai parametri
 * @param char parametro utente
 * @return int 1, parametro corretto
 */
int checkInput(char* input)
{
    int pos = 0;
    while(input[pos]!='\0'){
        if(!(input[pos]>='0' && input[pos]<='9'))
            return 0;
        pos++;
    }
    return 1;
}

/**
 * Chiusura shared e semafori
 * 
 * @return void
 */
void clear_ipcs(void)
{
    reset_tabellone(tabellone,size_tabellone);

    //  detach dalla shared memory value
    if (shmdt(value) == -1)
    {
        printf("Errore detach memory value");
        exit(EXIT_FAILURE);
    }

    //  detach dalla shared memory tabellone
    if (shmdt(tabellone) == -1)
    {
        printf("Errore detach memory tabellone");
        exit(EXIT_FAILURE);
    }

    //  detach dalla shared memory variabile vittoria
    if (shmdt(vittoria) == -1)
    {
        printf("Errore detach memory tabellone");
        exit(EXIT_FAILURE);
    }

    //  detach dalla shared memory dimensione
    if (shmdt(dimensione) == -1)
    {
        printf("Errore detach memory value");
        exit(EXIT_FAILURE);
    }


    //  detach dalla shared memory pid
    
    if (shmdt(array_pid) == -1)
    {
        printf("Errore detach memory value");
        exit(EXIT_FAILURE);
    }


    // rimozione della shared memory

    if (shmctl(shmid, IPC_RMID, NULL) == -1)
    {
        printf("Errore rimozione shared shmid");
        exit(EXIT_FAILURE);
    }

    // rimozione della shared memory

    if (shmctl(shmid_tabellone, IPC_RMID, NULL) == -1)
    {
        printf("Errore rimozione shared shmid_tabellone");
        exit(EXIT_FAILURE);
    }

     // rimozione della shared memory

    if (shmctl(shmid_dimensione, IPC_RMID, NULL) == -1)
    {
        printf("Errore rimozione shared shmid");
        exit(EXIT_FAILURE);
    }  

    // rimozione della shared memory

    if (shmctl(shmid_pid, IPC_RMID, NULL) == -1)
    {
        printf("Errore rimozione shared shmid_pid");
        exit(1);
    }

    // rimozione del semaforo
    if (semctl(sem_id, 0, IPC_RMID, 0) == -1)
    {
        printf("Errore rimozione primo semaforo");
        exit(EXIT_FAILURE);
    }

    // rimozione del semaforo2
    if (semctl(sem_id2, 0, IPC_RMID, 0) == -1)
    {
        printf("Errore rimozione secondo semaforo");
        exit(EXIT_FAILURE);
    }

    // rimozione del array di semafori
    if (semctl(sem_array, 0, IPC_RMID, 0) == -1)
    {
        printf("Errore rimozione array di semafori");
        exit(EXIT_FAILURE);
    }

    // rimozione di semaforo mutex
    if (semctl(sem_mutex, 0, IPC_RMID, 0) == -1)
    {
        printf("Errore rimozione mutex");
        exit(EXIT_FAILURE);
    }

    // rimozione della shared memory

    if (shmctl(shmid_vittoria, IPC_RMID, NULL) == -1)
    {
        printf("Errore rimozione shared shmid_vittoria");
        exit(EXIT_FAILURE);
    }


    // rimozione della shared memory

    if (shmctl(shmid_player1, IPC_RMID, NULL) == -1)
    {
        printf("Errore rimozione shared player1");
        exit(EXIT_FAILURE);
    }

     // rimozione della shared memory

    if (shmctl(shmid_player2, IPC_RMID, NULL) == -1)
    {
        printf("Errore rimozione shared player2");
        exit(EXIT_FAILURE);
    }

}

/**
 * Funzione usata per gestire il CTRL C
 * 
 * @param int sig parametro che inidica il segnale preso in ingresso
 * @return void
 */
void ctrlcHandler(int sig) 
{ 
    
    if (!shutDown)
    {
        printf("\nAttenzione, al prossimo ctrl+c il server terminerà\n");// se il server riceve due ctrl+c terminerà il server e qui avvisa il gestore che ne ha appena ricevuto uno
        shutDown++;
    }
    else
    {
        printf("\nServer terminato!\n");
        int store1 = array_pid[1];
        int store2 = array_pid[2];
        array_pid[1] = -1;
        array_pid[2] = -1;
        if (store1 != 0)
        {
            kill(store1, SIGUSR1);
        }
        if (store2 != 0)
        {
            kill(store2, SIGUSR1);
        }

        clear_ipcs();
        exit(EXIT_SUCCESS);
    }
    
}


/**
 * Funzione usata per gestire il SIGALRM
 * 
 * @param int sig parametro che inidica il segnale preso in ingresso
 * @return void
 */
void alarmHandler(int sig)
{
    if(allarme){
        printf("\nTempo scaduto, vittoria a tavolino per %s \n",(((turn % 2)+1)==1)?player2:player1);
        int pidMorto = array_pid[(turn % 2)+1];
        array_pid[(turn % 2)+1] = 0;
        kill(pidMorto, SIGUSR2);
        turn++;
        (((turn % 2)+1)==1)? kill(array_pid[1], SIGUSR1):kill(array_pid[2], SIGUSR1);
        clear_ipcs();
        exit(EXIT_SUCCESS);
    }
        
}

/**
 * Funzione per gestire SIGUSR1 e SIGUSR2
 * 
 * @param int parametro che inidica il segnale preso in ingresso
 * @return void
 */void handlerVittoriaTavolino(int sig)
{
    
    if(array_pid[1] && array_pid[2]){
        kill(array_pid[1], SIGUSR1);
        kill(array_pid[2], SIGUSR1); 
        printf("\nTavolo chiuso!\n"); 
    }
    else
    {
        int pidVincente = 0;
        if(array_pid[1]){
            pidVincente = array_pid[1];
        }
        else if(array_pid[2]){
            
            pidVincente = array_pid[2];
        }
        else{   
            printf("\nServer terminato! \n"); 
            clear_ipcs();
            exit(1);
        }
        kill(pidVincente, SIGUSR1); 
        if(*value>=2 && (!array_pid[1] || !array_pid[2]) ){
            printf("\nServer terminato! (Vittoria tavolino) \n"); 
        }
        else{
            printf("\nServer terminato! \n"); 
        }        
    }
    clear_ipcs();
    exit(EXIT_SUCCESS);

}