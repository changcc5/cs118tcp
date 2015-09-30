#define SYN_FLAG 0x0001
#define ACK_FLAG 0x0002
#define FIN_FLAG 0x0004
#define NONE_FLAG 0x0008

#define PACKET_SIZE 1000
#define DATA_SIZE 990

#pragma pack(1)
struct packet
{
    // Sequence and acknowledgement flags
    int seq_num;

    // SYN, ACK, FIN flags
    short flags;

    // Data
    int d_length;
    char data[DATA_SIZE];
};
#pragma pack(0)

int check_syn(struct packet *p);
void set_syn(struct packet *p);

int check_ack(struct packet *p);
void set_ack(struct packet *p);

int check_fin(struct packet *p);
void set_fin(struct packet *p);

int check_none(struct packet *p);
void set_none(struct packet *p);

struct packet make_packet();

void set_data(struct packet *p, char *data, int length);