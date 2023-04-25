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

// CTRL C
int giocatore;

int dopo_menu = 0;
int primo_menu = 0;

// memoria condivisa e semafori

// Semaforo Mutex
int sem_mutex;

// Array di Semafori - controllo turno giocata
int sem_array;

// semaforo 1 --> i player arrivano e lo decrementano
int sem_id;

// semaforo 2 --> per attesa dei due player nella lobby
int sem_id2;

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

int main(int argc, char *argv[])
{

    // controllo parametri input
    // DEVE esserci un nome, e puo esserci dopo un asterisco per farlo giocare in modalita auto
    if (argc < 2)
    {
        printf("Numero parametri inseriti invalido! %d\n", argc);
        return -1;
    }
    if (argc == 3 && !(argv[2][0] == '*' && argv[2][1] == '\0'))
    {
        printf("Parametro giocatore automatico invalido\n");
        return -1;
    }

    if (strlen(argv[1]) > 199)
    {
        printf("Nome giocatore troppo lungo\n");
        return -1;
    }

    // gestisco i segnali, qui il ctrl+c
    if (signal(SIGINT, ctrlcHandler) == SIG_ERR)
    {
        exit(-1);
    }

    if (signal(SIGHUP, ctrlcHandler) == SIG_ERR)
    {
        exit(-1);
    }

    // gestisco i segnali, qui la vittoria a tavolino
    if (signal(SIGUSR1, handlerVittoriaTavolino) == SIG_ERR)
    {
        exit(-1);
    }

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
        perror("Errore nella generazione della chiave");
        return -1;
    }

    sem_id;
    // prendo il semaforo
    if ((sem_id = semget(key_sem, 1, 0666 | IPC_CREAT)) == -1)
    {
        exit(1);
    }

    sem_id2;
    // prendo il semaforo
    if ((sem_id2 = semget(key_sem2, 1, 0666 | IPC_CREAT)) == -1)
    {
        exit(1);
    }

    sem_mutex;
    // prendo mutex
    if ((sem_mutex = semget(key_sem_mutex, 1, 0666 | IPC_CREAT)) == -1)
    {
        printf("suca0_1");
        exit(1);
    }

    sem_array;
    // prendo Array di Semafori
    if ((sem_array = semget(key_sem_array, 2, 0666 | IPC_CREAT)) == -1)
    {
        printf("Errore creazione Array di semafori ");
        exit(1);
    }

    // prendo segmento di memoria condivisa - ARRAY PID
    size_t size_pid = sizeof(int) * 3;
    shmid_pid = shmget(key_pid, size_pid, IPC_CREAT | S_IRUSR | S_IWUSR);
    array_pid = (int *)shmat(shmid_pid, NULL, 0);

    if (array_pid == (void *)-1)
    {
        perror("Errore nell'attach della memoria condivisa");
        exit(EXIT_FAILURE);
    }

    if (!array_pid[0])
    { // check server off
        printf("Server offline, quitto!\n");
        if (shmdt(array_pid) == -1)
        {
            printf("Errore detach memory value");
            exit(1);
        }
        exit(1);
    }

    // operazioni wait e signal
    struct sembuf wait_op = {0, -1, 0};
    struct sembuf signal_op = {0, 1, 0};

    if (semop(sem_id, &wait_op, 1) == -1)
    {
        if (errno == 4)
        {
            if (semop(sem_id, &wait_op, 1) == -1)
            {
                printf("Primissima wait errore\n");
                exit(1);
            }
        } // per colpa della ctrlc
        else
        {
            printf("Primissima wait errore\n");
            exit(1);
        }
    }

    // prendo segmento di memoria condivisa per Value
    size_t size = sizeof(int);
    shmid = shmget(key, size, IPC_CREAT | S_IRUSR | S_IWUSR);
    value = (int *)shmat(shmid, NULL, 0);

    // Checcko se attach riuscito
    if (value == (void *)-1)
    {
        perror("Errore nell'attach della memoria condivisa");
        exit(EXIT_FAILURE);
    }

    // prendo segmento di memoria condivisa - INTERO PER VERIFICARE VITTORIA
    size_t size_vittoria = sizeof(int);
    shmid_vittoria = shmget(key_vittoria, size_vittoria, IPC_CREAT | S_IRUSR | S_IWUSR);
    vittoria = (int *)shmat(shmid_vittoria, NULL, 0);

    // Checcko se attach riuscito
    if (vittoria == (void *)-1)
    {
        perror("Errore nell'attach della memoria condivisa");
        exit(EXIT_FAILURE);
    }

    // Genero segmento di memoria condivisa - ARRAY DI 4 INTERI PER DIMENSIONI DEL TABELLONE + PEDINE
    size_t size_dimensione = sizeof(int) * 4;
    shmid_dimensione = shmget(key_dimensione, size_dimensione, IPC_CREAT | S_IRUSR | S_IWUSR);
    dimensione = (int *)shmat(shmid_dimensione, NULL, 0);

    // Checcko se attach riuscito
    if (dimensione == (void *)-1)
    {
        perror("Errore nell'attach della memoria condivisa");
        exit(EXIT_FAILURE);
    }

    // genero segmento di memoria array per i nomi
    size_t size_nomiArray = sizeof(char) * 200;
    shmid_player1 = shmget(key_arrayNomi, size_pid, IPC_CREAT | S_IRUSR | S_IWUSR);
    player1 = (char *)shmat(shmid_player1, NULL, 0);
    shmid_player2 = shmget(key_arrayNomi1, size_pid, IPC_CREAT | S_IRUSR | S_IWUSR);
    player2 = (char *)shmat(shmid_player2, NULL, 0);

    int gameMode = 0;
    int rigioco;
    printIntroGame();
    do
    {
        printf("SCEGLI: ");
        scanf(" %i: ", &gameMode);
    } while (gameMode != 1 && gameMode != 2);

    // BHE TECNICAMENTE QUI SERVIREBBE UN MUTEX A VALUE, MA NEL 99,999% DEI CASI IL PROGRAMMA NON DA PROBLEMI PERCHE RISPETTO ALLA CPU L'UOMO NEL PREMERE INVIO A TERMINALE E' STRA LENTO
    giocatore = *value; // salvo che giocatore sono
    *value += 1;
    // metto il nome del giocatore nel rispettivo slot
    (giocatore == 0) ? strcpy(player1, argv[1]) : strcpy(player2, argv[1]);

    dopo_menu = 1;
    primo_menu = 1;
    rigioco = 0;

    if (gameMode == 2)
    {
        libera_posto_occupato();
        *value -= 1;
        exit(1);
    }

    printf("\n\nIn attesa di giocatore ... \n\n\n");
    // Manda un singolo ack al Server (1/2)
    if (semop(sem_id2, &signal_op, 1) == -1)
    {
        exit(1);
    }

    // ACCEDO IN MUTEX con semaforo a 0 A SHARED MEMORY PER SAPERE SE SONO GIOCATORE 1 O 2
    // nb: la shared memory viene creata da server che poi mette il semaforo ad 1

    // decremento mutex
    // leggo che giocatore sono
    // incremento mutex
    if (semop(sem_mutex, &wait_op, 1) == -1)
    {
        if (errno == 4)
        {
            if (semop(sem_mutex, &wait_op, 1) == -1)
            {
                printf("Errore wait secondo semaforo\n");
                exit(1);
            }
        } // per colpa della ctrlc
        else
        {
            printf("Errore wait secondo semaforo\n");
            exit(1);
        }
    }
    // giocatore = *value; // salvo che giocatore sono
    int value_stored = giocatore;
    int startMatch = 1;
    giocatore++;
    array_pid[giocatore] = getpid();
    //*value += 1;
    if (semop(sem_mutex, &signal_op, 1) == -1)
    {
        exit(1);
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
        perror("Errore nell'attach della memoria condivisa");
        exit(EXIT_FAILURE);
    }

    while (gameMode != 2)
    {

        printf("%s sei giocatore #%i\n", argv[1], giocatore);
        printf("Pedina utilizzata: %c", (giocatore == 1) ? dimensione[2] : dimensione[3]);
        struct sembuf wait_player = {giocatore - 1, -1, 0};
        struct sembuf signal_player = {giocatore - 1, 1, 0};
        int mosse = 0;
        int chosenColumn = 0;

        printf("\n\nDUE PLAYER: %s & %s\n", player1, player2);

        do
        {
            if (semop(sem_array, &wait_player, 1) == -1)
            {
                if (errno == 4)
                {
                    if (semop(sem_array, &wait_player, 1) == -1)
                    {
                        printf("exit 130 sono qua");
                        exit(1);
                    }
                }
                else if (errno == 43)
                {
                    printf(" ");
                }
                else
                {
                    printf("%i", errno);
                    printf("exit 130");
                    exit(1);
                }
            }

            if (*vittoria != 1)
            {
                if (startMatch)
                {
                    printTable(tabellone, 2);
                    startMatch = 0;
                }
                else
                {
                    printTable(tabellone, 0);
                }

                do
                {
                    do
                    {
                        char temp_str[9] = " ";
                        printf("%s usa pedina: %c\nSCEGLI COLONNA: ", argv[1], (giocatore == 1) ? dimensione[2] : dimensione[3]);
                        scanf("%s",temp_str);
                        chosenColumn = atoi(temp_str);
                    } while (!chosenColumn);

                    mosse++;
                } while (!putPawn(chosenColumn, (giocatore == 1) ? dimensione[2] : dimensione[3], tabellone));
                printTable(tabellone, 1);
                if (semop(sem_id2, &signal_op, 1) == -1)
                {
                    printf("exit 140");
                    exit(1);
                }

                if (semop(sem_array, &wait_player, 1) == -1)
                {
                    if (errno == 4)
                    {
                        if (semop(sem_array, &wait_player, 1) == -1)
                        {
                            printf("Exit 130 maso\n");
                            exit(1);
                        }
                    }
                    else
                    {
                        printf("Exit 130 vero\n");
                        exit(1);
                    }
                }
            }

        } while (*vittoria == 0);

        if (*value == -1)
        { // in caso di pareggio
            printf("Pareggio!\n\n\n\n\n");
        }
        else if (giocatore == *value)
        {
            printf("Complimenti hai vinto il match!\n");
        }
        else
        {
            printf("Il tuo avversario ha vinto il match!\n");
        }

        startMatch = 1;
        rigioco = 1;

        if (rigioco)
        {

            printIntroGame();
            dopo_menu = 0;
            do
            {
                printf("SCEGLI: ");
                scanf(" %i: ", &gameMode);
            } while (gameMode != 1 && gameMode != 2);

            // Manda un singolo ack al Server (1/2)
            if (semop(sem_id2, &signal_op, 1) == -1)
            {
                exit(1);
            }
            dopo_menu = 1;

            startMatch = 1;
        }

        if (gameMode == 2)
        {
            // AVVISIAMO il server che sto quittando
            // rimuovo il mio pid dall'array
            // array_pid[giocatore] = 0;
            kill(array_pid[0], SIGUSR1); // mando sigUSER1 a server
            printf("Abbandono il tavolo!\n");
            exit(1);
        }
    }

    return 0;
}

