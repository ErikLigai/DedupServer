#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <openssl/md5.h>
#include "file_storage_util.h"

#ifndef SERVER_UTIL
#define SERVER_UTIL

#define MY_ENCODING "ISO-8859-1"

/** Returns a hash list of all the files in the .dedup */
HashNode* populateData();

// checks if the file already exists in the server directory
int cfileexists(const char * filename);

u_int32_t FourByteToLU(unsigned char bytes[4]);

// make the .dedup file and write to it based on the data structure
void testXmlwriterFilename(const char *uri);

// convert input
xmlChar * ConvertInput(const char *in, const char *encoding);

// hash the file contents
void hashContents(unsigned char *hash, char *filename);

// encode a command request character
char encodeChar(char commandChar);


// hash the contents of a file and put that hash into provided hash array
void hashContents(unsigned char *hash, char *filename) {
    unsigned char c[MD5_DIGEST_LENGTH];
    int i;
    FILE *inFile = fopen (filename, "rb");
    MD5_CTX mdContext;
    int bytes;
    unsigned char data[1024];

    if (inFile == NULL) {
        inFile = fopen(filename, "w+");
    }

    MD5_Init (&mdContext);
    while ((bytes = fread (data, 1, 1024, inFile)) != 0)
        MD5_Update (&mdContext, data, bytes);
    MD5_Final (c,&mdContext);

    for(i = 0; i < MD5_DIGEST_LENGTH; i++) {
        hash[i] = c[i];
    }

    fclose (inFile);
}

/** Gets the response from the server and prints the supplied message (if any) */
void interpretResponse(char message[]);

// ---- IMPLEMENTATION ----

int cfileexists(const char * filename) {
    FILE *file;
	file = fopen(filename, "r");
    if (file != NULL) {
		printf("file is here\n");
        fclose(file);
        return 1;
    }
    return 0;
}

u_int32_t FourByteToLU(unsigned char bytes[4]) {
  u_int32_t value = 0;
  for (int i = 0; i < 4; ++i) {
      value = value | bytes[i];
      if (i < 3) { value = value << 8; }
  }
  return value;
}

char encodeChar(char commandChar) {

    /** List command */
    if (commandChar == 'l') {
        return (char) 0x00;

    /** Upload command */
    } else if (commandChar == 'u') {
        return (char) 0x02;

    /** Delete command */
    } else if (commandChar == 'r') {
        return (char) 0x04;

    /** Download command */
    } else if (commandChar == 'd') {
        return (char) 0x06;

    /** Quit command */
    } else if (commandChar == 'q') {
        return (char) 0x08;

    }

    /** Unknown command. Error */
    fprintf(stderr, "CERROR not a valid command\n");
    fflush(stdout);
    return 0xFF;

}

void interpretResponse(char* message) {
    switch (message[0]) {
        case (char) 0x01:
        case (char) 0x03:
        case (char) 0x05:
        case (char) 0x07:
        case (char) 0x09:
            fprintf(stdout, "OK\n");
            break;

        /** Print out the error message */
        case (char) 0xFF:
            fprintf(stdout, "%s\n", message + 1);

    }
}

// if there's an existing .dedup file, populate data structure with existing data
HashNode* populateData() {
    HashNode* hashList = NULL;
    xmlDoc* doc = NULL;
    xmlNode *root, *child, *node;

    LIBXML_TEST_VERSION

    doc = xmlReadFile(".dedup", NULL, 0);

    if (doc == NULL) {
        fprintf(stderr, "Error: could not parse file .dedup\n");
    }

    /* Get the root element of the XML */
    root = xmlDocGetRootElement(doc);

    /* Get reference to root's first child */
    child = root->children;


    for (node = child; node; node = node->next) { /* Traverse siblings */

        /* If <file> is the parent, we get all of its children */
        if ((!xmlStrcmp(node->name, (const xmlChar *) "file"))) {

            xmlNode* temp = node->children;
            xmlChar* hashname = NULL;

            /* Here, we look for the hashname and the respective filenames */
            while (temp != NULL) {

                if ((!xmlStrcmp(temp->name, (const xmlChar*) "hashname"))) {
                    if (hashname != NULL) { /* Not cleared! */
                        fprintf(stderr, "Error: Hashname wasn't cleared!\n");
                        exit(1);
                    }

                    hashname = xmlNodeListGetString(temp->doc, temp->children, 1);

                } else if ((!xmlStrcmp(temp->name, (const xmlChar*) "knownas"))) {

                    xmlChar* filename = xmlNodeListGetString(temp->doc, temp->children, 1);

                    if (hashList == NULL) {
                        hashList = createHashNode((char*) hashname);
                    } else {
                        HashNode* lookupHashNode = searchHashList(hashList, (char*) hashname);

                        if (lookupHashNode == NULL) {
                            addToHashList(hashList, createHashNode((char*) hashname));
                        } else {
                            /** Do nothing... */
                        }
                    }

                    HashNode* tmp = searchHashList(hashList, (char*) hashname);
                    if (tmp->fileList != NULL) {
                        addToFileList(tmp->fileList, createFileNode((char*) filename));
                    } else {
                        tmp->fileList = createFileNode((char*) filename);
                    }

                    xmlFree(filename);
                }

                /* Get next child */
                temp = temp->next;
            }

            /* Free the hashName at the end, since we're only expecting one */
            xmlFree(hashname);
        }

    }

    /** Cleanup */
    xmlFreeDoc(doc);
    xmlCleanupParser();
    return hashList;
}

#endif