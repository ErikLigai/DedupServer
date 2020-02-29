#define _GNU_SOURCE

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <sys/sendfile.h>
#include <sys/types.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include "server_util.h"

#define MAX_SIZE 50
#define MAX_BUFFER_SIZE BUFSIZ
#define NUM_CLIENT 1

void *connection_handler(void *socket_desc);

int main(int argc, char* argv[]) {
    int sock_desc;
    struct sockaddr_in serv_addr;
    char sbuff[MAX_BUFFER_SIZE], rbuff[MAX_BUFFER_SIZE], filebuff[MAX_BUFFER_SIZE], strbuff[MAX_BUFFER_SIZE];
    int clientPort;
    char* address;

    if (argc != 3) {
        fprintf(stderr, "Error: Not enough arguements specified!\n");
        exit(1);
    }

    /** Setup the arguments */
    address = (char*) argv[1];
    sscanf(argv[2], "%d", &clientPort);

    /** Failed to create socket; exit */
    if ((sock_desc = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        fprintf(stderr, "Error: Failed to create socket\n");
        exit(1);
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr(address);
    serv_addr.sin_port = htons(clientPort);

    if (connect(sock_desc, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
        printf("Failed to connect to server\n");
    }

    /** Get the client request */
    while (1) {
        /** Clear all the buffers here */
        uint32_t fileSize = 0;
        memset(sbuff, 0, sizeof(sbuff));
        memset(rbuff, 0, sizeof(rbuff));
        memset(strbuff, 0, sizeof(strbuff));
        memset(filebuff, 0, sizeof(filebuff));
        memset(strbuff, 0, sizeof(strbuff));


        /** Flush and get input from user */
        fflush(stdin);
        fflush(stdout);
        
        if (fgets(strbuff, MAX_BUFFER_SIZE, stdin) == NULL) {
            fprintf(stdout, "CERROR could not read from stdin\n");
            exit(1);
        }

        /** Parses user input */
        char* parsedCommand = strtok(strbuff, " ");

        /** Command is the first character that the program reads in */
        unsigned char command = encodeChar((char) parsedCommand[0]);
        sbuff[0] = command;

        /** CLIENT LIST LOGIC */
        if (command == 0x00) {
            /** Send the command */
            send(sock_desc, sbuff, 1, 0);
            
            /** Receive server response - puts the process to sleep */
            recv(sock_desc, rbuff, sizeof(rbuff), 0);
            
            /**
             * We use a buffer to keep the response of the server,
             * so we can print it at the end.
             */
            int messageSize = (int) sizeof(rbuff);
            char successMessage[messageSize];
            strcpy(successMessage, rbuff);

            /** Successful response from the server */
            if (rbuff[0] == (char) 0x01) {
                
                /** Decode the file size */
                unsigned short fileBytes = 0;
                fileBytes = (fileBytes | rbuff[1]) << 8;
                fileBytes = (fileBytes | rbuff[2]);

                fflush(stdout);
                memset(rbuff, 0, sizeof(rbuff));

                /** Keep receiving filenames */
                while (fileBytes > 0) {
                    memset(rbuff, 0, sizeof(rbuff));
                    int fileBytesRead = recv(sock_desc, rbuff, sizeof(rbuff), 0);
                    fflush(stdout);


                    /** 
                     * Here, we check for null terminated strings, since
                     * we can't possibly know whether a data packet was
                     * sent in chunks, or sent in a massive packet.
                     */         
                    int index = 0;
                    while (index != fileBytesRead) {
                        if (index == 0) {
                            fprintf(stdout, "OK+ ");
                        }

                        if (rbuff[index] == '\0') {
                            fprintf(stdout, "\n");
                            if (index < fileBytesRead - 1) {
                                fprintf(stdout, "OK+ ");
                            }
                            
                        } else {
                            fprintf(stdout, "%c", rbuff[index]);
                        }

                        ++index;
                    }

                    fflush(stdout);
                    /** Decrement the amount of file bytes expecting to be read */
                    fileBytes = fileBytes - fileBytesRead;
                }
            }

            /** Finish by interpreting whether we got successful output */
            interpretResponse(successMessage);

        /** CLIENT UPLOAD LOGIC */
        } else if (command == 0x02)  {
            
            char* filename = strtok(NULL, " ");
        

            /** Client didn't specify filename */
            if (filename == NULL) {
                fprintf(stdout, "CERROR no file name specified\n");

            } else {
                filename[strcspn(filename, "\n")] = 0;

                /** Here, we map the filename to send to the server as a request */
                int i = 0;
                while (filename[i] != '\0') {
                    sbuff[i + 1] = filename[i];
                    ++i;
                }

                sbuff[i + 1] = '\0';

                /** Open the file to get the size of the file to send */
                FILE *fp = fopen(filename, "r");

                /** Client error - File not found in local directory */
                if (fp == NULL) {
                    fprintf(stdout, "CERROR file not found in local directory\n");
                    continue;
                }

                fseek(fp, 0L, SEEK_END);
                fileSize = ftell(fp);
                fclose(fp);

                /** Map the file's size to a 4 byte value (uint32_t) */
                sbuff[i + 2] = (fileSize >> 24) & 0xFF;
                sbuff[i + 3] = (fileSize >> 16) & 0xFF;
                sbuff[i + 4] = (fileSize >> 8) & 0xFF;
                sbuff[i + 5] = (fileSize) & 0xFF;

                /** Send upload request */
                send(sock_desc, sbuff, (i + 6) * sizeof(char), 0);

                /** Start uploading file contents here */
                FILE *file = fopen(filename, "r");
                if (file == NULL) {
                    fprintf(stdout, "CERROR file could not be opened in local directory\n");
                }

                /** Clear the file buffer */
                memset(filebuff, 0, MAX_BUFFER_SIZE * sizeof(char));

                /** Get FD of file */
                int fileFd = fileno(file);

                /** Continuosly send chunks of the file to a buffer, and then put
                 *  the contents of the buffer into the outgoing socket */
                while(1) {
                    /** Read from file and store inside a buffer */
                    int bytes_remaining = read(fileFd, filebuff, MAX_BUFFER_SIZE * sizeof(char));

                    /* No more bytes to read from the file */
                    if (bytes_remaining == 0) { break; }

                    /** Keeps track of where we are in the buffer */
                    void *p = &filebuff;

                    /** Write the file to the socket in chunks */
                    while (bytes_remaining > 0) {

                        int bytes_written = send(sock_desc, p, bytes_remaining, 0);
                        if (bytes_written <= 0) {
                            fprintf(stdout, "CERROR error in uploading file\n");
                        }

                        /** Decrement amount of bytes remaining from the read */
                        bytes_remaining -= bytes_written;
                        p += bytes_written;
                    }
                }

                fclose(fp);

                /** Client gets put to sleep if recv gets nothing*/
                memset(rbuff, 0, sizeof(rbuff));
                recv(sock_desc, rbuff, sizeof(rbuff), 0);
                interpretResponse(rbuff);
            }

        /** Send delete request */
        } else if (command == 0x04) {
            /** Client sends: | 0x06 | filename | \0 */

            /** Get filename */
            char* filename = strtok(NULL, " ");
            filename[strcspn(filename, "\n")] = 0;

            if (filename == NULL) {
                fprintf(stdout, "CERROR no file name specified!\n");


            } else {
                /** Encode filename to send */
                int i = 0;
                while (filename[i] != '\0') {
                    sbuff[i+1] = filename[i];
                    ++i;
                }
                sbuff[i + 1] = '\0';
                
                send(sock_desc, sbuff, sizeof(sbuff), 0);
            }

            recv(sock_desc, rbuff, sizeof(rbuff), 0);

            interpretResponse(rbuff);


        /** CLIENT DOWNLOAD LOGIC */
        } else if (command == 0x06) {

            /** Get filename */
            char* filename = strtok(NULL, " ");
            filename[strcspn(filename, "\n")] = 0;

            if (filename == NULL) {
                fprintf(stdout, "CERROR no file name specified\n");

            } else {
                
                /** Encode filename to send */
                int i = 0;
                while (filename[i] != '\0') {
                    sbuff[i+1] = filename[i];
                    ++i;
                }

                sbuff[i + 1] = '\0';

                /** Send the request */
                send(sock_desc, sbuff, (i + 1) * sizeof(char), 0);

                /**
                 * Wait for a response from the server - this 
                 * effectively sleeps the process until a packet
                 * is received from the server. This could be the form
                 * of a successful response, or an error with a message
                 * from the server.
                 */
                memset(rbuff, 0, sizeof(rbuff));
                recv(sock_desc, rbuff, sizeof(rbuff), 0);
                interpretResponse(rbuff);

                /** 
                 * Successful response from the server,
                 * so we choose to parse the file size given
                 * and call recv.
                 */
                if (rbuff[0] == (char) 0x07) {
                    unsigned char fileSizeBuffer[4];
                    fileSizeBuffer[0] = rbuff[1];
                    fileSizeBuffer[1] = rbuff[2];
                    fileSizeBuffer[2] = rbuff[3];
                    fileSizeBuffer[3] = rbuff[4];

                    int fileSize = FourByteToLU(fileSizeBuffer);

                    /** Start reading contents of the file */
                    if (fileSize == 0) { break; }
                    if (fileSize < 0) { fprintf(stderr, "Error: File read error!\n"); }

                    FILE *fp = fopen(filename, "w");
                    while (fileSize > 0) {
                        int bytes_read = recv(sock_desc, rbuff, sizeof(rbuff), 0);
                        fwrite(rbuff, bytes_read, 1, fp);
                        fileSize -= bytes_read;
                    }

                    fclose(fp);
                }

            }

        /** CLIENT QUIT LOGIC */
        } else if (command == 0x08) {
            /** Tell the server that the client wants to quit */
            send(sock_desc, sbuff, 1, 0);

            /** Wait for the server's response */
            recv(sock_desc, rbuff, 1, 0);

            interpretResponse(rbuff);
            goto close;
        }

        sleep(1);
    }

/** Label for when the client wants to abruptly close his connection to a socket */
close:
    close(sock_desc);
    return 0;
}