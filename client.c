#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/time.h>

#define SERVER_PORT 5201
#define BUFFER_SIZE 1470

// 记录传输状态
typedef struct {
    int sockfd;
    long total_bytes_sent;
    long total_bytes_received;
    struct timeval start_time;
    struct sockaddr_in server_addr;  // 仅用于UDP
    int is_udp;  // 标志当前是否使用UDP
    double bandwidth_limit_mbps;
} transfer_info_t;

void* listen_for_bandwidth_limit(void* arg) {
    transfer_info_t* info = (transfer_info_t*)arg;
    char buffer[BUFFER_SIZE];
    while (1) {
        // 服务端消息格式 "BANDWIDTH_LIMIT:xxx"
        ssize_t n = recv(info->sockfd, buffer, BUFFER_SIZE, 0);
        if (n > 0) {
            buffer[n] = '\0';  // 添加字符串终止符
            if (strncmp(buffer, "BANDWIDTH_LIMIT:", 16) == 0) {
                // 提取限速值
                double new_limit = atof(buffer + 16);
                if (new_limit > 0) {
                    info->bandwidth_limit_mbps = new_limit;
                    printf("Received new bandwidth limit: %.2f Mbps\n", info->bandwidth_limit_mbps);
                }
            }
        }
    }
    return NULL;
}

// 发送数据（上传）
void* send_data(void* arg) {
    transfer_info_t* info = (transfer_info_t*)arg;
    char buffer[BUFFER_SIZE];
    memset(buffer, 'A', sizeof(buffer));  // 填充缓冲区数据

    long bytes_sent_in_second = 0;
    struct timeval start_time, end_time;

    gettimeofday(&start_time, NULL);

    if (info->is_udp) {
        // UDP发送
        while (1) {
            ssize_t n = sendto(info->sockfd, buffer, BUFFER_SIZE, 0, (struct sockaddr*)&(info->server_addr), sizeof(info->server_addr));
            if (n <= 0) {
                break;
            }
            info->total_bytes_sent += n;  // 累积上传的字节数
            bytes_sent_in_second += n;

            gettimeofday(&end_time, NULL);
            double elapsed_time = (end_time.tv_sec - start_time.tv_sec) + ((end_time.tv_usec - start_time.tv_usec) / 1000000.0);
            
            double max_bytes_per_sec = (info->bandwidth_limit_mbps * 1e6) / 8;

            // 如果本秒内发送的字节数超过限速 则休眠到1秒结束
            if (elapsed_time < 1.0 && bytes_sent_in_second >= max_bytes_per_sec) {
                usleep((1.0 - elapsed_time) * 1000000);
                gettimeofday(&start_time, NULL);
                bytes_sent_in_second = 0;
            } else if (elapsed_time >= 0) {
                gettimeofday(&start_time, NULL);
                bytes_sent_in_second = 0;
            }
        }
    } else {
        // TCP发送
        while (1) {
            ssize_t n = send(info->sockfd, buffer, BUFFER_SIZE, 0);  // 发送数据
            if (n <= 0) {
                break;
            }

            info->total_bytes_sent += n;  // 累积上传的字节数
            bytes_sent_in_second += n;

            gettimeofday(&end_time, NULL);
            double elapsed_time = (end_time.tv_sec - start_time.tv_sec) + 
                                  ((end_time.tv_usec - start_time.tv_usec) / 1000000.0);

            // 限制上传带宽
            double max_bytes_per_sec = (info->bandwidth_limit_mbps * 1e6) / 8;

            if (elapsed_time < 1.0 && bytes_sent_in_second >= max_bytes_per_sec) {
                usleep((1.0 - elapsed_time) * 1000000);
                gettimeofday(&start_time, NULL);
                bytes_sent_in_second = 0;
            } else if (elapsed_time >= 1.0) {
                gettimeofday(&start_time, NULL);
                bytes_sent_in_second = 0;
            }
        }
    }

    return NULL;
}

