#include "sham.h"
//LLM GENERATED CODE
// Global variables
int sockfd;
struct sockaddr_in server_addr;
float loss_rate = 0.0;
int chat_mode = 0;
uint32_t client_seq = 100;  // Initial client sequence number
uint32_t server_seq = 0;
uint32_t expected_ack = 0;
uint16_t send_window = DEFAULT_WINDOW_SIZE;
uint16_t server_window = DEFAULT_WINDOW_SIZE;
enum conn_state state = CLOSED;

// Sliding window for file transfer
struct packet_tracker window[SLIDING_WINDOW];
int window_base = 0;
int next_seq_num = 0;
int bytes_in_flight = 0;

void handle_handshake() {
    struct sham_packet packet;
    int retries = 0;
    
    // Send SYN
    packet.header.seq_num = client_seq;
    packet.header.ack_num = 0;
    packet.header.flags = SYN_FLAG;
    packet.header.window_size = send_window;
    packet.data_len = 0;
    
    send_packet(sockfd, &server_addr, &packet);
    state = SYN_SENT;
    
    // Wait for SYN-ACK
    while (retries < MAX_RETRIES) {
        if (recv_packet(sockfd, &server_addr, &packet, loss_rate, RTO_MS) > 0) {
            if ((packet.header.flags & (SYN_FLAG | ACK_FLAG)) == (SYN_FLAG | ACK_FLAG) &&
                packet.header.ack_num == client_seq + 1) {
                server_seq = packet.header.seq_num;
                server_window = packet.header.window_size;
                expected_ack = client_seq + 1;
                client_seq++;
                break;
            }
        } else {
            // Retransmit SYN
            retries++;
            packet.header.seq_num = client_seq;
            packet.header.ack_num = 0;
            packet.header.flags = SYN_FLAG;
            packet.header.window_size = send_window;
            packet.data_len = 0;
            send_packet(sockfd, &server_addr, &packet);
        }
    }
    
    if (retries >= MAX_RETRIES) {
        fprintf(stderr, "Connection failed: no response from server\n");
        exit(1);
    }
    
    // Send ACK
    packet.header.seq_num = client_seq;
    packet.header.ack_num = server_seq + 1;
    packet.header.flags = ACK_FLAG;
    packet.header.window_size = send_window;
    packet.data_len = 0;
    
    send_packet(sockfd, &server_addr, &packet);
    write_log("RCV ACK FOR SYN");
    state = ESTABLISHED;
}

void handle_termination() {
    struct sham_packet packet;
    int fin_acked = 0;
    int retries = 0;
    
    // Send FIN
    packet.header.seq_num = client_seq;
    packet.header.ack_num = server_seq + 1;
    packet.header.flags = FIN_FLAG;
    packet.header.window_size = send_window;
    packet.data_len = 0;
    
    send_packet(sockfd, &server_addr, &packet);
    state = FIN_WAIT_1;
    
    while (!fin_acked && retries < MAX_RETRIES) {
        if (recv_packet(sockfd, &server_addr, &packet, loss_rate, RTO_MS) > 0) {
            if (packet.header.flags & ACK_FLAG) {
                write_log("RCV ACK FOR FIN");
                fin_acked = 1;
                state = FIN_WAIT_2;
            }
        } else {
            retries++;
            packet.header.seq_num = client_seq;
            packet.header.ack_num = server_seq + 1;
            packet.header.flags = FIN_FLAG;
            packet.header.window_size = send_window;
            packet.data_len = 0;
            send_packet(sockfd, &server_addr, &packet);
        }
    }
    
    // Wait for server's FIN
    while (state != CLOSED && retries < MAX_RETRIES) {
        if (recv_packet(sockfd, &server_addr, &packet, loss_rate, RTO_MS) > 0) {
            if (packet.header.flags & FIN_FLAG) {
                // Send final ACK
                packet.header.seq_num = client_seq + 1;
                packet.header.ack_num = server_seq + 2;
                packet.header.flags = ACK_FLAG;
                packet.header.window_size = send_window;
                packet.data_len = 0;
                
                write_log("SND ACK FOR FIN");
                send_packet(sockfd, &server_addr, &packet);
                state = TIME_WAIT;
                
                // Wait a bit for duplicate FINs
                usleep(500000);
                state = CLOSED;
                break;
            }
        } else {
            retries++;
        }
    }
}

