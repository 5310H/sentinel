/**
 * @file server_cert.h
 * @brief Embedded TLS certificate and key for HTTPS
 * 
 * This certificate is embedded in the firmware for HTTPS support.
 * Generated with: scripts/generate_long_cert.sh
 * Valid until: 2036-01-25
 */

#ifndef SERVER_CERT_H
#define SERVER_CERT_H

// Embedded certificate and private key (PEM format)
// This includes both the certificate and key in one string
extern const char server_cert_pem[];
extern const unsigned int server_cert_pem_len;

#endif // SERVER_CERT_H
