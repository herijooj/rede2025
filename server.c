#include "sockets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>

#define GRID_SIZE 8
#define MAX_TREASURES 8
#define OBJECTS_DIR "./objetos"

typedef struct {
    int x, y;
    char filename[512];  // Increased from 64 to 512 to accommodate full paths
    int discovered;
} Treasure;

typedef struct {
    int player_x, player_y;
    Treasure treasures[MAX_TREASURES];
    int treasure_count;
    int socket_fd;
    struct sockaddr_ll client_addr;
    uint8_t seq_num;  // Will be masked to 5 bits when used
} GameState;

// Function prototypes
void init_game(GameState *game);
void display_server_state(const GameState *game);
int find_treasure_files(GameState *game);
int handle_movement(GameState *game, PacketType move_type);
int send_file_to_client(GameState *game, const char *filepath, PacketType file_type);
void process_client_packet(GameState *game, const Packet *pkt);
void log_movement(const GameState *game, const char *direction);
int check_treasure_discovery(GameState *game);
int count_undiscovered(const GameState *game);

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <interface>\n", argv[0]);
        return 1;
    }

    GameState game = {0};
    
    // Create raw socket
    game.socket_fd = create_raw_socket(argv[1]);
    if (game.socket_fd < 0) {
        fprintf(stderr, "Failed to create raw socket\n");
        return 1;
    }

    // Get interface info
    if (get_interface_info(game.socket_fd, argv[1], &game.client_addr) < 0) {
        close(game.socket_fd);
        return 1;
    }

    // Initialize game
    init_game(&game);
    
    printf("=== TREASURE HUNT SERVER ===\n");
    printf("Interface: %s\n", argv[1]);
    printf("Waiting for client connections...\n\n");
    
    display_server_state(&game);

    // Main server loop
    Packet pkt;
    struct sockaddr_ll client_addr;
    socklen_t addr_len = sizeof(client_addr);
    
    while (1) {
        // Use direct recvfrom with proper packet unpacking
        PacketRaw raw_pkt;
        ssize_t received = recvfrom(game.socket_fd, &raw_pkt, sizeof(PacketRaw), 0,
                                  (struct sockaddr *)&client_addr, &addr_len);
        
        if (received == sizeof(PacketRaw)) {
            // Unpack the received packet
            unpack_packet(&raw_pkt, &pkt);
            
            if (validate_packet(&pkt)) {
                // Update client address for responses
                game.client_addr = client_addr;
                process_client_packet(&game, &pkt);
                display_server_state(&game);
            }
        }
    }

    close(game.socket_fd);
    return 0;
}

void init_game(GameState *game) {
    // Initialize player position at bottom-left (0,0)
    game->player_x = 0;
    game->player_y = 0;
    game->seq_num = 0;
    
    // Find treasure files
    game->treasure_count = find_treasure_files(game);
    
    // Randomly place treasures on the grid
    srand(time(NULL));
    for (int i = 0; i < game->treasure_count; i++) {
        int placed = 0;
        while (!placed) {
            int x = rand() % GRID_SIZE;
            int y = rand() % GRID_SIZE;
            
            // Check if position is already occupied
            int occupied = 0;
            for (int j = 0; j < i; j++) {
                if (game->treasures[j].x == x && game->treasures[j].y == y) {
                    occupied = 1;
                    break;
                }
            }
            
            if (!occupied) {
                game->treasures[i].x = x;
                game->treasures[i].y = y;
                game->treasures[i].discovered = 0;
                placed = 1;
            }
        }
    }
}

int find_treasure_files(GameState *game) {
    DIR *dir = opendir(OBJECTS_DIR);
    if (!dir) {
        printf("Warning: Could not open %s directory\n", OBJECTS_DIR);
        return 0;
    }

    struct dirent *entry;
    int count = 0;
    
    while ((entry = readdir(dir)) != NULL && count < MAX_TREASURES) {
        // Check if filename matches pattern: digit 1-8 followed by a dot (1.xxx to 8.xxx)
        if (entry->d_name[0] >= '1' && entry->d_name[0] <= '8' && 
            entry->d_name[1] == '.' && strlen(entry->d_name) > 2) {
            snprintf(game->treasures[count].filename, sizeof(game->treasures[count].filename),
                    "%s/%s", OBJECTS_DIR, entry->d_name);
            count++;
        }
    }
    
    closedir(dir);
    return count;
}