// 接收数据（下载）
void* receive_data(void* arg) {
    transfer_info_t* info = (transfer_info_t*)arg;
    char buffer[BUFFER_SIZE];
    socklen_t addr_len = sizeof(info->server_addr);

    long bytes_received_in_second = 0;
    struct timeval start_time, end_time;

    gettimeofday(&start_time, NULL);

    if (info->is_udp) {
        // UDP接收
        while (1) {
            ssize_t n = recvfrom(info->sockfd, buffer, BUFFER_SIZE, 0, (struct sockaddr*)&(info->server_addr), &addr_len);
            if (n <= 0) {
                perror("recvfrom failed");
                break;
            }

            info->total_bytes_received += n;  // 累积下载的字节数
            bytes_received_in_second += n;

            gettimeofday(&end_time, NULL);
            double elapsed_time = (end_time.tv_sec - start_time.tv_sec) + 
                                  ((end_time.tv_usec - start_time.tv_usec) / 1000000.0);

            double max_bytes_per_sec = (info->bandwidth_limit_mbps * 1e6) / 8;
            if (elapsed_time < 1.0 && bytes_received_in_second >= max_bytes_per_sec) {
                usleep((1.0 - elapsed_time) * 1000000);
                gettimeofday(&start_time, NULL);
                bytes_received_in_second = 0;
            } else if (elapsed_time >= 1.0) {
                gettimeofday(&start_time, NULL);
                bytes_received_in_second = 0;
            }
        }
    } else {
        // TCP接收
        while (1) {
            ssize_t n = recv(info->sockfd, buffer, BUFFER_SIZE, 0); // 接收数据
            if (n <= 0) {
                break;
            }

            info->total_bytes_received += n;  // 累积下载的字节数
            bytes_received_in_second += n;

            gettimeofday(&end_time, NULL);
            double elapsed_time = (end_time.tv_sec - start_time.tv_sec) + 
                                  ((end_time.tv_usec - start_time.tv_usec) / 1000000.0);

            double max_bytes_per_sec = (info->bandwidth_limit_mbps * 1e6) / 8;
            if (elapsed_time < 1.0 && bytes_received_in_second >= max_bytes_per_sec) {
                usleep((1.0 - elapsed_time) * 1000000); // 限速逻辑
                gettimeofday(&start_time, NULL);
                bytes_received_in_second = 0;
            } else if (elapsed_time >= 1.0) {
                gettimeofday(&start_time, NULL);
                bytes_received_in_second = 0;
            }
        }
    }

    return NULL;
}

