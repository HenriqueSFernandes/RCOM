CC = gcc
CFLAGS = -Wall

SRC = src/
INCLUDE = include/
BIN = bin/

# THESE ONLY WORK INSIDE THE LAB NETWORK

# URL = ftp://rcom:rcom@ftp.netlab.fe.up.pt/pipe.txt
# URL = ftp://rcom:rcom@ftp.netlab.fe.up.pt/files/crab.mp4
# URL = ftp://rcom:rcom@ftp.netlab.fe.up.pt/README

# PUBLIC SERVERS

URL = ftp://demo:password@test.rebex.net/pub/example/readme.txt


all: $(BIN)/download

$(BIN)/download: main.c $(SRC)/*.c
	$(CC) $(CFLAGS) -o $@ $^ -I$(INCLUDE)

run: $(BIN)/download
	./$(BIN)/download $(URL)

clean:
	rm -f $(BIN)/download
