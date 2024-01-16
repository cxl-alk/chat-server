#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <argp.h>
#include <arpa/inet.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <assert.h>
#include <sys/select.h>

struct server_arguments {
	int port;
};

struct client {
    char nick[256];
    struct client *next;
    struct room *room;
    int num;
    int sock;
};

struct room {
    char name[256];
    char pass[256];
    struct room *next;
    int userCount;
};

void handleNewClient(int sock);
void delUser(int sock);
void delRoom(struct room *room);
void handleCommand(int *nfds, fd_set *main_fds, uint8_t recvBuff[], int clientSock);
int myComp(const void* a, const void* b);
void listRooms(int sock);
void listUsers(int sock);
void leave(int sock, int *nfds, fd_set *main_fds);
void joinRoom(int sock, uint8_t recvBuff[]);
void changeNick(int sock, uint8_t recvBuff[]);
void sendMSG(int sock, uint8_t recvBuff[]);
void chat(int sock, uint8_t recvBuff[]);

struct client *clientList = NULL;
struct room *roomList = NULL;
int numUsers = 0;
int numRooms = 0;

error_t server_parser(int key, char *arg, struct argp_state *state) {
	struct server_arguments *args = state->input;
	error_t ret = 0;
	switch(key) {
	case 'p':
		/* Validate that port is correct and a number, etc!! */
		args->port = atoi(arg);
		if (args->port < 1025) {
			argp_error(state, "Invalid option for a port, must be a number greater than 1024");
		}
		break;
    case ARGP_KEY_END:
        if (!args->port) {
            argp_error(state, "Must specify port.");
        }
        break;
	default:
		ret = ARGP_ERR_UNKNOWN;
		break;
	}
	return ret;
}

void server_parseopt(int argc, char *argv[], struct server_arguments *args) {

	/* bzero ensures that "default" parameters are all zeroed out */
	bzero(args, sizeof(*args));

	struct argp_option options[] = {
		{ "port", 'p', "port", 0, "The port to be used for the server" ,0},
		{0}
	};
	struct argp argp_settings = { options, server_parser, 0, 0, 0, 0, 0 };
	if (argp_parse(&argp_settings, argc, argv, 0, NULL, args) != 0) {
		printf("Got an error condition when parsing\n");
	}

	/* What happens if you don't pass in parameters? Check args values
	 * for sanity and required parameters being filled in */

	/* If they don't pass in all required settings, you should detect
	 * this and return a non-zero value from main */
	//printf("Got port %d and salt %s with length %ld\n", args->port, args->salt, args->salt_len);
}

int main(int argc, char *argv[]) {
    struct server_arguments args;
    int sock, rv;

    server_parseopt(argc, argv, &args);

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if(sock < 0){
        fprintf(stderr, "Socket initialization failed.\n");
        exit(1);
    }

    struct sockaddr_in servAddr;
    bzero(&servAddr, sizeof(servAddr));
    servAddr.sin_family = AF_INET;
    servAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servAddr.sin_port = htons(args.port);

    rv = bind(sock, (struct sockaddr*) &servAddr, sizeof(servAddr));
    if(rv < 0){
        fprintf(stderr, "Bind failed.\n");
        exit(1);
    }

    rv = listen(sock, 1024);
    if(rv < 0){
        fprintf(stderr, "failed to listen on socket.\n");
        exit(1);
    }

    fd_set select_fds, main_fds;
    FD_ZERO(&main_fds);
    FD_ZERO(&select_fds);
    FD_SET(sock, &main_fds);
    int nfds = sock;

    // add and remove other FDs in here
    while(1){
        select_fds = main_fds; // Updating this while in loop is bad
        rv = select(nfds + 1, &select_fds, NULL, NULL, NULL);
        
        if(rv < 0){
            fprintf(stderr, "select() failed\n");
            exit(1);
        }
        for(int i = 0; i <= nfds; i++) {
            if(FD_ISSET(i, &select_fds)){
                // if in server listening socket
                if(i == sock){
                    int clientSock = accept(sock, NULL, NULL);
                    if(clientSock < 0){
                        fprintf(stderr, "accept() failed\n");
                        exit(1);
                    }
                    FD_SET(clientSock, &main_fds);
                    if(clientSock > nfds){
                        nfds = clientSock;
                    }

                    handleNewClient(clientSock);
                }else{
                    uint8_t recvBuff[2000];
                    bzero(recvBuff, sizeof(recvBuff));
                    ssize_t numBytes = recv(i, recvBuff, sizeof(recvBuff), 0);
                    if(numBytes < 0){
                        fprintf(stderr, "failed to recv in main\n");
                        exit(1);
                    }else if(numBytes == 0){
                        close(i);
                        FD_CLR(i, &main_fds);
                        if(i == nfds){
                            while(!FD_ISSET(nfds, &main_fds)){
                                nfds--;
                            }
                        }
                        delUser(i);
                    }else{
                        // check command and perform it
                        handleCommand(&nfds, &main_fds, recvBuff, i);
                    }
                }
            }
        }
    }

    return 0;
}

