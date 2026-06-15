#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>

#include "../components/esphome_api/esphome_api_protocol.h"
#include "../generated_proto/api.pb.h"

// Mock ESPHome server that simulates the handshake
void *mock_server_thread(void *arg) {
    int server_fd = *(int *)arg;
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    printf("[MOCK] Starting mock ESPHome server...\n");

    // Listen for connections
    if (listen(server_fd, 3) < 0) {
        perror("listen");
        return NULL;
    }

    while (1) {
        int client_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);
        if (client_socket < 0) {
            perror("accept");
            continue;
        }

        printf("[MOCK] Client connected\n");

        // Read Hello request
        uint8_t buffer[1024];
        ssize_t len = recv(client_socket, buffer, sizeof(buffer), 0);
        if (len > 0) {
            printf("[MOCK] Received Hello (%zd bytes)\n", len);
            // Send response requiring encryption
            uint8_t response[] = {0x01, 0x00, 0x0b, 0x01, 0x45, 0x6e, 0x63, 0x72, 0x79, 0x70, 0x74, 0x69, 0x6f, 0x6e, 0x20, 0x72, 0x65, 0x71, 0x75, 0x69, 0x72, 0x65, 0x64};
            send(client_socket, response, sizeof(response), 0);
        }

        close(client_socket);
    }

    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s <port>\n", argv[0]);
        return 1;
    }

    int port = atoi(argv[1]);

    // Create socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == 0) {
        perror("socket failed");
        return 1;
    }

    // Set socket options
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt");
        return 1;
    }

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    // Bind
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        return 1;
    }

    // Start server thread
    pthread_t thread;
    pthread_create(&thread, NULL, mock_server_thread, &server_fd);

    printf("[MOCK] Mock ESPHome server listening on port %d\n", port);
    printf("[MOCK] Press Ctrl+C to stop\n");

    // Wait for interrupt
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    int sig;
    sigwait(&set, &sig);

    close(server_fd);
    printf("[MOCK] Server stopped\n");

    return 0;
}