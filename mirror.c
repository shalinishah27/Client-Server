/*
* Project: Client-server project with load balancing
*/

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <dirent.h>
#include <sys/types.h>
#include <signal.h>
#include <stdbool.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <time.h>
#include <sys/stat.h>
#include <zlib.h>
#include <limits.h>

#define MAX_TOKENS 8
#define CHUNK_SIZE 16384
#define MAX_CLIENTS 4
// Server root directory
char rootDir[] = ".";

// custom flags
char getAction[] = "-1";
char unzipAction[] = "-3";
char fileFoundAction[] = "@#$%";
char fileNotFoundAction[] = "-2";
char ack[] = "-ack";

// custom messages
char exitMsg[] = "quit\n";
char wellcomeMsg[] = "Welcome to the Server!\n=========================================\n \
List of Commands\n \
> findfile <fileName> - Returns Filename,size(in bytes), and date created\n \
> sgetfiles size1 size2 <-u> Returns files whose sizes are >=size1 and <=size2\n \
> dgetfiles date1 date2 <-u> Returns files whose dates are >=date1 and <=date2\n \
> getfiles  file1 file2 file3 file4 file5 file6 <-u > Returns files with matching names (upto 6 files)\n \
> gettargz  <extension list> <-u> Returns files whose extensions match (upto 6 file types)\n \
> *<-u> unzip temp.tar.gz in the pwd of client\n \
> quit - exit\n \
=========================================\n";
char errorMsg[] = "Invalid command, please try again!";
char foundMsg[] = "I have the file!";
char notFoundMsg[] = "I don't have the file!";
char downloadMsg[] = "Please wait for download";



// Functions to handle client commands
void findFile(char* file, int sd);
void sgetFiles(int size1, int size2, bool unzip, int sd);
void dgetFiles(char* date1, char* date2, bool unzip, int sd);
void getFiles(char* filenames[], int numFiles, bool unzip, int sd);
void getTargz(char* extensions[], int numFiles, bool unzip, int sd);

void serviceClient(int);
int processRequest(char msg[], int sd);
bool getAck(int sd);
void sendAck(int sd);


void send_file(int sd, bool unzip) {
    char file[] = "temp.tar.gz";

    char dirName[255];
    strcpy(dirName, rootDir);
    strcat(dirName, "/");
    // defining required var
    DIR* dp;
    dp = opendir(dirName);
    struct dirent* dirp;
    bool isFound = false;

    // read all dirs
    while ((dirp = readdir(dp)) != NULL)
    {
        // find that file
        if (strcmp(dirp->d_name, file) == 0 && dirp->d_type != DT_DIR)
        {
            isFound = true;
            printf("> '%s' found\n", file);
            // write(sd, foundMsg, sizeof(foundMsg));

            // after this, client will be in - donwloadFile() method
            write(sd, getAction, sizeof(getAction));
            printf("> downloadFile() in client called\n", file);
            getAck(sd);

            // sending file name
            write(sd, file, strlen(file));
            printf("> fileName sent\n");
            getAck(sd);

            strcat(dirName, file);
            int fd = open(dirName, O_RDONLY, S_IRUSR | S_IWUSR);

            long fileSize = lseek(fd, 0L, SEEK_END);

            // send filesize
            write(sd, &fileSize, sizeof(long));
            printf("> fileSize sent\n");
            getAck(sd);

            // create buffer of fileSize
            char* buffer = malloc(fileSize);
            lseek(fd, 0L, SEEK_SET);
            read(fd, buffer, fileSize);
            write(sd, buffer, fileSize);
            getAck(sd);

            if (unzip == true) {
                write(sd, unzipAction, sizeof(unzipAction));
                printf("Unzip the filed\n");
                getAck(sd);
            }
            free(buffer);
            close(fd);
            printf("> '%s' sent\n", file);
        }
    }
    if (!isFound)
    {
        printf("> '%s' not found\n", file);
        write(sd, notFoundMsg, sizeof(notFoundMsg));
    }

    closedir(dp);
}

void compress_files(char* file_list[], int num_files) {
  // Compress the given files into a tar.gz archive
    char command[1000] = "tar -czvf temp.tar.gz";
    for (int i = 0; i < num_files; i++) {
        strcat(command, " ");
        strcat(command, file_list[i]);
    }
    system(command);
}