void handleNewClient(int sock) {
    uint8_t recvBuff[2000];
    uint8_t sendBuff[2000];
    char name[5 + 1];
    bzero(recvBuff, sizeof(recvBuff));
    bzero(sendBuff, sizeof(sendBuff));
    ssize_t numBytes = recv(sock, recvBuff, sizeof(recvBuff), 0);
    if(numBytes < 0){
        fprintf(stderr, "recv() failed in new client\n");
        return;
    }
    // find available rand num
    int randNum = -1;
    int numFound = 1;
    while(numFound){
        randNum++;
        numFound = 0;
        struct client *curr = clientList;
        // go through linked list check if any client has same num
        while(curr != NULL){
            if(curr->num == randNum){
                numFound = 1;
                break;
            }
            curr = curr->next;
        }
    }
    // building response datagram
    *(uint32_t *)sendBuff = htonl(6);
    *(uint16_t *)&sendBuff[4] = htons(0x0417);
    sendBuff[6] = 0x9a;
    sendBuff[7] = 0x00;
    sprintf(name, "rand%d", randNum);
    memcpy(sendBuff + 8, name, 5);

    // creating new client
    struct client *newClient = malloc(sizeof(struct client));
    newClient->num = randNum;
    //strncpy(newClient->nick, name, 5);
    strcpy(newClient->nick, name);
    newClient->sock = sock;
    newClient->next = NULL;
    newClient->room = NULL;

    if(!clientList){
        clientList = newClient;
    }else{
        newClient->next = clientList;
        clientList = newClient;
    }

    numUsers++;
    numBytes = send(sock, sendBuff, 13, 0);
    if(numBytes < 0){
        fprintf(stderr, "send() failed in new client");
        exit(1);
    }
}

void delUser(int sock){
    struct client *prev = NULL;
    struct client *curr = clientList;
    while(curr != NULL){
        // if head need to be removed
        if(clientList->sock == sock){
            clientList = clientList->next;
            // if in room, decrease room's user count
            if(curr->room){
                curr->room->userCount -= 1;
                // if room count == 0 means no one in room so delete it
                if(curr->room->userCount == 0){
                    delRoom(curr->room);
                }
            }
            numUsers--;
            free(curr);
            break;
        }else if(curr->sock == sock){
            prev->next = curr->next;
            if(curr->room){
                curr->room->userCount -= 1;
                if(curr->room->userCount == 0){
                    delRoom(curr->room);
                }
            }
            free(curr);
            numUsers--;
            break;
        }
        prev = curr;
        curr = curr->next;
    }
}

void delRoom(struct room *room){
    struct room *prev = NULL;
    struct room *curr = roomList;
    while(curr != NULL){
        if(roomList == room){ // if head needs to be removed
            roomList = room->next;
            numRooms--;
            free(room);
            break;
        }else if(curr == room){
            prev->next = curr->next;
            numRooms--;
            free(curr);
            break;
        }
        prev = curr;
        curr = curr->next;
    }
}

void handleCommand(int *nfds, fd_set *main_fds, uint8_t recvBuff[], int clientSock){
    if(recvBuff[6] == 0x09){
        listRooms(clientSock);
    }
    if(recvBuff[6] == 0x0c){
        listUsers(clientSock);
    }
    if(recvBuff[6] == 0x03){
        joinRoom(clientSock, recvBuff);
    }
    if(recvBuff[6] == 0x15){
        chat(clientSock, recvBuff);
    }
    if(recvBuff[6] == 0x0f){
        changeNick(clientSock, recvBuff);
    } 
    if(recvBuff[6] == 0x12){
        sendMSG(clientSock, recvBuff);
    }
    if(recvBuff[6] == 0x06){
        leave(clientSock, nfds, main_fds);
    }
    return;
}

int myComp(const void* a, const void* b){
    return strcmp(*(const char**) a, *(const char**) b);
}

