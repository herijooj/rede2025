#include "sockets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/time.h>
#include <unistd.h>  // Added for usleep

// Helper function to get current timestamp in milliseconds
static long long get_timestamp_ms(void) {
    struct timeval tp;
    gettimeofday(&tp, NULL);
    return tp.tv_sec * 1000LL + tp.tv_usec / 1000;
}

// Calculate CRC (XOR) over header and data fields
uint8_t calculate_crc(const Packet *pkt) {
    uint8_t crc = 0;
    // XOR over size, sequence, type
    crc ^= pkt->size;
    crc ^= pkt->seq;
    crc ^= pkt->type;
    
    // XOR over data bytes
    for (int i = 0; i < pkt->size; i++) {
        crc ^= pkt->data[i];
    }
    return crc;
}

// Validate packet structure and checksum
int validate_packet(const Packet *pkt) {
    if (!pkt) return 0;
    
    // Check start marker
    if (pkt->start_marker != START_MARKER) return 0;
    
    // Validate size (7 bits, so 0-127 is automatically enforced by bit field)
    if (pkt->size > MAX_DATA_SIZE) return 0;
    
    // Validate sequence number (5 bits, so 0-31 is automatically enforced by bit field)
    // No additional check needed due to bit field constraint
    
    // Validate packet type (4 bits, so 0-15 is automatically enforced by bit field)
    if (pkt->type > PKT_ERROR) return 0;
    
    // Verify checksum
    return calculate_crc(pkt) == pkt->checksum;
}

// Set socket timeout
int set_socket_timeout(int socket_fd, int timeout_ms) {
    struct timeval timeout;
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;
    
    if (setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        perror("setsockopt SO_RCVTIMEO failed");
        return -1;
    }
    
    if (setsockopt(socket_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0) {
        perror("setsockopt SO_SNDTIMEO failed");
        return -1;
    }
    
    return 0;
}

// Get interface information
int get_interface_info(int socket_fd, const char *iface, struct sockaddr_ll *addr) {
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, iface, IFNAMSIZ - 1);
    
    // Get interface index
    if (ioctl(socket_fd, SIOCGIFINDEX, &ifr) < 0) {
        perror("ioctl SIOCGIFINDEX failed");
        return -1;
    }
    
    // Setup sockaddr_ll structure
    memset(addr, 0, sizeof(struct sockaddr_ll));
    addr->sll_family = AF_PACKET;
    addr->sll_protocol = htons(ETH_P_ALL);
    addr->sll_ifindex = ifr.ifr_ifindex;
    
    return 0;
}

// Create raw socket
int create_raw_socket(const char *iface) {
    int sock_fd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (sock_fd < 0) {
        perror("socket creation failed");
        return -1;
    }
    
    // Get interface info
    struct sockaddr_ll addr;
    if (get_interface_info(sock_fd, iface, &addr) < 0) {
        close(sock_fd);
        return -1;
    }
    
    // Bind to interface
    if (bind(sock_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind failed");
        close(sock_fd);
        return -1;
    }
    
    // Enable promiscuous mode
    struct packet_mreq mr;
    memset(&mr, 0, sizeof(mr));
    mr.mr_ifindex = addr.sll_ifindex;
    mr.mr_type = PACKET_MR_PROMISC;
    
    if (setsockopt(sock_fd, SOL_PACKET, PACKET_ADD_MEMBERSHIP, &mr, sizeof(mr)) < 0) {
        perror("setsockopt PACKET_ADD_MEMBERSHIP failed");
        close(sock_fd);
        return -1;
    }
    
    return sock_fd;
}

// Send packet with retransmission and exponential backoff
int send_packet(int socket_fd, const Packet *pkt, struct sockaddr_ll *addr) {
    if (!pkt || !addr) return -1;
    
    // Pack the logical packet into wire format
    PacketRaw raw_pkt;
    pack_packet(pkt, &raw_pkt);
    
    // Calculate checksum on the logical packet before sending
    ((Packet *)pkt)->checksum = calculate_crc(pkt);
    raw_pkt.checksum = pkt->checksum;
    
    int timeout_ms = 1000;  // Start with 1 second timeout
    const int max_retries = 10;  // More retries for problematic sequences
    int retries = 0;
    
    while (retries < max_retries) {
        // Set timeout for this attempt
        if (set_socket_timeout(socket_fd, timeout_ms) < 0) {
            return -1;
        }
        
        // Add progressive delay between retries to avoid overwhelming socket buffer
        if (retries > 0) {
            usleep(retries * 100000); // 100ms * retry_count delay
        }
        
        // Send the packed packet with retry logic for EAGAIN
        ssize_t sent = -1;
        int send_attempts = 0;
        while (send_attempts < 5 && sent != sizeof(PacketRaw)) {
            sent = sendto(socket_fd, &raw_pkt, sizeof(PacketRaw), 0,
                         (struct sockaddr *)addr, sizeof(struct sockaddr_ll));
            
            if (sent != sizeof(PacketRaw)) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    // Socket buffer full, wait and retry
                    usleep(200000); // 200ms delay
                    send_attempts++;
                    continue;
                } else {
                    break; // Other error, don't retry sends
                }
            }
        }
        
        if (sent != sizeof(PacketRaw)) {
            retries++;
            timeout_ms = (timeout_ms < 2000) ? timeout_ms + 300 : 2000;
            continue;
        }
        
        // For certain packet types, don't wait for ACK (like ACKs themselves)
        if (pkt->type == PKT_ACK || pkt->type == PKT_NACK) {
            return 0; // Don't wait for ACK on ACK packets
        }
        
        // Wait for ACK - STRICT sequence number matching
        long long ack_start = get_timestamp_ms();
        
        while (get_timestamp_ms() - ack_start < timeout_ms) {
            PacketRaw ack_raw;
            struct sockaddr_ll from_addr;
            socklen_t from_len = sizeof(from_addr);
            
            ssize_t received = recvfrom(socket_fd, &ack_raw, sizeof(PacketRaw), 0, 
                                      (struct sockaddr *)&from_addr, &from_len);
            
            if (received == sizeof(PacketRaw)) {
                Packet ack;
                unpack_packet(&ack_raw, &ack);
                
                if (ack.start_marker == START_MARKER && 
                    ack.type == PKT_ACK &&
                    validate_packet(&ack) &&
                    ack.seq == pkt->seq) {
                    return 0;  // Success - exact ACK received
                }
            }
            
            // Brief pause to avoid busy waiting
            usleep(10000); // 10ms
        }
        
        retries++;
        timeout_ms = (timeout_ms < 2000) ? timeout_ms + 300 : 2000;
    }
    
    return -1;  // All retries failed
}

