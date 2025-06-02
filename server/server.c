// === server.c (con un solo thread per partita) ===
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT 8080
#define MAX_MATCH 10
#define MAX_PLAYERS 10
#define MAX_REQUESTS 5
#define BUFFER_SIZE 1024

#define USERNAME_MAX 32

typedef struct Match Match;
void *handlePlayer(void *arg);
void deleteJoinRequestForAllPlayers(Match *match);

typedef enum {
    NONE = -1,
    PENDING,
    ACCEPTED,
    REJECTED
} RequestStatus;

typedef enum {
    WAITING,
    VICTORY, 
    DRAW,
    IN_PROGRESS
} MatchStatus;

typedef struct {
    Match *match;
    RequestStatus status;
} PlayerJoinRequest;

typedef struct {
    char username[USERNAME_MAX];
    int client_fd;

    PlayerJoinRequest requests[MAX_REQUESTS];
    int num_requests;
} Player;

typedef struct {
    Player *player;
    RequestStatus status;
} JoinRequest;

struct Match {  // definizione completa
    int id;
    Player *player1;
    Player *player2;
    int turno;
    char board[3][3];

    MatchStatus status;
    JoinRequest requests[MAX_REQUESTS];
};


Match *matches[MAX_MATCH];
pthread_mutex_t matches_mutex = PTHREAD_MUTEX_INITIALIZER;

Player *players[MAX_PLAYERS];
pthread_mutex_t players_mutex = PTHREAD_MUTEX_INITIALIZER;


void initBoard(Match *p) {
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            p->board[i][j] = ' ';
}

bool ask_continue(int client_fd) {
    char buffer[BUFFER_SIZE];
    dprintf(client_fd, "LOBBY: Vuoi giocare un'altra partita? (si/no)\n");

    int n = read(client_fd, buffer, BUFFER_SIZE - 1);
    if (n <= 0) {
        return 0;
    }

    buffer[n] = '\0';
    buffer[strcspn(buffer, "\n")] = '\0';  

    if (strcasecmp(buffer, "si") == 0) return 1;
    return 0;
}

void reset_match (Match* p) {
    initBoard(p);
    p->turno = 1;

}

MatchStatus check_status(Match *p) {
    char (*b)[3] = p->board;

    for (int i = 0; i < 3; i++) {
        if (b[i][0] != ' ' && b[i][0] == b[i][1] && b[i][1] == b[i][2])
            return VICTORY;
        if (b[0][i] != ' ' && b[0][i] == b[1][i] && b[1][i] == b[2][i])
            return VICTORY;
    }

    if (b[0][0] != ' ' && b[0][0] == b[1][1] && b[1][1] == b[2][2])
        return VICTORY;
    if (b[0][2] != ' ' && b[0][2] == b[1][1] && b[1][1] == b[2][0])
        return VICTORY;

    int full = 1;
    for (int i = 0; i < 3 && full; i++)
        for (int j = 0; j < 3 && full; j++)
            if (b[i][j] == ' ')
                full = 0;

    if (full)
        return DRAW;

    return IN_PROGRESS;
}


void sendBoard(int fd, Match *p) {
    dprintf(fd, "[GRIGLIA]\n");
    char buf[128];
    for (int i = 0; i < 3; i++) {
        snprintf(buf, sizeof(buf), " %c | %c | %c \n", p->board[i][0], p->board[i][1], p->board[i][2]);
        dprintf(fd, "%s", buf);
        if (i < 2) dprintf(fd, "---+---+---\n");
    }
}


void deleteMatch(Match *p) {
    pthread_mutex_lock(&matches_mutex);
    deleteJoinRequestForAllPlayers(p);
    for (int i = 0; i < MAX_MATCH; i++) {
        if (matches[i] == p) {
            matches[i] = NULL;
            break;
        }
    }
    
    pthread_mutex_unlock(&matches_mutex);
}

Match *trova_partita(int id) {
    if (matches[id]) //verifico che non sia nullo
        return matches[id];
    
    return NULL;
}