/* --- NEW: send desired output filename to the server --- */
void send_output_filename(const char *output_file) {
    struct sham_packet name_pkt;
    memset(&name_pkt, 0, sizeof(name_pkt));

    name_pkt.header.seq_num   = client_seq;
    name_pkt.header.ack_num   = server_seq + 1;
    name_pkt.header.flags     = ACK_FLAG;
    name_pkt.header.window_size = send_window;

    strncpy(name_pkt.data, output_file, MAX_PAYLOAD_SIZE - 1);
    name_pkt.data_len = strlen(output_file) + 1;   // include terminating '\0'

    send_packet(sockfd, &server_addr, &name_pkt);
    client_seq += name_pkt.data_len;               // advance sequence number
}


void handle_file_transfer(const char* input_file, const char* output_file) {
    FILE* file = fopen(input_file, "rb");
    if (!file) {
        perror("Failed to open input file");
        return;
    }
    
    // Initialize sliding window
    memset(window, 0, sizeof(window));
    
    // Get file size
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    int packets_sent = 0;
    int packets_acked = 0;
    int total_packets = (file_size + MAX_PAYLOAD_SIZE - 1) / MAX_PAYLOAD_SIZE;
    
    while (packets_acked < total_packets && state == ESTABLISHED) {
        // Send packets within window and flow control limits
        while (packets_sent - packets_acked < SLIDING_WINDOW && 
               packets_sent < total_packets &&
               bytes_in_flight < server_window) {
            
            int idx = packets_sent % SLIDING_WINDOW;
            
            // Read data from file
            window[idx].packet.data_len = fread(window[idx].packet.data, 1, 
                                                MAX_PAYLOAD_SIZE, file);
            
            if (window[idx].packet.data_len > 0) {
                // Set packet headers
                window[idx].packet.header.seq_num = client_seq;
                window[idx].packet.header.ack_num = server_seq + 1;
                window[idx].packet.header.flags = ACK_FLAG;
                window[idx].packet.header.window_size = send_window;
                
                // Send packet
                send_packet(sockfd, &server_addr, &window[idx].packet);
                gettimeofday(&window[idx].send_time, NULL);
                window[idx].retries = 0;
                window[idx].acked = 0;
                
                client_seq += window[idx].packet.data_len;
                bytes_in_flight += window[idx].packet.data_len;
                packets_sent++;
            }
        }
        
        // Check for ACKs and handle retransmissions
        struct sham_packet ack_packet;
        
        // Set socket to non-blocking for ACK reception
        int flags = fcntl(sockfd, F_GETFL, 0);
        fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
        
        // Try to receive ACK
        if (recv_packet(sockfd, &server_addr, &ack_packet, loss_rate, 100) > 0) {
            if (ack_packet.header.flags & ACK_FLAG) {
                uint32_t ack_num = ack_packet.header.ack_num;
                
                // Update flow control window
                if (server_window != ack_packet.header.window_size) {
                    server_window = ack_packet.header.window_size;
                    write_log("FLOW WIN UPDATE=%u", server_window);
                }
                
                // Mark acknowledged packets
                for (int i = 0; i < SLIDING_WINDOW; i++) {
                    int idx = (packets_acked + i) % SLIDING_WINDOW;
                    if (window[idx].packet.data_len > 0 && !window[idx].acked) {
                        uint32_t packet_end = window[idx].packet.header.seq_num + 
                                            window[idx].packet.data_len;
                        if (packet_end <= ack_num) {
                            window[idx].acked = 1;
                            bytes_in_flight -= window[idx].packet.data_len;
                            packets_acked++;
                        } else {
                            break;  // This packet and all after it are not acked
                        }
                    }
                }
            }
        }
        
        // Reset socket to blocking
        fcntl(sockfd, F_SETFL, flags);
        
        // Check for timeouts and retransmit
        for (int i = 0; i < SLIDING_WINDOW && i < packets_sent - packets_acked; i++) {
            int idx = (packets_acked + i) % SLIDING_WINDOW;
            if (window[idx].packet.data_len > 0 && !window[idx].acked) {
                if (has_timed_out(&window[idx].send_time, RTO_MS)) {
                    if (window[idx].retries < MAX_RETRIES) {
                        write_log("TIMEOUT SEQ=%u", window[idx].packet.header.seq_num);
                        write_log("RETX DATA SEQ=%u LEN=%d", 
                                 window[idx].packet.header.seq_num,
                                 window[idx].packet.data_len);
                        
                        send_packet(sockfd, &server_addr, &window[idx].packet);
                        gettimeofday(&window[idx].send_time, NULL);
                        window[idx].retries++;
                    } else {
                        fprintf(stderr, "Max retries exceeded for packet %d\n", i);
                        fclose(file);
                        return;
                    }
                }
            }
        }
        
        // Small delay to prevent busy waiting
        usleep(10000);  // 10ms
    }
    
    fclose(file);
    
    // Calculate and print MD5 of sent file
    unsigned char md5_result[MD5_DIGEST_LENGTH];
    char md5_string[33];
    FILE* check_file = fopen(input_file, "rb");
    MD5_CTX md5_ctx;
    MD5_Init(&md5_ctx);
    
    char buffer[1024];
    int bytes;
    while ((bytes = fread(buffer, 1, 1024, check_file)) != 0) {
        MD5_Update(&md5_ctx, buffer, bytes);
    }
    
    MD5_Final(md5_result, &md5_ctx);
    fclose(check_file);
    
    for (int i = 0; i < MD5_DIGEST_LENGTH; i++) {
        sprintf(&md5_string[i*2], "%02x", md5_result[i]);
    }
    
    printf("File sent successfully. MD5: %s\n", md5_string);
}