void listRooms(int sock){
    uint8_t sendBuff[2000];
    // put all names into array of char pointers and add up total length
    char *names[numRooms];
    struct room *curr = roomList;
    int index = 0;
    int totalLen = 1;
    while(curr != NULL){
        names[index++] = curr->name;
        totalLen += 1 + strlen(curr->name);
        curr = curr->next;
    }
    // sort
    qsort(names, numRooms, sizeof(const char*), myComp);

    bzero(sendBuff, sizeof(sendBuff));
    *(uint32_t *)sendBuff = htonl(totalLen);
    *(uint16_t *)&sendBuff[4] = htons(0x0417);
    sendBuff[6] = 0x9a;
    sendBuff[7] = 0x00;
    index = 8;
    // for each room name in array, add len and then name after to buffer
    for(int i = 0; i < numRooms; i++){
        int len = strlen(names[i]);
        sendBuff[index++] = (uint8_t) len;
        memcpy(sendBuff + index, names[i], len);
        index += len;
    }
    send(sock, sendBuff, totalLen + 7, 0);
}

void listUsers(int sock){
    uint8_t sendBuff[2000];
    bzero(sendBuff, sizeof(sendBuff));
    struct client *local = clientList;
    // get the client sending request
    while(local != NULL){
        if(local->sock == sock){
            break;
        }
        local = local->next;
    }
    // if not in room use server user count
    int userCount = 0;
    if(!local->room){
        userCount = numUsers;
    }else{
        userCount = local->room->userCount;
    }
    char *names[userCount];
    struct client *curr = clientList;
    int index = 0;
    int totalLen = 1;
    if(!local->room){
        while(curr != NULL){
            names[index++] = curr->nick;
            totalLen += 1 + strlen(curr->nick);
            curr = curr->next;
        }
    }else{
        while(curr != NULL){
            if(local->room == curr->room){
                names[index++] = curr->nick;
                totalLen += 1 + strlen(curr->nick);
            }
            curr = curr->next;
        }
    }
    qsort(names, userCount, sizeof(char *), myComp);
    *(uint32_t *)sendBuff = htonl(totalLen);
    *(uint16_t *)&sendBuff[4] = htons(0x0417);
    sendBuff[6] = 0x9a;
    sendBuff[7] = 0x00;
    index = 8;
    for(int i = 0; i < userCount; i++){
        int len = strlen(names[i]);
        sendBuff[index++] = (uint8_t) len;
        memcpy(sendBuff + index, names[i], len);
        index += len;
    }
    send(sock, sendBuff, totalLen + 7, 0);
}

void leave(int sock, int *nfds, fd_set *main_fds){
    uint8_t sendBuff[8]; // just one short confirmation packet
    struct client *local = clientList;
    while(local != NULL){
        if(local->sock == sock){
            break;
        }
        local = local->next;
    }
    bzero(sendBuff, sizeof(sendBuff));
    *(uint32_t *)sendBuff = htonl(1);
    *(uint16_t *)&sendBuff[4] = htons(0x0417);
    sendBuff[6] = 0x9a;
    sendBuff[7] = 0x00;
    // if not in a room
    if(!local->room){
        send(sock, sendBuff, sizeof(sendBuff), 0);
        FD_CLR(sock, main_fds);
        if(sock == *nfds){
            while(!FD_ISSET(*nfds, main_fds)){
                *nfds -= 1;
            }
        }
        close(sock);
        delUser(sock);
    }else{
        local->room->userCount -= 1;
        if(local->room->userCount == 0){
            delRoom(local->room);
        }
        local->room = NULL;
        send(sock, sendBuff, sizeof(sendBuff), 0);
    }
}

