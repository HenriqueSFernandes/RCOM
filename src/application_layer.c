// Application layer protocol implementation

#include "../include/application_layer.h"
#include "../include/link_layer.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
  size_t fileSize;
  char *filename;
} FileMetadata;

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
    return -1;
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
    return -1;
  }

  unsigned char packet[dataSize + 4];

  packet[0] = 2; // Control field 2 (data)
  packet[1] = sequenceNumber;
  packet[2] = (dataSize >> 8) & 0xFF;
  packet[3] = dataSize & 0xFF;
  memcpy(packet + 4, data, dataSize);

  printf("Data packet with size %zu:\n", dataSize + 4);
  for (int i = 0; i < dataSize + 4; i++) {
    printf("%02x ", packet[i]);
  }
  printf("\n");

  return llwrite(packet, dataSize + 4);
}

int receiveStartControlPacket(const unsigned char *packet, size_t packetSize,
                              FileMetadata *metadata) {
  if (packet == NULL || packet[0] != 1) {
    return 1;
  }

  // If the first parameter isn't the file size, return error.
  if (packet[1] != 0) {
    return 1;
  }

  unsigned char filesizeSize =
      packet[2]; // size in octects of the file size value.

  int fileSize = 0;
  for (int i = 0; i < filesizeSize; i++) {
    fileSize |= (packet[i + 3]) << (i * 8);
  }
  metadata->fileSize = fileSize;

  // If the second parameter isn't the file name, return error.
  if (packet[filesizeSize + 3] != 1) {
    return 1;
  }

  unsigned char filenameSize = packet[filesizeSize + 4];
  char filename[filenameSize + 1];
  for (int i = 0; i < filenameSize; i++) {
    filename[i] = packet[filesizeSize + 5 + i];
  }
  filename[filenameSize] = '\0';
  metadata->filename = filename;
  return 0;
}

int receiveEndControlPacket(const unsigned char *packet, size_t packetSize,
                            FileMetadata metadata) {
  if (packet == NULL || packet[0] != 3) {
    return 1;
  }

  if (packet[1] != 0) {
    return 1;
  }

  unsigned char filesizeSize =
      packet[2]; // size in octects of the file size value.

  int fileSize = 0;
  for (int i = 0; i < filesizeSize; i++) {
    fileSize |= (packet[i + 3]) << (i * 8);
  }
  if (metadata.fileSize != fileSize) {
    perror("Error: start packet filesize doesn't match end packet filesize.\n");
    return 1;
  }

  // If the second parameter isn't the file name, return error.
  if (packet[filesizeSize + 3] != 1) {
    return 1;
  }

  unsigned char filenameSize = packet[filesizeSize + 4];
  char filename[filenameSize + 1];
  for (int i = 0; i < filenameSize; i++) {
    filename[i] = packet[filesizeSize + 5 + i];
  }
  filename[filenameSize] = '\0';
  if (metadata.filename != filename) {
    perror("Error: start packet filename doesn't match end packet filename.\n");
    return 1;
  }
  return 0;
}

int receiveDataPacket(unsigned char *packet, size_t *packetSize) {

  if (packet == NULL || packetSize == NULL || packet[0] != 2) {
    return 1;
  }
  *packetSize = packet[2] * 256 + packet[3];

  packet += 4;
  printf("Data packet with size %zu\n", *packetSize);

  for (size_t i = 0; i < *packetSize; i++) {
    printf("%02x ", packet[i]);
  }
  printf("\n");

  return 0;
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

    if (sendControlPacket(filename, 1, fileSize) < 0) {
      perror("Error sending the start control packet.\n");
      fclose(fptr);
      llclose(FALSE);
      return;
    }

    int sequenceNumber = 0;
    while ((bytesRead = fread(buffer, 1, 1000, fptr)) > 0) {
      if (sendDataPacket(buffer, bytesRead, sequenceNumber++) < 0) {
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
    FileMetadata fileMetadata = {0, ""};

    unsigned char packet[1020]; // the data packet should be at most 1000, but
                                // lets add 20 to be sure.

    FILE *fptr = fopen(filename, "wb");

    if (fptr == NULL) {
      perror("Error opening file.\n");
      fclose(fptr);
      llclose(FALSE);
      return;
    }

    int receiving = 1;

    while (receiving) {
      size_t bytesRead = llread(packet);
      if (bytesRead == 0) {
        continue;
      }
      if (bytesRead < 0) {
        perror("Failed to read from packet from link layer.\n");
        fclose(fptr);
        llclose(FALSE);
        return;
      }

      if (packet[0] == 1) {
        // Start control packet
        if (receiveStartControlPacket(packet, bytesRead, &fileMetadata)) {
          perror("Error reading start control packet.\n");
          fclose(fptr);
          llclose(FALSE);
          return;
        }

        printf("name: %s, size: %zu\n", fileMetadata.filename,
               fileMetadata.fileSize);
      } else if (packet[0] == 3) {
        // End control packet
        if (receiveEndControlPacket(packet, bytesRead, fileMetadata)) {
          perror("Error reading end control packet.\n");
          fclose(fptr);
          llclose(FALSE);
          return;
        }

      } else if (packet[0] == 2) {
        // Data packet

        if (receiveDataPacket(packet, &bytesRead)) {
          perror("Error reading data packet.\n");
          fclose(fptr);
          llclose(FALSE);
          return;
        }
        printf("aaaaaData packet with size %zu\n", bytesRead);

        for (size_t i = 0; i < bytesRead + 4; i++) {
          printf("%02x ", packet[i]);
        }
        printf("\n");
        fwrite(packet + 4, 1, bytesRead, fptr);
      }
    }
    fclose(fptr);
  }

  if (llclose(TRUE) == -1) {
    perror("Error closing connection.\n");
    return;
  }
}