void crea_partita(Player *player) { //gestire caso se non ci sono spazi nell'array
    pthread_mutex_lock(&matches_mutex);
    for (int i = 0; i < MAX_MATCH; i++) {
        if (!matches[i]) {
            Match *p = malloc(sizeof(Match));
            memset(p, 0, sizeof(Match));
            p->id = i;
            p->player1 = player;
            p->player2 = NULL;
            p->turno = 1;
            p->status = WAITING;
            matches[i] = p;

            dprintf(player->client_fd, "CLEAR\n");
            dprintf(player->client_fd, "🆕 Partita creata con ID %d. Attendi avversari...\n", i);
            break;
        }
    }
    pthread_mutex_unlock(&matches_mutex);
    return;
}

Match *unisciti_a_partita(Match *p, Player *player) {
    if (p->player2 == NULL) {
        p->player2 = player;
        return p;
    }

    return NULL;
}

void showMatchesList(Player *player) {
    dprintf(player->client_fd, "Elenco Partite:\n");
    int count = 0;
    for (int i = 0; i < MAX_MATCH; i++) {
        if (matches[i] && matches[i]->player1 != player) {
            count++;
            if(matches[i]->status == WAITING)
                dprintf(player->client_fd, "  - ID: %d, Proprietario: %s, Stato: IN ATTESA\n", i,matches[i]->player1->username);
            else if(matches[i]->status == VICTORY || matches[i]->status == DRAW)
                dprintf(player->client_fd, "  - ID: %d, Proprietario: %s, Stato: TERMINATA\n", i,matches[i]->player1->username);
            else
                dprintf(player->client_fd, "  - ID: %d, Proprietario: %s, Stato: IN CORSO\n", i,matches[i]->player1->username);
            } 
    }
    if (count == 0) {
        dprintf(player->client_fd, " (Nessuna partita attiva)\n");
    }
}


void deleteJoinRequestForAllPlayers(Match *match){  //Elimina tutte le richieste di ingresso nei player per uno specifico match
    for(int i=0; i< MAX_PLAYERS; i++){
        if (!players[i]) continue;

        for(int j=0; j<players[i]->num_requests; j++)
            if(players[i]->requests[j].match==match){
                for(int k=j; k<players[i]->num_requests-1; k++)
                    players[i]->requests[k]= players[i]->requests[k+1];

                players[i]->num_requests--;
                break;
            }
    }
}

void deleteJoinRequestForPlayer(Player *player){
    for(int i=0; i<player->num_requests; i++){
        player->requests[i].match = NULL;
        player->requests[i].status = -1;
    }
    player->num_requests=0;
}

void handlePlayerDisconnect(Player *player){    //Viene chiamato alla disconnessione di un giocatore
    pthread_mutex_lock(&players_mutex);
    pthread_mutex_lock(&matches_mutex);
    for(int i=0; i< MAX_MATCH; i++){            //Elimino le partite di cui il giocatore è proprietario e tutti le richieste che i giocatori 
        if(matches[i]){ 
            if(matches[i]->player1==player){   //hanno fatto per quelle partite
                deleteJoinRequestForAllPlayers(matches[i]);
                free(matches[i]);
                matches[i]= NULL;
            }else{
                 for(int j=0; j<MAX_REQUESTS; j++)  //controllo se ci sono delle richieste in questa partita dal player che si sta disconnetendo
                     if(matches[i]->requests[j].player == player){
                        matches[i]->requests[j].player = NULL;
                        matches[i]->requests[j].status = -1;
                        break;
                     }
            }   
        }
    }
    pthread_mutex_unlock(&matches_mutex);
    close(player->client_fd);
    for(int i=0; i< MAX_PLAYERS; i++)   //Elimino il giocatore dall'elenco dei giocatori
        if(players[i] == player){
            free(player);
            players[i]=NULL;
            break;
        }
    
    pthread_mutex_unlock(&players_mutex);
}

bool addJoinRequest(Match *match, Player *player) {

    if(match->status != WAITING){
        dprintf(player->client_fd, "ERROR: Puoi richiedere l'accesso solo alle partite in attesa \n");
        return false;
    }

    // Check if the player already made a request
    for (int i = 0; i < MAX_REQUESTS; i++) {
        if (match->requests[i].player == player) {
            dprintf(player->client_fd, "ERROR: Hai già richiesto l'accesso \n");
            return false;
        }
    }

    // Find a free slot
    int index = -1;
    for (int i = 0; i < MAX_REQUESTS; i++) {
        if (match->requests[i].player == NULL) {
            index = i;
            break;
        }
    }

    if (index == -1) {
        dprintf(player->client_fd, "ERROR: Non c'è spazio per altre richieste \n");
        return false;  // No space
    }

    if (player->num_requests >= MAX_REQUESTS) {
        dprintf(player->client_fd, "ERROR: Hai già fatto troppe richieste \n");
        return false;  // No space
    }

    player->requests[player->num_requests].match = match;
    player->requests[player->num_requests].status = PENDING;
    player->num_requests++;
    match->requests[index].player = player;
    match->requests[index].status = PENDING;

    dprintf(player->client_fd, "INFO: Hai richiesto l'accesso per la partita %d, attendi di essere accettato\n", match->id);
    return true;
}

