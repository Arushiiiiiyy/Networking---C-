#include "sham.h"
//LLM GENERATED CODE
// Global variables
int sockfd;
struct sockaddr_in server_addr, client_addr;
float loss_rate = 0.0;
int chat_mode = 0;
uint32_t server_seq = 5000;  // Initial server sequence number
uint32_t client_seq = 0;
uint32_t expected_seq = 0;
uint16_t recv_window = DEFAULT_WINDOW_SIZE;
enum conn_state state = CLOSED;

// Buffer for out-of-order packets
struct {
    struct sham_packet packet;
    int valid;
} packet_buffer[100];

void handle_handshake() {
    struct sham_packet packet;
    
    // Wait for SYN
    while (1) {
        if (recv_packet(sockfd, &client_addr, &packet, loss_rate, 0) > 0) {
            if (packet.header.flags & SYN_FLAG) {
                client_seq = packet.header.seq_num;
                expected_seq = client_seq + 1;
                write_log("RCV ACK FOR SYN");
                break;
            }
        }
    }
    
    // Send SYN-ACK
    packet.header.seq_num = server_seq;
    packet.header.ack_num = expected_seq;
    packet.header.flags = SYN_FLAG | ACK_FLAG;
    packet.header.window_size = recv_window;
    packet.data_len = 0;
    
    send_packet(sockfd, &client_addr, &packet);
    
    // Wait for ACK
    while (1) {
        if (recv_packet(sockfd, &client_addr, &packet, loss_rate, RTO_MS) > 0) {
            if ((packet.header.flags & ACK_FLAG) && 
                packet.header.ack_num == server_seq + 1) {
                server_seq++;
                state = ESTABLISHED;
                break;
            }
        } else {
            // Retransmit SYN-ACK
            packet.header.seq_num = server_seq;
            packet.header.ack_num = expected_seq;
            packet.header.flags = SYN_FLAG | ACK_FLAG;
            packet.header.window_size = recv_window;
            packet.data_len = 0;
            send_packet(sockfd, &client_addr, &packet);
        }
    }
}

void handle_termination() {
    struct sham_packet packet;
    int fin_received = 0;
    int fin_sent = 0;
    
    while (state != CLOSED) {
        if (recv_packet(sockfd, &client_addr, &packet, loss_rate, 100) > 0) {
            if (packet.header.flags & FIN_FLAG) {
                if (!fin_received) {
                    fin_received = 1;
                    
                    // Send ACK for FIN
                    packet.header.seq_num = server_seq;
                    packet.header.ack_num = expected_seq + 1;
                    packet.header.flags = ACK_FLAG;
                    packet.header.window_size = recv_window;
                    packet.data_len = 0;
                    
                    write_log("SND ACK FOR FIN");
                    send_packet(sockfd, &client_addr, &packet);
                    
                    if (!fin_sent) {
                        // Send our FIN
                        packet.header.seq_num = server_seq;
                        packet.header.ack_num = expected_seq + 1;
                        packet.header.flags = FIN_FLAG;
                        packet.header.window_size = recv_window;
                        packet.data_len = 0;
                        
                        send_packet(sockfd, &client_addr, &packet);
                        fin_sent = 1;
                    }
                }
            } else if (packet.header.flags & ACK_FLAG) {
                if (fin_sent && packet.header.ack_num == server_seq + 1) {
                    state = CLOSED;
                    break;
                }
            }
        } else if (!fin_sent && fin_received) {
            // Send our FIN if we haven't yet
            packet.header.seq_num = server_seq;
            packet.header.ack_num = expected_seq + 1;
            packet.header.flags = FIN_FLAG;
            packet.header.window_size = recv_window;
            packet.data_len = 0;
            
            send_packet(sockfd, &client_addr, &packet);
            fin_sent = 1;
        }
    }
}