void handle_chat_mode() {
    struct sham_packet packet, data_packet;
    fd_set read_fds;
    int max_fd = sockfd + 1;
    char input_buffer[MAX_PAYLOAD_SIZE];
    uint32_t server_ack = server_seq + 1;
    
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
                    data_packet.header.seq_num = client_seq;
                    data_packet.header.ack_num = server_ack;
                    data_packet.header.flags = ACK_FLAG;
                    data_packet.header.window_size = send_window;
                    strcpy(data_packet.data, input_buffer);
                    data_packet.data_len = strlen(input_buffer);
                    
                    send_packet(sockfd, &server_addr, &data_packet);
                    client_seq += data_packet.data_len;
                    
                    printf("You: %s\n", input_buffer);
                }
            }
            
            // Check for network input
            if (FD_ISSET(sockfd, &read_fds)) {
                if (recv_packet(sockfd, &server_addr, &packet, loss_rate, 0) > 0) {
                    if (packet.header.flags & FIN_FLAG) {
                        printf("Server disconnected.\n");
                        handle_termination();
                        break;
                    }
                    
                    if (packet.data_len > 0 && packet.header.seq_num == server_ack) {
                        packet.data[packet.data_len] = '\0';
                        printf("Server: %s\n", packet.data);
                        server_ack += packet.data_len;
                        
                        // Send ACK
                        struct sham_packet ack_packet;
                        ack_packet.header.seq_num = client_seq;
                        ack_packet.header.ack_num = server_ack;
                        ack_packet.header.flags = ACK_FLAG;
                        ack_packet.header.window_size = send_window;
                        ack_packet.data_len = 0;
                        
                        send_packet(sockfd, &server_addr, &ack_packet);
                    }
                }
            }
        }
    }
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage:\n");
        fprintf(stderr, "  File Transfer: %s <server_ip> <server_port> <input_file> <output_file> [loss_rate]\n", argv[0]);
        fprintf(stderr, "  Chat Mode: %s <server_ip> <server_port> --chat [loss_rate]\n", argv[0]);
        exit(1);
    }
    
    char* server_ip = argv[1];
    int server_port = atoi(argv[2]);
    char* input_file = NULL;
    char* output_file = NULL;
    
    // Parse arguments
    if (argc >= 4 && strcmp(argv[3], "--chat") == 0) {
        chat_mode = 1;
        if (argc >= 5) {
            loss_rate = atof(argv[4]);
        }
    } else if (argc >= 5) {
        input_file = argv[3];
        output_file = argv[4];
        if (argc >= 6) {
            loss_rate = atof(argv[5]);
        }
    } else {
        fprintf(stderr, "Invalid arguments\n");
        exit(1);
    }
    
    // Initialize logging
    init_logging("client_log.txt");
    
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
    server_addr.sin_port = htons(server_port);
    
    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        perror("Invalid server IP address");
        close(sockfd);
        exit(1);
    }
    
    printf("Connecting to %s:%d (chat_mode=%d, loss_rate=%.2f)\n", 
           server_ip, server_port, chat_mode, loss_rate);
    
    // Handle connection
    handle_handshake();
    printf("Connection established\n");
    
    if (chat_mode) {
        handle_chat_mode();
    } else {
        send_output_filename(output_file);
        handle_file_transfer(input_file, output_file);
        handle_termination();
    }
    
    // Cleanup
    close(sockfd);
    close_logging();
    
    return 0;
}
//LLM GENERATED CODE ENDED 