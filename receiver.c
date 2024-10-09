// Write to serial port in non-canonical mode
// Modified by: Eduardo Nuno Almeida [enalmeida@fe.up.pt]

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

// Baudrate settings are defined in <asm/termbits.h>, which is
// included by <termios.h>
#define BAUDRATE B38400
#define _POSIX_SOURCE 1 // POSIX compliant source

#define FALSE 0
#define TRUE 1

#define BUF_SIZE 5

enum states {
  START,
  FLAG_RCV,
  A_RCV,
  C_RCV,
  BCC_OK,
  STOPSTOP,
};

volatile int STOP = FALSE;

int send(int fd) {

  // Create string to send
  unsigned char F = 0x7E;
  unsigned char A = 0x03;
  unsigned char C = 0x03;
  unsigned char BCC1 = A ^ C;

  unsigned char buf[BUF_SIZE] = {F, A, C, BCC1, F};

  // In non-canonical mode, '\n' does not end the writing.
  // Test this condition by placing a '\n' in the middle of the buffer.
  // The whole buffer must be sent even with the '\n'.

  int bytes = write(fd, buf, BUF_SIZE);
  printf("%d bytes written\n", bytes);

  // Wait until all bytes have been written to the serial port
  sleep(1);
}

int receive(int fd) {
  unsigned char a_rcv;
  unsigned char c_rcv;
  enum states current_state = START;
  while (current_state != STOPSTOP) {
    if (current_state == START) {
      unsigned char byte[1] = {0};
      read(fd, byte, 1);
      printf("Entered START\n");
      printf("READ %x\n", byte[0]);

      if (byte[0] == 0x7E) {
        current_state = FLAG_RCV;
      } else {
        printf("Received error!");
      }
    }
    if (current_state == FLAG_RCV) {
      unsigned char byte[1] = {0};
      read(fd, byte, 1);
      printf("Entered FLAG_RCV\n");
      printf("READ %x\n", byte[0]);
      a_rcv = byte[0];
      if (byte[0] == 0x03 || byte[0] == 0x01) {
        current_state = A_RCV;
      } else if (byte[0] != 0x7E) {
        current_state = START;
      } else {
        printf("Received a flag");
      }
    }
    if (current_state == A_RCV) {
      unsigned char byte[1] = {0};
      read(fd, byte, 1);
      printf("Entered A_RCV\n");
      printf("READ %x\n", byte[0]);
      c_rcv = byte[0];
      if (byte[0] == 0x7E) {
        current_state = FLAG_RCV;
      } else if (byte[0] == 0x03 || byte[0] == 0x07) {
        current_state = C_RCV;
      } else {
        printf("Received trash!");
        current_state = START;
      }
    }
    if (current_state == C_RCV) {
      unsigned char byte[1] = {0};
      read(fd, byte, 1);
      printf("Entered C_RCV\n");
      printf("READ %x\n", byte[0]);
      if (byte[0] == 0x7E) {
        current_state = FLAG_RCV;
      } else if (byte[0] == (a_rcv ^ c_rcv)) {
        current_state = BCC_OK;
      } else {
        printf("Received trash!");
        current_state = START;
      }
    }
    if (current_state == BCC_OK) {
      unsigned char byte[1] = {0};
      read(fd, byte, 1);
      printf("Entered BCC_OK\n");
      printf("READ %x\n", byte[0]);
      if (byte[0] == 0x7E) {
        current_state = STOPSTOP;
      } else {
        current_state = START;
        printf("Received trash!");
      }
    }
  }
  printf("FINISH\n");
  return 0;
}

// Loop for input
// unsigned char receive_buf[BUF_SIZE] = {0}; // +1: Save space for the final
// '\0' char

// while (STOP == FALSE)
// {
//     // Returns after 5 chars have been input
//     int bytes = read(fd, receive_buf, BUF_SIZE);

//     if (receive_buf[4] == 0x7E)
//         STOP = TRUE;
// }

// for (int i = 0; i < 5; i++)
// {
//     printf("%x\n", receive_buf[i]);
// }

// if ((receive_buf[1] ^ receive_buf[2]) != receive_buf[3])
// {
//     perror("Error on byte validation");
// }

int main(int argc, char *argv[]) {
  // Program usage: Uses either COM1 or COM2
  const char *serialPortName = argv[1];

  if (argc < 2) {
    printf("Incorrect program usage\n"
           "Usage: %s <SerialPort>\n"
           "Example: %s /dev/ttyS1\n",
           argv[0], argv[0]);
    exit(1);
  }

  // Open serial port device for reading and writing, and not as controlling tty
  // because we don't want to get killed if linenoise sends CTRL-C.
  int fd = open(serialPortName, O_RDWR | O_NOCTTY);

  if (fd < 0) {
    perror(serialPortName);
    exit(-1);
  }

  struct termios oldtio;
  struct termios newtio;

  // Save current port settings
  if (tcgetattr(fd, &oldtio) == -1) {
    perror("tcgetattr");
    exit(-1);
  }

  // Clear struct for new port settings
  memset(&newtio, 0, sizeof(newtio));

  newtio.c_cflag = BAUDRATE | CS8 | CLOCAL | CREAD;
  newtio.c_iflag = IGNPAR;
  newtio.c_oflag = 0;

  // Set input mode (non-canonical, no echo,...)
  newtio.c_lflag = 0;
  newtio.c_cc[VTIME] = 0; // Inter-character timer unused
  newtio.c_cc[VMIN] = 5;  // Blocking read until 5 chars received

  // VTIME e VMIN should be changed in order to protect with a
  // timeout the reception of the following character(s)

  // Now clean the line and activate the settings for the port
  // tcflush() discards data written to the object referred to
  // by fd but not transmitted, or data received but not read,
  // depending on the value of queue_selector:
  //   TCIFLUSH - flushes data received but not read.
  tcflush(fd, TCIOFLUSH);

  // Set new port settings
  if (tcsetattr(fd, TCSANOW, &newtio) == -1) {
    perror("tcsetattr");
    exit(-1);
  }

  printf("New termios structure set\n");

  receive(fd);
  send(fd);

  // Restore the old port settings
  if (tcsetattr(fd, TCSANOW, &oldtio) == -1) {
    perror("tcsetattr");
    exit(-1);
  }

  close(fd);

  return 0;
}
