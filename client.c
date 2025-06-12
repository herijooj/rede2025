// client.c
#include "sockets.h"
#include <sys/stat.h>
#include <fcntl.h>
#include <linux/limits.h> // Added for PATH_MAX
#include <signal.h>
#include <stdbool.h>
#include <ctype.h>
#include <limits.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// Command types
typedef enum {
    CMD_INVALID = 0,
    CMD_BACKUP
} ClientCommand;

// Client context structure
typedef struct {
    int socket;
    ClientCommand cmd;
    char filename[PATH_MAX];
    struct sockaddr_ll addr;
} ClientContext;

// Function declarations
void display_help(const char* program_name);

// Initialize client context
static void init_client_context(ClientContext *ctx, int socket, char *filename, ClientCommand cmd) {
    memset(ctx, 0, sizeof(ClientContext));
    ctx->socket = socket;
    ctx->cmd = cmd;
    strncpy(ctx->filename, filename, PATH_MAX - 1);
}

void backup_file(int socket, char *filename, struct sockaddr_ll *addr);

void display_help(const char* program_name) {
    printf(BLUE "NARBS Client (Not A Real Backup Solution)\\n" RESET);
    printf(GREEN "Usage:\\n" RESET);
    printf("  %s <interface> <filename>\\n\\n", program_name);
    printf(YELLOW "Commands:\\n" RESET);
    printf("  backup    - Create a backup of a file\\n\\n");
    printf(YELLOW "Options:\\n" RESET);
    printf("  -h        - Display this help message");
}

