CC = gcc
CFLAGS = -Wall

SRC = src/
INCLUDE = include/
BIN = bin/

# LAB SERVERS (THESE ONLY WORK INSIDE THE LAB NETWORK)

# URL = ftp://rcom:rcom@ftp.netlab.fe.up.pt/pipe.txt
# URL = ftp://rcom:rcom@ftp.netlab.fe.up.pt/files/crab.mp4
# URL = ftp://rcom:rcom@ftp.netlab.fe.up.pt/README

# PUBLIC SERVERS

# URL = ftp://ftp.scene.org/welcome.msg
URL = ftp://anonymous:anonymous@ftp.bit.nl/speedtest/100mb.bin

# LOCAL SERVER (READ README.md TO KNOW HOW TO SET UP THE SERVER)

# FILE = penguin.gif
# FILE = README.txt
# FILE = Sebenta.pdf
FILE = 5mb.txt
# URL = ftp://user:12345@localhost:2121/$(FILE)

.PHONY: all
all: $(BIN)/download

$(BIN)/download: main.c $(SRC)/*.c
	$(CC) $(CFLAGS) -o $@ $^ -I$(INCLUDE)

.PHONY: run
run: $(BIN)/download
	./$(BIN)/download $(URL)

.PHONY: clean
clean:
	rm -f $(BIN)/download

.PHONY: diff
diff:
	diff $(FILE) ftp-server/FTP/$(FILE)
