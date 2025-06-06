#include "server.h"


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <ctype.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>


// Inizializzazione strutture dati condivise
Match *matches[MAX_MATCH];
pthread_mutex_t matches_mutex = PTHREAD_MUTEX_INITIALIZER;

Player *players[MAX_PLAYERS];
pthread_mutex_t players_mutex = PTHREAD_MUTEX_INITIALIZER;

// Inizializza la griglia di gioco (3x3) con spazi vuoti
void initBoard(Match *p) {
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            p->board[i][j] = ' ';
}

// Chiede al client se vuole continuare dopo la fine della partita
bool askContinue(int client_fd) {
    char buffer[BUFFER_SIZE];
    dprintf(client_fd, "LOBBY: Vuoi giocare un'altra partita? (si/no)\n");

    int n = read(client_fd, buffer, BUFFER_SIZE - 1); // Legge il BUFFER_SIZE dal socket e li mette in buffer.
    if (n <= 0) {
        return 0;
    }

        buffer[n] = '\0'; // Imposta il terminatore di riga
        buffer[strcspn(buffer, "\n")] = '\0'; // Rimuove il carattere per andare a capo

       
    if (strcasecmp(buffer, "si") == 0) return 1;
    return 0;
}

// Reset di una partita 
void resetMatch (Match* p) {
    initBoard(p); // svuota la griglia
    p->status = WAITING; // riporta la partita in attesa di un nuovo giocatore
    p->turn = 1; // riporta al turno del primo giocatore

}

// Verifica lo stato corrente della partita (Vittoria, Pareggio, In corso)
MatchStatus checkStatus(Match *p) {
    char (*b)[3] = p->board;

    for (int i = 0; i < 3; i++) { // controlla se ci sono 3 simboli uguali sulla stesso riga o sulla stessa colonna
        if (b[i][0] != ' ' && b[i][0] == b[i][1] && b[i][1] == b[i][2])
            return VICTORY;
        if (b[0][i] != ' ' && b[0][i] == b[1][i] && b[1][i] == b[2][i])
            return VICTORY;
    }

    // controlla se ci sono 3 simboli uguali sulle diagonali
    if (b[0][0] != ' ' && b[0][0] == b[1][1] && b[1][1] == b[2][2]) 
        return VICTORY;
    if (b[0][2] != ' ' && b[0][2] == b[1][1] && b[1][1] == b[2][0])
        return VICTORY;

    //  controlla se la griglia e' piena   
    int full = 1;
    for (int i = 0; i < 3 && full; i++)
        for (int j = 0; j < 3 && full; j++)
            if (b[i][j] == ' ')
                full = 0;

    // se e' piena e' un pareggio
    if (full)
        return DRAW;

    // la partita non e' ancora terminata
    return IN_PROGRESS;
}

// Invia al client la rappresentazione testuale della griglia
void sendBoard(int fd, Match *p) {
    dprintf(fd, "[GRIGLIA]\n");
    char buf[128];
    for (int i = 0; i < 3; i++) {
        snprintf(buf, sizeof(buf), " %c | %c | %c \n", p->board[i][0], p->board[i][1], p->board[i][2]);
        dprintf(fd, "%s", buf);
        if (i < 2) dprintf(fd, "---+---+---\n");
    }
}

// Elimina una partita dall'elenco condiviso
void deleteMatch(Match *p) {
    pthread_mutex_lock(&matches_mutex); // blocca momentaneamente l'accesso a tale risolrsa
    deleteJoinRequestForAllPlayers(p); // elimina tutte le richieste di accesso a tale partita
    for (int i = 0; i < MAX_MATCH; i++) { 
        if (matches[i] == p) {
            matches[i] = NULL; // rimuove tale partita dalla struttura
            break;
        }
    }
    
    pthread_mutex_unlock(&matches_mutex); // sblocca l'accesso a tale risorsa
}

// Ritorna il puntatore ad una partita dato l'id
Match *findMatch(int id) {
    return matches[id] ? matches[id] : NULL;
}

