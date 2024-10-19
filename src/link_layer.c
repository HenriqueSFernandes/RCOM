// Link layer protocol implementation

#include "link_layer.h"
#include "serial_port.h"
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

int alarmEnabled;
int alarmCount = 0;
enum states current_state = START;  


void alarmHandler(int signal)
{
  alarmEnabled = FALSE;
  alarmCount++;
  printf("Alarm no:  %d \n", alarmCount);
  if (alarmCount < 4)
  {
    alarm(3);
    printf("Alarm Triggered! Retransmitting...\n"); // Ainda não está a retransmitir
  }
  else
  {
    alarmEnabled = TRUE;
    alarm(0);
    perror("Alarm Triggered more than 3 time! ERROR");
  }
}

////////////////////////////////////////////////
// LLOPEN
////////////////////////////////////////////////
int llopen(LinkLayer connectionParameters)
{
    (void)signal(SIGALRM, alarmHandler);
    alarm(3);

    int fd = openSerialPort(connectionParameters.serialPort,
                            connectionParameters.baudRate);
    if (fd < 0)
    {
        return -1;
    }

    if (connectionParameters.role == LlTx)
    { // Transmitor
        // Create string to send
        unsigned char F = 0x7E;
        unsigned char A = 0x03;
        unsigned char C = 0x03;
        unsigned char BCC1 = A ^ C;

        unsigned char first = 1; //debug

        // ISTO É SE QUISERMOS RECEBER OS 5 DE UMA SÒ VEZ

        // unsigned char buf[FLAG_BUFFER_SIZE] = {F, A, C, BCC1, F};
        // int bytes = write(fd, buf, FLAG_BUFFER_SIZE);
        // printf("%d bytes written\n", bytes);

        // In non-canonical mode, '\n' does not end the writing.
        // Test this condition by placing a '\n' in the middle of the buffer.
        // The whole buffer must be sent even with the '\n'.

        // Agora vou fazer byte by byte stop and wait

        unsigned char buf[1] = {0};
        unsigned char UA[1] = {0};
        unsigned char a_rcv;
        unsigned char c_rcv;

        while (current_state != STOPSTOP && alarmEnabled == FALSE)
        {
            // Mandar bytes
            if (current_state == START)
            {
                buf[0] = F;
                int bytes = write(fd, buf, 1);
                printf("Flag escrita\n");
                printf("%d bytes written\n", bytes);
            }
            if (current_state == FLAG_RCV)
            {
                buf[0] = A;
                int bytes = write(fd, buf, 1);
                printf("'A' escrita\n");
                printf("%d bytes written\n", bytes);
            }
            if (current_state == A_RCV)
            {
                if (first) //debug
                {
                    first = 0;
                    buf[0] = 0x42;
                    int bytes = write(fd, buf, 1);
                    printf("'C' escrita\n");
                    printf("%d bytes written\n", bytes);
                }
                else
                {
                    buf[0] = C;
                    int bytes = write(fd, buf, 1);
                    printf("'C' escrita\n");
                    printf("%d bytes written\n", bytes);
                }
            }
            if (current_state == C_RCV)
            {
                buf[0] = BCC1;
                int bytes = write(fd, buf, 1);
                printf("'BCC1' escrita\n");
                printf("%d bytes written\n", bytes);
            }
            if (current_state == BCC_OK)
            {
                buf[0] = F;
                int bytes = write(fd, buf, 1);
                printf("Flag escrita\n");
                printf("%d bytes written\n", bytes);
            }

            // Verificar se recebeu bem
            read(fd, UA, 1);
            printf("Entered START\n");
            if (UA[0] == 0xFF)
            {
                current_state = FLAG_RCV;
                printf("O rx recebu flag do nada então vamos recomeçar a partir do flag rcv\n");
            }
            else if (UA[0] == 0x00)
            {
                current_state = START;
                printf("O rx recebu lixo então vamos recomeçar a partir do início\n");
            }
            else if (current_state == START && UA[0] == F)
            {
                current_state = FLAG_RCV;
                printf("Flag recebida de volta\n");
            }
            else if (current_state == FLAG_RCV && UA[0] == A)
            {
                current_state = A_RCV;
                a_rcv = UA[0];
                printf("'A' recebida de volta\n");
            }
            else if (current_state == A_RCV && UA[0] == 0x07)
            {
                current_state = C_RCV;
                c_rcv = UA[0];
                printf("'C' recebido UA 0x07\n");
            }
            else if (current_state == C_RCV && UA[0] == (a_rcv ^ c_rcv))
            {
                current_state = BCC_OK;
                printf("'BCC1' recebido de volta\n");
            }
            else if (current_state == BCC_OK && UA[0] == F)
            {
                current_state = STOPSTOP;
                printf("Flag recebida de volta\n");
            }
        }
        if (alarmEnabled == TRUE)
        {
            perror("Time exceeded! Exiting...");
            return 0;
        }
    }
    else
    {
        unsigned char a_rcv;
        unsigned char c_rcv;
        unsigned char F = 0x7E;
        unsigned char A = 0x03;
        unsigned char C = 0x07;
        unsigned char BCC1 = A ^ C;

        // ISTO É SE QUISERMOS RECEBER OS 5 DE UMA SÒ VEZ

        // unsigned char byte[FLAG_BUFFER_SIZE] = {0};
        // read(fd, byte, FLAG_BUFFER_SIZE);
        // printf("Entered START\n");
        // for (int i = 0; i < FLAG_BUFFER_SIZE; i++)
        // {
        //     printf("READ %x\n", byte[i]);
        // }

        // Agora vou fazer byte by byte stop and wait
        unsigned char buf[1] = {0};
        unsigned char byte[1] = {0};

        printf("Entered START\n");
        while (current_state != STOPSTOP && alarmEnabled == FALSE)
        {
            read(fd, byte, 1);
            if (current_state != BCC_OK && current_state != START && byte[0] == 0x7E) // Recebi flag do nada e mando flag para voltar ao início
            {
                current_state = FLAG_RCV;
                buf[0] = 0xFF;
                int bytes = write(fd, buf, 1);
                printf("Flag recebida do nada e mandei 0xFF para voltar ao início\n");
                printf("%d bytes written\n", bytes);
            }
            else if (current_state == START && byte[0] == 0x7E) // Recebi esperado
            {
                current_state = FLAG_RCV;
                buf[0] = F;
                int bytes = write(fd, buf, 1);
                printf("Flag recebida e escrita\n");
                printf("%d bytes written\n", bytes);
            }
            else if (current_state == START && byte[0] != 0x7E)
            { // Recebi lixo
                printf("Received trash!\n");
                buf[0] = 0x00;
                int bytes = write(fd, buf, 1);
                printf("Trash recebida e escrita 0x00\n");
                printf("%d bytes written\n", bytes);
            }
            else if (current_state == FLAG_RCV && byte[0] == 0x03) // (byte[0] == 0x03 || byte[0] == 0x01) isto ainda faz sentido? Recebi esperado
            {
                current_state = A_RCV;
                a_rcv = byte[0];
                buf[0] = A;
                int bytes = write(fd, buf, 1);
                printf("'A' recebida e escrita\n");
                printf("%d bytes written\n", bytes);
            }
            else if (current_state == FLAG_RCV && byte[0] != 0x03) // Recebi lixo
            {
                current_state = START;
                printf("Received trash!\n");
                buf[0] = 0x00;
                int bytes = write(fd, buf, 1);
                printf("Trash recebida e escrita 0x00\n");
                printf("%d bytes written\n", bytes);
            }
            else if (current_state == A_RCV && byte[0] == 0x03) // Recebi esperado
            {
                current_state = C_RCV;
                c_rcv = byte[0];
                buf[0] = C;
                int bytes = write(fd, buf, 1);
                printf("'C' recebida e escrita 0x07\n");
                printf("%d bytes written\n", bytes);
            }
            else if (current_state == A_RCV && byte[0] != 0x03) // Recebi lixo
            {
                current_state = START;
                printf("Received trash!\n");
                buf[0] = 0x00;
                int bytes = write(fd, buf, 1);
                printf("Trash recebida e escrita 0x00\n");
                printf("%d bytes written\n", bytes);
            }
            else if (current_state == C_RCV && byte[0] == (a_rcv ^ c_rcv)) // Recebi esperado
            {
                current_state = BCC_OK;
                buf[0] = BCC1;
                int bytes = write(fd, buf, 1);
                printf("'BCC1' recebida e escrita\n");
                printf("%d bytes written\n", bytes);
            }
            else if (current_state == C_RCV && byte[0] != (a_rcv ^ c_rcv)) // Recebi lixo
            {
                current_state = START;
                printf("Received trash!\n");
                buf[0] = 0x00;
                int bytes = write(fd, buf, 1);
                printf("Trash recebida e escrita 0x00\n");
                printf("%d bytes written\n", bytes);
            }
            else if (current_state == BCC_OK && byte[0] == 0x7E)
            {
                current_state = STOPSTOP;
                buf[0] = F;
                int bytes = write(fd, buf, 1);
                printf("Flag recebida e escrita\n");
                printf("%d bytes written\n", bytes);
            }
            else if (current_state == BCC_OK && byte[0] != 0x7E) // Recebi lixo
            {
                current_state = START;
                printf("Received trash!\n");
                buf[0] = 0x00;
                int bytes = write(fd, buf, 1);
                printf("Trash recebida e escrita 0x00\n");
                printf("%d bytes written\n", bytes);
            }
        }
    }

    return 1;
}

////////////////////////////////////////////////
// LLWRITE
////////////////////////////////////////////////
int llwrite(const unsigned char *buf, int bufSize)
{
    // TODO

    return 0;
}

////////////////////////////////////////////////
// LLREAD
////////////////////////////////////////////////
int llread(unsigned char *packet)
{
    // TODO

    return 0;
}

////////////////////////////////////////////////
// LLCLOSE
////////////////////////////////////////////////
int llclose(int showStatistics)
{
    // TODO

    int clstat = closeSerialPort();
    return clstat;
}
