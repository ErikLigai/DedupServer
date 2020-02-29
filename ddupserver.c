#define _XOPEN_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <dirent.h>
#include <sys/stat.h>
#include <dirent.h>
#include <signal.h>
#include <stdbool.h>
#include <libxml/xmlwriter.h>
#include <openssl/md5.h>
#include "server_util.h"

#define MAX_BUFFER_SIZE BUFSIZ
#define N BUFSIZ
#define MAX_CLIENT 50

#ifndef MAX_INPUT
#define MAX_INPUT 255
#endif

/** Synchronization locks and condition variables */
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t xml_lock = PTHREAD_MUTEX_INITIALIZER;

/** files that are being used by clients are placed here */
FileNode* in_use = NULL;
int counter = 0;
HashNode* serverHashList = NULL;

void saveState();

/** Check if file is in the linked list */
bool isInUse(char* filename) {
    if (in_use == NULL) { return false; }
    FileNode* lookup = searchFileList(in_use, filename);

    /** Exists */
    if (lookup != NULL) { return true; }

    return false;
}

/** Fired at the end when client has finished performing operation on file */
void removeFromInUse(char* filename) {
    deleteFileNode(&in_use, filename);
}

/**
 * testXmlwriterFilename:
 * @uri: the output URI
 *
 * test the xmlWriter interface when writing to a new file
 */
void testXmlwriterFilename(const char *uri) {
    int rc;
    xmlTextWriterPtr writer;

    /* Create a new XmlWriter for uri, with no compression. */
    writer = xmlNewTextWriterFilename(uri, 0);
    if (writer == NULL) {
        printf("testXmlwriterFilename: Error creating the xml writer\n");
        return;
    }

    // Start the document
    rc = xmlTextWriterStartDocument(writer, NULL, NULL, NULL);
    if (rc < 0) {
        printf
            ("testXmlwriterFilename: Error at xmlTextWriterStartDocument\n");
        return;
    }

    /* Start the root element of the document. */
    rc = xmlTextWriterStartElement(writer, BAD_CAST "repository");
    if (rc < 0) {
        printf
            ("testXmlwriterFilename: Error at xmlTextWriterStartElement\n");
        return;
    }

	// iterate through hashtable and create directory from existing file
    HashNode* currentHashNode = serverHashList;
	while (currentHashNode != NULL) {
        rc = xmlTextWriterStartElement(writer, BAD_CAST "file");
        if (rc < 0) {
            printf("testXmlwriterFilename: Error at xmlTextWriterStartElement\n");
            return;
        }

        rc = xmlTextWriterWriteFormatElement(writer, BAD_CAST "hashname",
                                            "%s", currentHashNode->hashname);
        if (rc < 0) {
            printf
                ("testXmlwriterFilename: Error at xmlTextWriterWriteFormatElement\n");
            return;
        }

        FileNode* currentFileNode = currentHashNode->fileList;
        while(currentFileNode != NULL) {
            rc = xmlTextWriterWriteFormatElement(writer, BAD_CAST "knownas",
                                            "%s", currentFileNode->filename);
            if (rc < 0) {
                printf
                    ("testXmlwriterFilename: Error at xmlTextWriterWriteFormatElement\n");
                return;
            }
            currentFileNode = currentFileNode->next;
        }

        // Close file
        rc = xmlTextWriterEndElement(writer);
        if (rc < 0) {
            printf
                ("testXmlwriterFilename: Error at xmlTextWriterEndElement\n");
            return;
        }

        currentHashNode = currentHashNode->next;
	}

    // Close the document
    rc = xmlTextWriterEndDocument(writer);
    if (rc < 0) {
        printf
            ("testXmlwriterFilename: Error at xmlTextWriterEndDocument\n");
        return;
    }

    xmlFreeTextWriter(writer);    

}

/**
 * Socket thread: This is the thread that the parent
 * thread in main creates to service new clients. The
 * amount of clients we can have in total is hardcoded to be
 * 50 (from the labs). Here, we give every client their own buffer
 * to prevent data corruption. 
 */