void handlePlayerLeavingMatch(Match *p){
        Player *current = (p->turno == 1) ? p->player1 : p->player2;
        Player *other = (p->turno == 1) ? p->player2 : p->player1;
        dprintf(other->client_fd, "INFO: L'altro giocatore ha lasciato la partita. Hai vinto, la partità verrà eliminata.\n");
        pthread_t tid;
        pthread_create(&tid, NULL, handlePlayer, other);
        pthread_detach(tid);
}

void backToMenu(Player *player){
    pthread_t tid;
    pthread_create(&tid, NULL, handlePlayer, player);
    pthread_detach(tid);

}

void *handleMatch(void *arg) {
    Match *p = (Match *)arg;
    char buffer[BUFFER_SIZE];
    p->status = IN_PROGRESS;
    initBoard(p);

    dprintf(p->player2->client_fd, "INFO: Connesso! Attendi il tuo turno...\n");

    while (1) {
        int current_fd = (p->turno == 1) ? p->player1->client_fd : p->player2->client_fd;
        int other_fd   = (p->turno == 1) ? p->player2->client_fd : p->player1->client_fd;

        dprintf(current_fd, "TURN: Tocca a te, inserisci riga e colonna (es. 1 2): \n");
        int n = read(current_fd, buffer, BUFFER_SIZE - 1);
        if (n <= 0) {
            Player *current = (p->turno == 1) ? p->player1 : p->player2;
            handlePlayerLeavingMatch(p);
            
            break;
        }

        buffer[n] = '\0';
        buffer[strcspn(buffer, "\n")] = '\0';  

        if (strcmp(buffer, "exit") == 0) {
            Player *current = (p->turno == 1) ? p->player1 : p->player2;
            handlePlayerLeavingMatch(p);
            
            break;
        }


        int row, col;
        if (sscanf(buffer, "%d %d", &row, &col) != 2 || row < 1 || row > 3 || col < 1 || col > 3) {
            dprintf(current_fd, "ERROR: Mossa non valida. Usa formato: riga colonna (es. 1 2)\n");
            continue;
        }

        row--; col--;  // da 1-based a 0-based

        if (p->board[row][col] != ' ') {
            dprintf(current_fd, "ERROR: Casella già occupata. Riprova.\n");
            continue;
        }

        p->board[row][col] = (p->turno == 1) ? 'X' : 'O';

        // Invio griglia aggiornata a entrambi
        sendBoard(p->player1->client_fd, p);
        sendBoard(p->player2->client_fd, p);
        MatchStatus status = check_status(p);

        if (status == VICTORY) {
            // gestisci fine partita e caso di vittoria:
            // il vincitore diventa propetario mentre il perdente viene disconnesso
            // gli viene chiesto se vuole continuare a giocare
            p->status = VICTORY;

            dprintf(current_fd, "Complimenti hai vinto la partita!\n");
            dprintf(other_fd, "Mi dispiace hai perso\n");
            Player *winner = (p->turno == 1) ? p->player1 : p->player2;
            Player *loser = (p->turno == 1) ? p->player2 : p->player1;
            backToMenu(loser);

            if(ask_continue(current_fd)) {
                reset_match(p);
                p->player1 = winner;
                p->player2 = NULL;
                backToMenu(winner);
                return NULL;
            }

            backToMenu(winner);

            break;
            
        }
        else if ( status == DRAW) {
            // gestisci fina partita e caso di pareggio:
            // viene chiesto ad entrambi i giocatori se hanno intensione di continuare a giocare
            //
            p->status = DRAW;
            dprintf(current_fd, "Avete pareggiato!\n");
            dprintf(other_fd, "Avete pareggiato!\n");

            if(ask_continue(current_fd) && ask_continue(other_fd)) {
                reset_match(p);
                continue;
            }
           else {
                backToMenu(p->player1);
                backToMenu(p->player2);
                break;
           }
        }
        else {
            dprintf(current_fd, "INFO: Attendi la mossa dell'avversario...\n");
            dprintf(other_fd, "INFO: L'avversario ha giocato in riga %d, colonna %d\n", row + 1, col + 1);
        }

        p->turno = (p->turno == 1) ? 2 : 1;
    }

    deleteMatch(p);
    return NULL;
}


