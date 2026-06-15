#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include "../components/esphome_api/esphome_api_protocol.h"

int main() {
    printf("Testing ESPHome switch command without encryption\n");

    // Create socket
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        return 1;
    }

    // Set socket to blocking mode
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags & ~O_NONBLOCK);

    // Connect to device
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(6053);
    inet_pton(AF_INET, "192.168.69.209", &server_addr.sin_addr);

    printf("Connecting to 192.168.69.209:6053...\n");
    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        printf("Connection failed: %s\n", strerror(errno));
        close(sock);
        return 1;
    }

    printf("Connected successfully\n");

    // Send Hello first
    if (esphome_send_hello(sock) != 0) {
        printf("ERROR: Failed to send Hello\n");
        close(sock);
        return 1;
    }

    printf("Sent Hello\n");

    // Read server response
    uint8_t preamble;
    ssize_t preamble_read = recv(sock, &preamble, 1, 0);
    if (preamble_read != 1) {
        printf("ERROR: Failed to read preamble: %s\n", strerror(errno));
        close(sock);
        return 1;
    }

    printf("Server preamble: 0x%02x\n", preamble);

    if (preamble == 0x01) {
        printf("Server requires encryption, but trying switch command anyway...\n");
    }

    // Try sending switch command (relay 28, which should be entity key for "sp28")
    // Entity keys usually start from 1
    uint32_t entity_key = 1; // Try entity key 1
    bool state = true;

    printf("Sending switch command: key=%u, state=%s\n", entity_key, state ? "ON" : "OFF");

    if (esphome_send_switch_command(sock, entity_key, state) != 0) {
        printf("ERROR: Failed to send switch command\n");
        close(sock);
        return 1;
    }

    printf("Switch command sent successfully\n");

    // Try to read response
    uint8_t response_buf[256];
    ssize_t response_len = recv(sock, response_buf, sizeof(response_buf), 0);
    if (response_len > 0) {
        printf("Received %zd bytes response:\n", response_len);
        for (int i = 0; i < response_len; i++) {
            printf("%02x ", response_buf[i]);
            if ((i + 1) % 16 == 0) printf("\n");
        }
        printf("\n");
    } else {
        printf("No response received\n");
    }

    close(sock);
    printf("Test completed\n");
    return 0;
}