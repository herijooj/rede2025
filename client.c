#include "sockets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <sys/statvfs.h>
#include <sys/stat.h>
#include <errno.h>

#define GRID_SIZE 8
#define RECEIVED_FILES_DIR "./received"

typedef struct {
    int x, y;
    int visited;
    int has_treasure;
    char treasure_name[64];
} GridCell;

typedef struct {
    int player_x, player_y;
    GridCell grid[GRID_SIZE][GRID_SIZE];
    int socket_fd;
    struct sockaddr_ll server_addr;
    uint8_t seq_num;
    int treasures_found;
    PacketType pending_move; // Track the pending move
} ClientState;

// Function prototypes
void init_client(ClientState *client);
void display_grid(const ClientState *client);
int send_movement(ClientState *client, PacketType move_type);
void process_server_packet(ClientState *client, const Packet *pkt);
int receive_file_transfer(ClientState *client, const Packet *initial_pkt);
void handle_treasure_file(const char *filename, PacketType file_type);
char get_user_input(void);
void setup_terminal(void);
void restore_terminal(void);
int check_disk_space(const char *path, size_t required_space);
void create_received_dir(void);

static struct termios old_termios;

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <interface>\n", argv[0]);
        return 1;
    }

    ClientState client = {0};
    
    // Create received files directory
    create_received_dir();
    
    // Create raw socket
    client.socket_fd = create_raw_socket(argv[1]);
    if (client.socket_fd < 0) {
        fprintf(stderr, "Failed to create raw socket\n");
        return 1;
    }

    // Get interface info
    if (get_interface_info(client.socket_fd, argv[1], &client.server_addr) < 0) {
        close(client.socket_fd);
        return 1;
    }

    // Initialize client
    init_client(&client);
    setup_terminal();
    
    printf("=== TREASURE HUNT CLIENT ===\n");
    printf("Interface: %s\n", argv[1]);
    printf("Use WASD keys or arrow keys to move (W/Up=Up, A/Left=Left, S/Down=Down, D/Right=Right), Q to quit\n\n");
    
    display_grid(&client);

    // Main client loop
    char input;
    while ((input = get_user_input()) != 'q' && input != 'Q') {
        PacketType move_type;
        int valid_move = 1;
        
        switch (input) {
            case 'w': case 'W': move_type = PKT_MOVE_UP; break;
            case 'a': case 'A': move_type = PKT_MOVE_LEFT; break;
            case 's': case 'S': move_type = PKT_MOVE_DOWN; break;
            case 'd': case 'D': move_type = PKT_MOVE_RIGHT; break;
            default: 
                valid_move = 0;
                printf("Invalid input. Use WASD or arrow keys to move, Q to quit.\n");
                break;
        }
        
        if (valid_move) {
            if (send_movement(&client, move_type) == 0) {
                // Wait for server response using direct recvfrom with proper unpacking
                PacketRaw raw_response;
                struct sockaddr_ll server_addr;
                socklen_t addr_len = sizeof(server_addr);
                
                ssize_t received = recvfrom(client.socket_fd, &raw_response, sizeof(PacketRaw), 0,
                                          (struct sockaddr *)&server_addr, &addr_len);
                
                if (received == sizeof(PacketRaw)) {
                    Packet response;
                    unpack_packet(&raw_response, &response);
                    
                    if (validate_packet(&response)) {
                        process_server_packet(&client, &response);
                        display_grid(&client);
                    }
                }
            }
        }
    }

    restore_terminal();
    close(client.socket_fd);
    printf("Game ended. Treasures found: %d\n", client.treasures_found);
    return 0;
}

void init_client(ClientState *client) {
    // Initialize player position at bottom-left (0,0)
    client->player_x = 0;
    client->player_y = 0;
    client->seq_num = 0;
    client->treasures_found = 0;
    
    // Initialize grid
    for (int y = 0; y < GRID_SIZE; y++) {
        for (int x = 0; x < GRID_SIZE; x++) {
            client->grid[y][x].x = x;
            client->grid[y][x].y = y;
            client->grid[y][x].visited = 0;
            client->grid[y][x].has_treasure = 0;
            memset(client->grid[y][x].treasure_name, 0, sizeof(client->grid[y][x].treasure_name));
        }
    }
    
    // Mark starting position as visited
    client->grid[0][0].visited = 1;
}