void printJoinRequests(Player *player){
    dprintf(player->client_fd, "Le richieste per le tue partite sono:\n");
    bool requests = false;
    pthread_mutex_lock(&matches_mutex);
    for(int i=0; i< MAX_MATCH; i++){
        if(matches[i] && matches[i]->player1==player){
            requests = true;
            bool found = false;
            dprintf(player->client_fd, "   Partita id %d: \n", i);
            for(int j=0;j<MAX_REQUESTS; j++)
                if(matches[i]->requests[j].player != NULL ){
                    found = true;
                    if(matches[i]->requests[j].status == PENDING)
                        dprintf(player->client_fd, "\t %d) Player: %s, Stato Richiesta: IN ATTESA\n", j, matches[i]->requests[j].player->username);
                    else if (matches[i]->requests[j].status == REJECTED)
                        dprintf(player->client_fd, "\t %d) Player: %s, Stato Richiesta: RIFIUTATA\n", j, matches[i]->requests[j].player->username);
                    else
                        dprintf(player->client_fd, "\t %d) Player: %s, Stato Richiesta: ACCETTATA\n", j, matches[i]->requests[j].player->username);
                }
            
            if(!found)
                    dprintf(player->client_fd, "\tNessuna richiesta al momento \n");
            
        }
    }
    pthread_mutex_unlock(&matches_mutex);
    if(!requests){
        dprintf(player->client_fd, "ERROR:   Non hai partite attive al momento \n");
        return ;
    }
}

void setRequestState(Player *player, Match *match, RequestStatus status){
    for(int i=0; i<player->num_requests; i++){
        if(player->requests[i].match == match){
            player->requests[i].status = status;
            return;
        }
    }
}

void printMyRequests(Player *player){

    dprintf(player->client_fd, "CLEAR\n");
    dprintf(player->client_fd, "INFO: Elenco partite per cui hai richiesto l'accesso\n");
    if(player->num_requests == 0){
        dprintf(player->client_fd, "ERROR: Non hai fatto richiesto l'accesso a nessuna partita\n");
        return ;
    }

    for(int i=0; i<player->num_requests; i++)
        if(player->requests[i].status == PENDING)
            dprintf(player->client_fd, " - Partita ID: %d, Proprietario: %s, Stato: IN ATTESA \n",player->requests[i].match->id ,player->requests[i].match->player1->username);
        else if (player->requests[i].status == REJECTED)
            dprintf(player->client_fd, " - Partita ID: %d, Proprietario: %s, Stato: RIFIUTATA \n",player->requests[i].match->id ,player->requests[i].match->player1->username);
        else
            dprintf(player->client_fd, " - Partita ID: %d, Proprietario: %s, Stato: ACCETTATA \n",player->requests[i].match->id ,player->requests[i].match->player1->username);

}