void joinRoom(int sock, uint8_t recvBuff[]){
    uint8_t sendBuff[8];
    struct client *local = clientList;
    while(local != NULL){
        if(local->sock == sock){
            break;
        }
        local = local->next;
    }
    bzero(sendBuff, sizeof(sendBuff));
    *(uint32_t *)sendBuff = htonl(1);
    *(uint16_t *)&sendBuff[4] = htons(0x0417);
    sendBuff[6] = 0x9a;
    sendBuff[7] = 0x00;

    uint8_t nameLen = recvBuff[7];
    uint8_t passLen = recvBuff[8 + nameLen];
    char name[nameLen + 1]; // extra for nullbyte
    char pass[passLen + 1];
    bzero(name, sizeof(name));
    bzero(pass, sizeof(pass));
    memcpy(name, recvBuff + 8, nameLen);
    memcpy(pass, recvBuff + 9 + nameLen, passLen);
    // finding room
    int found = 0;
    struct room *curr = roomList;
    while(curr != NULL){
        if(strcmp(name, curr->name) == 0){
            found = 1;
            break;
        }
        curr = curr->next;
    }
    // if found then try password
    if(found){
        if(strcmp(curr->pass, pass) == 0){
            curr->userCount += 1;
            local->room = curr;
            send(sock, sendBuff, sizeof(sendBuff), 0);
        }else{
            // password mismatch so send error packet (copied from wireshark)
            uint8_t errorPacket[] = {
                0x00, 0x00, 0x00, 0x1e, 0x04, 0x17, 0x9a, 0x01,
                0x49, 0x6e, 0x76, 0x61, 0x6c, 0x69, 0x64, 0x20,
                0x70, 0x61, 0x73, 0x73, 0x77, 0x6f, 0x72, 0x64,
                0x2e, 0x20, 0x49, 0x74, 0x27, 0x73, 0x20, 0x61,
                0x20, 0x74, 0x72, 0x61, 0x70
            };
            send(sock, errorPacket, sizeof(errorPacket), 0);
        }
    }else{ // create new room and add user
        struct room *newRoom = malloc(sizeof(struct room));
        //strncpy(newRoom->name, name, nameLen);
        strcpy(newRoom->name, name);
        //strncpy(newRoom->pass, pass, passLen);
        strcpy(newRoom->pass, pass);
        newRoom->userCount = 1;
        local->room = newRoom;
        // if list empty
        if(!roomList){
            roomList = newRoom;
        }else{
            newRoom->next = roomList;
            roomList = newRoom;
        }
        numRooms++;
        send(sock, sendBuff, sizeof(sendBuff), 0);
    }
}

void changeNick(int sock, uint8_t recvBuff[]){
    uint8_t sendBuff[8];
    struct client *local = clientList;
    while(local != NULL){
        if(local->sock == sock){
            break;
        }
        local = local->next;
    }
    bzero(sendBuff, sizeof(sendBuff));
    *(uint32_t *)sendBuff = htonl(1);
    *(uint16_t *)&sendBuff[4] = htons(0x0417);
    sendBuff[6] = 0x9a;
    sendBuff[7] = 0x00;

    // get new nick
    uint8_t nickLen = recvBuff[7];
    char nick[nickLen + 1];
    bzero(nick, sizeof(nick));
    memcpy(nick, recvBuff + 8, nickLen);
    // find if nick already exists
    int found = 0;
    struct client *curr = clientList;
    while(curr != NULL){
        if(strcmp(nick, curr->nick) == 0){
            found = 1;
            break;
        }
        curr = curr->next;
    }

    if(found){
        uint8_t errorPacket[] = {
            0x00, 0x00, 0x00, 0x4b, 0x04, 0x17, 0x9a, 0x01,
            0x54, 0x68, 0x69, 0x73, 0x20, 0x6e, 0x69, 0x63,
            0x6b, 0x20, 0x62, 0x65, 0x6c, 0x6f, 0x6e, 0x67,
            0x73, 0x20, 0x74, 0x6f, 0x20, 0x73, 0x6f, 0x6d,
            0x65, 0x6f, 0x6e, 0x65, 0x20, 0x65, 0x6c, 0x73,
            0x65, 0x2e, 0x20, 0x48, 0x65, 0x20, 0x68, 0x61,
            0x73, 0x20, 0x61, 0x20, 0x76, 0x65, 0x72, 0x79,
            0x20, 0x70, 0x61, 0x72, 0x74, 0x69, 0x63, 0x75,
            0x6c, 0x61, 0x72, 0x20, 0x73, 0x65, 0x74, 0x20,
            0x6f, 0x66, 0x20, 0x73, 0x6b, 0x69, 0x6c, 0x6c,
            0x73, 0x2e
        };
        send(sock, errorPacket, sizeof(errorPacket), 0);
    }else{
        strcpy(local->nick, nick);
        local->num = -1; // frees up rand num availability
        send(sock, sendBuff, sizeof(sendBuff), 0);
    }
}

