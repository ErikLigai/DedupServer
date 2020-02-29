#ifndef FILE_STORAGE_UTIL
#define FILE_STORAGE_UTIL

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define MAX_INPUT 255   /** This is the standard anyway */
#define MD5_SIZE 33

typedef struct FileNode {
    char filename[MAX_INPUT];
    struct FileNode* next;
} FileNode;

typedef struct HashNode {
    char hashname[MD5_SIZE];
    struct FileNode* fileList;  /** Linked list of filenames */
    struct HashNode* next;
} HashNode;

/** Creates a hash node. */
HashNode* createHashNode(char* hash);

/** Add new hash to list. */
void addToHashList(HashNode* hashList, HashNode* hashNode);

/** Returns true if the hash exists. */
HashNode* searchHashList(HashNode* hashList, char* hash);

/** Removes hash node based on hashname. */
void deleteHashNode(HashNode** head, char* hash);

/** Delete an entire hash list. */
void deleteHashList(HashNode* hashList);

/** Add new node to file list. */
FileNode* createFileNode(char* file);

/** Add new filename to list of filenames. */
void addToFileList(FileNode* fileList, FileNode* fileNode);

/** Returns true if the filename exists. */
FileNode* searchFileList(FileNode* fileList, char* file);

/** Removes hash node based on filename. */
void deleteFileNode(FileNode** head, char* file);

/** Delete an entire file list. */
void deleteFileList(FileNode* fileList);

//==========================================================
// HASH LINKED LIST IMPLEMENTATION
//==========================================================

HashNode* createHashNode(char* hash) {
    HashNode* newHashNode = (HashNode*) malloc(sizeof(HashNode));
    
    strcpy(newHashNode->hashname, hash);
    newHashNode->fileList = NULL;
    newHashNode->next = NULL;
    
    return newHashNode;
}

void addToHashList(HashNode* hashList, HashNode* hashNode) {
    HashNode* current = hashList;
    HashNode* prev = NULL;

    while (current != NULL) {
        prev = current;
        current = current->next;
    }

    current = hashNode;
    if (prev != NULL) { prev->next = current; }

}

HashNode* searchHashList(HashNode* hashList, char* hash) {
    HashNode* current;
    current = hashList;
    while (current != NULL) {
        if (strcmp(current->hashname, hash) == 0) { return current; }
        current = current->next;
    }

    return NULL;
}

void deleteHashNode(HashNode** head, char* hash) {
    HashNode* current = *head;
    HashNode* prev = NULL;

    if (current != NULL && (strcmp(current->hashname, hash) == 0)) {
        *head = current->next;
        free(current);
        return;
    }

    while (current != NULL && (strcmp(current->hashname, hash) != 0)) {
        prev = current;
        current = current->next;
    }

    if (current == NULL) { return; }

    prev->next = current->next;
    free(current);
}

void deleteHashList(HashNode* hashList) {
    HashNode *p = hashList;
    while (p != NULL) {
        hashList = hashList->next;
        free(p);
        p = hashList;
    }
}

void printHashList(HashNode* hashList) {
    HashNode* current = NULL;
    current = hashList;

    while (current != NULL) {
        fprintf(stdout, "%s->", current->hashname);
        current = current->next;
    }

    fprintf(stdout, "END\n");
}

//==========================================================
// FILE LINKED LIST IMPLEMENTATION
//==========================================================

FileNode* createFileNode(char* file) {
    FileNode* newFileNode = (FileNode*) malloc(sizeof(FileNode));
    
    strcpy(newFileNode->filename, file);
    newFileNode->next = NULL;
    
    return newFileNode;
}

void addToFileList(FileNode* fileList, FileNode* fileNode) {
    FileNode* current = fileList;
    FileNode* prev = NULL;

    while (current != NULL) {
        prev = current;
        current = current->next;
    }

    current = fileNode;
    if (prev != NULL) { prev->next = current; }
}

FileNode* searchFileList(FileNode* fileList, char* file) {
    FileNode* current;
    current = fileList;
    
    while (current != NULL) {
        if (strcmp(current->filename, file) == 0) { return current; }
        current = current->next;
    }

    return NULL;
}

void deleteFileNode(FileNode** head, char* file) {
    FileNode* current = *head;
    FileNode* prev = NULL;

    if (current != NULL && (strcmp(current->filename, file) == 0)) {
        *head = current->next;
        free(current);
        return;
    }

    while (current != NULL && (strcmp(current->filename, file) != 0)) {
        prev = current;
        current = current->next;
    }

    if (current == NULL) { return; }

    prev->next = current->next;
    free(current);
}

void deleteFileList(FileNode* fileList) {
    FileNode* current = fileList;
    FileNode* prev = NULL;
    
    while (current != NULL) {
        prev = current;
        current = current->next;
        free(prev);
    }
}

void printFileList(FileNode *fileList) {
    FileNode* current = fileList;
    while (current != NULL) {
        fprintf(stdout, "%s->", current->filename);
        current = current->next;
    }

    fprintf(stdout, "END\n");
}

//==========================================================

#endif