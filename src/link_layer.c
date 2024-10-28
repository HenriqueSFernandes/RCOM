// Link layer protocol implementation

#include "../include/link_layer.h"
#include "../include/serial_port.h"
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

// MISC
#define _POSIX_SOURCE 1 // POSIX compliant source
#define FLAG_BUFFER_SIZE 5

int allRead = FALSE;
int alarmEnabled;
int alarmCount = 0;
enum states current_state = START;
LinkLayer parameters;
unsigned char C_NS = 0;
int fd;

// Increases the counter on alarm signal.
void alarmHandler(int signal) {
  alarmCount++;
  alarmEnabled = TRUE;
  printf("Alarm! Count: %d\n", alarmCount);
}

/// @brief Disables alarm and resets the counter.
void disableAlarm() {
  alarmEnabled = FALSE;
  alarm(0);
  alarmCount = 0;
}

/// @brief Sends control frame.
/// @param A 0x03 if transmitter or reply by receiver, 0x01 otherwise.
/// @param C 0x03 if transmitter, 0x07 if receiver.
int sendControlFrame(unsigned char A, unsigned char C) {
  unsigned char frame[5] = {0x7E, A, C, 0, 0x7E};
  frame[3] = frame[1] ^ frame[2];

  if (write(fd, frame, 5) < 0) {
    perror("Error sending control frame!\n");
    return -1;
  }
  return 0;
}

/// @brief Receives a control frame.
/// @param expectedA Expected A.
/// @param expectedC Expected C.
int receiveControlFrame(unsigned char expectedA, unsigned char expectedC) {
  enum states currentState = START;

  while (currentState != STOP) {
    unsigned char byte = 0;
    int bytes;
    if ((bytes = read(fd, &byte, sizeof(byte))) < 0) {
      perror("Error reading byte from control frame!\n");
      return -1;
    }
    if (bytes > 0) {
      switch (currentState) {
      case START:
        if (byte == 0x7E)
          currentState = FLAG_RCV;
        break;
      case FLAG_RCV:
        if (byte == 0x7E)
          continue;
        if (byte == expectedA)
          currentState = A_RCV;
        break;
      case A_RCV:
        if (byte == 0x7E)
          currentState = FLAG_RCV;
        else if (byte == expectedC)
          currentState = C_RCV;
        else
          currentState = START;
        break;
      case C_RCV:
        if (byte == 0x7E)
          currentState = FLAG_RCV;
        else if (byte == (expectedC ^ expectedA))
          currentState = BCC_OK;
        else
          currentState = START;
        break;

      case BCC_OK:
        if (byte == 0x7E)
          currentState = STOP;
        else
          currentState = START;
        break;
      default:
        currentState = START;
      }
    }
  }
  return 0;
}

/// @brief Sends the control frame and waits for a response, triggering
/// retransmissions if needed.
/// @param A A to send.
/// @param C C to send.
/// @param expectedA Expected A.
/// @param expectedC Expected C.
int sendControlAndAwaitAck(unsigned char A, unsigned char C,
                           unsigned char expectedA, unsigned char expectedC) {
  enum states currentState = START;
  (void)signal(SIGALRM, alarmHandler);
  if (sendControlFrame(A, C))
    return -1;
  alarm(parameters.timeout);

  while (currentState != STOP && alarmCount <= parameters.nRetransmissions) {
    unsigned char byte = 0;
    int bytes;

    if ((bytes = read(fd, &byte, sizeof(byte))) < 0) {
      perror("Error reading byte from control frame!\n");
      return -1;
    }
    if (bytes > 0) {
      switch (currentState) {
      case START:
        if (byte == 0x7E)
          currentState = FLAG_RCV;
        break;
      case FLAG_RCV:
        if (byte == 0x7E)
          continue;
        if (byte == expectedA)
          currentState = A_RCV;
        break;
      case A_RCV:
        if (byte == 0x7E)
          currentState = FLAG_RCV;
        else if (byte == expectedC)
          currentState = C_RCV;
        else
          currentState = START;
        break;
      case C_RCV:
        if (byte == 0x7E)
          currentState = FLAG_RCV;
        else if (byte == (expectedC ^ expectedA))
          currentState = BCC_OK;
        else
          currentState = START;
        break;

      case BCC_OK:
        if (byte == 0x7E)
          currentState = STOP;
        else
          currentState = START;
        break;
      default:
        currentState = START;
      }
    }
    if (currentState == STOP) {
      disableAlarm();
      return 0;
    }

    if (alarmEnabled) {
      printf("Retransmitting...\n");
      alarmEnabled = FALSE;
      if (alarmCount <= parameters.nRetransmissions) {
        if (sendControlFrame(A, C))
          return -1;
        alarm(parameters.timeout);
      }
      currentState = START;
    }
  }
  disableAlarm();
  return -1;
}