/*
FUNZIONE CHE ACCETTA UNA COLONNA NELLA QUALE BUTTARE LA PEDINA
RETURN: status operazione 0 se fallita o 1 se successo


1 2 3 4 5
x x x x x
x x x x 0
x 0 x x 0
0 0 x 0 0

x x x x x /x x x x x x x x x x x x x x x x x x x x x x x x x
*/
int putPawn(int column, int player, int *matrix)
{
    int check = 0;
    // check se la colonna esiste
    if (column <= 0 || column > COLONNE)
    {
        printf("Colonna non esistente! Scegline un'altra!\n");
        return 0;
    }
    // check se c'è spazio
    for (int i = RIGHE - 1; i >= 0 && !check; i--)
    {
        // ipotizzo che -1 significhi spazio libero
        if (matrix[convertPos(i, column)] == 0) // controllo se spazio libero
        {
            matrix[convertPos(i, column)] = player; // piazzo pedina
            check = 1;
        }
    }

    if (!check) // se non ho piazzato nessuna pedina perchè non c'è spazio ritorno 0
    {
        printf("Colonna piena! Scegline un'altra!\n");
        return 0;
    }
    return 1;
}

void printTable(int *matrix, int mode)
{

    if (mode == 2)
    {
        printf("\nMatch iniziato!\n");
    }
    else if (mode == 1)
    {
        printf("\nMossa eseguita!\n");
    }
    else
    {
        printf("\n\n\n");
        printf("Mossa dell'aversario!\n");
    }
    int pos = 0;
    for (int row = 0; row < RIGHE; row++)
    {
        for (int clm = 1; clm <= COLONNE; clm++)
        {
            pos = convertPos(row, clm);

            if (matrix[pos] == 0)
            {
                printf("▢ ");
            }
            else
            {
                printf("%c ", (char)matrix[pos]);
            }
        }
        printf("\n");
    }

    printf("\n");
    printf("\n");
}