void *socketThread(void *arg) {
    /** Get the newly created socket as an argument */
	int newSocket = *((int *)arg);
	
    /** Command character that the client sends to the server */
    unsigned char command;
	
    /** Buffers used for servicing client's needs */
    int fileSize = 0;
	char filename[MAX_INPUT];
    char clientMessage[MAX_BUFFER_SIZE];
    char serverMessage[MAX_BUFFER_SIZE];
    char buffer[MAX_BUFFER_SIZE];

    /**
     * Main while loop that iterates 
     * to check if a client sends a request
     */
	while(1) {
		fprintf(stdout, "Socket running\n");

        /** Clear everything here */
        memset(clientMessage, 0, sizeof(clientMessage));
        memset(serverMessage, 0, sizeof(serverMessage));
        memset(filename, 0, sizeof(filename));
        memset(buffer, 0, sizeof(buffer));

        /** Client message is the first packet that the client issues */
        if(recv(newSocket, clientMessage, sizeof(clientMessage), 0) == 0) {
            fprintf(stderr, "Error: Could not receive request!\n");

        } else {
            command = clientMessage[0];
            fprintf(stdout, "Command: %u\n", command);

            /** SERVER LIST LOGIC */
            if (command == 0x00) {
                /** Number of bytes to send for each filename read */
                unsigned short filenameBytes = 0;

                /** Search through all the files */
                HashNode* currentHashNode = serverHashList;
                while (currentHashNode != NULL) {
                    FileNode* currentFileNode = currentHashNode->fileList;

                    /** Filenames are stored as a linked list */
                    while (currentFileNode != NULL) {
                        /** Increment the number of bytes to send by the length of the filename */
                        filenameBytes += (unsigned short) strlen(currentFileNode->filename) + 1;
                        currentFileNode = currentFileNode->next;
                    }

                    currentHashNode = currentHashNode->next;
                }


                memset(serverMessage, 0, sizeof(serverMessage));
                serverMessage[0] = (char) 0x01;

                /** Encode the file count as 2 bytes */
                char length[2];
                length[0] = (filenameBytes >> 8) & 0xFF;
                length[1] = filenameBytes & (char) 0xFF;

                serverMessage[1] = length[0];
                serverMessage[2] = length[1];

                /** Send the socket */
                send(newSocket, serverMessage, sizeof(serverMessage), 0);

                HashNode* newCurrentHashNode = serverHashList;
                while (newCurrentHashNode != NULL) {
                    FileNode* newCurrentFileNode = newCurrentHashNode->fileList;

                    /** Send files as we go */
                    while (newCurrentFileNode != NULL) {
                        unsigned short filenameSize = (unsigned short)strlen(newCurrentFileNode->filename) + 1;                        
                        char filenameBuffer[filenameSize];
                        strcpy(filenameBuffer, newCurrentFileNode->filename);
                        filenameBuffer[filenameSize - 1] = '\0';
                        fprintf(stdout, "Filename: %s\n", filenameBuffer);
                        send(newSocket, filenameBuffer, filenameSize, 0);

                        newCurrentFileNode = newCurrentFileNode->next;
                    }

                    newCurrentHashNode = newCurrentHashNode->next;
                }

            /** END OF SERVER LIST LOGIC */

            /** SERVER UPLOAD LOGIC */
            } else if (command == 0x02) {

                /** Get the filename */
                int i = 1;
                while (clientMessage[i] != '\0') {
                    filename[i - 1] = clientMessage[i];
                    ++i;
                }

                filename[i] = '\0';
                fprintf(stdout, "filename: %s\n", filename);

                pthread_mutex_lock(&mutex);

/** 
 * Lock and check if file currently being used (i.e. another file
 * with the same name currently uploading at the same time). We
 * use a condition variable to check if a file is in use, using a
 * linked list to store the filenames temporarily, and when
 * they are done being used, 
 */
upload_check:
                if (isInUse(filename) == 1) {
                    fprintf(stdout, "Socket number %d is waiting...\n", newSocket);
                    pthread_cond_wait(&cond, &mutex);
                    goto upload_check;
                }

                /** Add to in use files if list doesn't exist */
                if (in_use == NULL) {
                    in_use = createFileNode(filename);

                } else {
                    addToFileList(in_use, createFileNode(filename));
                }

                /** Unlock and let another thread through to check */
                pthread_mutex_unlock(&mutex);

                /** Protocol for file size */
                unsigned char fileSizeBuffer[4];
                fileSizeBuffer[0] = clientMessage[i + 1];
                fileSizeBuffer[1] = clientMessage[i + 2];
                fileSizeBuffer[2] = clientMessage[i + 3];
                fileSizeBuffer[3] = clientMessage[i + 4];

                /** Convert to int */
                fileSize = FourByteToLU(fileSizeBuffer);

                /** File size is zero, we use the default hash value */
                if (fileSize == 0) {
                    unsigned char* hash = (unsigned char*) malloc((MD5_DIGEST_LENGTH) * sizeof(unsigned char*));
                    hashContents(hash, filename);

                    /**
                     * Get the hashname to use
                     * default is d41d8cd98f00b204e9800998ecf8427e
                     */
                    char actualname[33];
                    for (int i = 0; i < 16; ++i) {
                        sprintf(&actualname[i*2], "%02x", (unsigned int) hash[i]);
                    }

                    rename(filename, actualname);
                }

                if (fileSize < 0) { fprintf(stderr, "Error: File read error!\n"); }

                FILE* uploadfp = fopen(filename, "w");
                
                /** 
                 * Main receiving section. Here, since
                 * we don't know how many bytes to read in,
                 * we feed fwrite with a buffer and the amount
                 * of bytes read in from a particular packet of data.
                 */
                while (fileSize > 0) {
                    if (fileSize < 0) { fprintf(stderr, "Error: File read error!\n"); }
                    int bytes_read = recv(newSocket, buffer, sizeof(buffer), 0);
                    
                    fwrite(buffer, bytes_read, 1, uploadfp);

                    /** Decrement after we're done */                    
                    fileSize -= bytes_read;
                }

                fclose(uploadfp);

                /** Create and populate the hash string and check if it exists */
                unsigned char* hash = (unsigned char*) malloc((MD5_DIGEST_LENGTH) * sizeof(unsigned char*));
                hashContents(hash, filename);

                /** 
                 * Source: https://stackoverflow.com/questions/7627723/how-to-create-a-md5-hash-of-a-string-in-c
                 * Here, we get the actualname that the file will be stored using the md5 checksum. 
                 */
                char actualname[33];
                for (int i = 0; i < 16; ++i) {
                    sprintf(&actualname[i*2], "%02x", (unsigned int) hash[i]);
                }

                /**
                 * Search for hash if it already exists. If it does,
                 * the we check for whether a filename associated to the
                 * hash exists. If it does, we delete the file temporarily made.
                 * Otherwise, we create the respective nodes as necessary.
                 */
                HashNode* lookupHashNode = searchHashList(serverHashList, actualname);

                /** Hashname exists */
                if (lookupHashNode != NULL) {

                    /** There exists a file list already under the given hashname */
                    if (lookupHashNode->fileList != NULL) {
                        FileNode* lookupFileNode = searchFileList(lookupHashNode->fileList, filename);

                        /** File with hash and filename already exists */
                        if (lookupFileNode != NULL) {

                            /** Send an error message */
                            memset(serverMessage, 0, sizeof(serverMessage));

                            serverMessage[0] = (char) 0xFF;
                            char* errorMessage = "SERROR file already exists\n";

                            /** Delete the currently uploaded file */
                            if ((remove(filename)) != 0) {
                                fprintf(stderr, "Unable to delete file!\n");
                            }

                            /** Send error message to server */
                            int j = 0;
                            while (errorMessage[j] != '\0') {
                                serverMessage[j+1] = errorMessage[j];
                                ++j;
                            }

                            serverMessage[j] = '\0';
                            send(newSocket, serverMessage, MAX_BUFFER_SIZE * sizeof(char), 0);
                            goto upload_end;

                        /** File doesn't exist - must be added to file list! */
                        } else {
                            addToFileList(lookupHashNode->fileList, createFileNode(filename));
                        }

                    /** The filename is not associated with the given hashname - shouldn't happen */
                    } else {
                        lookupHashNode->fileList = createFileNode(filename);
                    }

                /** Hash doesn't exist - add to linked list */
                } else {
                    if (serverHashList != NULL) {
                        addToHashList(serverHashList, createHashNode(actualname));
                    } else {
                        serverHashList = createHashNode(actualname);
                    }

                    /** Add respective filename as the head */
                    HashNode* current = searchHashList(serverHashList, actualname);
                    current->fileList = createFileNode(filename);
                }

                /** Rename the file */
                rename(filename, actualname);

                /** Clear sending buffer and send response */
                memset(serverMessage, 0, sizeof(serverMessage));
                serverMessage[0] = (char) 0x03;
                send(newSocket, serverMessage, MAX_BUFFER_SIZE * sizeof(char), 0);


 /** Deal with closing file and changing the condition variable */
 upload_end:
               /** Remove from in use list */
                removeFromInUse(filename);
                pthread_cond_signal(&cond);

            } else if (command == 0x04) {
                // fprintf(stdout, "A client wants to delete!!\n");
                // fprintf(stdout, "%s\n", clientMessage);
                // fflush(stdout);
                ///////////////////////////////////////
                // here we want to delete the given file name from the linked list of files.
                /** Get the filename */
                int i = 1;
                while (clientMessage[i] != '\0') {
                    filename[i - 1] = clientMessage[i];
                    ++i;
                }

                filename[i] = '\0';

                fprintf(stdout, "filename: %s\n", filename);

                pthread_mutex_lock(&mutex);

delete_check:
                if (isInUse(filename) == 1) {
                    // fprintf(stdout, "Delete Waiting\n");
                    pthread_cond_wait(&cond, &mutex);
                    goto delete_check;
                }

                /** Add to in use files */
                /** If list doesn't exist */
                if (in_use == NULL) {
                    in_use = createFileNode(filename);

                } else {
                    addToFileList(in_use, createFileNode(filename));
                }

                pthread_mutex_unlock(&mutex);

                HashNode* current = serverHashList; // pointer to current hash item
                
                while (current != NULL) {
                    FileNode* found = searchFileList(current->fileList, filename); // search for file in current hash item
                    
                    if (found != NULL) { // found file in current hashNode
                        if (current->fileList != NULL && current->fileList->next != NULL) {
                            // fprintf(stdout, "deleting child %s\n", filename);
                            deleteFileNode(&(current->fileList), filename);
                        } else if (current->fileList != NULL && current->fileList->next == NULL) {
                            // fprintf(stdout, "deleting hash item");
                            deleteFileNode(&(current->fileList), filename);
                            // fprintf(stdout, "hashname to remove: %s\n", current->hashname);
                            remove(current->hashname);
                            // if (status == 0) {
                                // fprintf(stdout, "%s file deleted successfully\n", current->hashname);
                            
                            // } else {
                                // fprintf(stdout, "Unable to delete the file\n");
                            // }
                            
                            deleteHashNode(&serverHashList, current->hashname); // delete entire hash
                        } else {
                            // fprintf(stdout, "HashNode count is invalid\n");
                        }
                        memset(buffer, 0, sizeof(buffer));
                        buffer[0] = 0x05;
                        send(newSocket, buffer, 1, 0);
                        break;
                    }
                    current = current->next;
                }

                if (current == NULL) {
                    /** Send an error message */
                    memset(serverMessage, 0, sizeof(serverMessage));

                    serverMessage[0] = (char) 0xFF;
                    char* errorMessage = "SERROR file to delete not found\n";

                    int j = 0;
                    while (errorMessage[j] != '\0') {
                        serverMessage[j+1] = errorMessage[j];
                        ++j;
                    }

                    serverMessage[j] = '\0';
                    send(newSocket, serverMessage, MAX_BUFFER_SIZE * sizeof(char), 0);
                    goto delete_end;
                }

 delete_end:
               /** Remove from in use list */
                removeFromInUse(filename);
                pthread_cond_signal(&cond);

            /** END OF SERVER UPLOAD LOGIC */

            /** SERVER DOWNLOAD LOGIC */
            } else if (command == (char) 0x06) {

                /** Read from the client's request */
                int i = 0;
                while (clientMessage[i+1] != '\0') {
                    filename[i] = clientMessage[i + 1];
                    ++i;
                }

                filename[i] = '\0';

                /** Same as uploading */
                pthread_mutex_lock(&mutex);

/**
 * The logic is the same for uploading, since the file will be in use.
 * A synchronization issue may occur when a client wants to remove a file
 * that another use wants to use.
 */
download_check:
                if (isInUse(filename) == 1) {
                    fprintf(stdout, "Download Waiting");
                    pthread_cond_wait(&cond, &mutex);

                    goto download_check;
                }

                if (in_use == NULL) {
                    in_use = createFileNode(filename);

                } else {
                    addToFileList(in_use, createFileNode(filename));
                }

                /** Unlock the mutex for other threads to continue */
                pthread_mutex_unlock(&mutex);

                /** Check if the file exists */
                char filenameToDownload[33] = "";

                /** Check if the file exists */
                HashNode* tmp = serverHashList;
                while (tmp != NULL) {
                    if (tmp->fileList != NULL) {
                        FileNode* lookupFilename = searchFileList(tmp->fileList, filename);

                        if (lookupFilename != NULL) {

                            fprintf(stdout, "%s\n", tmp->hashname);
                            strcpy(filenameToDownload, tmp->hashname);
                            break;
                        }
                    }

                    tmp = tmp->next;
                }

                /** File doesn't exist */
                if (filenameToDownload[0] == '\0') {
                    serverMessage[0] = (char) 0xFF;
                    char* errorMessage = "SERROR file doesn't exist\n";

                    int j = 0;
                    while (errorMessage[j] != '\0') {
                        serverMessage[j+1] = errorMessage[j];
                        ++j;
                    }

                    serverMessage[j] = '\0';

                    /** Send the error message */
                    send(newSocket, serverMessage, MAX_BUFFER_SIZE * sizeof(char), 0);
                    goto download_end;
                }

                memset(serverMessage, 0, sizeof(serverMessage));

                /** 
                 * Otherwise, the file exists,
                 * so we can respond to the client
                 * and send the file.
                 */
                serverMessage[0] = (char) 0x07;
                
                /** Get the file size to send */
                FILE* downloadfp = fopen(filenameToDownload, "r");
                fseek(downloadfp, 0L, SEEK_END);
                int fileSize = ftell(downloadfp);
                fprintf(stdout, "Size of file: %u\n", fileSize);
                fclose(downloadfp);

                /** Parse file size */
                serverMessage[1] = (fileSize >> 24) & 0xFF;
                serverMessage[2] = (fileSize >> 16) & 0xFF;
                serverMessage[3] = (fileSize >> 8) & 0xFF;
                serverMessage[4] = (fileSize) & 0xFF;

                /** Send the protocol message */
                send(newSocket, serverMessage, sizeof(serverMessage), 0);

                /** Open the file and commence sending process */
                FILE* dfilefp = fopen(filenameToDownload, "r");
                if (dfilefp == NULL) {
                    fprintf(stderr, "Error: Could not open file\n");
                }

                memset(buffer, 0, MAX_BUFFER_SIZE * sizeof(char));

                /** Get FD of file */
                int filefd = fileno(dfilefp);

                /** Continuosly send chunks of the file to a buffer, and then put
                 *  the contents of the buffer into the outgoing socket. */
                while(1) {

                    /** Read from file and store inside a buffer */
                    int bytes_remaining = read(filefd, buffer, MAX_BUFFER_SIZE * sizeof(char));
                    fprintf(stdout, "Bytes remaining: %d\n", bytes_remaining);

                    /* No more bytes to read from the file */
                    if (bytes_remaining == 0) { break; }

                    /** Keeps track of where we are in the buffer */
                    void *p = &buffer;

                    /** Write the file to the socket in chunks */
                    while (bytes_remaining > 0) {

                        int bytes_written = send(newSocket, p, bytes_remaining, 0);
                        if (bytes_written <= 0) {
                            fprintf(stderr, "Error: Write error!\n");
                        }

                        bytes_remaining -= bytes_written;
                        p += bytes_written;
                    }
                }

                fclose(dfilefp);

/** Again this is the same as the uploading logic */
download_end:
                /** Remove from in use list */
                removeFromInUse(filename);
                pthread_cond_signal(&cond);

            /** Client requesting to quit */
            } else if (command == 0x08) {
                memset(buffer, 0, sizeof(buffer));
                buffer[0] = 0x09;
                send(newSocket, buffer, 1, 0);

            /** Handle errors */
            } else {
                fprintf(stderr, "Not a valid protocol command!\n");
                fflush(stdout);
            }
        }

        /** Lock this for only one thread to write at a time */
        pthread_mutex_lock(&xml_lock);
        saveState();
        pthread_mutex_unlock(&xml_lock);

		sleep(1);
	}

	fprintf(stdout, "Exit socketThread\n");
	close(newSocket);
	pthread_exit(NULL);

}