void display_server_state(const GameState *game) {
    printf("\n=== SERVER STATE ===\n");
    printf("Player position: (%d, %d)\n", game->player_x, game->player_y);
    printf("Treasures found: %d/%d\n", 
           game->treasure_count - count_undiscovered(game), game->treasure_count);
    
    printf("\nGrid (P=Player, T=Treasure, D=Discovered, .=Empty):\n");
    printf("  ");
    for (int x = 0; x < GRID_SIZE; x++) printf("%d ", x);
    printf("\n");
    
    for (int y = GRID_SIZE - 1; y >= 0; y--) {
        printf("%d ", y);
        for (int x = 0; x < GRID_SIZE; x++) {
            char cell = '.';
            
            // Check if player is here
            if (game->player_x == x && game->player_y == y) {
                cell = 'P';
            } else {
                // Check for treasures
                for (int i = 0; i < game->treasure_count; i++) {
                    if (game->treasures[i].x == x && game->treasures[i].y == y) {
                        cell = game->treasures[i].discovered ? 'D' : 'T';
                        break;
                    }
                }
            }
            printf("%c ", cell);
        }
        printf("\n");
    }
    
    printf("\nTreasure locations:\n");
    for (int i = 0; i < game->treasure_count; i++) {
        printf("  %s at (%d,%d) - %s\n",
               game->treasures[i].filename,
               game->treasures[i].x, game->treasures[i].y,
               game->treasures[i].discovered ? "DISCOVERED" : "hidden");
    }
    printf("========================\n\n");
}

int count_undiscovered(const GameState *game) {
    int count = 0;
    for (int i = 0; i < game->treasure_count; i++) {
        if (!game->treasures[i].discovered) count++;
    }
    return count;
}

void process_client_packet(GameState *game, const Packet *pkt) {
    switch (pkt->type) {
        case PKT_MOVE_RIGHT:
            if (handle_movement(game, PKT_MOVE_RIGHT)) {
                log_movement(game, "RIGHT");
                // Check for treasure first, then send appropriate response
                int treasure_found = check_treasure_discovery(game);
                if (!treasure_found) {
                    send_ack_with_position(game->socket_fd, &game->client_addr, PKT_OK_ACK, game->player_x, game->player_y);
                }
            } else {
                send_error(game->socket_fd, &game->client_addr, ERR_NO_PERMISSION);
            }
            break;
            
        case PKT_MOVE_LEFT:
            if (handle_movement(game, PKT_MOVE_LEFT)) {
                log_movement(game, "LEFT");
                int treasure_found = check_treasure_discovery(game);
                if (!treasure_found) {
                    send_ack_with_position(game->socket_fd, &game->client_addr, PKT_OK_ACK, game->player_x, game->player_y);
                }
            } else {
                send_error(game->socket_fd, &game->client_addr, ERR_NO_PERMISSION);
            }
            break;
            
        case PKT_MOVE_UP:
            if (handle_movement(game, PKT_MOVE_UP)) {
                log_movement(game, "UP");
                int treasure_found = check_treasure_discovery(game);
                if (!treasure_found) {
                    send_ack_with_position(game->socket_fd, &game->client_addr, PKT_OK_ACK, game->player_x, game->player_y);
                }
            } else {
                send_error(game->socket_fd, &game->client_addr, ERR_NO_PERMISSION);
            }
            break;
            
        case PKT_MOVE_DOWN:
            if (handle_movement(game, PKT_MOVE_DOWN)) {
                log_movement(game, "DOWN");
                int treasure_found = check_treasure_discovery(game);
                if (!treasure_found) {
                    send_ack_with_position(game->socket_fd, &game->client_addr, PKT_OK_ACK, game->player_x, game->player_y);
                }
            } else {
                send_error(game->socket_fd, &game->client_addr, ERR_NO_PERMISSION);
            }
            break;
            
        default:
            printf("Received unknown packet type: %d\n", pkt->type);
            send_ack(game->socket_fd, &game->client_addr, PKT_NACK);
            break;
    }
}

