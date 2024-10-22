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

int allRead = FALSE;
int setupFirst = TRUE;
int alarmEnabled;
int alarmCount = 0;
enum states current_state = START;
LinkLayer parameters; 


void alarmHandler(int signal)
{
  alarmEnabled = FALSE;
  alarmCount++;
  printf("Alarm no:  %d \n", alarmCount);
  if (alarmCount < parameters.nRetransmissions)
  {
    alarm(parameters.timeout);
    printf("Alarm Triggered! Retransmitting...\n"); // Ainda não está a retransmitir
    llopen(parameters);
  }
  else
  {
    alarmEnabled = TRUE;
    alarm(0);
    char error_message[100];
    snprintf(error_message, sizeof(error_message), "Alarm Triggered more than %d times! ERROR", parameters.nRetransmissions);
    perror(error_message);
  }
}
void resetAlarm()
{
  alarmCount = 0;
  alarm(parameters.timeout);
}

////////////////////////////////////////////////
// LLOPEN
////////////////////////////////////////////////
int llopen(LinkLayer connectionParameters)
{
    parameters = connectionParameters;
    (void)signal(SIGALRM, alarmHandler);

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
        //unsigned char first = 1; //Para experimentar o reenvio DEBUG
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
            else  if (current_state == FLAG_RCV)
            {
                buf[0] = A;
                int bytes = write(fd, buf, 1);
                printf("'A' escrita\n");
                printf("%d bytes written\n", bytes);
            }
            else if (current_state == A_RCV)
            {
                // if (first) //debug
                // {
                //     first = 0;
                //     buf[0] = 0x42;
                //     int bytes = write(fd, buf, 1);
                //     printf("'C' escrita\n");
                //     printf("%d bytes written\n", bytes);
                // }
                // else
                // {
                    buf[0] = C;
                    int bytes = write(fd, buf, 1);
                    printf("'C' escrita\n");
                    printf("%d bytes written\n", bytes);
                // }
            }
            else if (current_state == C_RCV)
            {
                buf[0] = BCC1;
                int bytes = write(fd, buf, 1);
                printf("'BCC1' escrita\n");
                printf("%d bytes written\n", bytes);
            }
            else if (current_state == BCC_OK)
            {
                buf[0] = F;
                int bytes = write(fd, buf, 1);
                printf("Flag escrita\n");
                printf("%d bytes written\n", bytes);
            }

            //Já mandei o byte agora vou ler
            resetAlarm();
            allRead = FALSE;
            UA[0] = 0x00;

            //Fazer while para nao recomeçar sem antes receber STOP & WAIT
            while (!allRead && alarmEnabled == FALSE){
                
                // Verificar se recebeu bem
                read(fd, UA, 1);

                
                if (UA[0] != 0x00) {
                    alarm(0);
                    allRead = TRUE;
                }
                if (UA[0] == 0x00) continue;
                else if (UA[0] == 0xFF)
                {
                    current_state = FLAG_RCV;
                    printf("O rx recebu flag do nada então vamos recomeçar a partir do flag rcv\n");
                    // sleep(1);
                }
                else if (UA[0] == 0xFE)
                {
                    current_state = START;
                    printf("O rx recebu lixo então vamos recomeçar a partir do início\n");
                    // sleep(1);
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
            alarm(0);
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
        unsigned char buf[1] = {0}; //byte para enviar/escrever
        unsigned char byte[1] = {0}; //byte lido

        printf("Entered START\n");
        while (current_state != STOPSTOP && alarmEnabled == FALSE)
        {
            read(fd, byte, 1);
            if (byte[0] != 0x00) {
                alarm(0);
            }
            if (byte[0] == 0x00 && current_state != C_RCV) continue; //Se nao recebeu nada volta a ler
            else if (current_state != BCC_OK && current_state != START && byte[0] == 0x7E) // Recebi flag do nada e mando flag para voltar ao início
            {
                current_state = FLAG_RCV;
                buf[0] = 0xFF;
                int bytes = write(fd, buf, 1);
                resetAlarm();
                byte[0] = 0x00;
                printf("Flag recebida do nada e mandei 0xFF para voltar ao início\n");
                printf("%d bytes written\n", bytes);
            }
            else if (current_state == START && byte[0] == 0x7E) // Recebi esperado
            {
                current_state = FLAG_RCV;
                buf[0] = F;
                int bytes = write(fd, buf, 1);
                resetAlarm();
                byte[0] = 0x00;
                printf("Flag recebida e escrita\n");
                printf("%d bytes written\n", bytes);
            }
            else if (current_state == START && byte[0] != 0x7E)
            { // Recebi lixo
                printf("Received trash!\n");
                buf[0] = 0xFE;
                int bytes = write(fd, buf, 1);
                resetAlarm();
                byte[0] = 0x00;
                printf("Trash recebida e escrita 0xFE\n");
                printf("%d bytes written\n", bytes);
            }
            else if (current_state == FLAG_RCV && byte[0] == 0x03) // (byte[0] == 0x03 || byte[0] == 0x01) isto ainda faz sentido? Recebi esperado
            {
                current_state = A_RCV;
                a_rcv = byte[0];
                buf[0] = A;
                int bytes = write(fd, buf, 1);
                resetAlarm();
                byte[0] = 0x00;
                printf("'A' recebida e escrita\n");
                printf("%d bytes written\n", bytes);
            }
            else if (current_state == FLAG_RCV && byte[0] != 0x03) // Recebi lixo
            {
                current_state = START;
                printf("Received trash!\n");
                buf[0] = 0xFE;
                int bytes = write(fd, buf, 1);
                resetAlarm();
                byte[0] = 0x00;
                printf("Trash recebida e escrita 0xFE\n");
                printf("%d bytes written\n", bytes);
            }
            else if (current_state == A_RCV && byte[0] == 0x03) // Recebi esperado
            {
                current_state = C_RCV;
                c_rcv = byte[0];
                buf[0] = C;
                int bytes = write(fd, buf, 1);
                resetAlarm();
                byte[0] = 0x00;
                printf("'C' recebida e escrita 0x07\n");
                printf("%d bytes written\n", bytes);
            }
            else if (current_state == A_RCV && byte[0] != 0x03) // Recebi lixo
            {
                current_state = START;
                printf("Received trash!\n");
                buf[0] = 0xFE;
                int bytes = write(fd, buf, 1);
                resetAlarm();
                byte[0] = 0x00;
                printf("Trash recebida e escrita 0xFE\n");
                printf("%d bytes written\n", bytes);
            }
            else if (current_state == C_RCV && byte[0] == (a_rcv ^ c_rcv)) // Recebi esperado
            {
                current_state = BCC_OK;
                buf[0] = BCC1;
                int bytes = write(fd, buf, 1);
                resetAlarm();
                byte[0] = 0x00;
                printf("'BCC1' recebida e escrita\n");
                printf("%d bytes written\n", bytes);
            }
            else if (current_state == C_RCV && byte[0] != (a_rcv ^ c_rcv)) // Recebi lixo
            {
                current_state = START;
                printf("Received trash!\n");
                buf[0] = 0xFE;
                int bytes = write(fd, buf, 1);
                resetAlarm();
                byte[0] = 0x00;
                printf("Trash recebida e escrita 0xFE\n");
                printf("%d bytes written\n", bytes);
            }
            else if (current_state == BCC_OK && byte[0] == 0x7E)
            {
                current_state = STOPSTOP;
                buf[0] = F;
                int bytes = write(fd, buf, 1);
                resetAlarm();
                byte[0] = 0x00;
                printf("Flag recebida e escrita\n");
                printf("%d bytes written\n", bytes);
            }
            else if (current_state == BCC_OK && byte[0] != 0x7E) // Recebi lixo
            {
                current_state = START;
                printf("Received trash!\n");
                buf[0] = 0xFE;
                int bytes = write(fd, buf, 1);
                resetAlarm();
                byte[0] = 0x00;
                printf("Trash recebida e escrita 0xFE\n");
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