int handleRequest(Player *player){
    dprintf(player->client_fd, "CLEAR\n");
    char buffer[BUFFER_SIZE];
    while(1){
        printJoinRequests(player);
        dprintf(player->client_fd, "Specifica idPartita e idRichiesta (es. 1 2) della richiesta\n "
                                    "LOBBY: oppure digita indietro per tornare al menu: \n");
        int n = read(player->client_fd, buffer, BUFFER_SIZE - 1);
        if (n <= 0) break;
        

        buffer[n] = '\0';
        buffer[strcspn(buffer, "\n")] = '\0';

        if (strcmp(buffer, "indietro") == 0) {
            dprintf(player->client_fd, "CLEAR\n");
            dprintf(player->client_fd, "🔙 Ritorno al menu.\n");
            return -1;
        }

        int matchId, requestId;
        if (sscanf(buffer, "%d %d", &matchId, &requestId) != 2 || matchId < 0 || matchId >= MAX_MATCH || requestId < 0 || requestId >= MAX_REQUESTS) {
            dprintf(player->client_fd, "CLEAR\n");
            dprintf(player->client_fd, "ERROR: Scelta non valida. Usa formato: idPartita idRichiesta (es. 1 2)\n");
            continue;
        }
        pthread_mutex_lock(&matches_mutex);
        bool valid = true;
        if (!matches[matchId] || matches[matchId]->player1 != player) {
            dprintf(player->client_fd, "CLEAR\n");
            dprintf(player->client_fd, "ERROR: Puoi gestire richieste solo per le tue partite. Riprova\n");
            pthread_mutex_unlock(&matches_mutex);
            continue;
        }  
        if (matches[matchId]->requests[requestId].player == NULL ||
            matches[matchId]->requests[requestId].status != PENDING) {
            dprintf(player->client_fd, "CLEAR\n");
            dprintf(player->client_fd, "ERROR: Puoi gestire solo richieste che sono in attesa di risposta. Riprova\n");
            pthread_mutex_unlock(&matches_mutex);
            continue;
        }
        pthread_mutex_unlock(&matches_mutex);
        
        dprintf(player->client_fd, "Cosa vuoi fare per la richiesta di %s della partita ID: %d\n",matches[matchId]->requests[requestId].player->username, matchId);
        dprintf(player->client_fd, "- 1. accetta\n");
        dprintf(player->client_fd, "- 2. rifiuta\n");
        dprintf(player->client_fd, "- 3. ignora\n");
        dprintf(player->client_fd, "LOBBY: - 4. indietro\n");
        n = read(player->client_fd, buffer, BUFFER_SIZE - 1);
        if (n <= 0) break;
        

        buffer[n] = '\0';
        buffer[strcspn(buffer, "\n")] = '\0';

        if (strcmp(buffer, "indietro") == 0 || strcmp(buffer, "4") == 0) {
            dprintf(player->client_fd, "CLEAR\n");
            dprintf(player->client_fd, "🔙 Ritorno al menu.\n");
            return -1;
        }
        pthread_mutex_lock(&matches_mutex);
        if (strcmp(buffer, "1") == 0 || strcmp(buffer, "accetta") == 0) {
            matches[matchId]->player2 = matches[matchId]->requests[requestId].player;
            matches[matchId]->requests[requestId].status = ACCEPTED;
            dprintf(player->client_fd,"CLEAR\n");
            dprintf(player->client_fd, "INFO: Richiesta accettata, la partita %d inizierà a breve.\n", matchId);
            Player *player2 = matches[matchId]->requests[requestId].player;
            setRequestState(player2,matches[matchId],ACCEPTED);
            dprintf(player2->client_fd,"CLEAR\n");
            dprintf(player2->client_fd, "INFO: La tua richiesta per la partita %d è stata accettata! La partita inizierà.\n", matchId);
            matches[matchId]->requests[requestId].player = NULL;
            matches[matchId]->requests[requestId].status = -1;
            pthread_mutex_unlock(&matches_mutex);

            deleteJoinRequestForPlayer(player);
            return matchId;
        } else if(strcmp(buffer, "2") == 0 || strcmp(buffer, "rifiuta") == 0){
            Player *player2 = matches[matchId]->requests[requestId].player;
            matches[matchId]->requests[requestId].status = REJECTED;
            setRequestState(player2,matches[matchId],REJECTED);
            dprintf(player->client_fd,"CLEAR\n");
            dprintf(player->client_fd, "INFO: Hai rifiutato la richiesta di %s della partita %d.\n", matches[matchId]->requests[requestId].player->username, matchId);
        }
        pthread_mutex_unlock(&matches_mutex);


    }
    return -1;
}

void *clientMonitorThread(void *arg) {
    Player *player = (Player *)arg;
    while (1) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(player->client_fd, &fds);

        struct timeval timeout = {1, 0}; // Timeout di 1 secondo

        int result = select(player->client_fd + 1, &fds, NULL, NULL, &timeout);
        if (result < 0) {
            perror("select error");
            break;
        }

        if (result > 0 && FD_ISSET(player->client_fd, &fds)) {
            char buf;
            int n = recv(player->client_fd, &buf, 1, MSG_PEEK);

            if (n <= 0) {
                printf("⚠️ Client %s disconnesso (monitor)\n", player->username);
                handlePlayerDisconnect(player);
                break;
            }
        }

    }
    return NULL;
}