void display_grid(const ClientState *client) {
    printf("\n=== TREASURE HUNT GRID ===\n");
    printf("Player position: (%d, %d) | Treasures found: %d\n", 
           client->player_x, client->player_y, client->treasures_found);
    printf("Legend: P=Player, *=Treasure, o=Visited, .=Unvisited\n\n");
    
    printf("  ");
    for (int x = 0; x < GRID_SIZE; x++) printf("%d ", x);
    printf("\n");
    
    for (int y = GRID_SIZE - 1; y >= 0; y--) {
        printf("%d ", y);
        for (int x = 0; x < GRID_SIZE; x++) {
            char cell = '.';
            
            // Check if player is here
            if (client->player_x == x && client->player_y == y) {
                cell = 'P';
            } else if (client->grid[y][x].has_treasure) {
                cell = '*';
            } else if (client->grid[y][x].visited) {
                cell = 'o';
            }
            
            printf("%c ", cell);
        }
        printf("\n");
    }
    
    if (client->treasures_found > 0) {
        printf("\nTreasures discovered:\n");
        for (int y = 0; y < GRID_SIZE; y++) {
            for (int x = 0; x < GRID_SIZE; x++) {
                if (client->grid[y][x].has_treasure) {
                    printf("  %s at (%d,%d)\n", client->grid[y][x].treasure_name, x, y);
                }
            }
        }
    }
    
    printf("===========================\n");
    printf("Move: W/↑(Up) A/←(Left) S/↓(Down) D/→(Right) or Arrow Keys, Q(Quit): ");
    fflush(stdout);
}

int send_movement(ClientState *client, PacketType move_type) {
    // Store the intended movement for later confirmation
    client->pending_move = move_type;
    
    Packet move_pkt = {
        .start_marker = START_MARKER,
        .size = 0,
        .seq = client->seq_num++,
        .type = move_type
    };
    move_pkt.checksum = calculate_crc(&move_pkt);
    
    // Pack the packet for transmission
    PacketRaw raw_pkt;
    pack_packet(&move_pkt, &raw_pkt);
    
    // Send the packed packet
    ssize_t sent = sendto(client->socket_fd, &raw_pkt, sizeof(PacketRaw), 0,
                         (struct sockaddr *)&client->server_addr, sizeof(client->server_addr));
    
    return (sent == sizeof(PacketRaw)) ? 0 : -1;
}

void process_server_packet(ClientState *client, const Packet *pkt) {
    switch (pkt->type) {
        case PKT_OK_ACK:
            // Regular movement was successful, update client position from server data
            if (pkt->size == 2) {
                client->player_x = pkt->data[0];
                client->player_y = pkt->data[1];
            }
            // Mark new position as visited
            client->grid[client->player_y][client->player_x].visited = 1;
            printf("Move successful! New position: (%d,%d)\n", client->player_x, client->player_y);
            break;
            
        case PKT_ERROR:
            if (pkt->size > 0) {
                if (pkt->data[0] == ERR_NO_PERMISSION) {
                    printf("Invalid move - out of bounds!\n");
                } else if (pkt->data[0] == ERR_NO_SPACE) {
                    printf("Error: Insufficient disk space!\n");
                }
            }
            break;
            
        case PKT_SIZE:
            // File transfer starting - this means move was successful AND treasure found
            // The PKT_SIZE packet now contains the new position.
            if (pkt->size >= sizeof(uint32_t) + 2) {
                client->player_x = pkt->data[sizeof(uint32_t)];
                client->player_y = pkt->data[sizeof(uint32_t) + 1];
            }

            // Mark new position as visited
            client->grid[client->player_y][client->player_x].visited = 1;
            
            printf("Move successful! Treasure discovered at (%d,%d)! Receiving file...\n", 
                   client->player_x, client->player_y);
            receive_file_transfer(client, pkt);
            break;
            
        default:
            printf("Received unknown packet type: %d\n", pkt->type);
            break;
    }
}

