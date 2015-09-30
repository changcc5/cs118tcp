CC=gcc -g

all: clean util.o packet.o sender.o receiver.o
	$(CC) -o sender sender.o util.o packet.o
	$(CC) -o receiver receiver.o util.o packet.o

%.o: %.c
	$(CC) -o $@ -c $<

.PHONY: clean

clean:
	rm -f *.o