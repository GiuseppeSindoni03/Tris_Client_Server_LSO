#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>

#define PORT 8080
#define BUFFER_SIZE 1024

void handle_client(int client_socket) {
    char buffer[BUFFER_SIZE];
    int bytes_read;

    while ((bytes_read = read(client_socket, buffer, sizeof(buffer) - 1)) > 0) {
        buffer[bytes_read] = '\0';
        printf("📥 Messaggio ricevuto: %s\n", buffer);
        write(client_socket, buffer, strlen(buffer));  // echo
    }

    printf("🔌 Client disconnesso.\n");
    close(client_socket);
}

int main() {
    int server_fd, client_socket;
    struct sockaddr_in address;
    socklen_t addr_len = sizeof(address);

    // Creazione socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("❌ Errore nella creazione del socket");
        exit(EXIT_FAILURE);
    }

    // Configurazione indirizzo
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY; // 0.0.0.0
    address.sin_port = htons(PORT);

    // Bind
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("❌ Errore nel bind");
        exit(EXIT_FAILURE);
    }

    // Listen
    if (listen(server_fd, 5) < 0) {
        perror("❌ Errore nel listen");
        exit(EXIT_FAILURE);
    }

    printf("✅ Server in ascolto sulla porta %d...\n", PORT);

    while (1) {
        if ((client_socket = accept(server_fd, (struct sockaddr *)&address, &addr_len)) < 0) {
            perror("❌ Errore nell'accept");
            continue;
        }

        printf("🔗 Nuovo client connesso.\n");

        if (fork() == 0) {
            close(server_fd);
            handle_client(client_socket);
            exit(0);
        }

        close(client_socket);
    }

    return 0;
}