int main(int argc, char* argv[]) {
    int sd, client, portNumber, status;
    struct sockaddr_in socAddr;

    // if there are not enough arguements
    if (argc != 2) {
        printf("Please run with: %s <Port Number>\n", argv[0]);
        exit(0);
    }

    // creating socket descriptor
    if ((sd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        fprintf(stderr, "Cannot create socket\n");
        exit(1);
    }

    // creating socket address
    socAddr.sin_family = AF_INET;
    socAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    // on port no - portNumber
    sscanf(argv[1], "%d", &portNumber);
    socAddr.sin_port = htons((uint16_t)portNumber);

    // binding socket to socket addr
    bind(sd, (struct sockaddr*)&socAddr, sizeof(socAddr));

    // listening on that socket with max backlog of 8 connections
    listen(sd, MAX_CLIENTS);

   // Connect to mirror server
    int mirror_socket;
    struct sockaddr_in mirror_addr;
    char buffer[1024] = {0};
    if ((mirror_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        fprintf(stderr, "Cannot create socket for mirror\n");
        exit(1);
    }
    mirror_addr.sin_family = AF_INET;
    mirror_addr.sin_port = htons(8086);
    if (inet_pton(AF_INET, "127.0.0.1", &mirror_addr.sin_addr) <= 0) {
        fprintf(stderr, "Invalid address for mirror\n");
        exit(1);
    }
    if (connect(mirror_socket, (struct sockaddr *)&mirror_addr, sizeof(mirror_addr)) < 0) {
        fprintf(stderr, "Cannot connect to mirror\n");
        exit(1);
    }

    // Keep track of number of clients
    int num_clients = 0;

    while (1) {
        // block process until a client appears on this socket
        client = accept(sd, NULL, NULL);

        // Handle first 4 client connections on this server
        if (num_clients < 4) {
            // let the child process handle the rest
            if (!fork())
            {
                printf("Client/%d is connected to server!\n", getpid());
                serviceClient(client);
            }
        } else {
            // Send alternating connections to mirror and server
            if (num_clients % 2 == 1) {
                // Send message to mirror
                char *msg = "Mirror please";
                send(mirror_socket, msg, strlen(msg), 0);

                // Read response from mirror
                read(mirror_socket, buffer, 1024);
                printf("Mirror: %s\n", buffer);

                // Send message back to client
                send(client, buffer, strlen(buffer), 0);
            } else {
                // let the child process handle the rest
                if (!fork())
                {
                    printf("Client/%d is connected to mirror!\n", getpid());
                    serviceClient(client);
                }
            }
        }

        close(client);
        num_clients++;
    }

    return 0;
}

void serviceClient(int sd) {
    char message[255];
    int n;

    write(sd, wellcomeMsg, sizeof(wellcomeMsg));

    while (1)
        // reads message from client
        if (n = read(sd, message, 255)) {
            message[n] = '\0';
            printf("Client/%d: %s", getpid(), message);
            if (!strcasecmp(message, exitMsg))
            {
                printf("Client/%d is disconnected!\n", getpid());
                close(sd);
                exit(0);
            }

            processRequest(message, sd);
        }
}

int processRequest(char msg[], int sd) {
    char* command, * fileName;

    // copying original msg to save it from mutating
    char tempMessage[255];
    strcpy(tempMessage, msg);


    char* tokens[MAX_TOKENS]; // MAX_TOKENS is a constant representing the maximum number of tokens per command
    int numTokens = 0;
    bool unzip = 0; // Flag to indicate if -u option is present

    // Tokenize the command
    char* token = strtok(tempMessage, " \n");
    while (token != NULL && numTokens < MAX_TOKENS) {
        if (strcmp(token, "-u") == 0) {
            unzip = true;
            printf("unzip found\n");
        }
        else {
            tokens[numTokens++] = token;
        }
        token = strtok(NULL, " \n");
    }

    // Match the command and call the appropriate function
    if (numTokens == 2 && strcmp(tokens[0], "findfile") == 0) {
        findFile(tokens[1], sd);
    }
    else if (numTokens <= 4 && strcmp(tokens[0], "sgetfiles") == 0) {
        int size1 = atoi(tokens[1]);
        int size2 = atoi(tokens[2]);
        sgetFiles(size1, size2, unzip, sd);
    }
    else if (numTokens <= 4 && strcmp(tokens[0], "dgetfiles") == 0) {
        char* date1 = tokens[1];
        char* date2 = tokens[2];
        dgetFiles(date1, date2, unzip, sd);
    }
    else if (strcmp(tokens[0], "getfiles") == 0) {
        char* files[] = { NULL };
        int numFiles = 0;
        int i;
        for (i = 1; i < numTokens && numFiles < 6; i++) {
            if (strcmp(tokens[i], "-u") == 0) {
                // The -u flag is not a file name, so skip it
                continue;
            }
            files[numFiles++] = tokens[i];
        }
        getFiles(files, numFiles, unzip, sd);
    }
    else if (numTokens >= 2 && numTokens <= 7 && strcmp(tokens[0], "gettargz") == 0) {
        char* extensions[] = { NULL }; // Array to store up to 6 extensions
        int numExtensions = 0;
        int i;
        for (i = 1; i < numTokens && numExtensions < 6; i++) {
            if (strcmp(tokens[i], "-u") == 0) {
                // The -u flag is not an extension, so skip it
                continue;
            }
            extensions[numExtensions++] = tokens[i];
        }
        getTargz(extensions, numExtensions, unzip, sd);
    }
    else {
        printf("Invalid command.\n");
    }
}


void findFile(char* file, int sd) {
    printf("> findFiles Called: %s\n", file);

    char dirName[255];
    strcpy(dirName, rootDir);
    strcat(dirName, "/");
    DIR* dp;
    dp = opendir(dirName);

    if (dp == NULL) {
        printf("Could not open directory.\n");
        return;
    }

    struct dirent* dirp;
    struct stat filestat;
    bool isFound = false;

    // read all dirs
    while ((dirp = readdir(dp)) != NULL) {
        // find that file
        if (strcmp(dirp->d_name, ".") == 0 && strcmp(dirp->d_name, "..") == 0) {
            continue;
        }

        strcat(dirName, dirp->d_name);
        if (stat(dirName, &filestat) < 0) {
            printf("Could not get file status.\n");
            return;
        }
        if (S_ISDIR(filestat.st_mode)) {
            printf("Skipping directory %s\n", dirp->d_name);
        }
        else if (strcmp(dirp->d_name, file) == 0) {
            char file_info[1000];
            sprintf(file_info, "Found file %s\n  Size: %ld bytes\n  Created: %s", file, filestat.st_size, ctime(&filestat.st_ctime));
            write(sd, file_info, strlen(file_info));
            isFound = true;
            break;
        }
        strcpy(dirName, rootDir);
        strcat(dirName, "/");
    }
    if (!isFound)
    {
        printf("> '%s' not found\n", file);
        write(sd, notFoundMsg, sizeof(notFoundMsg));
    }

    closedir(dp);
}

void sgetFiles(int size1, int size2, bool unzip, int sd) {
    printf("> sgetFiles Called: \n");
    DIR* dir;
    struct dirent* ent;
    struct stat st;
    char path[1000];
    int file_count = 0;
    char* file_list[1000];

    if ((dir = opendir(".")) != NULL) {
        while ((ent = readdir(dir)) != NULL) {
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
                continue;
            }
            if (lstat(ent->d_name, &st) == -1) {
                continue;
            }
            if (!S_ISDIR(st.st_mode)) {
              // Check file size and add to list if it's within the range
                int file_size = st.st_size;
                if (file_size >= size1 && file_size <= size2) {
                    file_list[file_count] = strdup(ent->d_name);
                    file_count++;
                }
            }
        }
        closedir(dir);
    }
    else {
   // Failed to open directory
        perror("");
        exit(EXIT_FAILURE);
    }
    if (file_count == 0) {
        write(sd, notFoundMsg, sizeof(notFoundMsg));
        printf("None of the files were found.\n");
    }
    else {
        char files_found[] = "The following files with the specified sizes were found:";
        write(sd, files_found, sizeof(files_found));
        printf("The following files were found:\n");
        for (int i = 0; i < file_count; i++) {
            printf("%s\n", file_list[i]);
            write(sd, file_list[i], sizeof(file_list[i]));
        }
    }

    if (file_count > 0) {
        compress_files(file_list, file_count);
        send_file(sd, unzip);
    }


}
void dgetFiles(char* date1, char* date2, bool unzip, int sd) {
    printf("> dgetFiles Called: \n");
    DIR* dir;
    struct dirent* ent;
    struct stat st;
    char path[1000];
    int file_count = 0;
    char* file_list[1000];

    // Parse date strings into time_t values
    struct tm date1_tm = { 0 };
    struct tm date2_tm = { 0 };
    strptime(date1, "%Y-%m-%d", &date1_tm);
    strptime(date2, "%Y-%m-%d", &date2_tm);
    time_t date1_t = mktime(&date1_tm);
    time_t date2_t = mktime(&date2_tm);

    if ((dir = opendir(".")) != NULL) {
        while ((ent = readdir(dir)) != NULL) {
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
                continue;
            }
            if (lstat(ent->d_name, &st) == -1) {
                continue;
            }
            if (!S_ISDIR(st.st_mode)) {
              // Check file modification time and add to list if it's within the range
                time_t file_time = st.st_mtime;
                if (file_time >= date1_t && file_time <= date2_t) {
                    file_list[file_count] = strdup(ent->d_name);
                    file_count++;
                }
            }
        }
        closedir(dir);
    }

    else {
   // Failed to open directory
        perror("");
        exit(EXIT_FAILURE);
    }
    if (file_count == 0) {
        write(sd, notFoundMsg, sizeof(notFoundMsg));
        printf("None of the files were found.\n");
    }
    else {
        char files_found[] = "The following files with the specified dates were found:";
        write(sd, files_found, sizeof(files_found));
        printf("The following files were found:\n");
        for (int i = 0; i < file_count; i++) {
            printf("%s\n", file_list[i]);
            write(sd, file_list[i], sizeof(file_list[i]));
        }
    }
    if (file_count > 0) {
        compress_files(file_list, file_count);
        send_file(sd, unzip);
    }
}
void getFiles(char* filenames[], int numFiles, bool unzip, int sd) {
    printf("> getFiles Function Called::");
    DIR* dir;
    struct dirent* ent;
    int file_count = 0;
    char* file_list[1000];

    // Check if each file exists in the current directory
    for (int i = 0; i < numFiles; i++) {

        if ((dir = opendir(".")) != NULL) {
            while ((ent = readdir(dir)) != NULL) {
                if (strcmp(ent->d_name, filenames[i]) == 0) {
                    file_list[file_count] = strdup(ent->d_name);
                    file_count++;
                }
            }
            closedir(dir);
        }
        else {
            // Failed to open directory
            perror("");
            exit(EXIT_FAILURE);
        }
    }

    // Display results
    if (file_count == 0) {
        write(sd, notFoundMsg, sizeof(notFoundMsg));
        printf("None of the files were found.\n");
    }
    else {
        char files_found[] = "The following files with the specified extensions were found:";
        write(sd, files_found, sizeof(files_found));
        printf("The following files were found:\n");
        for (int i = 0; i < file_count; i++) {
            printf("%s\n", file_list[i]);
            write(sd, file_list[i], sizeof(file_list[i]));
        }
    }

    // Compress the found files if necessary
    if (file_count > 0) {
        compress_files(file_list, file_count);
        send_file(sd, unzip);
    }
}
void getTargz(char* extensions[], int numFiles, bool unzip, int sd) {
    printf("> getTargz Function  Called: \n");
    DIR* dir;
    struct dirent* ent;
    int file_count = 0;
    char* file_list[1000];

    // Check if each file with the specified extension exists in the current directory
    if ((dir = opendir(".")) != NULL) {
        while ((ent = readdir(dir)) != NULL) {
            for (int i = 0; i < numFiles; i++) {
                const char* extension = extensions[i];
                size_t extension_length = strlen(extension);
                size_t name_length = strlen(ent->d_name);
                if (name_length > extension_length && strcmp(ent->d_name + name_length - extension_length, extension) == 0) {
                    file_list[file_count] = strdup(ent->d_name);
                    file_count++;
                    break;
                }
            }
        }
        closedir(dir);
    }
    else {
        // Failed to open directory
        perror("");
        exit(EXIT_FAILURE);
    }

    // Display results
    if (file_count == 0) {
        char none_extensions[] = "None of the files with the specified extensions were found.\n";
        write(sd, none_extensions, sizeof(none_extensions));
        printf("None of the files with the specified extensions were found.\n");
    }
    else {
        char extensions_found[] = "The following files with the specified extensions were found:";
        write(sd, extensions_found, sizeof(extensions_found));
        printf("The following files with the specified extensions were found:\n");
        for (int i = 0; i < file_count; i++) {
            printf("%s\n", file_list[i]);
            write(sd, file_list[i], sizeof(file_list[i]));
        }
    }

    // Compress the found files if necessary
    if (file_count > 0) {
        compress_files(file_list, file_count);
        send_file(sd, unzip);
    }
}
void sendAck(int sd) {
    write(sd, ack, sizeof(ack));
}
bool getAck(int sd) {
    char ackMsg[sizeof(ack)];
    read(sd, ackMsg, sizeof(ackMsg));
    if (strcmp(ack, ackMsg) == 0)
        return true;
    else
        return false;
}