// Crea una nuova partita per il giocatore
void createMatch(Player *player) { 
    pthread_mutex_lock(&matches_mutex); // blocca momentaneamente l'accesso a tale risorsa
    for (int i = 0; i < MAX_MATCH; i++) { // scorre la struttura che contiene le partite
        if (!matches[i]) { // non appena incontra uno slot libero crea la partita 
            Match *p = malloc(sizeof(Match));
            memset(p, 0, sizeof(Match));
            p->id = i;
            p->player1 = player;
            p->player2 = NULL;
            p->turn = 1;
            p->status = WAITING;
            matches[i] = p;

            dprintf(player->client_fd, "CLEAR\n");
            dprintf(player->client_fd, "🆕 Partita creata con ID %d. Attendi avversari...\n", i);
            break;
        }
    }
    pthread_mutex_unlock(&matches_mutex);  // sblocca l'accesso a tale risorsa
    return;
}

// Verifica se esiste una richiesta di partecipazione accettata, e in tal caso elimina le richieste di partecipazione per le altre partite
bool checkAcceptedRequest(Player *player) {
    for (int i = 0; i < player->num_requests; i++) {
        if (player->requests[i].status == ACCEPTED) {
            deleteJoinRequestForPlayer(player);
            return true;
        }
    }
    return false;
}

