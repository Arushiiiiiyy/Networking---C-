#ifndef SHAM_H
#define SHAM_H
//LLM GENERATED CODE 
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <time.h>
#include <openssl/md5.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/select.h>
#include <stdarg.h>

// Flags for connection management
#define SYN_FLAG 0x1
#define ACK_FLAG 0x2
#define FIN_FLAG 0x4

// Protocol parameters
#define MAX_PAYLOAD_SIZE 1024
#define DEFAULT_WINDOW_SIZE 10240  // 10 packets worth
#define SLIDING_WINDOW 10
#define RTO_MS 500  // Retransmission timeout in milliseconds
#define MAX_RETRIES 5

// S.H.A.M. Header Structure
struct sham_header {
    uint32_t seq_num;      // Sequence Number
    uint32_t ack_num;      // Acknowledgment Number
    uint16_t flags;        // Control flags (SYN, ACK, FIN)
    uint16_t window_size;  // Flow control window size
};

// Packet structure
struct sham_packet {
    struct sham_header header;
    char data[MAX_PAYLOAD_SIZE];
    int data_len;
};

// Packet tracking for sliding window
struct packet_tracker {
    struct sham_packet packet;
    struct timeval send_time;
    int retries;
    int acked;
};

// Connection state
enum conn_state {
    CLOSED,
    SYN_SENT,
    SYN_RECEIVED,
    ESTABLISHED,
    FIN_WAIT_1,
    FIN_WAIT_2,
    CLOSE_WAIT,
    CLOSING,
    LAST_ACK,
    TIME_WAIT
};

// Logging functions
FILE* log_file;
int logging_enabled;

void init_logging(const char* filename) {
    const char* env_log = getenv("RUDP_LOG");
    logging_enabled = (env_log != NULL && strcmp(env_log, "1") == 0);
    
    if (logging_enabled) {
        log_file = fopen(filename, "w");
        if (!log_file) {
            perror("Failed to open log file");
            logging_enabled = 0;
        }
    }
}

void close_logging() {
    if (logging_enabled && log_file) {
        fclose(log_file);
    }
}

void write_log(const char* format, ...) {
    if (!logging_enabled || !log_file) return;
    
    char time_buffer[30];
    struct timeval tv;
    time_t curtime;
    
    gettimeofday(&tv, NULL);
    curtime = tv.tv_sec;
    
    strftime(time_buffer, 30, "%Y-%m-%d %H:%M:%S", localtime(&curtime));
    fprintf(log_file, "[%s.%06d] [LOG] ", time_buffer, tv.tv_usec);
    
    va_list args;
    va_start(args, format);
    vfprintf(log_file, format, args);
    va_end(args);
    
    fprintf(log_file, "\n");
    fflush(log_file);
}

// Utility functions
int should_drop_packet(float loss_rate) {
    if (loss_rate <= 0.0) return 0;
    return ((float)rand() / RAND_MAX) < loss_rate;
}

void send_packet(int sockfd, struct sockaddr_in* addr, struct sham_packet* packet) {
    int total_size = sizeof(struct sham_header) + packet->data_len;
    char buffer[sizeof(struct sham_header) + MAX_PAYLOAD_SIZE];
    
    memcpy(buffer, &packet->header, sizeof(struct sham_header));
    if (packet->data_len > 0) {
        memcpy(buffer + sizeof(struct sham_header), packet->data, packet->data_len);
    }
    
    sendto(sockfd, buffer, total_size, 0, (struct sockaddr*)addr, sizeof(*addr));
    
    // Log the sent packet
    if (packet->header.flags & SYN_FLAG) {
        if (packet->header.flags & ACK_FLAG) {
            write_log("SND SYN-ACK SEQ=%u ACK=%u", 
                     packet->header.seq_num, packet->header.ack_num);
        } else {
            write_log("SND SYN SEQ=%u", packet->header.seq_num);
        }
    } else if (packet->header.flags & FIN_FLAG) {
        write_log("SND FIN SEQ=%u", packet->header.seq_num);
    } else if (packet->header.flags & ACK_FLAG) {
        if (packet->data_len == 0) {
            write_log("SND ACK=%u WIN=%u", 
                     packet->header.ack_num, packet->header.window_size);
        }
    }
    
    if (packet->data_len > 0) {
        write_log("SND DATA SEQ=%u LEN=%d", 
                 packet->header.seq_num, packet->data_len);
    }
}

int recv_packet(int sockfd, struct sockaddr_in* addr, struct sham_packet* packet, 
                float loss_rate, int timeout_ms) {
    socklen_t addr_len = sizeof(*addr);
    char buffer[sizeof(struct sham_header) + MAX_PAYLOAD_SIZE];
    
    // Set timeout if specified
    if (timeout_ms > 0) {
        struct timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }
    
    int n = recvfrom(sockfd, buffer, sizeof(buffer), 0, 
                     (struct sockaddr*)addr, &addr_len);
    
    if (n < sizeof(struct sham_header)) {
        return -1;  // Timeout or error
    }
    
    memcpy(&packet->header, buffer, sizeof(struct sham_header));
    packet->data_len = n - sizeof(struct sham_header);
    
    if (packet->data_len > 0) {
        memcpy(packet->data, buffer + sizeof(struct sham_header), packet->data_len);
        
        // Simulate packet loss for data packets
        if (should_drop_packet(loss_rate)) {
            write_log("DROP DATA SEQ=%u", packet->header.seq_num);
            return -1;  // Pretend we didn't receive it
        }
    }
    
    // Log the received packet
    if (packet->header.flags & SYN_FLAG) {
        if (packet->header.flags & ACK_FLAG) {
            write_log("RCV SYN-ACK SEQ=%u ACK=%u", 
                     packet->header.seq_num, packet->header.ack_num);
        } else {
            write_log("RCV SYN SEQ=%u", packet->header.seq_num);
        }
    } else if (packet->header.flags & FIN_FLAG) {
        write_log("RCV FIN SEQ=%u", packet->header.seq_num);
    } else if (packet->header.flags & ACK_FLAG) {
        if (packet->data_len == 0) {
            write_log("RCV ACK=%u", packet->header.ack_num);
        }
    }
    
    if (packet->data_len > 0) {
        write_log("RCV DATA SEQ=%u LEN=%d", 
                 packet->header.seq_num, packet->data_len);
    }
    
    return n;
}

long get_current_time_ms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

int has_timed_out(struct timeval* send_time, int timeout_ms) {
    struct timeval now;
    gettimeofday(&now, NULL);
    
    long elapsed = (now.tv_sec - send_time->tv_sec) * 1000 +
                   (now.tv_usec - send_time->tv_usec) / 1000;
    
    return elapsed >= timeout_ms;
}

#endif // SHAM_H
//LLM GENERATED CODE ENDED 