int receive_file_transfer(ClientState *client, const Packet *initial_pkt) {
    char filename[64] = {0};
    char filepath[128] = {0};
    PacketType file_type = PKT_TEXT_ACK;
    uint32_t file_size = 0;
    uint32_t bytes_received = 0;
    FILE *file = NULL;
    uint8_t expected_seq = 255; // Track expected sequence number
    
    // The first packet (PKT_SIZE) is passed in, process it first.
    if (initial_pkt->type == PKT_SIZE && initial_pkt->size >= sizeof(uint32_t)) {
        memcpy(&file_size, initial_pkt->data, sizeof(uint32_t));
        file_size = ntohl(file_size);
        printf("File size: %u bytes\n", file_size);
        
        // Check disk space
        if (!check_disk_space(RECEIVED_FILES_DIR, file_size)) {
            printf("Error: Insufficient disk space!\n");
            return -1;
        }
        
        // Send single ACK for the initial PKT_SIZE packet
        Packet ack = {
            .start_marker = START_MARKER,
            .size = 0,
            .seq = initial_pkt->seq,
            .type = PKT_ACK
        };
        ack.checksum = calculate_crc(&ack);
        
        PacketRaw ack_raw;
        pack_packet(&ack, &ack_raw);
        sendto(client->socket_fd, &ack_raw, sizeof(PacketRaw), 0,
              (struct sockaddr *)&client->server_addr, sizeof(client->server_addr));
        
        expected_seq = (initial_pkt->seq + 1) % 32; // Next expected sequence
    } else {
        fprintf(stderr, "Error: receive_file_transfer started with invalid packet.\n");
        return -1;
    }

    // Set socket timeout for file transfer
    set_socket_timeout(client->socket_fd, 2000); // Increased to 2 second timeout

    // Receive subsequent packets until end of file
    Packet pkt;
    int consecutive_timeouts = 0;
    const int max_timeouts = 15; // Increased timeout tolerance
    
    while (consecutive_timeouts < max_timeouts) {
        // Use direct recvfrom for better control during file transfer
        PacketRaw raw_pkt;
        struct sockaddr_ll server_addr;
        socklen_t addr_len = sizeof(server_addr);
        
        ssize_t received = recvfrom(client->socket_fd, &raw_pkt, sizeof(PacketRaw), 0,
                                  (struct sockaddr *)&server_addr, &addr_len);
        
        if (received != sizeof(PacketRaw)) {
            consecutive_timeouts++;
            if (consecutive_timeouts % 5 == 0) {
                printf("Timeout waiting for packet (%d/%d) - expected seq=%d\n", 
                       consecutive_timeouts, max_timeouts, expected_seq);
            }
            continue;
        }
        
        // Unpack and validate packet
        unpack_packet(&raw_pkt, &pkt);
        if (!validate_packet(&pkt)) {
            consecutive_timeouts++;
            continue;
        }
        
        consecutive_timeouts = 0; // Reset timeout counter on valid packet
        
        // Always send ACK for valid packets - but only once
        Packet ack = {
            .start_marker = START_MARKER,
            .size = 0,
            .seq = pkt.seq,
            .type = PKT_ACK
        };
        ack.checksum = calculate_crc(&ack);
        
        PacketRaw ack_raw;
        pack_packet(&ack, &ack_raw);
        sendto(client->socket_fd, &ack_raw, sizeof(PacketRaw), 0,
              (struct sockaddr *)&server_addr, sizeof(server_addr));
        
        // Process packet only if it's the expected sequence (strict order)
        if (pkt.seq != expected_seq) {
            // Out of order packet - just ACK it but don't process
            continue;
        }
        
        switch (pkt.type) {
            case PKT_TEXT_ACK:
            case PKT_VIDEO_ACK:
            case PKT_IMAGE_ACK:
                // Filename packet
                file_type = pkt.type;
                strncpy(filename, (char*)pkt.data, pkt.size);
                filename[pkt.size] = '\0';
                snprintf(filepath, sizeof(filepath), "%s/%s", RECEIVED_FILES_DIR, filename);
                
                if (file) fclose(file);  // Close any previous file
                file = fopen(filepath, "wb");
                if (!file) {
                    printf("Error: Could not create file %s\n", filepath);
                    return -1;
                }
                printf("Receiving: %s\n", filename);
                break;
                
            case PKT_DATA:
                // File data packet - write in strict order
                if (file && pkt.size > 0) {
                    fwrite(pkt.data, 1, pkt.size, file);
                    bytes_received += pkt.size;
                    printf("Received %u/%u bytes (seq: %d)\r", bytes_received, file_size, pkt.seq);
                    fflush(stdout);
                }
                break;
                
            case PKT_END_FILE:
                // End of file transfer
                if (file) {
                    fclose(file);
                    file = NULL;
                }
                printf("\nFile transfer completed: %s\n", filename);
                
                // Mark treasure on grid
                client->grid[client->player_y][client->player_x].has_treasure = 1;
                strncpy(client->grid[client->player_y][client->player_x].treasure_name, 
                       filename, sizeof(client->grid[client->player_y][client->player_x].treasure_name) - 1);
                client->treasures_found++;
                
                // Handle the treasure file
                handle_treasure_file(filepath, file_type);
                return 0;
                
            default:
                // Ignore unexpected packets
                break;
        }
        
        expected_seq = (pkt.seq + 1) % 32; // Update expected sequence
    }
    
    if (file) fclose(file);
    printf("\nFile transfer failed due to timeouts\n");
    return -1;
}

