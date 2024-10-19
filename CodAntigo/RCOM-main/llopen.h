// Write to serial port in non-canonical mode
// Modified by: Eduardo Nuno Almeida [enalmeida@fe.up.pt]

#include <fcntl.h>
#include <signal.h>
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
// int alarmEnabled = FALSE;
// int alarmCount = 0;
int received = 0;
int fd = 0;

enum states
{
  START,
  FLAG_RCV,
  A_RCV,
  C_RCV,
  BCC_OK,
  STOPSTOP,
};

volatile int STOP = FALSE;


int send();

int receive();

void alarmHandler(int signal);

void subscribeAlarm();

int main(int argc, char *argv[]);