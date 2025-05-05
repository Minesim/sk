//Mateusz Golisz
#define _POSIX_C_SOURCE 199309L //aby <time.h> działał
#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <netinet/ip_icmp.h>
#include <netinet/ip.h>
#include <unistd.h>
#include <poll.h>
#include <time.h>

void ERROR(const char* str)
{
    fprintf(stderr, "%s: %s\n", str, strerror(errno));  // NOLINT(*-err33-c)
    exit(EXIT_FAILURE);
}

u_int16_t compute_icmp_checksum(const void *buff, int length)
{
    const u_int16_t* ptr = buff;
    u_int32_t sum = 0;
    assert (length % 2 == 0);
    for (; length > 0; length -= 2)
        sum += *ptr++;
    sum = (sum >> 16U) + (sum & 0xffffU);
    return ~(sum + (sum >> 16U));
}

double average_rtt(double rttimes[]) {
    int no_of_responses = 3;
    double avg = 0;
    for (int i = 0; i < no_of_responses; i++) {
        avg += rttimes[i];
    }
    avg /= no_of_responses;
    return avg;
}


int main(int argc, char* argv[]) {
    //sprawdź czy w ogóle dostaliśmy argument
    if(argc != 2) {
        printf("bledna ilosc argumentow\n");
        return -1;
    }

    //spróbuj stworzyć socket
    int sock_fd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (sock_fd < 0) {
        ERROR("socket error");
    }


    //dodawanie danych o adresacie: 
    char* target_ip = argv[1];
    struct sockaddr_in recipient;
    memset (&recipient, 0, sizeof(recipient));
    recipient.sin_family = AF_INET;
    //sprawdzenie argument to poprawny adres ip; konwersja
    if (inet_pton (AF_INET, target_ip, &recipient.sin_addr) != 1){
        ERROR("incorrect ip");
    }


    int target_reached = 0; //bool
    //faktycznie wyślij, iteracje:
    for (int ttl = 1; ttl <= 30 && !target_reached; ttl++) {
        //ustaw nowy TTL
        if(setsockopt (sock_fd, IPPROTO_IP, IP_TTL, &ttl, sizeof(ttl)) < 0) {
            ERROR("set ttl error");
        }

        //przygotowanie do odbioru
        int no_of_responses = 0;
        int no_of_unique_responses = 0;
        char ip_responses[3][INET_ADDRSTRLEN];
        struct timespec start_times[3];
        memset(&start_times, 0, sizeof(start_times));
        struct timespec end_times[3];
        memset (&end_times, 0, sizeof(end_times));

        //wysylanie
        for (int i = 1; i <= 3; i++) {
            //tworzenie danych do wysylki
            struct icmphdr header;
            header.type = ICMP_ECHO;
            header.code = 0;
            header.un.echo.id = getpid();
            header.un.echo.sequence = 3*ttl + i;
            header.checksum = 0;
            header.checksum = compute_icmp_checksum ((u_int16_t*)&header, sizeof(header));


            //zacznij pomiar czasu
            clock_gettime(CLOCK_MONOTONIC, &start_times[i-1]);
            //wyslij
            if (sendto (
                sock_fd, &header, sizeof(header), 0,
                (struct sockaddr*)&recipient, sizeof(recipient)) < 0) {
                ERROR("sendto error");
            }
        }


        //oczekiwanie i odbieranie
        struct pollfd fds;
        fds.fd = sock_fd;
        fds.events = POLLIN;

        //returns -1 if error, 0 if timeout, x > 0 if received x specified events
        for (int i = 0; i < 3; i++) {
            int ret = poll(&fds, 1, 1000); //1000ms
            if (ret == -1) {
                ERROR("poll error");
            }
            else if (ret == 0) {
                //timeout, do nothing
                //wypisz samą gwiazdkę here
                continue;
            }
            else if (ret > 0) {
                struct sockaddr_in sender_data;
                socklen_t sender_data_len = sizeof(sender_data);
                u_int8_t recv_buffer[IP_MAXPACKET];
                
                //odbierz pakiet
                ssize_t packet_len = recvfrom(sock_fd, recv_buffer, IP_MAXPACKET, 0, (struct sockaddr *)&sender_data, &sender_data_len);
                if (packet_len < 0) {
                    ERROR("recvfrom error");
                }                

                struct iphdr* ip_header = (struct iphdr*) recv_buffer;
                struct icmphdr* icmp_header = (struct icmphdr*) (recv_buffer + ip_header->ihl * 4);

                int response_type = icmp_header->type;
                if (response_type == ICMP_TIME_EXCEEDED) {
                    ip_header = (void *)icmp_header + 8;
                    icmp_header = (void *)ip_header + 4 * ip_header->ihl;
                }

                int received_seq = icmp_header->un.echo.sequence;
                int response_id = icmp_header->un.echo.id;

                //zly pakiet
                if (!(response_id == getpid() && received_seq >= 3*ttl+1 && received_seq <= 3*ttl+3)) {
                    i--;
                    continue;                    
                }

                int nr = received_seq-1 % 3;
                clock_gettime(CLOCK_MONOTONIC, &end_times[nr]);
                no_of_responses++;
                
                if(response_type == ICMP_ECHOREPLY) {
                    target_reached = 1;
                }

                int is_unique = 1; // czy nowo otrzymana odpowiedź nie wystąpiła już dla tego ttl
                //konwersja ip na string
                char sender_ip_str[INET_ADDRSTRLEN];
                if(inet_ntop(AF_INET, &(sender_data.sin_addr), sender_ip_str, sizeof(sender_ip_str)) == NULL) {
                    ERROR("inet_ntop error");
                }
                //sprawdzenie czy takie ip wystąpiło już w tym ttl
                for(int j = 0; j < i; j++) {
                    if (strcmp(sender_ip_str, ip_responses[j]) == 0) {
                        is_unique = 0;
                        break;
                    }
                }
                if (is_unique) {
                    no_of_unique_responses++;
                    strncpy(ip_responses[i], sender_ip_str, INET_ADDRSTRLEN);
                }
            }
        }
        
        
        //wypisywanie rezultatów dla danego ttl
        if (ttl < 10) printf(" "); //wyrównanie szerokości
        printf("%d. ",ttl);

        double rttimes[3];
        for(int i = 0; i < no_of_responses; i++) {
            rttimes[i] = (end_times[i].tv_sec - start_times[i].tv_sec) * 1000.0 +
                (end_times[i].tv_nsec - start_times[i].tv_nsec) / 1.0e6;
        }

        
        
        if (no_of_responses == 0) {
            printf("*\r\n");
        }
        else {
            for (int i = 0; i < no_of_unique_responses; i++) {
                printf("%s ", ip_responses[i]);
            }
            if (no_of_responses == 3) {
                double avg_rtt = average_rtt(rttimes);
                printf("%.2fms\r\n", avg_rtt);
            }
            else printf("???\r\n");
        }
    }
    
    close(sock_fd);
    return 0;
}