void sendMSG(int sock, uint8_t recvBuff[]){
    uint8_t sendBuff[8];
    struct client *local = clientList;
    while(local != NULL){
        if(local->sock == sock){
            break;
        }
        local = local->next;
    }
    bzero(sendBuff, sizeof(sendBuff));
    *(uint32_t *)sendBuff = htonl(1);
    *(uint16_t *)&sendBuff[4] = htons(0x0417);
    sendBuff[6] = 0x9a;
    sendBuff[7] = 0x00;
    uint8_t nickLen = recvBuff[7];
    uint8_t msgLen = recvBuff[8 + nickLen + 1];
    char nick[nickLen + 1];
    char msg[msgLen + 1];
    bzero(nick, sizeof(nick));
    bzero(msg, sizeof(msg));
    memcpy(nick, recvBuff + 8, nickLen);
    memcpy(msg, recvBuff + nickLen + 9 + 1, msgLen);

    int found = 0;
    struct client *curr = clientList;
    while(curr != NULL){
        if(strcmp(nick, curr->nick) == 0){
            found = 1;
            int localLen = strlen(local->nick);
            int totalLen = 1 + localLen + 2 + msgLen;
            char msgBuff[totalLen + 7];
            bzero(msgBuff, sizeof(msgBuff));
            *(uint32_t *)msgBuff = htonl(totalLen);
            *(uint16_t *)&msgBuff[4] = htons(0x0417);
            msgBuff[6] = 0x12;
            msgBuff[7] = (uint8_t) localLen;
            memcpy(msgBuff + 8, local->nick, localLen);
            msgBuff[localLen + 8] = 0x00;
            msgBuff[localLen + 9] = (uint8_t) msgLen;
            memcpy(msgBuff + localLen + 10, msg, msgLen);
            send(curr->sock, msgBuff, sizeof(msgBuff), 0);

            break;
        }
        curr = curr->next;
    }
    if(found){
        send(sock, sendBuff, sizeof(sendBuff), 0);
    }else{ // send error
        uint8_t errorPacket[] = {
            0x00, 0x00, 0x00, 0x29, 0x04, 0x17, 0x9a, 0x01,
            0x4e, 0x69, 0x63, 0x6b, 0x20, 0x6e, 0x6f, 0x74,
            0x20, 0x70, 0x72, 0x65, 0x73, 0x65, 0x6e, 0x74,
            0x2e, 0x20, 0x48, 0x65, 0x27, 0x73, 0x20, 0x67,
            0x6f, 0x6e, 0x65, 0x20, 0x74, 0x6f, 0x20, 0x41,
            0x6c, 0x64, 0x65, 0x72, 0x61, 0x61, 0x6e, 0x2e
        };
        send(sock, errorPacket, sizeof(errorPacket), 0);
    }
}

void chat(int sock, uint8_t recvBuff[]){
    struct client *local = clientList;
    while(local != NULL){
        if(local->sock == sock){
            break;
        }
        local = local->next;
    }
    if(!local->room){
        uint8_t errorPacket[] = { // if not in room
            0x00, 0x00, 0x00, 0x2b, 0x04, 0x17, 0x9a, 0x01,
            0x59, 0x6f, 0x75, 0x20, 0x73, 0x70, 0x65, 0x61,
            0x6b, 0x20, 0x74, 0x6f, 0x20, 0x6e, 0x6f, 0x20,
            0x6f, 0x6e, 0x65, 0x2e, 0x20, 0x54, 0x68, 0x65,
            0x72, 0x65, 0x20, 0x69, 0x73, 0x20, 0x6e, 0x6f,
            0x20, 0x6f, 0x6e, 0x65, 0x20, 0x68, 0x65, 0x72,
            0x65, 0x2e
        };
        send(sock, errorPacket, sizeof(errorPacket), 0);
    }else{ // in room
        uint8_t sendBuff[8]; // sent back to the calling client
        bzero(sendBuff, sizeof(sendBuff));
        *(uint32_t *)sendBuff = htonl(1);
        *(uint16_t *)&sendBuff[4] = htons(0x0417);
        sendBuff[6] = 0x9a;
        sendBuff[7] = 0x00;
        int roomLen = recvBuff[7];
        int msgLen = recvBuff[9 + roomLen];
        int localLen = strlen(local->nick);

        send(sock, sendBuff, sizeof(sendBuff), 0);

        // sent to everyone in room
        int totalLen = 1 + roomLen + 1 + localLen + 2 + msgLen;
        char msgBuff[totalLen + 7];
        char msg[msgLen + 1];
        memcpy(msg, recvBuff + 7 + 1 + roomLen + 2, msgLen);
        *(uint32_t *)msgBuff = htonl(totalLen);
        *(uint16_t *)&msgBuff[4] = htons(0x0417);
        msgBuff[6] = 0x15;

        msgBuff[7] = (uint8_t) roomLen;
        memcpy(msgBuff + 8, local->room->name, roomLen);

        msgBuff[8 + roomLen] = (uint8_t) localLen;
        memcpy(msgBuff + 9 + roomLen, local->nick, localLen);

        msgBuff[9 + roomLen + localLen] = 0x00;
        msgBuff[10 + roomLen + localLen] = (uint8_t) msgLen;
        memcpy(msgBuff + 11 + roomLen + localLen, msg, msgLen);
        
        struct client *curr = clientList;
        while(curr != NULL){
            if(local != curr && local->room == curr->room){
                send(curr->sock, msgBuff, sizeof(msgBuff), 0);
            }
            curr = curr->next;
        }
    }
}