/*
    converto da posizione riga/colonna ad indice array monodimensionale
    NB: ragioniamo dal basso
*/
int convertPos(int row, int column)
{
    column--;
    return column + (COLONNE * (row));
}

/*
    stampo logo e scelta gioco
*/
void printIntroGame()
{
    printf("    ______                          __ __    \n");
    printf("   / ____/____ _________  ____ _   / // /  ◉ \n");
    printf("  / /_  / __  / ___/_  / / __ `/  / // /_  ◉ \n");
    printf(" / __/ / /_/ / /    / /_/ /_/ /  /__  __/  ◉ \n");
    printf("/_/   /_____/_/    /___/___,_/     /_/     ◉ \n\n");
    printf("MENÜ:\n1) GIOCA 1V1\n2) QUIT\n\n");
}

void ctrlcHandler(int sig)
{
    if (primo_menu)
    {

        if (!(array_pid[1] && array_pid[2]))
        {
            printf("\n\nUtente ritirato!\n");
        }
        else
        {
            printf("\n\n\nMi ritiro\n");
        }

        if (dopo_menu)
        {
            array_pid[giocatore] = 0;
        }
        kill(array_pid[0], SIGUSR1); // mando sigUSER1 a server
        // ATTENZIONE, VA COMUNICATO AL SERVER CHE MI SON RITIRATO E QUINDI L'ALTRO CLIENT VINCE A TAVOLINO
        exit(1);
        // DOBBIAMO INFORMARE I CLIENT CHE SERVER TERMINATO, ci servono i loro pid probabilmente
    }
    else
    {
        libera_posto_occupato();
        exit(1);
    }
}

void libera_posto_occupato(void)
{
    struct sembuf signal_op = {0, 1, 0};
    struct sembuf wait_op = {0, -1, 0};
    if (semop(sem_id, &signal_op, 1) == -1)
    {
        exit(1);
    }
}

void handlerVittoriaTavolino(int sig)
{

    if (array_pid[1] == -1 && array_pid[2] == -1)
    {
        printf("\nIl tavolo è chiuso per chiusura del server\n");
        exit(1);
    }
    if (array_pid[1] && array_pid[2])
    {
        printf("Il tavolo è chiuso per abbandono\n");
        exit(1);
    }
    else
    {
        printf("\n\nHo vinto a tavolino\n");
        exit(1);
    }
}