/** Save the state of the dedup when we SIGTERM */
void saveState() {
	LIBXML_TEST_VERSION
	if (access (".dedup", F_OK) == -1) {
        // file doesn't exist
        fprintf(stdout, ".dedup not here, creating one");
        FILE *fp = fopen(".dedup", "w");
        testXmlwriterFilename(".dedup");
        fclose(fp);
    } else {
        fprintf(stdout, ".dedup exists, call xml write");
        testXmlwriterFilename(".dedup");
    }
}


/**
 * The signal handler, where receiving a
 * SIGTERM lets the server save its state as
 * a dedup file and close all of its sockets before quitting.
 */
void signalHandler(int sigNum) {
	if (sigNum == SIGTERM) { // when the process is killed
		saveState();
        // Free memory
        HashNode* currentHashNode = serverHashList;
        while (currentHashNode != NULL) {
            if (currentHashNode->fileList != NULL) {
                deleteFileList(currentHashNode->fileList);
            }

            currentHashNode = currentHashNode->next;
        }

        deleteHashList(serverHashList);

		exit(0);
	}
}

/**
 * The server will read from a .dedup file if it exists, and at runtime,
 * create a tree of nodes with values equal to the md5checksum value, and
 * uses strcmp to traverse the tree.
 */
int main(int argc, char *argv[]) {

    // /* DAEMONIZATION */
	FILE *logFP = NULL;
	pid_t process_id = 0;
	pid_t sid = 0;

	/* Create child process */
	process_id = fork();
	if (process_id < 0) {
		fprintf(stdout, "Error: Failed to fork process!\n");
		exit(1);
	}

	/* Kill parent process to rip from TTY */
	if (process_id > 0) {
		printf("process_id of child process %d \n", process_id);
		exit(0);
	}

	/* Unmask the file mode - READ, WRITE, EXECUTE */
	umask(0);

	/* Set new session */
	sid = setsid();
	if(sid < 0) {
		exit(1);
	}

	if (argc != 3) {
		fprintf(stderr, "Error: Not enough arguments supplied.\n");
		exit(1);
	}

    int port; 						    // port specified
	char* dirName;		                // directory of server

    // setup the arguments
    sscanf(argv[2], "%d", &port);
	dirName = (char *) argv[1];


	/* Change the current working directory to root */
	if ((chdir(dirName)) != 0) {
        fprintf(stdout, "Could not daemonize process\n");
        exit(1);
    }

	/* Close output streams */
	close(STDIN_FILENO);
	close(STDOUT_FILENO);
	close(STDERR_FILENO);

	// /* Open a log file in write mode. */
	logFP = fopen("Log.txt", "w+");

	struct sigaction sa;
	memset(&sa, 0, sizeof(struct sigaction));
	sa.sa_handler = signalHandler;
	sigaction(SIGTERM, &sa, NULL); // let SIGTERM be handled by sigaction

	int serverSocket, newSocket;
	struct sockaddr_in serverAddr;
	struct sockaddr_storage serverStorage;	// this is used to handle generic sockets
	socklen_t addr_size;


    fprintf(stdout, "Port: %d\n", port);
    fprintf(stdout, "Directory Name: %s\n", dirName);


	// create the socket
	if ((serverSocket = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
		fprintf(stderr, "Error: Could not create socket!\n");
		exit(1);
	}

	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(port);
	serverAddr.sin_addr.s_addr = inet_addr("127.0.0.1");

	memset(serverAddr.sin_zero, '\0', sizeof(serverAddr.sin_zero));

	// bind the address struct to the socket
	bind(serverSocket, (struct sockaddr *) &serverAddr, sizeof(serverAddr));

	// listen on the socket, with 40 max connection requests queued
	if (listen(serverSocket, 50) == 0) {
		//printf("Listening\n");
	} else {
		fprintf(stderr, "Error: Could not listen\n");
		exit(1);
	}

	pthread_t tid[60];
	int i = 0;

    // if there is an existing dedup file, populate data structure with dedup contents
    // otherwise just create a new .dedup file
    if (access (".dedup", F_OK) == -1) {
        // file doesn't exist so we create a new dedup file
        fprintf(stdout, ".dedup not here, creating one");
        FILE *fp = fopen(".dedup", "w");
        fclose(fp);
    } else {
        FILE *fp = fopen(".dedup", "r+");
        fseek(fp, 0, SEEK_END);
        int size = ftell(fp);
        if (size == 0) { // if file is empty, don't populate data
            fclose(fp);
        } else { // populate data with xml contents
            fprintf(stdout, ".dedup exists, call xml write");
            serverHashList = populateData();
        }
        
    }

    HashNode* current = serverHashList;
    while (current != NULL) {
        if (current->fileList != NULL) {
            printFileList(current->fileList);
        }
        current = current->next;
    }

    /** Initialize locks and condition variable */
    pthread_cond_init(&cond, NULL);
    pthread_mutex_init(&mutex, NULL);
    pthread_mutex_init(&xml_lock, NULL);

	/* Main loop */
	while (1) {

		/* Dont block context switches, let the process sleep for some time */
		sleep(1);

		fprintf(logFP, "Server started running...\n");
		fflush(logFP);

		addr_size = sizeof(serverStorage);

		// accept(...) blocks caller until a connection is present
		newSocket = accept(serverSocket, (struct sockaddr *) &serverStorage, &addr_size);
		fprintf(stdout, "New Socket FD: %d\n", newSocket);
		// sockets[socketCounter++] = newSocket;


		// for each client request creates a thread and assign the client request to it to process
        // so the main thread can entertain next request
		if (newSocket >= 0) {
			if(pthread_create(&tid[i], NULL, socketThread, &newSocket) != 0) {
				printf("Failed to create thread\n");
			}
			++i;

		} else if (newSocket < 0) {
			printf("Failed to connect");
		}

        if (i >= 50) {
			i = 0;
			while(i < 50) {
				pthread_join(tid[i++], NULL);
			}
			i = 0;
        }
    }
	printf("PROGRAM END");

	fclose(logFP);
	return (0);
}