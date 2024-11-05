// Application layer protocol implementation

#include "../include/application_layer.h"
#include "../include/link_layer.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAXFILENAMESIZE 256

typedef struct {
  size_t fileSize;
  char filename[MAXFILENAMESIZE];
} FileMetadata;

FileMetadata fileMetadata = {0, ""};

/**
 * @brief Generates a hexdump of the specified file.
 *
 * This function constructs a command to generate a hexdump of the file
 * specified by the filename parameter and executes it using the system
 * function. The hexdump command used is "hexdump -C <filename>".
 * Useful for debugging purposes.
 *
 * @param filename The path to the file to be hexdumped.
 * @return Returns 0 on success, or 1 if an error occurs while executing the
 * hexdump command.
 */
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

/**
 * @brief Sends a control packet containing the file size and filename.
 *
 * This function constructs a control packet with the specified control value,
 * file size, and filename, and sends it using the llwrite function.
 *
 * @param filename The name of the file to be included in the control packet.
 * @param controlValue The control value indicating the type of control packet.
 *                     Typically, 1 for start and 3 for end.
 * @param fileSize The size of the file to be included in the control packet.
 * @return int Returns 0 on success, or -1 if the filename is NULL.
 */
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

/**
 * @brief Sends a data packet.
 *
 * This function constructs a data packet with the given data and sequence
 * number, and sends it using the `llwrite` function. The packet format is as
 * follows:
 * - Byte 0: Control field (2 for data)
 * - Byte 1: Sequence number
 * - Byte 2: Data size (most significant byte)
 * - Byte 3: Data size (least significant byte)
 * - Bytes 4 to (dataSize + 3): Data
 *
 * @param data Pointer to the data to be sent.
 * @param dataSize Size of the data to be sent.
 * @param sequenceNumber Sequence number of the packet.
 * @return 0 on success, -1 if the data pointer is NULL, or the return value of
 * `llwrite`.
 */
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

#ifdef DEBUG
  printf("Data packet with size %zu:\n", dataSize + 4);
  for (size_t i = 0; i < dataSize + 4; i++) {
    printf("%02x ", packet[i]);
  }
  printf("\n");
#endif

  return llwrite(packet, dataSize + 4);
}

/**
 * @brief Receives and parses a start control packet to extract file metadata.
 *
 * This function processes a start control packet to extract the file size and
 * file name, storing them in the provided metadata structure. The packet is
 * expected to follow a specific format:
 * - The first byte should be 1, indicating a start control packet.
 * - The second byte should be 0, indicating the file size parameter.
 * - The third byte indicates the size (in octets) of the file size value.
 * - The next bytes contain the file size value.
 * - The byte following the file size should be 1, indicating the file name
 * parameter.
 * - The next byte indicates the size of the file name.
 * - The remaining bytes contain the file name.
 *
 * @param packet Pointer to the start control packet.
 * @param metadata Pointer to the FileMetadata structure where the extracted
 *                 file size and file name will be stored.
 * @return int Returns 0 on success, or 1 on error (e.g., invalid packet format,
 *             filename too big).
 */
int receiveStartControlPacket(const unsigned char *packet,
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
  if (filenameSize + 1 > MAXFILENAMESIZE) {
    perror("Filename too big.\n");
    return 1;
  }
  for (int i = 0; i < filenameSize; i++) {
    metadata->filename[i] = packet[filesizeSize + 5 + i];
  }
  metadata->filename[filenameSize] = '\0';

  return 0;
}

/**
 * @brief Receives and validates the end control packet.
 *
 * This function checks the integrity of the end control packet by verifying
 * the packet type, file size, and file name against the provided metadata.
 *
 * @param packet Pointer to the end control packet.
 * @param metadata Pointer to the FileMetadata structure containing the expected
 * file size and file name.
 * @return int Returns 0 if the packet is valid, otherwise returns 1.
 */
int receiveEndControlPacket(const unsigned char *packet,
                            const FileMetadata *metadata) {
  if (packet == NULL || packet[0] != 3) {
    return 1;
  }

  if (packet[1] != 0) {
    return 1;
  }

  unsigned char filesizeSize =
      packet[2]; // size in octects of the file size value.

  size_t fileSize = 0;
  for (size_t i = 0; i < filesizeSize; i++) {
    fileSize |= (packet[i + 3]) << (i * 8);
  }
  if (metadata->fileSize != fileSize) {
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
  if (strcmp(metadata->filename, filename) != 0) {
    perror("Error: start packet filename doesn't match end packet filename.\n");
    return 1;
  }
  return 0;
}

/**
 * @brief Receives a data packet and validates its contents.
 *
 * This function checks if the provided packet is valid, verifies the sequence
 * number, and extracts the packet size. If the packet is valid, the size of the
 * packet is stored in the provided packetSize pointer.
 *
 * @param packet Pointer to the data packet.
 * @param packetSize Pointer to an integer where the size of the packet will be
 * stored.
 * @param expectedSequenceNumber The expected sequence number of the packet.
 * @return 0 if the packet is valid and the size is successfully extracted, 1
 * otherwise.
 */
int receiveDataPacket(unsigned char *packet, int *packetSize,
                      int expectedSequenceNumber) {

  if (packet == NULL || packetSize == NULL || packet[0] != 2) {
    return 1;
  }

  if (packet[1] != expectedSequenceNumber) {
    printf(
        "The data packet received contains an unexpected sequence number.\n");
    return 1;
  }

  *packetSize = packet[2] * 256 + packet[3];

  packet += 4;

#ifdef DEBUG
  printf("Data packet with size %d\n", *packetSize);
  for (int i = 0; i < *packetSize; i++) {
    printf("%02x ", packet[i]);
  }
  printf("\n");
#endif

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

    if (sendControlPacket(filename, 3, fileSize) < 0) {
      perror("Error sending the end control packet.\n");
      fclose(fptr);
      llclose(FALSE);
      return;
    }

    fclose(fptr);
    llclose(TRUE);
    return;
  }

  if (linkLayer.role == LlRx) {

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
    int sequenceNumber = 0;

    while (receiving) {
      int bytesRead = llread(packet);
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
        if (receiveStartControlPacket(packet, &fileMetadata)) {
          perror("Error reading start control packet.\n");
          fclose(fptr);
          llclose(FALSE);
          return;
        }

        printf("Metadata received:\n\tfilename: %s\n\tsize: %zu bytes\n",
               fileMetadata.filename, fileMetadata.fileSize);
      } else if (packet[0] == 3) {
        // End control packet
        if (receiveEndControlPacket(packet, &fileMetadata)) {
          perror("Error reading end control packet.\n");
          fclose(fptr);
          llclose(FALSE);
          return;
        }
        printf("End control packet received, file metadata matches.\n");
        receiving = 0;

      } else if (packet[0] == 2) {
        // Data packet
        if (receiveDataPacket(packet, &bytesRead, sequenceNumber++)) {
          perror("Error reading data packet.\n");
          fclose(fptr);
          llclose(FALSE);
          return;
        }

#ifdef DEBUG
        printf("Data packet with size %d\n", bytesRead);
        for (int i = 0; i < bytesRead + 4; i++) {
          printf("%02x ", packet[i]);
        }
        printf("\n");
#endif
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
