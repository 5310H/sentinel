#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>

// Include the ESPHome API headers
#include "../components/esphome_api/esphome_api_protocol.h"

// Mock storage for testing
typedef struct {
    char hostname[64];
    char password[64];
    char encryption_key[64];
} esphome_device_config_t;

esphome_device_config_t test_device = {
    .hostname = "192.168.69.209",
    .password = "password",
    .encryption_key = ""  // Empty for plain connection test
};

int main() {
    printf("Testing ESPHome encryption requirement detection\n");

    // Create socket
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        printf("Failed to create socket\n");
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
    inet_pton(AF_INET, test_device.hostname, &server_addr.sin_addr);

    printf("Connecting to %s:6053...\n", test_device.hostname);
    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        printf("Connection failed: %s\n", strerror(errno));
        close(sock);
        return 1;
    }

    printf("Connected successfully\n");

    // Send Hello message
    if (esphome_send_hello(sock) != 0) {
        printf("Failed to send Hello\n");
        close(sock);
        return 1;
    }

    printf("Sent Hello message\n");

    // Read server response
    uint8_t response_buffer[512];
    uint32_t response_msg_type;
    int response_len = esphome_read_message(sock, response_buffer, sizeof(response_buffer), &response_msg_type);

    if (response_len == -2) {
        printf("SUCCESS: Server requires encryption (preamble 0x01 detected)\n");
    } else if (response_len > 0) {
        printf("Server allows plain connection (preamble 0x00)\n");
        printf("Response type: %u, length: %d\n", response_msg_type, response_len);
    } else {
        printf("Failed to read response (errno=%d): %s\n", errno, strerror(errno));
    }

    close(sock);
    return 0;
}