////////////////////////////////////////////////
// LLOPEN
////////////////////////////////////////////////
int llopen(LinkLayer connectionParameters) {
  parameters = connectionParameters;
  (void)signal(SIGALRM, alarmHandler);

  fd = openSerialPort(connectionParameters.serialPort,
                      connectionParameters.baudRate);
  if (fd < 0) {
    return -1;
  }

  if (connectionParameters.role == LlTx) { // Transmitter
    // Sends control frame with A=0x03 and C=0x03 and waits for a response with
    // A=0x03 and C=0x07.
    if (sendControlAndAwaitAck(0x03, 0x03, 0x03, 0x07))
      return -1;
    printf("Control frame sent and received correctly, connection established "
           "successfully.\n");
  } else {
    // Waits for a control frame with A=0x03 and C=0x03.
    if (receiveControlFrame(0x03, 0x03))
      return -1;
    // Responds with a control frame with A=0x03 and C=0x07.
    if (sendControlFrame(0x03, 0x07))
      return -1;
    printf("Control frame received and sent correctly, connection established "
           "successfully.\n");
  }

  return 0;
}

////////////////////////////////////////////////
// LLWRITE
////////////////////////////////////////////////
int llwrite(const unsigned char *buf, int bufSize) {
  // Onde é que fica o destuffing? depois de tirar as flags?
  unsigned char F = 0x7E;
  unsigned char A = 0x03;
  unsigned char BCC1 = A ^ C_NS;
  unsigned char BBC2;
  for (int i = 0; i < bufSize; i++) {
    BBC2 ^= buf[i];
  }
  unsigned char frame[sizeof(F) + sizeof(A) + sizeof(C_NS) + sizeof(BCC1) +
                      bufSize + sizeof(BBC2) + sizeof(F)];
  frame[0] = F;
  frame[1] = A;
  frame[2] = C_NS;
  frame[3] = BCC1;
  for (int i = 0; i < bufSize; i++) {
    frame[i + 4] = buf[i];
  }
  frame[4 + bufSize] = BBC2;
  frame[5 + bufSize] = F;
  write(fd, frame, 1);
  C_NS = 1 - C_NS; // Talvez
  return 0;
}

////////////////////////////////////////////////
// LLREAD
////////////////////////////////////////////////
int llread(unsigned char *packet) {
  printf("Reading packet\n");
  unsigned char F = packet[0];
  if (F != 0x7E) {
    printf("Flag not found\n");
    return -1;
  }
  unsigned char A = packet[1];
  if (A != 0x03) // Será que pode ser 0x01?
  {
    printf("A not found\n");
    return -1;
  }
  unsigned char C =
      packet[2]; // Fazer a verificacao de se for o certo receb se for o errado
                 // manda RR mas discarta e não guarda
  unsigned char BCC1 = packet[3]; // Ainda temos de verificar se está ok
  unsigned char control = packet[4];

  if (control == 1) { // CONTROL START Temos de verificar se o último é igual,
                      // provavlemnte guardar packet global
    // T == 0 File size
    // T == 1 File name
    // Outros Ts??
    unsigned char T1 =
        packet[5]; // Consideramos que é o size? ou fazemos o check?
    unsigned char L1 = packet[6];
    unsigned char size[L1];
    for (int i = 0; i < L1; i++) {
      size[i] = packet[i + 7];
    }
    unsigned char T2 = packet[7 + L1];
    unsigned char L2 = packet[8 + L1];
    unsigned char name[L2];
    for (int i = 0; i < L2; i++) {
      name[i] = packet[i + 9 + L1];
    }
    unsigned char BCC2 = packet[9 + L1 + L2];
    unsigned char F2 = packet[10 + L1 + L2];
    printf("Control: %d\n", control);
  }
  if (control == 2) { // DATA
    unsigned char sequenceNumber = packet[5];
    unsigned char L1 = packet[6];
    unsigned char L2 = packet[7];
    unsigned char data[256 * L2 + L1];
    for (int i = 0; i < 256 * L2 + L1; i++) {
      data[i] = packet[i + 8];
    }
    unsigned char BCC2 = packet[8 + 256 * L2 + L1];
    unsigned char F2 = packet[9 + 256 * L2 + L1];
    printf("Data: ");
    for (int i = 0; i < 256 * L2 + L1; i++) {
      printf("%c", data[i]);
    }
    // Mandar para a application layer
  }
  if (control == 3) { // Verificar se é igual ao priemiro control
    // Zé ric quanto é o tamanaho do packet? preciso de saber tamanho do name e
    // tamanho do size para poder criar um array para guardar o primeiro control
    // para comparar com o terceiro control
  }
  return 0;
}

////////////////////////////////////////////////
// LLCLOSE
////////////////////////////////////////////////
int llclose(int showStatistics) {
  // Transmitter sends disc, receiver sends disc and waits for response.
  if (parameters.role == LlTx) {
    // Sends A=0x03 and C=0x0B, waits for response A=0x01, C=0x0B (disconnect
    // frames).
    if (sendControlAndAwaitAck(0x03, 0x0B, 0x01, 0x0B))
      return closeSerialPort();
    if (sendControlFrame(0x01, 0x07))
      return closeSerialPort();
    printf("Disconnected!\n");

  } else if (parameters.role == LlRx) {
    if (receiveControlFrame(0x03, 0x0B))
      return closeSerialPort();
    if (sendControlAndAwaitAck(0x01, 0x0B, 0x01, 0x07))
      return closeSerialPort();
    printf("Disconnected!\n");
  }

  int clstat = closeSerialPort();
  return clstat;
}
