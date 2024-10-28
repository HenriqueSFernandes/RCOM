// Link layer protocol implementation

#include "../include/link_layer.h"
#include "../include/serial_port.h"
#include <fcntl.h>
#include <math.h>
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

#define RR0 0xAA
#define RR1 0xAB
#define REJ0 0x54
#define REJ1 0x55

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
      alarmEnabled = FALSE;
      if (alarmCount <= parameters.nRetransmissions) {
        printf("Retransmitting...\n");
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
  unsigned char newPacket[bufSize + 1];
  memcpy(&newPacket, buf, bufSize);

  unsigned char bcc2 = 0;
  for (size_t i = 0; i < bufSize; i++) {
    bcc2 ^= buf[i];
  }
  newPacket[bufSize] = bcc2;

  unsigned char stuffedPacket[(bufSize + 1) * 2 + 5];
  size_t stuffedPacketSize;

  if (stuffPacket(newPacket, bufSize + 1, stuffedPacket + 4,
                  &stuffedPacketSize)) {
    perror("Error stuffing packet!\n");
    return -1;
  }
  stuffedPacketSize += 5;

  // Insert the flags.
  stuffedPacket[0] = 0x7E;
  stuffedPacket[1] = 0x03;
  stuffedPacket[2] = informationFrameNumber;
  stuffedPacket[3] = 0x03 ^ informationFrameNumber;
  stuffedPacket[stuffedPacketSize - 1] = 0x7E;
  informationFrameNumber ^=
      0x80; // Toggle informationFrameNumber between 0x00 and 0x80.

  printf("Packet after stuffing:\n");
  for (size_t i = 0; i < stuffedPacketSize; i++) {
    printf("%02x ", stuffedPacket[i]);
  }
  printf("\n");

  // Send the packet.
  if (write(fd, stuffedPacket, stuffedPacketSize) < 0) {
    perror("Error writing stuffed packet.\n");
    return -1;
  }
  printf("Packet sent!\n");

  // Verify response
  enum states currentState = START;
  (void)signal(SIGALRM, alarmHandler);
  alarm(parameters.timeout);

  unsigned char receivedA = 0;
  unsigned char receivedC = 0;

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
        receivedA = 0;
        receivedC = 0;
        if (byte == 0x7E)
          currentState = FLAG_RCV;
        break;
      case FLAG_RCV:
        if (byte == 0x7E)
          continue;
        if (byte == 0x03) {
          receivedA = byte;
          currentState = A_RCV;
        } else {
          currentState = START;
        }
        break;
      case A_RCV:
        if (byte == RR0 || byte == RR1 || byte == REJ0 || byte == REJ1) {
          receivedC = byte;
          currentState = C_RCV;
        } else if (byte == 0x7E)
          currentState = FLAG_RCV;
        else
          currentState = START;
        break;
      case C_RCV:
        if (byte == (receivedA ^ receivedC))
          currentState = BCC_OK;
        else if (byte == 0x7E)
          currentState = FLAG_RCV;
        else
          currentState = START;
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
      // TODO: reject based on information number (currently using same reject
      // for both)
      if (receivedC == REJ0 || receivedC == REJ1) {
        alarmEnabled = TRUE;
        alarmCount = 0;
        printf("Packet rejected by receiver, trying again...\n");
      }
      if (receivedC == RR0 || receivedC == RR1) {
        disableAlarm();
        return stuffedPacketSize;
      }
    }
    // Verify if the alarm fired
    if (alarmEnabled) {
      alarmEnabled = FALSE;
      if (alarmCount <= parameters.nRetransmissions) {
        printf("Retransmitting packet...\n");
        if (write(fd, stuffedPacket, stuffedPacketSize) < 0) {
          perror("Error writing stuffed packet.\n");
          return -1;
        }
        alarm(parameters.timeout);
      }
      currentState = START;
    }
  }
  disableAlarm();
  return -1;
}

////////////////////////////////////////////////
// LLREAD
////////////////////////////////////////////////
int llread(unsigned char *packet) {
  enum states currentState = START;
  size_t packetIndex = 0;

  unsigned char receivedA = 0;
  unsigned char receivedC = 0;

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
        receivedC = 0;
        receivedA = 0;
        if (byte == 0x7E) {
          printf("received flag\n");
          currentState = FLAG_RCV;
        }
        break;
      case FLAG_RCV:
        if (byte == 0x7E)
          continue;
        if (byte == 0x03) {
          printf("received A: %02x\n", byte);
          receivedA = byte;
          currentState = A_RCV;
        } else
          currentState = START;
        break;
      case A_RCV:
        if (byte == 0x00 || byte == 0x80) {
          printf("received C: %02x\n", byte);
          receivedC = byte;
          currentState = C_RCV;
        } else if (byte == 0x7E)
          currentState = FLAG_RCV;
        else
          currentState = START;
        break;
      case C_RCV:
        if (byte == (receivedC ^ receivedA)) {
          printf("received bcc1: %02x\n", byte);
          currentState = DATA;
        } else if (byte == 0x7E)
          currentState = FLAG_RCV;
        else
          currentState = START;
        break;
      case DATA:
        printf("receiving data...\n");
        if (byte == 0x7E) {
          unsigned char destuffedPacket[packetIndex];
          size_t destuffedPacketSize;
          if (destuffPacket(packet, packetIndex, destuffedPacket,
                            &destuffedPacketSize)) {
            return -1;
          }
          unsigned char receivedBCC2 = destuffedPacket[destuffedPacketSize - 1];
          unsigned char bcc2 = 0;
          for (size_t i = 0; i < destuffedPacketSize - 1; i++) {
            bcc2 ^= destuffedPacket[i];
          }
          unsigned char responseC;
          if (bcc2 == receivedBCC2) {
            // if the current frame is 0, ready to receive 1.
            responseC = (receivedC == 0x00) ? RR1 : RR0;
            printf("BCC2 matches, approving with %02x\n", responseC);
          } else {
            responseC = (receivedC == 0x00) ? REJ0 : REJ1;
            printf("received bcc2: %02x", receivedBCC2);
            printf("calculated bcc2: %02x", bcc2);
            printf("BCC2 doesn't match, rejecting with %02x\n", responseC);
          }
          if (sendControlFrame(0x03, responseC)) {
            return -1;
          }
          return destuffedPacketSize;
        } else {
          packet[packetIndex++] = byte;
        }
        break;
      default:
        currentState = START;
      }
    }
  }

  return -1;
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