void get_server_ip(char* server_ip) {
    int client_fd;
    struct sockaddr_in broadcast_addr;
    socklen_t addr_len = sizeof(broadcast_addr);
    char buffer[BUFFER_SIZE];

    if ((client_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("udp socket create failed");
        exit(EXIT_FAILURE);
    }

    int broadcast = 1;
    if (setsockopt(client_fd, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast)) < 0) {
        perror("setsockopt failed");
        close(client_fd);
        exit(EXIT_FAILURE);
    }

    memset(&broadcast_addr, 0, sizeof(broadcast_addr));
    broadcast_addr.sin_family = AF_INET;
    broadcast_addr.sin_port = htons(5202); // 服务端监听广播的端口
    broadcast_addr.sin_addr.s_addr = inet_addr("192.168.18.255");

    char message[] = "DISCOVER_SERVER";
    sendto(client_fd, message, strlen(message), 0, (struct sockaddr*)&broadcast_addr, addr_len);

    ssize_t len = recvfrom(client_fd, buffer, BUFFER_SIZE, 0, (struct sockaddr*)&broadcast_addr, &addr_len);
    if (len > 0) {
        buffer[len] = '\0';
        strcpy(server_ip, buffer);
        printf("server ip: %s\n", server_ip);
    }

    close(client_fd);
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <tcp/udp> <up/down/double>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int mode = 0; // 0: UP, 1: DOWN, 2: DOUBLE
    int is_udp = 0;  // 默认TCP

    // 解析协议类型
    if (strcmp(argv[1], "tcp") == 0) {
        is_udp = 0;  // TCP
    } else if (strcmp(argv[1], "udp") == 0) {
        is_udp = 1;  // UDP
    } else {
        fprintf(stderr, "Invalid protocol: %s. Use 'tcp' or 'udp'.\n", argv[1]);
        exit(EXIT_FAILURE);
    }

    // 解析模式类型
    if (strcmp(argv[2], "up") == 0) {
        mode = 0;  // 上传模式
    } else if (strcmp(argv[2], "down") == 0) {
        mode = 1;  // 下载模式
    } else if (strcmp(argv[2], "double") == 0) {
        mode = 2;  // 双向模式
    } else {
        fprintf(stderr, "Invalid mode: %s. Use 'up', 'down', or 'double'.\n", argv[2]);
        exit(EXIT_FAILURE);
    }

    char server_ip[INET_ADDRSTRLEN];
    get_server_ip(server_ip);

    int sockfd;
    struct sockaddr_in server_addr;
    struct timeval end_time;
    pthread_t send_thread, receive_thread, bandwidth_thread;

    // 创建套接字
    if (is_udp) {
        // UDP 套接字
        if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
            perror("UDP socket creation failed");
            exit(EXIT_FAILURE);
        }
    } else {
        // TCP 套接字
        if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
            perror("TCP socket creation failed");
            exit(EXIT_FAILURE);
        }
    }

    // 配置服务器地址
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = inet_addr(server_ip);

    if (!is_udp) {
        // TCP连接
        if (connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            perror("connect failed");
            close(sockfd);
            exit(EXIT_FAILURE);
        }
    }

    // 初始化传输信息
    transfer_info_t transfer_info = {
        .sockfd = sockfd,
        .total_bytes_sent = 0,
        .total_bytes_received = 0,
        .server_addr = server_addr,
        .is_udp = is_udp,
        .bandwidth_limit_mbps = 1e6,
    };
    gettimeofday(&transfer_info.start_time, NULL);

    // 启动监听带宽限制线程
    if (pthread_create(&bandwidth_thread, NULL, listen_for_bandwidth_limit, &transfer_info) != 0) {
        perror("failed to create bandwidth thread");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    // UDP 模式下down double 客户端先发送启动信号
    if (is_udp) {
        char init_msg[] = "INIT";
        sendto(sockfd, init_msg, sizeof(init_msg), 0, (struct sockaddr*)&server_addr, sizeof(server_addr));

        // 接收服务器的确认
        char ack_buffer[BUFFER_SIZE];
        socklen_t addr_len = sizeof(server_addr);
        ssize_t ack_len = recvfrom(sockfd, ack_buffer, BUFFER_SIZE, 0, (struct sockaddr*)&server_addr, &addr_len);
        if (ack_len <= 0 || strncmp(ack_buffer, "ACK", 3) != 0) {
            perror("failed to reveive ACK from server");
            close(sockfd);
            exit(EXIT_FAILURE);
        }
    }
    
    // 根据模式启动相应的线程
    if (mode == 0 || mode == 2) {  // 上传模式或双向模式，启动发送线程
        if (pthread_create(&send_thread, NULL, send_data, &transfer_info) != 0) {
            perror("failed to create send thread");
            close(sockfd);
            exit(EXIT_FAILURE);
        }
    }

    if (mode == 1 || mode == 2) {  // 下载模式或双向模式，启动接收线程
        if (pthread_create(&receive_thread, NULL, receive_data, &transfer_info) != 0) {
            perror("failed to create send thread");
            close(sockfd);
            exit(EXIT_FAILURE);
        }
    }

    // 等待用户按下回车键结束测试
    printf("Press Enter to stop the test...\n");
    getchar();

    // 关闭线程
    if (mode == 0 || mode == 2) {
        pthread_cancel(send_thread);
        pthread_join(send_thread, NULL);
    }
    
    if (mode == 1 || mode == 2) {
        pthread_cancel(receive_thread);
        pthread_join(receive_thread, NULL);
    }

    // 计算带宽
    gettimeofday(&end_time, NULL);
    double elapsed_time = (end_time.tv_sec - transfer_info.start_time.tv_sec) +
                          ((end_time.tv_usec - transfer_info.start_time.tv_usec) / 1000000.0);

    double upload_bandwidth = (transfer_info.total_bytes_sent * 8.0) / elapsed_time / 1e6;
    double download_bandwidth = (transfer_info.total_bytes_received * 8.0) / elapsed_time / 1e6;

    printf("Total data sent: %ld bytes\n", transfer_info.total_bytes_sent);
    printf("Total data received: %ld bytes\n", transfer_info.total_bytes_received);
    printf("Upload Bandwidth: %.2f Mbps\n", upload_bandwidth);
    printf("Download Bandwidth: %.2f Mbps\n", download_bandwidth);

    // 关闭套接字
    close(sockfd);
    pthread_cancel(bandwidth_thread);
    pthread_join(bandwidth_thread, NULL);
    return 0;
}
