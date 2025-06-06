#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>

#define PORT 8080
#define BUFFER_SIZE 1024

void sendMessage(int sock, const char *message) {
    char buffer[BUFFER_SIZE];
    snprintf(buffer, BUFFER_SIZE, "%s\n", message);
    send(sock, buffer, strlen(buffer), 0); // Invia il messaggio al server
}

void printMessage(const char *message) {
    if (strstr(message, "INFO")) {
        printf("ℹ️   %s\n", message + 6);
    } else if (strstr(message, "GRIGLIA")) {
        printf("\033[2J\033[H");
        printf("%s\n", message + 9);
    } else if (strstr(message, "LOBBY")) {
        printf("%s: ", message + 7);
        fflush(stdout); 
    } else if (strstr(message, "CLEAR")) {
        printf("\033[2J\033[H");
        fflush(stdout); 
    } else if (strstr(message, "TURN")) {
        printf("%s", message + 6);
        fflush(stdout); 
    } else if (strstr(message, "ERROR")) {
        printf("❌ %s \n", message + 7);
    }  else {
        printf("%s\n", message);
    }
}

int main() {
    int sock;
    struct sockaddr_in serv_addr;
    struct hostent *he;
    char buffer[BUFFER_SIZE];

    sock = socket(AF_INET, SOCK_STREAM, 0); // Crea socket TCP
    if (sock < 0) {
        perror("❌ Errore creazione socket");
        exit(EXIT_FAILURE);
    }

    serv_addr.sin_family = AF_INET; // IPv4
    serv_addr.sin_port = htons(PORT); // Imposta il numero di porta del server a cui connettersi

    // Risoluzione DNS del nome "server"
    if ((he = gethostbyname("server")) == NULL) {
        perror("❌ Errore DNS");
        exit(EXIT_FAILURE);
    }
    memcpy(&serv_addr.sin_addr, he->h_addr_list[0], he->h_length);

    // Connessione al server
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) { 
        perror("❌ Connessione fallita");
        exit(EXIT_FAILURE);
    }

    printf("✅ Connesso al server.\n");

    while (1) {
        fd_set read_fds; // Rappresenta un insieme di file descriptor da monitorare per eventi in lettura
        FD_ZERO(&read_fds); // Inizializza a vuoto

        FD_SET(sock, &read_fds); // Aggiunge il socket collegato al server
        FD_SET(STDIN_FILENO, &read_fds); // Aggiunge lo stdin

        int max_fd = (sock > STDIN_FILENO) ? sock : STDIN_FILENO; // Calcola il valore massimo dei file descriptor necessario per select

        struct timeval timeout = {1, 0};  // 1 secondo
        int ready = select(max_fd + 1, &read_fds, NULL, NULL, &timeout); // Aspetta fino a 1 secondo per vedere se il socket è pronto per leggere qualcosa
        if (ready < 0) {
            perror("❌ Errore select");
            break;
        }

        if (FD_ISSET(sock, &read_fds)) { // Socket pronto per la lettura
            int n = read(sock, buffer, BUFFER_SIZE - 1); // Legge i dati dal buffer e li salva
            if (n <= 0) {
                printf("🔌 Disconnesso dal server.\n");
                break;
            }

            buffer[n] = '\0';

            char *line = strtok(buffer, "\n"); // Divide il buffer in righe separate
            while (line != NULL) { 
                printMessage(line);
                line = strtok(NULL, "\n");
            }
        }

        // Input dell’utente
        else if (FD_ISSET(STDIN_FILENO, &read_fds)) {

            char input[BUFFER_SIZE];
            if (fgets(input, BUFFER_SIZE, stdin) == NULL) { // Legge una riga dallo stdin e in caso di errore esce
                printf("❌ Errore nella lettura dell'input.\n");
                break;
            }

            input[strcspn(input, "\n")] = '\0'; // rimuove il carattere newline e sostituendolo con terminatore di stringa

            if (strcmp(input, "exit") == 0) { // L'utente desidera uscire
                printf("🚪 Uscita richiesta. Chiusura connessione...\n");
                sendMessage(sock, "exit");
                break;
            }

            sendMessage(sock, input); // Invia il messaggio al server
        }
    }

    close(sock);
    return 0;
}
