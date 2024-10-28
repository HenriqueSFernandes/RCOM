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
unsigned char informationFrameNumber =
    0; // Used to generate the information frame.

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

int stuffPacket(const unsigned char *packet, size_t packetSize,
                unsigned char *newPacket, size_t *newPacketSize) {
  // Assumes newPacket has at least double the size of the packet.

  if (packet == NULL || newPacket == NULL || newPacketSize == NULL) {
    return 1;
  }

  size_t newPacketIndex = 0;

  for (size_t packetIndex = 0; packetIndex < packetSize;
       packetIndex++, newPacketIndex++) {
    // Replace FLAG (0x7E) with 0x7D5E.
    if (packet[packetIndex] == 0x7E) {
      newPacket[newPacketIndex++] = 0x7D;
      newPacket[newPacketIndex] = 0x5E;
    }
    // Replace ESC (0x7D) with 0x7D5E.
    else if (packet[packetIndex] == 0x7D) {
      newPacket[newPacketIndex++] = 0x7D;
      newPacket[newPacketIndex] = 0x5D;
    } else {
      // printf("no flag detected, copying from index %zd (%02x) to index %zd
      // (%02x) \n", packetIndex, packet[packetIndex], newPacketIndex,
      // newPacket[newPacketIndex]);
      newPacket[newPacketIndex] = packet[packetIndex];
    }
  }
  *newPacketSize = newPacketIndex;

  return 0;
}

int destuffPacket(const unsigned char *packet, size_t packetSize,
                  unsigned char *newPacket, size_t *newPacketSize) {
  // TODO: bcc validation has to happen AFTER destuffing.
  if (packet == NULL || newPacket == NULL || newPacketSize == NULL) {
    return 1;
  }

  size_t newPacketIndex = 0;

  for (size_t packetIndex = 0; packetIndex < packetSize;
       packetIndex++, newPacketIndex++) {
    if (packet[packetIndex] != 0x7D) {
      newPacket[newPacketIndex] = packet[packetIndex];
    } else {
      if (packet[packetIndex + 1] == 0x5E) {
        newPacket[newPacketIndex] = 0x7E;
      } else if (packet[packetIndex + 1] == 0x5D) {
        newPacket[newPacketIndex] = 0x7D;
      }
      packetIndex++;
    }
  }

  *newPacketSize = newPacketIndex;
  printf("Destuffed packet with size %zd:\n", *newPacketSize);
  for (size_t i = 0; i < *newPacketSize; i++) {
    printf("%02x ", newPacket[i]);
  }
  printf("\n");

  return 0;
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
  if (buf == NULL) {
    return -1;
  }

  // Add header and footer to packet (A, C, BCC1, ..., BCC2). The flags will be
  // added later.
  unsigned char newPacket[bufSize + 4];
  newPacket[0] = 0x03;
  newPacket[1] = informationFrameNumber;
  newPacket[2] = 0x03 ^ informationFrameNumber;
  memcpy(&newPacket[3], buf, bufSize);

  unsigned char bcc2 = 0;
  for (size_t i = 0; i < bufSize; i++) {
    bcc2 ^= buf[i];
  }
  newPacket[bufSize + 3] = bcc2;
  informationFrameNumber ^= 1; // Toggle informationFrameNumber between 0 and 1.

  unsigned char stuffedPacket[(bufSize + 4) * 2 + 2];
  size_t stuffedPacketSize;

  if (stuffPacket(newPacket, bufSize + 4, stuffedPacket + 1,
                  &stuffedPacketSize)) {
    perror("Error stuffing packet!\n");
    return -1;
  }
  stuffedPacketSize += 2;

  // Insert the flags.
  stuffedPacket[0] = 0x7E;
  stuffedPacket[stuffedPacketSize - 1] = 0x7E;

  printf("Packet after stuffing:\n");
  for (size_t i = 0; i < stuffedPacketSize; i++) {
    printf("%02x ", stuffedPacket[i]);
  }
  printf("\n");

	// Send the packet.
	
	// Verify ack.


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