void handle_treasure_file(const char *filename, PacketType file_type) {
    char command[256];
    
    switch (file_type) {
        case PKT_TEXT_ACK:
            printf("Opening text file...\n");
            snprintf(command, sizeof(command), "less \"%s\"", filename);
            system(command);
            break;
            
        case PKT_IMAGE_ACK:
            printf("Opening image file...\n");
            snprintf(command, sizeof(command), "xdg-open \"%s\" 2>/dev/null || feh \"%s\" 2>/dev/null || echo 'Could not open image'", filename, filename);
            system(command);
            break;
            
        case PKT_VIDEO_ACK:
            printf("Opening media file...\n");
            // Check if it's an audio file
            if (strstr(filename, ".mp3") || strstr(filename, ".wav") || strstr(filename, ".ogg")) {
                snprintf(command, sizeof(command), "xdg-open \"%s\" 2>/dev/null || mpg123 \"%s\" 2>/dev/null || aplay \"%s\" 2>/dev/null || echo 'Could not open audio file'", filename, filename, filename);
            } else {
                snprintf(command, sizeof(command), "xdg-open \"%s\" 2>/dev/null || vlc \"%s\" 2>/dev/null || echo 'Could not open video'", filename, filename);
            }
            system(command);
            break;
            
        default:
            printf("Unknown file type, saved as: %s\n", filename);
            break;
    }
}

char get_user_input(void) {
    char c;
    if (read(STDIN_FILENO, &c, 1) == 1) {
        // Handle escape sequences for arrow keys
        if (c == '\033') { // ESC sequence
            char seq[2];
            if (read(STDIN_FILENO, &seq[0], 1) == 1 && 
                read(STDIN_FILENO, &seq[1], 1) == 1) {
                if (seq[0] == '[') {
                    switch (seq[1]) {
                        case 'A': return 'w'; // Up arrow -> W
                        case 'B': return 's'; // Down arrow -> S  
                        case 'C': return 'd'; // Right arrow -> D
                        case 'D': return 'a'; // Left arrow -> A
                    }
                }
            }
            return 0; // Invalid escape sequence
        }
        return c;
    }
    return 0;
}

void setup_terminal(void) {
    struct termios new_termios;
    
    // Save current terminal settings
    tcgetattr(STDIN_FILENO, &old_termios);
    
    // Set new terminal settings for immediate input
    new_termios = old_termios;
    new_termios.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);
}

void restore_terminal(void) {
    // Restore original terminal settings
    tcsetattr(STDIN_FILENO, TCSANOW, &old_termios);
}

int check_disk_space(const char *path, size_t required_space) {
    struct statvfs stats;
    
    if (statvfs(path, &stats) != 0) {
        return 0; // Assume insufficient space on error
    }
    
    size_t available_space = stats.f_bsize * stats.f_bavail;
    return available_space >= required_space;
}

void create_received_dir(void) {
    struct stat st = {0};
    
    if (stat(RECEIVED_FILES_DIR, &st) == -1) {
        if (mkdir(RECEIVED_FILES_DIR, 0755) != 0) {
            printf("Warning: Could not create %s directory\n", RECEIVED_FILES_DIR);
        }
    }
}