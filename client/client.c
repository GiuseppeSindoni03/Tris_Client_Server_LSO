#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h> 

#define PORT 8080
#define BUFFER_SIZE 1024

int main() {
    int sock;
    struct sockaddr_in server_address;
    char buffer[BUFFER_SIZE];

    // Creazione socket
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("❌ Errore nella creazione del socket");
        exit(EXIT_FAILURE);
    }

    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(PORT);

    struct hostent *he;
    if ((he = gethostbyname("server")) == NULL) {
        perror("❌ Indirizzo non valido");
        exit(EXIT_FAILURE);
    }
    memcpy(&server_address.sin_addr, he->h_addr_list[0], he->h_length);

    // Connessione al server
    if (connect(sock, (struct sockaddr *)&server_address, sizeof(server_address)) < 0) {
        perror("❌ Connessione fallita");
        exit(EXIT_FAILURE);
    }

    printf("✅ Connesso al server!\n");

    while (1) {
        printf("✉️ Scrivi un messaggio: ");
        fgets(buffer, BUFFER_SIZE, stdin);
        buffer[strcspn(buffer, "\n")] = 0;  // rimuove il newline

        send(sock, buffer, strlen(buffer), 0);

        int valread = read(sock, buffer, BUFFER_SIZE - 1);
        if (valread > 0) {
            buffer[valread] = '\0';
            printf("📨 Risposta del server: %s\n", buffer);
        }
    }

    close(sock);
    return 0;
}
