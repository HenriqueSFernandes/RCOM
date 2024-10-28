// Application layer protocol implementation

#include "../include/application_layer.h"
#include "../include/link_layer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int hexdump(const char *filename) {
  char command[256];
  snprintf(command, sizeof(command), "hexdump -C %s", filename);
  int result = system(command);

  if (result == -1) {
    perror("Error executing hexdump");
    return 1;
  }
  return 0;
}

int sendControlPacket(const char *filename, unsigned char controlValue,
                      size_t fileSize) {
  if (filename == NULL) {
    return 1;
  }

  unsigned char L1 = sizeof(fileSize);
  unsigned char L2 = strlen(filename);

  unsigned char packet[5 + L1 + L2];
  packet[0] = controlValue; // 1 for start, 3 for end
  packet[1] = 0;            // 0 for file size
  packet[2] = L1;           // size of file size

  // extract individual bytes from the file size, little-endian (least
  // significant byte is stored first)
  for (int i = 0; i < L1; i++) {
    packet[3 + i] = (fileSize >> (8 * i)) & 0xFF;
  }

  packet[3 + L1] = 1;                    // 1 for file name
  packet[4 + L1] = L2;                   // size of file name
  memcpy(&packet[5 + L1], filename, L2); // filename

  return llwrite(packet, 5 + L1 + L2);
}

int sendDataPacket(unsigned char *data, size_t dataSize, int sequenceNumber) {
  if (data == NULL) {
    return 1;
  }

  unsigned char packet[dataSize + 4];

  packet[0] = 2; // Control field 2 (data)
  packet[1] = sequenceNumber;
  packet[2] = (dataSize >> 8) & 0xFF;
  packet[3] = dataSize & 0xFF;
  memcpy(packet + 4, data, dataSize);

  return llwrite(packet, dataSize + 4);
}

void applicationLayer(const char *serialPort, const char *role, int baudRate,
                      int nTries, int timeout, const char *filename) {
  // Initialize link layer.
  LinkLayer linkLayer;
  strcpy(linkLayer.serialPort, serialPort);
  linkLayer.baudRate = baudRate;
  linkLayer.nRetransmissions = nTries;
  linkLayer.timeout = timeout;
  linkLayer.role = (!strcmp(role, "tx")) ? LlTx : LlRx;

  // Open serial connection
  if (llopen(linkLayer)) {
    perror("Error opening link layer.\n");
    if (llclose(FALSE)) {
      perror("Error closing link layer.\n");
    };
    return;
  };

  // Transmitter
  if (linkLayer.role == LlTx) {

    // Open the file
    FILE *fptr = fopen(filename, "r");
    if (fptr == NULL) {
      perror("File not found.\n");
      fclose(fptr);
      return;
    }

    unsigned char buffer[1000];
    size_t bytesRead;

    fseek(fptr, 0, SEEK_END);
    size_t fileSize = ftell(fptr);
    rewind(fptr);

    if (sendControlPacket(filename, 1, fileSize)) {
      perror("Error sending the start control packet.\n");
      fclose(fptr);
      llclose(FALSE);
      return;
    }

    int sequenceNumber = 0;
    while ((bytesRead = fread(buffer, 1, 1000, fptr)) > 0) {
      if (sendDataPacket(buffer, bytesRead, sequenceNumber)) {
        perror("Error sending data packet");
        fclose(fptr);
        llclose(FALSE);
        return;
      }
    }

    if (sendControlPacket(filename, 3, fileSize)) {
      perror("Error sending the end control packet.\n");
      fclose(fptr);
      llclose(FALSE);
      return;
    }

    fclose(fptr);
    llclose(FALSE);
    return;
  }

  if (linkLayer.role == LlRx) {
    FILE *fptr = fopen(filename, "wb");

    if (fptr == NULL) {
      perror("Error opening file.\n");
      fclose(fptr);
      llclose(FALSE);
      return;
    }
    llclose(FALSE);

    // TODO: finished read code (need llread first).
    //
  }
}
