#include <stdio.h> //printf
#include <string.h> //memset
#include <stdlib.h> //exit(0);
#include <unistd.h> // for close
#include <signal.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <math.h>

#include "packet.h"
#include "util.h"
 
#define TO_SEC 1 //timeout seconds
 
int timeout;

void die(char *s, int socket)
{
    perror(s);
    close(socket);
    exit(1);
}

void handle_sigalrm(int signo, siginfo_t *siginfo, void *context)
{
	//alert when timed out
    alarm(TO_SEC);
    timeout = 1;
}

int min(int a, int b) {
    return a < b ? a : b;
}

int main(int argc, char *argv[])
{
    int cwnd;
    cwnd = 4000; // default cwnd

    timeout = 0;

    int WINDOW_SIZE;

	double p_loss, p_corrupt;
    p_loss = 0.0;
    p_corrupt = 0.0;
    
    struct sockaddr_in serv_si, cli_si;
	struct packet snd_pkt, rcv_pkt;

    FILE *file;
    struct stat file_stat;
    int n_packets;
    int readlength;
     
    int portno, sockfd, i, recv_len, send_len;
    int slen = sizeof(cli_si);

    if (argc < 2)
    {
        fprintf(stderr, "Usage: %s port [cwnd] [loss ratio] [corrupt ratio]\n", argv[0]);
        exit(0);
    }

    switch (argc)
    {
        case 5:
            p_corrupt = atof(argv[4]);
        case 4:
            p_loss = atof(argv[3]);
        case 3:
            cwnd = atoi(argv[2]);
        default:
            portno = atoi(argv[1]);
            break;
    }

    WINDOW_SIZE = cwnd / PACKET_SIZE;

    //create a UDP socket
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) == -1)
    {
        die("socketfd error", sockfd);
    }
     
    // zero out the structure
    memset((char *) &serv_si, 0, sizeof(serv_si));
     
    serv_si.sin_family = AF_INET;
    serv_si.sin_port = htons(portno);
    serv_si.sin_addr.s_addr = htonl(INADDR_ANY);
     
    //bind socket to port
    if(bind(sockfd , (struct sockaddr*)&serv_si, sizeof(serv_si)) == -1)
    {
        die("error in bind\n", sockfd);
    }

    rcv_pkt = make_packet();
    
    msg("Waiting for file request...\n");
    if ((recv_len = recvfrom(sockfd, &rcv_pkt, sizeof(rcv_pkt), 0, (struct sockaddr *) &cli_si, (socklen_t*)&slen)) == -1)
	{
        die("error receive syn", sockfd);
	}

    snd_pkt = make_packet();
    
    if (check_syn(&rcv_pkt)) {
        msg("Received file request for %s\n", rcv_pkt.data);

    	file = fopen(rcv_pkt.data, "rb");

        if (file == NULL) // no file exists
        {
            msg("No such file, sending NONE packet...\n");

            set_none(&snd_pkt);
            if ((send_len = sendto(sockfd, &snd_pkt, sizeof(snd_pkt), 0, (struct sockaddr*)&cli_si, slen)) == -1)
            {
                die("Error sending NONE", sockfd);
            }

            teardown(file, sockfd);
        }
    }

    stat(rcv_pkt.data, &file_stat);
    n_packets = file_stat.st_size / DATA_SIZE;
    if (file_stat.st_size % DATA_SIZE)
    {
        n_packets++;
    }
    
    int base, next_seq, last_ack = 1;

    base = 1;
    next_seq = 1;
    last_ack = 0;

    int pktindex;

    struct packet pkt_win[WINDOW_SIZE];
    char buf[PACKET_SIZE];

    struct sigaction act;
    memset (&act, '\0', sizeof(act));
    act.sa_sigaction = &handle_sigalrm;
    act.sa_flags = 0;
    sigaction(SIGALRM, &act, NULL); //sets timeout handler

    // Send initial packets
    for (i = 0; i < min(WINDOW_SIZE, n_packets); i++) {
        memset(buf, 0, PACKET_SIZE);

        snd_pkt = make_packet();
        snd_pkt.seq_num = i + 1;

        readlength = fread(buf, sizeof(char), DATA_SIZE, file);
        set_data(&snd_pkt, buf, readlength);

        pkt_win[i] = snd_pkt;

        msg("<- DATA: SEQNUM %d ...\n", snd_pkt.seq_num);

        if (sendto(sockfd, &snd_pkt, sizeof(snd_pkt), 0, (struct sockaddr *)&cli_si, slen) < 0)
        {
            error("ERROR on sending");
        }

        next_seq++;
    }

	while (base <= n_packets) {
        if (timeout) {
            int n_char;
            int resend_base = base;

            msg("TIMEOUT: resending packets %d - %d\n", resend_base, min(resend_base + WINDOW_SIZE - 1, n_packets));
            
            while (resend_base < next_seq) {
                pktindex = (resend_base - 1) % WINDOW_SIZE;
                send_len = sendto(sockfd, &pkt_win[pktindex], sizeof(pkt_win[pktindex]), 0, (struct sockaddr*)&cli_si, slen);
                if (send_len < 0)
                {
                    die("Error sending packet during timeout", sockfd);
                }
                msg("Retransmitting DATA with SEQNUM %d ...\n", resend_base);
                resend_base++;
            }

            timeout = 0;
            alarm(TO_SEC);
        }

		rcv_pkt = make_packet();

		if ((recv_len = recvfrom(sockfd, &rcv_pkt, sizeof(rcv_pkt), 0, (struct sockaddr *) &cli_si, (socklen_t*)&slen)) == -1)
		{
	        // Timed out, recvfrom unblocked. 
            // Continue, let loop hit timeout block above
            continue;
		}
        else if (chance() < p_loss)
        {
            msg("Packet from receiver LOST\n");
        }
		else if (chance() < p_corrupt)
		{
			msg("Packet from receiver CORRUPT\n");
		}
		else if (check_fin(&rcv_pkt)) {
			msg("-> FIN-ACK\n");
			teardown(file, sockfd);
		}
		else if (rcv_pkt.seq_num == base) {
			// Correct ACK received, stop timer
            alarm(0);

            msg("-> ACK: SEQNUM %d\n", rcv_pkt.seq_num);
            last_ack = rcv_pkt.seq_num;
            base = rcv_pkt.seq_num + 1;

            // Send packets
            if (next_seq <= min(base + WINDOW_SIZE, n_packets + 1)) {
                memset(buf, 0, PACKET_SIZE);

                if ((readlength = fread(buf, sizeof(char), DATA_SIZE, file)) == 0) {
                    // struct packet fin_pkt = make_packet();

                    // set_fin(fin_pkt);
                    // fin_pkt.seq_num = next_seq;

                    
                    // if ((send_len = sendto(sockfd, &finpkt, sizeof(finpkt), 0, (struct sockaddr*)&cli_si, slen)) < 0) {
                    //     die("Error sending packet during fin", sockfd);
                    // }
                    // else {
                    //     msg("<- FIN ...\n");
                    //     alarm(TO_SEC);
                    // }
                    alarm(TO_SEC);
                }
                else {
                    snd_pkt = make_packet();

                    set_data(&snd_pkt, buf, readlength);
                    snd_pkt.seq_num = next_seq;

                    pktindex = (next_seq - 1) % WINDOW_SIZE;
                    pkt_win[pktindex] = snd_pkt;

                    msg("<- DATA: SEQNUM %d ...\n", next_seq);

                    if ((send_len = sendto(sockfd, &pkt_win[pktindex], sizeof(pkt_win[pktindex]), 0, (struct sockaddr*)&cli_si, slen)) < 0) {
                        die("Error sending packet during data", sockfd);
                    }

                    if (next_seq == base) {
                        alarm(TO_SEC);
                    }

                    if (next_seq <= n_packets)
                    {
                        next_seq++;
                    }
                }
            }
		}
		else {
			// Incorrect ACK received, restart timer
            alarm(TO_SEC);
		}
	}

    // Send FIN
    snd_pkt = make_packet();
    set_fin(&snd_pkt);
    msg("<- FIN ...\n");

    if ((send_len = sendto(sockfd, &snd_pkt, sizeof(snd_pkt), 0, (struct sockaddr*)&cli_si, slen)) < 0) {
        die("Error sending packet during teardown", sockfd);
    }

    rcv_pkt = make_packet();

    if ((recv_len = recvfrom(sockfd, &rcv_pkt, sizeof(rcv_pkt), 0, (struct sockaddr *) &cli_si, (socklen_t*)&slen)) > 0)
    {
        if (check_fin(&rcv_pkt)) {
            msg("-> FIN-ACK\n");
            teardown(file, sockfd);
        }
    }

    teardown(file, sockfd);
    return 0;
}