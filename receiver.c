#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libgen.h>

#include "packet.h"
#include "util.h"

void send_ack(int sockfd, struct packet snd_pkt, struct sockaddr_in serv_addr)
{
    msg("<- ACK: SEQNUM %d ...\n", snd_pkt.seq_num);
    int n_char;
    n_char = sendto(sockfd, &snd_pkt, sizeof(snd_pkt), 0, (struct sockaddr*)&serv_addr, sizeof(serv_addr));

    if (n_char < 0)
    {
        error("Error acking packet\n");
    }
}

int main(int argc, char *argv[])
{
    int sockfd;
    int portno, n_char;
    int slen;
    struct sockaddr_in serv_addr;
    struct hostent *server;

    char *hostname, *filepath;

    double p_loss, p_corrupt;
    p_loss = 0.0;
    p_corrupt = 0.0;

    int expect_seq_num;
    struct packet snd_pkt, rcv_pkt;

    FILE* file;

    if (argc < 4)
    {
        fprintf(stderr, "Usage: %s hostname port filepath [loss ratio] [corrupt ratio]\n", argv[0]);
        exit(0);
    }

    switch (argc)
    {
        case 6:
            p_corrupt = atof(argv[5]);
        case 5:
            p_loss = atof(argv[4]);
        default:
            filepath = argv[3];
            portno = atoi(argv[2]);
            hostname = argv[1];
            break;
    }

    sockfd = socket(AF_INET, SOCK_DGRAM, 0); // Using UDP!

    if (sockfd < 0)
    {
        error("Error opening socket");
    }

    server = gethostbyname(hostname);
    if (server == NULL)
    {
        fprintf(stderr, "Error: no such host\n");
        exit(0);
    }

    bzero((char *)&serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    bcopy((char *)server->h_addr, (char *)&serv_addr.sin_addr.s_addr, server->h_length);
    serv_addr.sin_port = htons(portno);

    // Create the initial packet
    expect_seq_num = 1;
    snd_pkt = make_packet();

    set_syn(&snd_pkt);
    snd_pkt.seq_num = 0;
    set_data(&snd_pkt, filepath, strlen(filepath));

    slen = sizeof(serv_addr);

    char str[100] = "copy_";

    char *dirc, *basec, *dname, *bname;
    dirc = strdup(filepath);
    basec = strdup(filepath);
    dname = dirname(dirc);
    bname = basename(basec);

    file = fopen(strcat(str, bname), "wb");

    // Send the initial packet
    msg("Sending file request to sender...\n");
    n_char = sendto(sockfd, &snd_pkt, sizeof(snd_pkt), 0, (struct sockaddr*)&serv_addr, slen);
    if (n_char < 0)
    {
        error("Error sending packet");
    }

    // Go-Back-N FSM
    while (1)
    {
        // Wait to receive a message
        // if (rcv_pkt)
        // {
        //     free(rcv_pkt);
        // }

        rcv_pkt = make_packet();

        n_char = recvfrom(sockfd, &rcv_pkt, sizeof(rcv_pkt), 0, (struct sockaddr*)&serv_addr, (socklen_t*)&slen);

        if (check_none(&rcv_pkt))
        {
            msg("No such file exists on the sender side\n");
            fclose(file);
            remove(str);
            teardown(NULL, sockfd);
        }

        if (check_fin(&rcv_pkt)) // server closing connection
        {
            msg("-> FIN\n");
            msg("<- FIN-ACK ...\n");

            //free(&snd_pkt);
            snd_pkt = make_packet();
            set_fin(&snd_pkt);

            n_char = sendto(sockfd, &snd_pkt, sizeof(snd_pkt), 0, (struct sockaddr*)&serv_addr, sizeof(serv_addr));
            if (n_char < 0)
            {
                error("Error sending FIN-ACK");
            }

            //free(&rcv_pkt);
            //free(&snd_pkt);
            teardown(file, sockfd);
            break;
        }

        if (n_char < 0)
        {
            msg("Packet from sender LOST\n");
        }
        else if (chance() < p_loss)
        {
            msg("Packet from sender LOST\n");
        }
        else if (chance() < p_corrupt)
        {
            msg("Packet from sender CORRUPT\n");
            
            msg("<- RE-ACK: SEQNUM %d ...\n", snd_pkt.seq_num);
            int n_char;
            n_char = sendto(sockfd, &snd_pkt, sizeof(snd_pkt), 0, (struct sockaddr*)&serv_addr, sizeof(serv_addr));

            if (n_char < 0)
            {
                error("Error acking packet\n");
            }
        }
        else if (rcv_pkt.seq_num == expect_seq_num)
        {
            msg("-> DATA: SEQNUM %d\n", rcv_pkt.seq_num);

            fwrite(rcv_pkt.data, 1, rcv_pkt.d_length, file);
            expect_seq_num++;

            //free(&snd_pkt);
            snd_pkt = make_packet();
            set_ack(&snd_pkt);
            snd_pkt.seq_num = rcv_pkt.seq_num;

            send_ack(sockfd, snd_pkt, serv_addr);
        }
        else if (rcv_pkt.seq_num < expect_seq_num)
        {
            msg("-> DATA: SEQNUM %d\n", rcv_pkt.seq_num);

            //free(&snd_pkt);
            snd_pkt = make_packet();
            set_ack(&snd_pkt);
            snd_pkt.seq_num = rcv_pkt.seq_num;

            msg("<- RE-ACK: SEQNUM %d ...\n", snd_pkt.seq_num);
            int n_char;
            n_char = sendto(sockfd, &snd_pkt, sizeof(snd_pkt), 0, (struct sockaddr*)&serv_addr, sizeof(serv_addr));

            if (n_char < 0)
            {
                error("Error acking packet\n");
            }
        }
        else
        {
            msg("-> DATA: SEQNUM %d\n", rcv_pkt.seq_num);
            
            msg("<- RE-ACK: SEQNUM %d ...\n", snd_pkt.seq_num);
            int n_char;
            n_char = sendto(sockfd, &snd_pkt, sizeof(snd_pkt), 0, (struct sockaddr*)&serv_addr, sizeof(serv_addr));

            if (n_char < 0)
            {
                error("Error acking packet\n");
            }
        }
    }
}