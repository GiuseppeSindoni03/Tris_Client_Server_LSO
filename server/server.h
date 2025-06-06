#ifndef SERVER_H
#define SERVER_H

#include <pthread.h>
#include <netinet/in.h>


#define PORT 8080
#define MAX_MATCH 10
#define MAX_PLAYERS 10
#define MAX_REQUESTS 5
#define BUFFER_SIZE 1024
#define USERNAME_MAX 32


typedef struct Match Match;

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
    int turn;
    char board[3][3];

    MatchStatus status;
    JoinRequest requests[MAX_REQUESTS];
};

extern Match *matches[MAX_MATCH];
extern Player *players[MAX_PLAYERS];
extern pthread_mutex_t matches_mutex;
extern pthread_mutex_t players_mutex;

// FIRME DELLE FUNZIONI
void initBoard(Match *p);
bool askContinue(int client_fd);
void resetMatch (Match* p);
MatchStatus checkStatus(Match *p) ;
void sendBoard(int fd, Match *p) ;
void deleteMatch(Match *p);
Match *findMatch(int id);
void createMatch(Player *player) ;
bool checkAcceptedRequest(Player *player);
void showMatchesList(Player *player);
void deleteJoinRequestForAllPlayers(Match *match);
void deleteJoinRequestForPlayer(Player *player);
void handlePlayerDisconnect(Player *player);
bool addJoinRequest(Match *match, Player *player);
void handlePlayerLeavingMatch(Match *p);
void backToMenu(Player *player);
void *handleMatch(void *arg);
void printJoinRequests(Player *player);
void setRequestState(Player *player, Match *match, RequestStatus status);
void printMyRequests(Player *player);
int handleRequest(Player *player);
void *clientMonitorThread(void *arg);
void *login(void *arg) ;
int handleRequestJoinMatch(Player *player);
void printMenu(int client_fd);
void *handlePlayer(void *arg);


#endif