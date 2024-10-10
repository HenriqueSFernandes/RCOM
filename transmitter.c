#include "llopen.h"

int send()
{

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
  alarm(3);
  return 0;
}

int receive()
{
  unsigned char a_rcv;
  unsigned char c_rcv;
  enum states current_state = START;
  while (current_state != STOPSTOP && alarmCount < 4)
  {
    unsigned char byte[1] = {0};
    if (read(fd, byte, 1) != 0)
    {
      if (current_state == START)
      {
        printf("Entered START\n");
        printf("READ %x\n", byte[0]);

        if (byte[0] == 0x7E)
        {
          current_state = FLAG_RCV;
        }
        else
        {
          printf("Received error!");
        }
      }
      else if (current_state == FLAG_RCV)
      {
        printf("Entered FLAG_RCV\n");
        printf("READ %x\n", byte[0]);
        a_rcv = byte[0];
        if (byte[0] == 0x03 || byte[0] == 0x01)
        {
          current_state = A_RCV;
        }
        else if (byte[0] != 0x7E)
        {
          current_state = START;
        }
        else
        {
          printf("Received a flag");
        }
      }
      else if (current_state == A_RCV)
      {
        printf("Entered A_RCV\n");
        printf("READ %x\n", byte[0]);
        c_rcv = byte[0];
        if (byte[0] == 0x7E)
        {
          current_state = FLAG_RCV;
        }
        else if (byte[0] == 0x03 || byte[0] == 0x07)
        {
          current_state = C_RCV;
        }
        else
        {
          printf("Received trash!");
          current_state = START;
        }
      }
      else if (current_state == C_RCV)
      {
        printf("Entered C_RCV\n");
        printf("READ %x\n", byte[0]);
        if (byte[0] == 0x7E)
        {
          current_state = FLAG_RCV;
        }
        else if (byte[0] == (a_rcv ^ c_rcv))
        {
          current_state = BCC_OK;
        }
        else
        {
          printf("Received trash!");
          current_state = START;
        }
      }
      else if (current_state == BCC_OK)
      {
        printf("Entered BCC_OK\n");
        printf("READ %x\n", byte[0]);
        if (byte[0] == 0x7E)
        {
          current_state = STOPSTOP;
        }
        else
        {
          current_state = START;
          printf("Received trash!");
        }
      }
      byte[0] = 0;
    }
  }
  printf("FINISH\n");
  alarm(0);
  return 0;
}

void alarmHandler(int signal)
{
  alarmEnabled = FALSE;
  alarmCount++;
  printf("Alarm no:  %d", alarmCount);
  if (alarmCount < 4)
  {
    send();
  }
  else
  {
    perror("Alarm Triggered more than 3 time! ERROR");
  }
}

void subscribeAlarm()
{

  (void)signal(SIGALRM, alarmHandler);

  // while (alarmCount < 4) {
  //   if (alarmEnabled == FALSE) {
  //     alarm(3); // Set alarm to be triggered in 3s
  //     alarmEnabled = TRUE;
  //   }
  // }
}

int main(int argc, char *argv[])
{
  // Program usage: Uses either COM1 or COM2
  const char *serialPortName = argv[1];

  if (argc < 2)
  {
    printf("Incorrect program usage\n"
           "Usage: %s <SerialPort>\n"
           "Example: %s /dev/ttyS1\n",
           argv[0], argv[0]);
    exit(1);
  }

  // Open serial port device for reading and writing, and not as controlling tty
  // because we don't want to get killed if linenoise sends CTRL-C.
  fd = open(serialPortName, O_RDWR | O_NOCTTY);

  if (fd < 0)
  {
    perror(serialPortName);
    exit(-1);
  }

  struct termios oldtio;
  struct termios newtio;

  // Save current port settings
  if (tcgetattr(fd, &oldtio) == -1)
  {
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
  newtio.c_cc[VMIN] = 0;  // Blocking read until 5 chars received

  // VTIME e VMIN should be changed in order to protect with a
  // timeout the reception of the following character(s)

  // Now clean the line and activate the settings for the port
  // tcflush() discards data written to the object referred to
  // by fd but not transmitted, or data received but not read,
  // depending on the value of queue_selector:
  //   TCIFLUSH - flushes data received but not read.
  tcflush(fd, TCIOFLUSH);

  // Set new port settings
  if (tcsetattr(fd, TCSANOW, &newtio) == -1)
  {
    perror("tcsetattr");
    exit(-1);
  }

  printf("New termios structure set\n");

  subscribeAlarm();
  printf("Set up alarm");

  send();
  printf("Sent");

  receive();
  printf("received");

  // Restore the old port settings
  if (tcsetattr(fd, TCSANOW, &oldtio) == -1)
  {
    perror("tcsetattr");
    exit(-1);
  }

  close(fd);

  return 0;
}
