// sockets.h
#ifndef SOCKETS_H
#define SOCKETS_H

#include <arpa/inet.h>
#include <linux/if_packet.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>  // Added for memcpy in inline functions

#define MAX_DATA_SIZE 64  // Keep smaller packets that were working better
#define START_MARKER   0x7E

// Packet types
typedef enum {
    PKT_ACK        = 0,
    PKT_NACK       = 1,
    PKT_OK_ACK     = 2,
    PKT_FREE       = 3,
    PKT_SIZE       = 4,  // Changed from PKT_LENGTH to match spec
    PKT_DATA       = 5,
    PKT_TEXT_ACK   = 6,
    PKT_VIDEO_ACK  = 7,
    PKT_IMAGE_ACK  = 8,
    PKT_END_FILE   = 9,
    PKT_MOVE_RIGHT = 10,
    PKT_MOVE_UP    = 11,
    PKT_MOVE_DOWN  = 12,
    PKT_MOVE_LEFT  = 13,
    PKT_ERROR      = 15
} PacketType;

// Error codes
typedef enum {
    ERR_NO_PERMISSION = 0,
    ERR_NO_SPACE      = 1
} ErrorCode;

// Packet structure - matches spec bit field requirements
#pragma pack(push, 1)
typedef struct {
    uint8_t start_marker;   // 8 bits: 0x7E
    uint8_t size_seq_type;  // Combined field: 7 bits size + 5 bits seq + 4 bits type = 16 bits
    uint8_t size_seq_type2; // Second byte of the combined field
    uint8_t checksum;       // 8 bits: XOR checksum over size, seq, type, and data
    uint8_t data[MAX_DATA_SIZE];
} PacketRaw;

// Helper structure for easier access to bit fields
typedef struct {
    uint8_t start_marker;
    uint8_t size;     // 0-127 (7 bits)
    uint8_t seq;      // 0-31 (5 bits)  
    uint8_t type;     // 0-15 (4 bits)
    uint8_t checksum;
    uint8_t data[MAX_DATA_SIZE];
} Packet;
#pragma pack(pop)

// Helper functions to pack/unpack the bit fields
static inline void pack_packet(const Packet *logical, PacketRaw *raw) {
    raw->start_marker = logical->start_marker;
    // Pack size (7 bits), seq (5 bits), type (4 bits) into 16 bits
    uint16_t packed = ((uint16_t)logical->size << 9) | 
                     ((uint16_t)logical->seq << 4) | 
                     ((uint16_t)logical->type);
    raw->size_seq_type = packed >> 8;
    raw->size_seq_type2 = packed & 0xFF;
    raw->checksum = logical->checksum;
    memcpy(raw->data, logical->data, MAX_DATA_SIZE);
}

static inline void unpack_packet(const PacketRaw *raw, Packet *logical) {
    logical->start_marker = raw->start_marker;
    // Unpack the 16-bit field
    uint16_t packed = ((uint16_t)raw->size_seq_type << 8) | raw->size_seq_type2;
    logical->size = (packed >> 9) & 0x7F;  // Extract 7 bits
    logical->seq = (packed >> 4) & 0x1F;   // Extract 5 bits
    logical->type = packed & 0x0F;         // Extract 4 bits
    logical->checksum = raw->checksum;
    memcpy(logical->data, raw->data, MAX_DATA_SIZE);
}

// Core functions
uint8_t calculate_crc(const Packet *pkt);
int     send_packet(int socket_fd, const Packet *pkt, struct sockaddr_ll *addr);
ssize_t receive_packet(int socket_fd, Packet *pkt, struct sockaddr_ll *addr);
void    send_ack(int socket_fd, struct sockaddr_ll *addr, uint8_t type);
void    send_ack_with_position(int socket_fd, struct sockaddr_ll *addr, uint8_t type, uint8_t x, uint8_t y);
void    send_error(int socket_fd, struct sockaddr_ll *addr, uint8_t code);
int     set_socket_timeout(int socket_fd, int timeout_ms);
int     get_interface_info(int socket_fd, const char *iface, struct sockaddr_ll *addr);
int     create_raw_socket(const char *iface);
int     validate_packet(const Packet *pkt);

#endif // SOCKETS_H