int handle_movement(GameState *game, PacketType move_type) {
    int new_x = game->player_x;
    int new_y = game->player_y;
    
    switch (move_type) {
        case PKT_MOVE_RIGHT: new_x++; break;
        case PKT_MOVE_LEFT:  new_x--; break;
        case PKT_MOVE_UP:    new_y++; break;
        case PKT_MOVE_DOWN:  new_y--; break;
        default: return 0;
    }
    
    // Check bounds
    if (new_x < 0 || new_x >= GRID_SIZE || new_y < 0 || new_y >= GRID_SIZE) {
        return 0; // Invalid move
    }
    
    // Update position
    game->player_x = new_x;
    game->player_y = new_y;
    return 1; // Valid move
}

int check_treasure_discovery(GameState *game) {
    for (int i = 0; i < game->treasure_count; i++) {
        if (game->treasures[i].x == game->player_x && 
            game->treasures[i].y == game->player_y && 
            !game->treasures[i].discovered) {
            
            game->treasures[i].discovered = 1;
            printf("TREASURE DISCOVERED at (%d,%d): %s\n", 
                   game->player_x, game->player_y, game->treasures[i].filename);
            
            // Determine file type and send
            PacketType file_type = PKT_TEXT_ACK;
            if (strstr(game->treasures[i].filename, ".jpg") || 
                strstr(game->treasures[i].filename, ".jpeg")) {
                file_type = PKT_IMAGE_ACK;
            } else if (strstr(game->treasures[i].filename, ".mp4")) {
                file_type = PKT_VIDEO_ACK;
            } else if (strstr(game->treasures[i].filename, ".mp3") ||
                      strstr(game->treasures[i].filename, ".wav") ||
                      strstr(game->treasures[i].filename, ".ogg")) {
                file_type = PKT_VIDEO_ACK; // Use VIDEO_ACK for audio files too
            }
            
            send_file_to_client(game, game->treasures[i].filename, file_type);
            return 1; // Treasure found
        }
    }
    return 0; // No treasure found
}