// Mostra le partite disponibili al giocatore
void showMatchesList(Player *player) {
    dprintf(player->client_fd, "Elenco Partite:\n");
    int count = 0;
    for (int i = 0; i < MAX_MATCH; i++) {
        if (matches[i] && matches[i]->player1 != player) { // filtra solo le partite create dagli altri giocatori
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

 //Elimina tutte le richieste di partecipazione per una partita
void deleteJoinRequestForAllPlayers(Match *match){ 
    for(int i=0; i< MAX_PLAYERS; i++){
        if (!players[i]) continue;

        for(int j=0; j<players[i]->num_requests; j++)
            if(players[i]->requests[j].match == match){
                for(int k=j; k<players[i]->num_requests-1; k++)
                    players[i]->requests[k]= players[i]->requests[k+1];

                players[i]->num_requests--;
                break;
            }
    }
}
// Elimina tutte le richieste fatte da uno specifico giocatore
void deleteJoinRequestForPlayer(Player *player){
    for(int i=0; i<player->num_requests; i++){
        player->requests[i].match = NULL;
        player->requests[i].status = -1;
    }
    player->num_requests=0;
}

 //Viene chiamato alla disconnessione di un giocatore
void handlePlayerDisconnect(Player *player){   
    pthread_mutex_lock(&players_mutex); 
    pthread_mutex_lock(&matches_mutex);
    for(int i=0; i< MAX_MATCH; i++){            //Elimina le partite di cui il giocatore è proprietario e tutti le richieste che i giocatori 
        if(matches[i]){ 
            if(matches[i]->player1==player){   //hanno fatto per quelle partite
                deleteJoinRequestForAllPlayers(matches[i]); // elimina tutte le richieste di accesso a tale partita
                free(matches[i]);
                matches[i]= NULL;
            }else{
                 for(int j=0; j<MAX_REQUESTS; j++)  //Controlla se ci sono delle richieste in questa partita dal player che si sta disconnetendo
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
    for(int i=0; i< MAX_PLAYERS; i++)   //Elimina il giocatore dall'elenco dei giocatori
        if(players[i] == player){
            free(player);
            players[i]=NULL;
            break;
        }
    
    pthread_mutex_unlock(&players_mutex);
}

// Aggiunge una richiesta per partecipare ad uno specifico Match
bool addJoinRequest(Match *match, Player *player) {

    if(match->player1 == player){ // Controlla che il propetario della partita sia diverso dal giocatore che richiede di partecipare
        dprintf(player->client_fd, "ERROR: Puoi richiedere l'accesso solo alle partite di altri giocatori \n");
        return false;
    }

    if(match->status != WAITING){ // Controlla che la partita abbia bisogno di un giocatore
        dprintf(player->client_fd, "ERROR: Puoi richiedere l'accesso solo alle partite in attesa \n");
        return false;
    }

    for (int i = 0; i < MAX_REQUESTS; i++) {
        if (match->requests[i].player == player) { // Controlla che il giocatore non abbia gia' fatto una richiesta per quella partita
            dprintf(player->client_fd, "ERROR: Hai già richiesto l'accesso \n");
            return false;
        }
    }

    // Cerca uno slot libero all'interno dell'array di richieste della partita
    int index = -1;
    for (int i = 0; i < MAX_REQUESTS; i++) {
        if (match->requests[i].player == NULL) {
            index = i;
            break;
        }
    }

    // Se l'array di richieste di quella partita e' pieno: e' stato raggiunto il numero massimo di richieste e non permette l'inserimento
    if (index == -1) {
        dprintf(player->client_fd, "ERROR: Non c'è spazio per altre richieste \n");
        return false;  // No space
    }

    // Se l'array di richieste di accesso di quel giocatore e' pieno: e' stato raggiunto il numero massimo di richieste di accesso per delle partite
    if (player->num_requests >= MAX_REQUESTS) { 
        dprintf(player->client_fd, "ERROR: Hai già fatto troppe richieste \n");
        return false;  // No space
    }

    // Inserisce una richiesta all'interno dell'array di richieste del player
    player->requests[player->num_requests].match = match; 
    player->requests[player->num_requests].status = PENDING;
    player->num_requests++;

    // Inserisce la richiesta di accesso del player nell'array di richieste della partita
    match->requests[index].player = player;
    match->requests[index].status = PENDING;

    dprintf(player->client_fd, "INFO: Hai richiesto l'accesso per la partita %d, attendi di essere accettato\n", match->id);
    return true;
}

// Gestisce il caso in cui un Giocatore abbandona la partita
void handlePlayerLeavingMatch(Match *p){
        // Determina il giocatore che ha abbandonato 
        Player *current = (p->turn == 1) ? p->player1 : p->player2;
        Player *other = (p->turn == 1) ? p->player2 : p->player1;
        dprintf(other->client_fd, "INFO: L'altro giocatore ha lasciato la partita. Hai vinto, la partità verrà eliminata.\n");

        // Fa partite un nuovo Thread con la funzione handlePlayer, che mostra il menu al giocatore rimasto
        pthread_t tid;
        pthread_create(&tid, NULL, handlePlayer, other);
        pthread_detach(tid); 
}

// Riporta il giocatore al menu principale
void backToMenu(Player *player){
    // Fa partite un nuovo Thread con la funzione handlePlayer
    pthread_t tid;
    pthread_create(&tid, NULL, handlePlayer, player);
    pthread_detach(tid);
}

// Gestisce l'intera partita
void *handleMatch(void *arg) {
    Match *p = (Match *)arg;
    char buffer[BUFFER_SIZE];

    p->status = IN_PROGRESS; // Imposta lo stato in corso 
    initBoard(p); // Inizializza lo stato della griglia 

    dprintf(p->player2->client_fd, "INFO: Connesso! Attendi il tuo turno...\n");

    while (1) { // Ogni iterazione rappresenta un turno della partita


        int current_fd = (p->turn == 1) ? p->player1->client_fd : p->player2->client_fd; // File descriptor del giocatore che deve fare la mossa
        int other_fd   = (p->turn == 1) ? p->player2->client_fd : p->player1->client_fd; // File descriptor del giocatore in attesa

        dprintf(current_fd, "TURN: Tocca a te, inserisci riga e colonna (es. 1 2): \n");
        int n = read(current_fd, buffer, BUFFER_SIZE - 1); // Legge il BUFFER_SIZE dal socket e li mette in buffer.

        // Gestisc il caso il giocatore abbandoni la partita
        if (n <= 0) { 
            Player *current = (p->turn == 1) ? p->player1 : p->player2;
            handlePlayerLeavingMatch(p);
            
            break;
        }

        buffer[n] = '\0'; // Imposta il terminatore di riga
        buffer[strcspn(buffer, "\n")] = '\0'; // Rimuove il carattere per andare a capo

        if (strcmp(buffer, "exit") == 0) {
            Player *current = (p->turn == 1) ? p->player1 : p->player2;
            handlePlayerLeavingMatch(p);
            
            break;
        }

        // Controlla se la mossa e' valida
        int row, col;
        if (sscanf(buffer, "%d %d", &row, &col) != 2 || row < 1 || row > 3 || col < 1 || col > 3) {
            dprintf(current_fd, "ERROR: Mossa non valida. Usa formato: riga colonna (es. 1 2)\n");
            continue;
        }

        row--; col--;  // da 1-based a 0-based

        // Controlla se la cella inserita e' occupata
        if (p->board[row][col] != ' ') {
            dprintf(current_fd, "ERROR: Casella già occupata. Riprova.\n");
            continue;
        }

        // Effettua la mossa, inserendo il simbolo del giocatore corrente
        p->board[row][col] = (p->turn == 1) ? 'X' : 'O';

        // Invio griglia aggiornata a entrambi
        sendBoard(p->player1->client_fd, p);
        sendBoard(p->player2->client_fd, p);

        //Si verifica lo stato attuale della partita
        MatchStatus status = checkStatus(p);


        if (status == VICTORY) { 
            p->status = VICTORY;

            dprintf(current_fd, "Complimenti hai vinto la partita!\n");
            dprintf(other_fd, "Mi dispiace hai perso\n");

            Player *winner = (p->turn == 1) ? p->player1 : p->player2;
            Player *loser = (p->turn == 1) ? p->player2 : p->player1;

            // Riporta il perdente al menu
            backToMenu(loser);

            // Chiede al vincitore se intende continuare a giocare
            if(askContinue(current_fd)) {
                dprintf(current_fd, "INFO: La partita è stata resettata, attendi nuovi giocatori!\n");
                resetMatch(p); 

                p->player1 = winner; // il vincitore diventa il propetario di quella partita
                p->player2 = NULL;

                backToMenu(winner); // riporta il giocatore al menu
                return NULL;
            }

            backToMenu(winner); // riporta il vincitore al menu principale

            break;
            
        }
        else if ( status == DRAW) {
            // gestisci fina partita e caso di pareggio:
            // viene chiesto ad entrambi i giocatori se hanno intensione di continuare a giocare
            //
            p->status = DRAW;
            dprintf(current_fd, "Avete pareggiato!\n");
            dprintf(other_fd, "Avete pareggiato!\n");

            if(askContinue(current_fd) && askContinue(other_fd)) { // Chiede sequenzialmente ad entrambi i giocatori se intendono continuare a giocare
                resetMatch(p); // Resetta la partita 
                continue;  // e riprendono a giocare
            }
           else {
                // Entrambi i giocatori tornano al menu
                backToMenu(p->player1);
                backToMenu(p->player2);

                // La partita termina
                break;
           }
        }
        else { // La partita e' ancora in corso
            dprintf(current_fd, "INFO: Attendi la mossa dell'avversario...\n");
            dprintf(other_fd, "INFO: L'avversario ha giocato in riga %d, colonna %d\n", row + 1, col + 1);
        }

        // Il turno passa all'altro giocatore
        p->turn = (p->turn == 1) ? 2 : 1;
    }

    deleteMatch(p); // Viene eliminata la partita
    return NULL;
}

// Mostra le re richieste di accesso per ogni partita creata dal giocatore in questione
void printJoinRequests(Player *player){
    dprintf(player->client_fd, "Le richieste per le tue partite sono:\n");

    bool requests = false;
    pthread_mutex_lock(&matches_mutex); // protegge l'accesso concorrente all'array globale matches[]

    for(int i=0; i< MAX_MATCH; i++){
        if(matches[i] && matches[i]->player1==player){ // filtra ogni partita il cui il giocatore e' il propretario
            requests = true;
            bool found = false;
            dprintf(player->client_fd, "   Partita id %d: \n", i);
            for(int j=0;j<MAX_REQUESTS; j++)
                if(matches[i]->requests[j].player != NULL ){ // Scorre tutte le richieste di quella partita
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

// Aggiorna lo stato di una richiesta fatta da un giocatore per una specifica partita
void setRequestState(Player *player, Match *match, RequestStatus status){
    for(int i=0; i<player->num_requests; i++){ 
        if(player->requests[i].match == match){
            player->requests[i].status = status;
            return;
        }
    }
}

// Stampa l'elenco di richieste di accesso di un giocatore
void printMyRequests(Player *player){
    dprintf(player->client_fd, "CLEAR\n");
    dprintf(player->client_fd, "INFO: Elenco partite per cui hai richiesto l'accesso\n");

    if(player->num_requests == 0){
        dprintf(player->client_fd, "ERROR: Non hai fatto richiesto l'accesso a nessuna partita\n");
        return ;
    }

    for(int i=0; i < player->num_requests; i++)
        if(player->requests[i].status == PENDING)
            dprintf(player->client_fd, " - Partita ID: %d, Proprietario: %s, Stato: IN ATTESA \n",player->requests[i].match->id ,player->requests[i].match->player1->username);
        else if (player->requests[i].status == REJECTED)
            dprintf(player->client_fd, " - Partita ID: %d, Proprietario: %s, Stato: RIFIUTATA \n",player->requests[i].match->id ,player->requests[i].match->player1->username);
        else
            dprintf(player->client_fd, " - Partita ID: %d, Proprietario: %s, Stato: ACCETTATA \n",player->requests[i].match->id ,player->requests[i].match->player1->username);

}
// Consente di gestire le richieste di accesso per tutte le partute create da un giocatore
int handleRequest(Player *player){
    dprintf(player->client_fd, "CLEAR\n");
    char buffer[BUFFER_SIZE];

    // il ciclo continua finche il giocatore non:
    // - accetta una richiesta (e la partita inizia)
    // - digita indietro
    // - oppure si disconnette
    while(1){ 
        printJoinRequests(player); // Stampa tutte le richieste di accesso per ogni partita
        dprintf(player->client_fd, "Specifica idPartita e idRichiesta (es. 1 2) della richiesta\n "
                                    "LOBBY: oppure digita indietro per tornare al menu: \n");
        int n = read(player->client_fd, buffer, BUFFER_SIZE - 1); // Legge il BUFFER_SIZE dal socket e li mette in buffer.
        if (n <= 0) break;
        

        buffer[n] = '\0'; // Imposta il terminatore di riga
        buffer[strcspn(buffer, "\n")] = '\0'; // Rimuove il carattere per andare a capo

        if (strcmp(buffer, "indietro") == 0) {
            dprintf(player->client_fd, "CLEAR\n");
            dprintf(player->client_fd, "🔙 Ritorno al menu.\n");
            return -1;
        }

        int matchId, requestId;
        // Controlla che l'input inserito sia del tipo (idPartita idRichiesta) e che gli id inseriti siano nel range corretto
        if (sscanf(buffer, "%d %d", &matchId, &requestId) != 2 || matchId < 0 || matchId >= MAX_MATCH || requestId < 0 || requestId >= MAX_REQUESTS) {
            dprintf(player->client_fd, "CLEAR\n");
            dprintf(player->client_fd, "ERROR: Scelta non valida. Usa formato: idPartita idRichiesta (es. 1 2)\n");
            continue;
        }

        pthread_mutex_lock(&matches_mutex); // protegge l'accesso concorrente all'array globale matches[]
        bool valid = true;
        if (!matches[matchId] || matches[matchId]->player1 != player) { // Controlla che la partita esista e che il giocatore ne sia il propetario
            dprintf(player->client_fd, "CLEAR\n");
            dprintf(player->client_fd, "ERROR: Puoi gestire richieste solo per le tue partite. Riprova\n");
            pthread_mutex_unlock(&matches_mutex);
            continue;
        }  
        if (matches[matchId]->requests[requestId].player == NULL ||     // Controlla che il giocatore che ha fatto la richiesta esista e che lo stato della richiesta sia ancora PENDING
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
        n = read(player->client_fd, buffer, BUFFER_SIZE - 1); // Legge il BUFFER_SIZE dal socket e li mette in buffer.
        if (n <= 0) break;
        

        buffer[n] = '\0'; // Imposta il terminatore di riga
        buffer[strcspn(buffer, "\n")] = '\0'; // Rimuove il carattere per andare a capo

        // Caso in cui il gicatore desidera tornare al menu
        if (strcmp(buffer, "indietro") == 0 || strcmp(buffer, "4") == 0) {
            dprintf(player->client_fd, "CLEAR\n");
            dprintf(player->client_fd, "🔙 Ritorno al menu.\n");
            return -1;
        }

        pthread_mutex_lock(&matches_mutex); // protegge l'accesso concorrente all'array globale matches[]
        if (strcmp(buffer, "1") == 0 || strcmp(buffer, "accetta") == 0) { // Caso nel quale l'utente ACCETTA la richiesta

            matches[matchId]->player2 = matches[matchId]->requests[requestId].player; // Imposta il secondo giocatore della partita
            matches[matchId]->requests[requestId].status = ACCEPTED; // Modifica lo stato della richiesta in ACCETTATA

            dprintf(player->client_fd,"CLEAR\n");
            dprintf(player->client_fd, "INFO: Richiesta accettata, la partita %d inizierà a breve.\n", matchId);

            Player *player2 = matches[matchId]->requests[requestId].player;
            setRequestState(player2,matches[matchId],ACCEPTED); // Setta tale richiesta come ACCETTA nell'elenco di richieste di accesso del giocatore

            dprintf(player2->client_fd,"CLEAR\n");
            dprintf(player2->client_fd, "INFO: La tua richiesta per la partita %d è stata accettata! La partita inizierà.\n", matchId);

            matches[matchId]->requests[requestId].player = NULL;
            matches[matchId]->requests[requestId].status = -1;

            pthread_mutex_unlock(&matches_mutex); // Protegge l'accesso concorrente all'array globale matches[]

            deleteJoinRequestForPlayer(player); // Elimina tutte le richiesta di accesso per tale partita
            return matchId; // La partita puo' iniziare

        } else if(strcmp(buffer, "2") == 0 || strcmp(buffer, "rifiuta") == 0){ // Caso nel quale il giocatore rifiuta quella richiesta

            Player *player2 = matches[matchId]->requests[requestId].player;
            matches[matchId]->requests[requestId].status = REJECTED; // Setta tale richiesta come RIFIUTATA nel array di richieste di tale partita

            setRequestState(player2,matches[matchId],REJECTED); // Setta tale richiesta come RIFIUTATA nell'array di richieste in uscita del giocatore

            dprintf(player->client_fd,"CLEAR\n");
            dprintf(player->client_fd, "INFO: Hai rifiutato la richiesta di %s della partita %d.\n", matches[matchId]->requests[requestId].player->username, matchId);
        }
        pthread_mutex_unlock(&matches_mutex);


    }
    return -1; // Torna al menu principale
}

// Serve a monitorare continuamente un giocatore si e' disconnesso in modo improvviso senza utilizzare explicitamente un comando come "exit"
void *clientMonitorThread(void *arg) {
    Player *player = (Player *)arg;

    while (1) {
        // Prepara il set di file descriptor da monitorare con select
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(player->client_fd, &fds); // Monitora il client

        struct timeval timeout = {1, 0}; // Timeout di 1 secondo

        int result = select(player->client_fd + 1, &fds, NULL, NULL, &timeout); // Aspetta fino a 1 secondo per vedere se il socket e' pronto per leggere qualcosa
        if (result < 0) {  // Errore
            perror("select error");
            break;
        }

        if (result > 0 && FD_ISSET(player->client_fd, &fds)) { // Il socket e' pronto
            char buf;
            int n = recv(player->client_fd, &buf, 1, MSG_PEEK); // Legge i byte senza consumarli

            if (n <= 0) { // Il socket e' stato chiuso
                printf("⚠️ Client %s disconnesso (monitor)\n", player->username);
                handlePlayerDisconnect(player); // Gestisce la diconnessione di un client
                break;
            }
        }

    }
    return NULL;
}

void *login(void *arg) {
    int client_fd = *(int *)arg;    // Ottiene il file descriptor del client passato come argomento al thread
    char buffer[BUFFER_SIZE];

    while (1) {
        dprintf(client_fd, "LOBBY: Inserisci il tuo username\n"); 

        int n = read(client_fd, buffer, BUFFER_SIZE - 1); // Legge il BUFFER_SIZE dal socket e li mette in buffer.
        if (n <= 0) {       // Se non riceve niente sul buffer chiude il socket e termina
            close(client_fd);
            return NULL;
        }

        buffer[n] = '\0'; // Imposta il terminatore di riga
        buffer[strcspn(buffer, "\n")] = '\0';  // Rimuove il carattere per andare a capo


        pthread_mutex_lock(&players_mutex); // Protegge l'accesso concorrente all'array globale players[]

        int exists = 0;   // Controlla se l'username inserito è già utilizzato
        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (players[i] && strcmp(players[i]->username, buffer) == 0) {
                exists = 1;
                break;
            }
        }

        if (exists) { // Se l'username è già utilizzato da errore
            pthread_mutex_unlock(&players_mutex);
            dprintf(client_fd, "ERROR: Username già in uso. Riprova con un altro.\n");
            continue;
        }

        
        for (int i = 0; i < MAX_PLAYERS; i++) {     // Cerca una cella libera per il nuovo player
            if (players[i] == NULL) {
                // Alloca dinamicamente un nuovo giocatore
                Player *p = malloc(sizeof(Player));
                memset(p, 0, sizeof(Player));

                if (!p) {
                    pthread_mutex_unlock(&players_mutex);
                    dprintf(client_fd, "ERROR: Errore interno procedura annullata.\n");
                    return NULL;
                }

                // Imposta i campi principali e registra il giocatore
                strncpy(p->username, buffer, USERNAME_MAX - 1);
                p->username[USERNAME_MAX - 1] = '\0';
                p->client_fd = client_fd;

                players[i] = p;

                pthread_mutex_unlock(&players_mutex);

                dprintf(client_fd,"CLEAR\n");
                dprintf(client_fd, "INFO: Login effettuato come %s\n", p->username);

                // Avvia il thread di gioco principale
                pthread_t tid;
                pthread_create(&tid, NULL, handlePlayer, p);
                pthread_detach(tid);

                // Avvia il thread  che monitora la disconnessione del client
                pthread_t monitor_thread;
                pthread_create(&monitor_thread, NULL, clientMonitorThread, p);
                pthread_detach(monitor_thread);
                return NULL;
            }
        }

        pthread_mutex_unlock(&players_mutex);     
        dprintf(client_fd, "ERROR: Server pieno. Riprova più tardi.\n");
        sleep(1); 
    }

    return NULL;
}

// Consente ad un giocatore di far richiesta di partecipazione a una delle partite in attesa di un giocatore
int handleRequestJoinMatch(Player *player){

    dprintf(player->client_fd, "CLEAR\n");
    showMatchesList(player); // Visualizza l'elenco di partite disponibili
    dprintf(player->client_fd, "LOBBY: Inserisci l'id della partita a cui vuoi partecipare oppure -1 per tornare indietro\n");

    while(1){ // Il loop  continua finche: il giocatore non torna indietro, non gli viene accettata una richiesta, non si disconnette 
        if(checkAcceptedRequest(player)) return -1; // Se esiste una richiesta di partecipazione accettata per una delle partite del giocatore esce
        
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(player->client_fd, &fds);
        struct timeval timeout = {1, 0}; // 1 secondo

        int ready = select(player->client_fd + 1, &fds, NULL, NULL, &timeout);
        if (ready < 0) {
            perror("select error");
            close(player->client_fd);
            return -1;
        }

        if (ready == 0) { // Timeout: nessun input, si ricontrolla lo stato nella prossima iterazione
            continue;
        }

        // Dati disponibili da leggere
        char id_buf[32];
        int n = read(player->client_fd, id_buf, sizeof(id_buf) - 1); // Legge il BUFFER_SIZE dal socket e li mette in buffer.
        if (n <= 0) {
            close(player->client_fd);
            return -1;
        }

        id_buf[n] = '\0';
        id_buf[strcspn(id_buf, "\n")] = '\0';

        int id = atoi(id_buf);

        if (id == -1) { // Uscita con -1
            dprintf(player->client_fd, "CLEAR\n");
            dprintf(player->client_fd, "🔙 Ritorno al menu.\n");
            return 0;
        }

        pthread_mutex_lock(&matches_mutex); // protegge l'accesso concorrente all'array globale matches[]
        Match *match = findMatch(id);

        dprintf(player->client_fd, "CLEAR\n");
        if (!match) {
            dprintf(player->client_fd, "ERROR: La partita non esiste, riprova\n");
            showMatchesList(player);
            dprintf(player->client_fd, "LOBBY: Inserisci l'id della partita a cui vuoi partecipare oppure -1 per tornare indietro\n");
        }
        else {
            addJoinRequest(match, player); // Aggiunge il giocatore alla lista di richieste per quella partita
            showMatchesList(player);
            dprintf(player->client_fd, "LOBBY: Inserisci l'id della partita a cui vuoi partecipare oppure -1 per tornare indietro\n");
        }
             
        pthread_mutex_unlock(&matches_mutex);
    }
    return 0;
}

void printMenu(int client_fd){
    dprintf(client_fd,
        "Puoi digitare uno dei seguenti comandi:\n"
        " 1) crea\n"
        " 2) partecipa\n"
        " 3) gestisci le tue partite\n"
        " 4) richieste di partecipazione\n"
        "LOBBY: In attesa del tuo comando...\n");
}

// Gestisce tutte le interazioni con il Client dopo il login, finche non entra in una partita o si disconnette
void *handlePlayer(void *arg) { 
    Player *player = (Player *)arg; // Ottiene il puntatore al giocatore
    
    if (!player) return NULL;
    printMenu(player->client_fd); // Stampa il menu iniziale
    char buffer[BUFFER_SIZE];

    while (1) {
        
        fd_set read_fds;        // Dichiaro un set di file descriptor da monitorare.
        FD_ZERO(&read_fds);     // Inizializzo il set a vuoto.
        FD_SET(player->client_fd, &read_fds); // Aggiunge il file descriptor del client (player->client_fd) al set, per controllare se ci sono dati da leggere su quel socket.
        struct timeval timeout = {1, 0}; // timeout di 1 secondo

        // select() controlla se il socket è pronto per essere letto.
        // player->client_fd + 1 è il valore massimo dei descrittori + 1 (richiesto da select()).
        // &read_fds è il set da controllare in lettura.
        // Gli altri due NULL indicano che non ci interessa la scrittura né le eccezioni.
        // &timeout specifica quanto tempo aspettare al massimo.
        int ready = select(player->client_fd + 1, &read_fds, NULL, NULL, &timeout);
        if (ready < 0) {                // Errore
            perror("select error");     
            return NULL;                   
        }

        // In caso esista una richiesta di partecipazione accettata esce dal menu, in modo che handleGame lo gestisca
        if (checkAcceptedRequest(player)) return NULL;
        

        if (ready > 0 && FD_ISSET(player->client_fd, &read_fds)) {
            int n = read(player->client_fd, buffer, BUFFER_SIZE - 1); // Legge il BUFFER_SIZE dal socket e li mette in buffer.
            if (n <= 0) {                          
                return NULL;  
            }

            buffer[n] = '\0'; // Imposta il terminatore di riga
            buffer[strcspn(buffer, "\n")] = '\0'; // Rimuove il carattere per andare a capo

            // L'utente sceglie di creare una partita
            if (strstr(buffer, "crea") || strstr(buffer, "1")) {       
                createMatch(player); 
                printMenu(player->client_fd); 
            
            // L'utente sceglie di visualizzare il menu per chiedere di partecipare ad una partita
            } else if (strstr(buffer, "partecipa") || strstr(buffer, "2")) {   
                if(handleRequestJoinMatch(player) == -1) return NULL; 
                printMenu(player->client_fd);   

            // L'utente decide di visualizzare le richieste di partecipazione per le sue partite
            } else if (strstr(buffer, "gestisci") || strstr(buffer, "3")) {                                                                            
                int matchId = handleRequest(player);                  
                if (matchId != -1) { // L'utente ha accettato una richiesta di partecipazione
                    // Avvia un nuovo thread che gestisce la partita chiamando handleMatch
                    pthread_t match_thread;
                    pthread_create(&match_thread, NULL, handleMatch, matches[matchId]);
                    pthread_detach(match_thread);
                    
                    return NULL;
                }
                printMenu(player->client_fd);
            } else if (strstr(buffer, "richieste") || strstr(buffer, "4")) { 
                printMyRequests(player);
                printMenu(player->client_fd);
            } else {
                dprintf(player->client_fd, "ERROR: Comando non valido. Riprova (1 crea / 2 partecipa / 3 gestisci / 4 richieste)\n");
            }
        }
    }

    return NULL;
}


int main() {
    setbuf(stdout, NULL); // Disattiva il buffer su stdout, così printf stampa subito
    memset(matches, 0, sizeof(matches)); // Inizializza a NULL l'array delle partite

    int server_fd = socket(AF_INET, SOCK_STREAM, 0); // Crea un socket TCP IPv4, server_fd sara' il file descriptor del server
    struct sockaddr_in server_addr = {0}; // Inizializza a zero la struttura che conterrà l’indirizzo del server

    server_addr.sin_family = AF_INET; // IPv4
    server_addr.sin_addr.s_addr = INADDR_ANY; // Accetta connessioni su qualsiasi interfaccia di rete
    server_addr.sin_port = htons(PORT); // Specifica la porta 

    bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)); // Collega il socket all'indirizzo e porta specificati
    listen(server_fd, 10); // Mette il socket in modalita' di ascolto
    printf("✅ Server in ascolto sulla porta %d...\n", PORT);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);

        // Attende una nuova connessione dal client
        int *client_fd = malloc(sizeof(int));
        *client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len); // Restituisce un nuovo socket per comunicare con quel client

        if (*client_fd < 0) { // Accept e' fallita
            free(client_fd);
            continue;
        }

        dprintf(*client_fd, "Benvenuto in TrisArena...\n");

        // Crea un nuovo thread che gestisce il login di questo client
        pthread_t tid;
        pthread_create(&tid, NULL, login, client_fd);
        pthread_detach(tid); // Libera risorse automaticamente alla fine
    }

    close(server_fd);
    return 0;
}