void handle_file_transfer() {
    struct sham_packet packet, ack_packet;
    FILE* output_file = NULL;
    char filename[256];
    //sprintf(filename, "output.txt", time(NULL));
    //int got_name = 0;
    //Nu code
    while (1) {
        if (recv_packet(sockfd, &client_addr, &packet, loss_rate, 0) > 0) {
            if ((packet.header.flags & ACK_FLAG) && packet.data_len > 0) {
                /* copy filename safely */
                strncpy(filename, packet.data, sizeof(filename) - 1);
                filename[sizeof(filename) - 1] = '\0';
                /* advance expected_seq so next data chunk is correct */
                expected_seq = packet.header.seq_num + packet.data_len;
                break;
            }
        }
    }
    //Nu code
    output_file = fopen(filename, "wb");
    if (!output_file) {
        perror("Failed to create output file");
        return;
    }
    
    memset(packet_buffer, 0, sizeof(packet_buffer));
    
    while (state == ESTABLISHED) {
        if (recv_packet(sockfd, &client_addr, &packet, loss_rate, 100) > 0) {
            if (packet.header.flags & FIN_FLAG) {
                // Connection termination
                fclose(output_file);
                
                // Calculate and print MD5
                unsigned char md5_result[MD5_DIGEST_LENGTH];
                char md5_string[33];
                FILE* file = fopen(filename, "rb");
                MD5_CTX md5_ctx;
                MD5_Init(&md5_ctx);
                
                char buffer[1024];
                int bytes;
                while ((bytes = fread(buffer, 1, 1024, file)) != 0) {
                    MD5_Update(&md5_ctx, buffer, bytes);
                }
                
                MD5_Final(md5_result, &md5_ctx);
                fclose(file);
                
                for (int i = 0; i < MD5_DIGEST_LENGTH; i++) {
                    sprintf(&md5_string[i*2], "%02x", md5_result[i]);
                }
                
                printf("MD5: %s\n", md5_string);
                
                // Handle termination
                handle_termination();
                break;
            }
            
            if (packet.data_len > 0) {
                if (packet.header.seq_num == expected_seq) {
                    // In-order packet
                    fwrite(packet.data, 1, packet.data_len, output_file);
                    expected_seq += packet.data_len;
                    
                    // Check buffered packets
                    int progress = 1;
                    while (progress) {
                        progress = 0;
                        for (int i = 0; i < 100; i++) {
                            if (packet_buffer[i].valid && 
                                packet_buffer[i].packet.header.seq_num == expected_seq) {
                                fwrite(packet_buffer[i].packet.data, 1, 
                                      packet_buffer[i].packet.data_len, output_file);
                                expected_seq += packet_buffer[i].packet.data_len;
                                packet_buffer[i].valid = 0;
                                progress = 1;
                            }
                        }
                    }
                } else if (packet.header.seq_num > expected_seq) {
                    // Out-of-order packet - buffer it
                    for (int i = 0; i < 100; i++) {
                        if (!packet_buffer[i].valid) {
                            packet_buffer[i].packet = packet;
                            packet_buffer[i].valid = 1;
                            break;
                        }
                    }
                }
                
                // Send cumulative ACK
                ack_packet.header.seq_num = server_seq;
                ack_packet.header.ack_num = expected_seq;
                ack_packet.header.flags = ACK_FLAG;
                ack_packet.header.window_size = recv_window;
                ack_packet.data_len = 0;
                
                send_packet(sockfd, &client_addr, &ack_packet);
            }
        }
    }
}

void handle_chat_mode() {
    struct sham_packet packet, data_packet;
    fd_set read_fds;
    int max_fd = sockfd + 1;
    char input_buffer[MAX_PAYLOAD_SIZE];
    
    printf("Chat mode started. Type /quit to exit.\n");
    
    // Set socket to non-blocking
    int flags = fcntl(sockfd, F_GETFL, 0);
    fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
    
    while (state == ESTABLISHED) {
        FD_ZERO(&read_fds);
        FD_SET(STDIN_FILENO, &read_fds);
        FD_SET(sockfd, &read_fds);
        
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 100000;  // 100ms
        
        int activity = select(max_fd, &read_fds, NULL, NULL, &tv);
        
        if (activity > 0) {
            // Check for keyboard input
            if (FD_ISSET(STDIN_FILENO, &read_fds)) {
                if (fgets(input_buffer, MAX_PAYLOAD_SIZE, stdin) != NULL) {
                    input_buffer[strcspn(input_buffer, "\n")] = 0;
                    
                    if (strcmp(input_buffer, "/quit") == 0) {
                        printf("Closing connection...\n");
                        handle_termination();
                        break;
                    }
                    
                    // Send message
                    data_packet.header.seq_num = server_seq;
                    data_packet.header.ack_num = expected_seq;
                    data_packet.header.flags = ACK_FLAG;
                    data_packet.header.window_size = recv_window;
                    strcpy(data_packet.data, input_buffer);
                    data_packet.data_len = strlen(input_buffer);
                    
                    send_packet(sockfd, &client_addr, &data_packet);
                    server_seq += data_packet.data_len;
                    
                    printf("You: %s\n", input_buffer);
                }
            }
            
            // Check for network input
            if (FD_ISSET(sockfd, &read_fds)) {
                if (recv_packet(sockfd, &client_addr, &packet, loss_rate, 0) > 0) {
                    if (packet.header.flags & FIN_FLAG) {
                        printf("Client disconnected.\n");
                        handle_termination();
                        break;
                    }
                    
                    if (packet.data_len > 0 && packet.header.seq_num == expected_seq) {
                        packet.data[packet.data_len] = '\0';
                        printf("Client: %s\n", packet.data);
                        expected_seq += packet.data_len;
                        
                        // Send ACK
                        struct sham_packet ack_packet;
                        ack_packet.header.seq_num = server_seq;
                        ack_packet.header.ack_num = expected_seq;
                        ack_packet.header.flags = ACK_FLAG;
                        ack_packet.header.window_size = recv_window;
                        ack_packet.data_len = 0;
                        
                        send_packet(sockfd, &client_addr, &ack_packet);
                    }
                }
            }
        }
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <port> [--chat] [loss_rate]\n", argv[0]);
        exit(1);
    }
    
    int port = atoi(argv[1]);
    
    // Parse optional arguments
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--chat") == 0) {
            chat_mode = 1;
        } else {
            loss_rate = atof(argv[i]);
        }
    }
    
    // Initialize logging
    init_logging("server_log.txt");
    
    // Seed random number generator
    srand(time(NULL));
    
    // Create socket
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("Socket creation failed");
        exit(1);
    }
    
    // Configure server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);
    
    // Bind socket
    if (bind(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(sockfd);
        exit(1);
    }
    
    printf("Server listening on port %d (chat_mode=%d, loss_rate=%.2f)\n", 
           port, chat_mode, loss_rate);
    
    // Handle connection
    handle_handshake();
    printf("Connection established\n");
    
    if (chat_mode) {
        handle_chat_mode();
    } else {
        handle_file_transfer();
    }
    
    // Cleanup
    close(sockfd);
    close_logging();
    
    return 0;
}
//LLM GENERATED CODE ENDED 