void *login(void *arg) {
    int client_fd = *(int *)arg;    //prendo il client dagli args passati
    char buffer[BUFFER_SIZE];

    while (1) {
        dprintf(client_fd, "LOBBY: Inserisci il tuo username\n");

        int n = read(client_fd, buffer, BUFFER_SIZE - 1);
        if (n <= 0) {       //se non ricevo niente sul buffer chiudo il socket 
            close(client_fd);
            return NULL;
        }

        buffer[n] = '\0';                      // imposta il terminatore di riga
        buffer[strcspn(buffer, "\n")] = '\0';  // rimuove il carattere per andare a capo

        pthread_mutex_lock(&players_mutex);     //blocco l'accesso ai player momentaneamente per evitare accessi concorrenti alla risorsa

        
        int exists = 0;                     // Controllo se username è già utilizzato
        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (players[i] && strcmp(players[i]->username, buffer) == 0) {
                exists = 1;
                break;
            }
        }

        if (exists) {               // Se l'username è già utilizzato do errore
            pthread_mutex_unlock(&players_mutex);
            dprintf(client_fd, "ERROR: Username già in uso. Riprova con un altro.\n");
            continue;
        }

        
        for (int i = 0; i < MAX_PLAYERS; i++) {     // Cerco una cella libera per il nuovo player
            if (players[i] == NULL) {
                Player *p = malloc(sizeof(Player));
                memset(p, 0, sizeof(Player));
                if (!p) {
                    pthread_mutex_unlock(&players_mutex);
                    dprintf(client_fd, "ERROR: Errore interno procedura annullata.\n");
                    return NULL;
                }

                strncpy(p->username, buffer, USERNAME_MAX - 1);
                p->username[USERNAME_MAX - 1] = '\0';
                p->client_fd = client_fd;

                players[i] = p;

                pthread_mutex_unlock(&players_mutex);

                dprintf(client_fd,"CLEAR\n");
                dprintf(client_fd, "INFO: Login effettuato come %s\n", p->username);
                pthread_t tid;
                pthread_create(&tid, NULL, handlePlayer, p);
                pthread_detach(tid);

                pthread_t monitor_thread;
                pthread_create(&monitor_thread, NULL, clientMonitorThread, p);
                pthread_detach(monitor_thread);
                return NULL;
            }
        }

        pthread_mutex_unlock(&players_mutex);       //Se non è stato effettuato un return prima do errore
        dprintf(client_fd, "ERROR: Server pieno. Riprova più tardi.\n");
        sleep(1); 
    }

    return NULL;
}


void handleRequestJoinMatch(Player *player){
    dprintf(player->client_fd, "CLEAR\n");
    while(1){
        showMatchesList(player);
        dprintf(player->client_fd, "LOBBY: Inserisci l'id della partita a cui vuoi partecipare oppure -1 per tornare indietro\n");

        char id_buf[32];
        int n = read(player->client_fd, id_buf, sizeof(id_buf) - 1);
        if (n <= 0) {
            close(player->client_fd);
            return ;
        }

        id_buf[n] = '\0';
        id_buf[strcspn(id_buf, "\n")] = '\0';
        int id = atoi(id_buf);
        if (id == -1) {
            dprintf(player->client_fd, "CLEAR\n");
            dprintf(player->client_fd, "🔙 Ritorno al menu.\n");
            return;
        }

        pthread_mutex_lock(&matches_mutex);
        Match *match = trova_partita(id);
        dprintf(player->client_fd, "CLEAR\n");
        if (!match) 
            dprintf(player->client_fd, "ERROR: La partita non esiste, riprova\n");
        else 
            addJoinRequest(match, player);
             
        pthread_mutex_unlock(&matches_mutex);
    }
}

void printMenu(int client_fd){
    dprintf(client_fd,
        "Puoi digitare uno dei seguenti comandi:\n"
        " 1) crea\n"
        " 2) partecipa\n"
        " 3) richieste proprie partite\n"
        " 4) richieste di partecipazione\n"
        "LOBBY: In attesa del tuo comando...\n");
}

