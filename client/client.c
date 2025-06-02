// === client.c ===
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
    send(sock, buffer, strlen(buffer), 0);
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

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("❌ Errore creazione socket");
        exit(EXIT_FAILURE);
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    if ((he = gethostbyname("server")) == NULL) {
        perror("❌ Errore DNS");
        exit(EXIT_FAILURE);
    }
    memcpy(&serv_addr.sin_addr, he->h_addr_list[0], he->h_length);

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("❌ Connessione fallita");
        exit(EXIT_FAILURE);
    }

    printf("✅ Connesso al server.\n");

    while (1) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(sock, &read_fds);
        FD_SET(STDIN_FILENO, &read_fds);

        int max_fd = (sock > STDIN_FILENO) ? sock : STDIN_FILENO;

        struct timeval timeout = {1, 0};  // 1 secondo
        int ready = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);
        if (ready < 0) {
            perror("❌ Errore select");
            break;
        }

        // 📩 Messaggio dal server
        if (FD_ISSET(sock, &read_fds)) {
            int n = read(sock, buffer, BUFFER_SIZE - 1);
            if (n <= 0) {
                printf("🔌 Disconnesso dal server.\n");
                break;
            }
            buffer[n] = '\0';
            char *line = strtok(buffer, "\n");
            while (line != NULL) {
                printMessage(line);
                line = strtok(NULL, "\n");
            }
        }

        // ⌨️ Input dell’utente
        else if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            char input[BUFFER_SIZE];
            if (fgets(input, BUFFER_SIZE, stdin) == NULL) {
                printf("❌ Errore nella lettura dell'input.\n");
                break;
            }

            input[strcspn(input, "\n")] = '\0';

            if (strcmp(input, "exit") == 0) {
                printf("🚪 Uscita richiesta. Chiusura connessione...\n");
                sendMessage(sock, "exit");
                break;
            }

            sendMessage(sock, input);
        }
    }

    close(sock);
    return 0;
}