void backup_file(int socket, char *filename, struct sockaddr_ll *addr) {
    DBG_INFO("Starting backup of %s\n", filename);

    char resolved_path[PATH_MAX];
    if (realpath(filename, resolved_path) == NULL) {
        DBG_ERROR("Cannot resolve file path %s: %s\n", filename, strerror(errno));
        fprintf(stderr, "Error: Invalid file path '%s': %s\n", filename, strerror(errno));
        return;
    }

    char *base_filename = strrchr(resolved_path, '/');
    if (base_filename) {
        base_filename++;
    } else {
        base_filename = resolved_path;
    }

    int fd = open(resolved_path, O_RDONLY);
    if (fd < 0) {
        DBG_ERROR("Cannot open file %s: %s\n", resolved_path, strerror(errno));
        fprintf(stderr, "Error: Cannot open file '%s' for reading: %s\n", resolved_path, strerror(errno));
        return;
    }

    struct stat st;
    if (fstat(fd, &st) < 0) {
        DBG_ERROR("Cannot stat file: %s\n", strerror(errno));
        close(fd);
        return;
    }

    uint64_t total_size = st.st_size;
    DBG_INFO("File size: %lu bytes\n", total_size);

    struct TransferStats stats;
    transfer_init_stats(&stats, total_size);
    size_t total_chunks = (total_size + MAX_DATA_SIZE - 1) / MAX_DATA_SIZE;
    DBG_INFO("File will be sent in %zu chunks\n", total_chunks);

    Packet packet = {0};
    memset(packet.padding, 0, PAD_SIZE);
    packet.start_marker = START_MARKER;
    SET_TYPE(packet.size_seq_type, PKT_BACKUP);
    SET_SEQUENCE(packet.size_seq_type, 0);
    SET_SIZE(packet.size_seq_type, strlen(base_filename) + 1 + sizeof(size_t));

    size_t max_filename_len = MAX_DATA_SIZE - sizeof(size_t) - 1;
    if (strlen(base_filename) >= max_filename_len) {
        DBG_ERROR("Filename too long (max %zu chars): %s\n", max_filename_len - 1, base_filename);
        fprintf(stderr, "Error: Filename exceeds maximum length\n");
        close(fd);
        return;
    }
    
    memset(packet.data, 0, MAX_DATA_SIZE);
    memcpy(packet.data, base_filename, strlen(base_filename));
    *((size_t *)(packet.data + strlen(base_filename) + 1)) = total_size;

    if (send_packet(socket, &packet, addr, true) < 0 ||
        wait_for_ack(socket, &packet, addr, PKT_ACK) != 0 ||
        wait_for_ack(socket, &packet, addr, PKT_OK_SIZE) != 0) {
        DBG_ERROR("Failed to initialize backup\n");
        close(fd);
        return;
    }

    char buffer[MAX_DATA_SIZE];
    uint8_t seq = 0;
    Packet data_packet = {0}; // Initialize outside the loop

    while (stats.total_received < total_size) {
        uint64_t to_read = total_size - stats.total_received;
        if (to_read > MAX_DATA_SIZE) {
            to_read = MAX_DATA_SIZE;
        }
        
        ssize_t bytes = read(fd, buffer, to_read);
        if (bytes <= 0) {
            DBG_ERROR("Read error: %s\n", strerror(errno));

            Packet error_packet = {0};
            error_packet.start_marker = START_MARKER;
            SET_TYPE(error_packet.size_seq_type, PKT_ERROR);
            SET_SIZE(error_packet.size_seq_type, 1);
            error_packet.data[0] = ERR_NO_ACCESS;
            send_packet(socket, &error_packet, addr, true);

            close(fd);
            return;
        }

        if (bytes > MAX_DATA_SIZE) {
            DBG_ERROR("Read more bytes than allowed: %zd\n", bytes);
            close(fd);
            return;
        }

        if (stats.total_received + bytes > total_size) {
            DBG_ERROR("Attempting to send more data than total size\n");
            close(fd);
            return;
        }

        if (seq > SEQ_NUM_MAX) {
            DBG_ERROR("Sequence number overflow\n");
            close(fd);
            return;
        }

        size_t remaining = total_size - stats.total_received;
        DBG_INFO("Preparing chunk: seq=%d, size=%zd, remaining=%zu\n",
                 seq, bytes, remaining);

        int retries = 0;
        bool chunk_sent = false;

        while (retries < MAX_RETRIES && !chunk_sent) {
            // Prepare data packet
            memset(&data_packet, 0, sizeof(Packet));
            data_packet.start_marker = START_MARKER;
            SET_TYPE(data_packet.size_seq_type, PKT_DATA);
            SET_SEQUENCE(data_packet.size_seq_type, seq);
            SET_SIZE(data_packet.size_seq_type, bytes & SIZEFIELD_MAX);
            memcpy(data_packet.data, buffer, bytes);
            memset(data_packet.padding, 0, PAD_SIZE);
            
            // Send data packet with is_send = true
            if (send_packet(socket, &data_packet, addr, true) >= 0) {
                // Wait for ACK or ERROR with is_send = false
                if (receive_packet(socket, &packet, addr, false) > 0) {
                    uint8_t received_type = GET_TYPE(packet.size_seq_type);
                    if (received_type == PKT_OK) {
                        uint8_t received_seq = GET_SEQUENCE(packet.size_seq_type);
                        if (received_seq == seq) {
                            // Update stats first with actual bytes sent
                            stats.total_received += bytes;
                            transfer_update_stats(&stats, 0, seq);  // Don't add bytes here
                            
                            seq = (seq + 1) & SEQ_NUM_MAX;
                            chunk_sent = true;
                            
                            // Update and display progress
                            debug_transfer_progress(&stats, &data_packet);
                        } else {
                            // Unexpected sequence number
                            DBG_WARN("Unexpected ACK sequence: expected %d, got %d\n", seq, received_seq);
                            retries++;
                            usleep(RETRY_DELAY_MS * 1000);
                        }
                    } else if (received_type == PKT_ERROR) {
                        DBG_ERROR("Server error code=%d seq=%u size=%u\n",
                                 packet.data[0], 
                                 GET_SEQUENCE(packet.size_seq_type),
                                 GET_SIZE(packet.size_seq_type));
                        DBG_WARN("Received ERROR, retransmitting seq=%d\n", seq);
                        retries++;
                        usleep(RETRY_DELAY_MS * 1000);
                    } else {
                        // Unexpected packet type, ignore and retry
                        DBG_WARN("Unexpected packet type=%d, retrying seq=%d\n", received_type, seq);
                        retries++;
                        usleep(RETRY_DELAY_MS * 1000);
                    }
                } else {
                    // No response, treat as timeout
                    retries++;
                    DBG_WARN("No response, retrying seq=%d (%d/%d)\n", seq, retries, MAX_RETRIES);
                    usleep(RETRY_DELAY_MS * 1000);
                }
                
                // Add progress debugging
                debug_transfer_progress(&stats, &data_packet);
            } else {
                // Send failed, increment retries
                retries++;
                DBG_WARN("Failed to send packet, retrying seq=%d (%d/%d)\n", seq, retries, MAX_RETRIES);
                usleep(RETRY_DELAY_MS * 1000);
            }
        }

        if (!chunk_sent) {
            DBG_ERROR("Failed to send chunk seq=%d after %d retries\n", seq, MAX_RETRIES);
            close(fd);
            return;
        }
    }

    if (stats.total_received == total_size) {
        DBG_INFO("All chunks sent successfully, sending END_TX\n");
        Packet end_packet = {0};
        end_packet.start_marker = START_MARKER;
        SET_TYPE(end_packet.size_seq_type, PKT_END_TX);
        SET_SEQUENCE(end_packet.size_seq_type, 0);
        SET_SIZE(end_packet.size_seq_type, 0);
        end_packet.crc = calculate_crc(&end_packet); // is_send = true

        int retries = 0;
        bool end_tx_sent = false;
        while (retries < MAX_RETRIES && !end_tx_sent) {
            if (send_packet(socket, &end_packet, addr, true) >= 0 &&
                wait_for_ack(socket, &end_packet, addr, PKT_OK_CHSUM) == 0) {
                print_transfer_summary(&stats);
                DBG_INFO("Transfer completed successfully\n");
                end_tx_sent = true;
            } else {
                retries++;
                DBG_WARN("Retrying END_TX (attempt %d/%d)\n", retries, MAX_RETRIES);
                usleep(RETRY_DELAY_MS * 1000);
            }
        }

        if (!end_tx_sent) {
            DBG_ERROR("Failed to send END_TX after %d retries\n", MAX_RETRIES);
        }
    } else {
        DBG_ERROR("Transfer incomplete, not sending END_TX\n");
    }

    close(fd);
}

int main(int argc, char *argv[]) {
    debug_init();
    debug_init_error_log("client");

    if (argc == 2 && strcmp(argv[1], "-h") == 0) {
        display_help(argv[0]);
        return 0;
    }

    if (argc != 3) { // Changed from 4 to 3
        fprintf(stderr, RED "Error: Invalid number of arguments\n" RESET);
        fprintf(stderr, "Use '%s <interface> <filename>' or '%s -h' for help\n", argv[0], argv[0]); // Simplified usage
        exit(1);
    }

    int socket_fd = cria_raw_socket(argv[1]);
    struct sockaddr_ll addr;
    if (get_interface_info(socket_fd, argv[1], &addr) < 0) {
        exit(1);
    }

    // Command is implicitly CMD_BACKUP
    ClientContext ctx;
    init_client_context(&ctx, socket_fd, argv[2], CMD_BACKUP); // argv[2] is filename, cmd is CMD_BACKUP
    ctx.addr = addr;

    backup_file(ctx.socket, ctx.filename, &ctx.addr);

    return 0;
}