void *handlePlayer(void *arg) { //funzione che gestisce il player quando si trova nel menu
    Player *player = (Player *)arg;
    
    if (!player) return NULL;
    printMenu(player->client_fd);
    char buffer[BUFFER_SIZE];

    while (1) {
        
        fd_set read_fds;        //dichiaro un set di file descriptor da monitorare.
        FD_ZERO(&read_fds);     //inizializzo il set a vuoto.
        FD_SET(player->client_fd, &read_fds);   //aggiungo il file descriptor del client (player->client_fd) al set, per controllare se ci sono dati da leggere su quel socket.
        struct timeval timeout = {1, 0}; // timeout di 1 secondo

        //select() controlla se il socket è pronto per essere letto.
        //player->client_fd + 1 è il valore massimo dei descrittori + 1 (richiesto da select()).
        //&read_fds è il set da controllare in lettura.
        //Gli altri due NULL indicano che non ci interessa la scrittura né le eccezioni.
        //&timeout specifica quanto tempo aspettare al massimo.
        int ready = select(player->client_fd + 1, &read_fds, NULL, NULL, &timeout);
        if (ready < 0) {                //Se select() ritorna un valore negativo, c'è stato un errore.
            perror("select error");     //stampo l'errore.
                 //gestisce tutto ciò che deve avvenire quando un giocatore si disconnette
                       //chiudo il socket.
            return NULL;                   //esco dalla funzione/thread
        }

        //Se questo player ha una richiesta di una partita accettata esco dal menu, in modo che handleGame lo gestisca
        for (int i = 0; i < player->num_requests; i++) 
            if (player->requests[i].status == ACCEPTED) {
                deleteJoinRequestForPlayer(player);     //Cancello le richieste che il player ha fatto per altre partite
                return NULL;
            }
        
        //Controllo se c'è qualcosa da leggere
        if (ready > 0 && FD_ISSET(player->client_fd, &read_fds)) {
            int n = read(player->client_fd, buffer, BUFFER_SIZE - 1);       //Legge il BUFFER_SIZE dal socket e li mette in buffer.
            if (n <= 0) {                           //Se n == 0, il client ha chiuso la connessione. Se n < 0, c'è stato un errore di lettura.
                     //gestisce tutto ciò che deve avvenire quando un giocatore si disconnette 
                           //chiudo il socket.
                return NULL;                        //esco dalla funzione/thread
            }

            buffer[n] = '\0';                       //aggiungo il terminatore di stringa C.
            buffer[strcspn(buffer, "\n")] = '\0';   //rimuove \n alla fine della stringa, sostituendolo con \0.

            // 🔄 Comandi utente
            if (strstr(buffer, "crea")) {       //se l'utente inserisce crea, creo la partita e ristampo il menu
                crea_partita(player);
                printMenu(player->client_fd);
            } else if (strstr(buffer, "partecipa") || strstr(buffer, "2")) {    //se l'utente inserisce partecipa/2 chiama il metodo per gestire
                handleRequestJoinMatch(player);                                 //le richieste di partecipazione e poi ristampa il menu
                printMenu(player->client_fd);   
            } else if (strstr(buffer, "richieste") || strstr(buffer, "3")) {    //se l'utente inserisce richieste/3 mostro le richieste che i giocatori
                                                                                //hanno fatto per le sue partite
                int matchId = handleRequest(player);                  //gestisce l'il player durante la fase di accettazione delle richieste
                if (matchId != -1) {
                    pthread_t match_thread;
                    pthread_create(&match_thread, NULL, handleMatch, matches[matchId]);
                    pthread_detach(match_thread);
                    
                    return NULL;
                }
                printMenu(player->client_fd);
            } else if (strstr(buffer, "4")) {
                printMyRequests(player);
                printMenu(player->client_fd);
            } else {
                dprintf(player->client_fd, "ERROR: Comando non valido. Riprova (crea / partecipa / richieste)\n");
            }
        }
    }

    return NULL;
}

int main() {
    setbuf(stdout, NULL);
    memset(matches, 0, sizeof(matches));

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr));
    listen(server_fd, 10);
    printf("✅ Server in ascolto sulla porta %d...\n", PORT);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int *client_fd = malloc(sizeof(int));
        *client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
        if (*client_fd < 0) {
            free(client_fd);
            continue;
        }

        dprintf(*client_fd, "Benvenuto in TrisArena...\n");
        pthread_t tid;
        pthread_create(&tid, NULL, login, client_fd);
        pthread_detach(tid);
    }

    close(server_fd);
    return 0;
}