// Receive packet with timeout
ssize_t receive_packet(int socket_fd, Packet *pkt, struct sockaddr_ll *addr) {
    if (!pkt || !addr) return -1;
    
    socklen_t addr_len = sizeof(struct sockaddr_ll);
    long long start_time = get_timestamp_ms();
    const int timeout_ms = 300;  // 300ms timeout
    
    while (get_timestamp_ms() - start_time < timeout_ms) {
        PacketRaw raw_pkt;
        ssize_t received = recvfrom(socket_fd, &raw_pkt, sizeof(PacketRaw), 0,
                                  (struct sockaddr *)addr, &addr_len);
        
        if (received == sizeof(PacketRaw)) {
            // Unpack the received packet
            unpack_packet(&raw_pkt, pkt);
            
            if (validate_packet(pkt)) {
                // Send ACK - always send ACK for valid packets
                Packet ack = {
                    .start_marker = START_MARKER,
                    .size = 0,
                    .seq = pkt->seq,
                    .type = PKT_ACK
                };
                ack.checksum = calculate_crc(&ack);
                
                PacketRaw ack_raw;
                pack_packet(&ack, &ack_raw);
                
                // Send ACK multiple times to ensure delivery for critical data packets
                int ack_attempts = (pkt->type == PKT_DATA) ? 2 : 1;
                for (int i = 0; i < ack_attempts; i++) {
                    sendto(socket_fd, &ack_raw, sizeof(PacketRaw), 0,
                          (struct sockaddr *)addr, addr_len);
                    if (i < ack_attempts - 1) {
                        usleep(10000); // 10ms delay between ACK attempts
                    }
                }
                
                return received;
            }
        }
    }
    
    return -1;  // Timeout or invalid packet
}

// Send ACK packet
void send_ack(int socket_fd, struct sockaddr_ll *addr, uint8_t type) {
    Packet ack = {
        .start_marker = START_MARKER,
        .size = 0,
        .seq = 0,  // Sequence number should be set by caller if needed
        .type = type
    };
    ack.checksum = calculate_crc(&ack);
    
    PacketRaw ack_raw;
    pack_packet(&ack, &ack_raw);
    
    sendto(socket_fd, &ack_raw, sizeof(PacketRaw), 0,
           (struct sockaddr *)addr, sizeof(struct sockaddr_ll));
}

// Send ACK packet with position
void send_ack_with_position(int socket_fd, struct sockaddr_ll *addr, uint8_t type, uint8_t x, uint8_t y) {
    Packet ack = {
        .start_marker = START_MARKER,
        .size = 2,  // For X and Y coordinates
        .seq = 0,  // Sequence number should be set by caller if needed
        .type = type
    };
    ack.data[0] = x;
    ack.data[1] = y;
    ack.checksum = calculate_crc(&ack);
    
    PacketRaw ack_raw;
    pack_packet(&ack, &ack_raw);
    
    sendto(socket_fd, &ack_raw, sizeof(PacketRaw), 0,
           (struct sockaddr *)addr, sizeof(struct sockaddr_ll));
}

// Send error packet
void send_error(int socket_fd, struct sockaddr_ll *addr, uint8_t code) {
    Packet err = {
        .start_marker = START_MARKER,
        .size = 1,
        .seq = 0,  // Sequence number should be set by caller if needed
        .type = PKT_ERROR,
        .data = {code}
    };
    err.checksum = calculate_crc(&err);
    
    PacketRaw err_raw;
    pack_packet(&err, &err_raw);
    
    sendto(socket_fd, &err_raw, sizeof(PacketRaw), 0,
           (struct sockaddr *)addr, sizeof(struct sockaddr_ll));
}