int send_file_to_client(GameState *game, const char *filepath, PacketType file_type) {
    FILE *file = fopen(filepath, "rb");
    if (!file) {
        printf("Error: Could not open file %s\n", filepath);
        send_error(game->socket_fd, &game->client_addr, ERR_NO_PERMISSION);
        return -1;
    }
    
    // Get file size
    struct stat st;
    if (stat(filepath, &st) < 0) {
        fclose(file);
        send_error(game->socket_fd, &game->client_addr, ERR_NO_PERMISSION);
        return -1;
    }
    
    printf("Sending file: %s (%ld bytes)\n", filepath, st.st_size);
    
    // Send file size using proper stop-and-wait
    Packet size_pkt = {
        .start_marker = START_MARKER,
        .size = sizeof(uint32_t) + 2, // Add 2 for coordinates
        .seq = (game->seq_num++) & 0x1F,
        .type = PKT_SIZE
    };
    uint32_t file_size = htonl(st.st_size);
    memcpy(size_pkt.data, &file_size, sizeof(uint32_t));
    size_pkt.data[sizeof(uint32_t)] = game->player_x;
    size_pkt.data[sizeof(uint32_t) + 1] = game->player_y;
    size_pkt.checksum = calculate_crc(&size_pkt);
    
    if (send_packet(game->socket_fd, &size_pkt, &game->client_addr) < 0) {
        fclose(file);
        return -1;
    }
    
    // Send filename with file type using proper stop-and-wait
    const char *filename = strrchr(filepath, '/');
    filename = filename ? filename + 1 : filepath;
    
    Packet name_pkt = {
        .start_marker = START_MARKER,
        .size = strlen(filename),
        .seq = (game->seq_num++) & 0x1F,
        .type = file_type
    };
    strcpy((char*)name_pkt.data, filename);
    name_pkt.checksum = calculate_crc(&name_pkt);
    
    if (send_packet(game->socket_fd, &name_pkt, &game->client_addr) < 0) {
        fclose(file);
        return -1;
    }
    
    // Send file data using modified stop-and-wait with aggressive error recovery
    uint8_t buffer[MAX_DATA_SIZE];
    size_t bytes_read;
    size_t total_sent = 0;
    int packet_count = 0;
    int consecutive_failures = 0;
    
    while ((bytes_read = fread(buffer, 1, MAX_DATA_SIZE, file)) > 0) {
        uint8_t current_seq = (game->seq_num++) & 0x1F;
        
        Packet data_pkt = {
            .start_marker = START_MARKER,
            .size = bytes_read,
            .seq = current_seq,
            .type = PKT_DATA
        };
        memcpy(data_pkt.data, buffer, bytes_read);
        data_pkt.checksum = calculate_crc(&data_pkt);
        
        // Special handling for the problematic range around packet 68,543
        if (packet_count >= 68500 && packet_count <= 68600) {
            printf("\nCritical range - packet %d, seq %d, extra delay...\n", packet_count, current_seq);
            usleep(5000000); // 5 second delay in critical range
        }
        
        if (send_packet(game->socket_fd, &data_pkt, &game->client_addr) < 0) {
            consecutive_failures++;
            printf("Failed to send data packet at offset %zu (packet %d, seq %d) - failure #%d\n", 
                   total_sent, packet_count, current_seq, consecutive_failures);
            
            // If we hit the problematic area, try to reset the connection
            if (consecutive_failures >= 3) {
                printf("Multiple failures detected, attempting connection reset...\n");
                usleep(10000000); // 10 second reset delay
                consecutive_failures = 0;
                
                // Try to resend this packet one more time after reset
                if (send_packet(game->socket_fd, &data_pkt, &game->client_addr) < 0) {
                    fclose(file);
                    return -1;
                }
            } else {
                fclose(file);
                return -1;
            }
        } else {
            consecutive_failures = 0; // Reset failure counter on success
        }
        
        total_sent += bytes_read;
        packet_count++;
        printf("Sent %zu/%ld bytes (seq: %d, pkt: %d)\r", total_sent, st.st_size, current_seq, packet_count);
        fflush(stdout);
        
        // Enhanced flow control with special handling for critical ranges
        if (packet_count % 1000 == 0) {
            printf("\nBuffer management pause at packet %d...\n", packet_count);
            usleep(3000000); // Increased to 3 second pause every 1000 packets
        } else if (packet_count >= 68000 && packet_count % 10 == 0) {
            usleep(500000); // 500ms delay every 10 packets in critical range
        } else if (packet_count % 100 == 0) {
            usleep(100000); // 100ms delay every 100 packets
        } else {
            usleep(3000); // Increased base delay to 3ms between packets
        }
    }
    
    // Send end of file using proper stop-and-wait
    Packet eof_pkt = {
        .start_marker = START_MARKER,
        .size = 0,
        .seq = (game->seq_num++) & 0x1F,
        .type = PKT_END_FILE
    };
    eof_pkt.checksum = calculate_crc(&eof_pkt);
    
    if (send_packet(game->socket_fd, &eof_pkt, &game->client_addr) < 0) {
        printf("Failed to send end-of-file packet\n");
        fclose(file);
        return -1;
    }
    
    fclose(file);
    printf("\nFile transfer completed: %s\n", filepath);
    return 0;
}

void log_movement(const GameState *game, const char *direction) {
    time_t now;
    time(&now);
    char *time_str = ctime(&now);
    time_str[strlen(time_str) - 1] = '\0'; // Remove newline
    
    printf("[%s] Player moved %s to (%d,%d)\n", 
           time_str, direction, game->player_x, game->player_y);
}