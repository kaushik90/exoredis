#include "redisserver.h"

int main(int argc, char *argv[]){
 // binding save method on exit signal
    FILE *fp;
    if(argc == 1){
        DB_FILE_PATH = DEFAULT_FILE_PATH;
        printf("%s %s\n", "No file path provided, using default: ", DB_FILE_PATH );
    } else if(argc == 2){
        DB_FILE_PATH = argv[1];
        if(access(DB_FILE_PATH, W_OK) == 0 || fopen(DB_FILE_PATH, "w+")){
            printf("%s %s\n", "DB will be save to file at: ", DB_FILE_PATH );
            fclose(fp);
        } else {
            DB_FILE_PATH = DEFAULT_FILE_PATH;
            printf("%s %s\n", "File path not writable. DB will be save to file at: ", DB_FILE_PATH );
        }
        
    } else {
        printf("%s\n", "Too many arguments." );
        return EXIT_FAILURE;
    }

    signal(SIGINT, sig_handler);

    COMMANDS = initializeCmdTable();

    HASH_TABLE = newHashTable(INITIAL_SIZE);

    GARBAGE_LIST = newGarbageList();

    if(COMMANDS && HASH_TABLE && GARBAGE_LIST){
        spinServer();       
    } else {
        freeHashTable(COMMANDS);
        freeHashTable(HASH_TABLE);
        free(GARBAGE_LIST);
        printf(RED "Failed to initialize server.\n" RESET);
        return EXIT_FAILURE; 
    }
    return EXIT_SUCCESS; 
}

int spinServer(){
    printf("%s\n", "Loading DB from file...");
    if(loadFromDB(DB_FILE_PATH) != -1){
        printf("%s\n", "DB load Successfull.");
    } else {
        printf("%s\n", "DB load Failed.");
    }
    printf("%s\n", "Server is ready and accepting connections on port 15000.");
    run();
    return 0;
}

void run(){
    evutil_socket_t listener;
    struct sockaddr_in sin;
    struct event_base *base;
    struct event *listener_event;

    base = event_base_new();
    if (!base)
        return; /*XXXerr*/

    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = 0;
    sin.sin_port = htons(SERVER_PORT);

    listener = socket(AF_INET, SOCK_STREAM, 0);
    evutil_make_socket_nonblocking(listener);

    if (bind(listener, (struct sockaddr*)&sin, sizeof(sin)) < 0) {
        perror("bind");
        return;
    }

    if (listen(listener, 16)<0) {
        perror("listen");
        return;
    }

    listener_event = event_new(base, listener, EV_READ|EV_PERSIST, do_accept, (void*)base);
    /*XXX check it */
    event_add(listener_event, NULL);

    event_base_dispatch(base);
}

void do_accept(evutil_socket_t listener, short event, void *arg){
    struct event_base *base = arg;
    struct sockaddr_storage ss;
    socklen_t slen = sizeof(ss);
    int fd = accept(listener, (struct sockaddr*)&ss, &slen);
    if (fd < 0) {
        perror("accept");
    } else if (fd > FD_SETSIZE) {
        close(fd);
    } else {
        struct bufferevent *bev;
        evutil_make_socket_nonblocking(fd);
        bev = bufferevent_socket_new(base, fd, BEV_OPT_CLOSE_ON_FREE);
        bufferevent_setcb(bev, readcb, NULL, errorcb, NULL);
        bufferevent_setwatermark(bev, EV_READ, 0, MAX_LINE);
        bufferevent_enable(bev, EV_READ|EV_WRITE);
    }
}

void readcb(struct bufferevent *bev, void *ctx){
    char tmp[10000];
    int n;
    unsigned long bytesread;
    argList* tokens = NULL;
    exoVal *result;

    /*
    * currently putting a cap on input buffer to be max upto 1mb
    * This is a good candidate for configurable options. If input
    * byte stream received from client is > 1mb, it will be dropped 
    * and an error message will be returned.
    */
    char *buf = (char*)malloc((sizeof(char) * INPUT_BUFFER_SIZE ) + 10);
    char *runner = buf;
    struct evbuffer *input, *output;
    output = bufferevent_get_output(bev);
    input = bufferevent_get_input(bev);

    bytesread = evbuffer_get_length(input);
    evbuffer_remove(input, buf, bytesread);
    runner += bytesread;
    /*
    * bufferevent_get_input API of libevent reads only first 4096 bytes form the socket buffer
    * per read callback. Below code reads rest of the bytes from the socket buffer and appends
    * it to our input buffer. Annoying hard coded limit of 4096 found can be found
    * at https://github.com/libevent/libevent/blob/master/buffer.c#L2198

    * HACK begins
    * get the file discriptior on which this instance of read callback is triggered.
    * We have to possibly read more data from the fd buffer if available
    */
    evutil_socket_t fd = bufferevent_getfd(bev);
    while(1){
        n = recv(fd, tmp, sizeof(tmp), 0);
        if (n <= 0){
            break;
        }
        memcpy(runner, tmp, n);
        runner += n;
        bytesread += n;
    }
    /*
    * HACK ends
    */

    /*
    * Currently putting a cap on max byte stream accepted from client at 1mb. If byte
    * stream received from client is > 1mb, protocol error will be returned
    */
    if(bytesread < INPUT_BUFFER_SIZE){
        tokens = bufferTokenizer(buf, bytesread);
    }

    if(tokens){
        if( tokens->size ){
            result = commandDispatcher(tokens);
            writeToBuffer(result, output);
        }
    } else {
        result = returnError(PROTOCOL_ERROR);
        writeToBuffer(result, output);
    }
    freeGarbage();
    free(buf);
}

void writeToBuffer(exoVal *result, struct evbuffer *output){
    exoString *str, *num_str;
    switch (result->ds_type){
    case SIMPLE_STRING:
        str = (exoString*)result->val_obj;
        evbuffer_add(output, str->buf, str->len); 
        evbuffer_add(output, "\r\n", 2);
        return;

    case RESP_ERROR:
        str = (exoString*)result->val_obj;
        evbuffer_add(output, str->buf, str->len);
        evbuffer_add(output, "\r\n", 2);
        return;

    case RESP_INTEGER:
        str = (exoString*)result->val_obj;
        evbuffer_add(output, ":", 1);
        evbuffer_add(output, str->buf, str->len);
        evbuffer_add(output, "\r\n", 2);
        return;

    case BULKSTRING:
        evbuffer_add(output, "$", 1);
        str = (exoString*)result->val_obj;
        num_str = numberToString(str->len);
        evbuffer_add(output, num_str->buf, num_str->len);
        evbuffer_add(output, "\r\n", 2);
        evbuffer_add(output, str->buf, str->len);
        evbuffer_add(output, "\r\n", 2);
        return;

    case RESP_ARRAY:
        writeRespArrayToBuffer(result, output);
        return;
    }
}

void writeRespArrayToBuffer(exoVal *result, struct evbuffer *output){
    linkedList *list = (linkedList*)result->val_obj;
    listNode *node = list->head;
    exoString *tmp;
    tmp = numberToString(list->size);
    evbuffer_add(output, "*", 1);
    evbuffer_add(output, tmp->buf, tmp->len);
    evbuffer_add(output, "\r\n", 2);
    while(node){
        tmp = numberToString(node->key->len);
        evbuffer_add(output, "$", 1);
        evbuffer_add(output, tmp->buf, tmp->len);
        evbuffer_add(output, "\r\n", 2);
        evbuffer_add(output, node->key->buf, node->key->len);
        evbuffer_add(output, "\r\n", 2);
        node = node->next;
    }
    // Have to think about it
    //freeLinkedList(list);
    return;
}

void errorcb(struct bufferevent *bev, short error, void *ctx){
    if (error & BEV_EVENT_EOF) {
        /* connection has been closed, do any clean up here */
        /* ... */
    } else if (error & BEV_EVENT_ERROR) {
        /* check errno to see what error occurred */
        /* ... */
    } else if (error & BEV_EVENT_TIMEOUT) {
        /* must be a timeout event handle, handle it */
        /* ... */
    }
    bufferevent_free(bev);
}

/*
Finds the command fired in the command hash_table.
If found, task is dispatched to the found command else
returns COMMAND NOT FOUND err
*/
exoVal* commandDispatcher(argList* tokens){
    exoString* cmd_str = tokens->head->key;
    exoVal *result = NULL;
    exoVal* node = get(COMMANDS, upCase(cmd_str));
    exoCmd* cmd;
    if(node){
        cmd = (exoCmd*)node->val_obj;
        result = executeCommand(cmd, tokens);
    } else {
        result = returnError(COMMAND_NOT_FOUND);
    }

    if(!node || cmd->free_args || result->ds_type == RESP_ERROR){
        freeAllArgs(tokens);
    } else {
        freeDeadArgs(tokens);
    }
    return result;
}

/*
Checks if the passed args are compliant to the end API.
If compliant, it sends of the args to the function pointer
store in the exoCmd struct. Else it retrun WRONG_NUMBER_OF_ARGUMENTS err
*/
exoVal* executeCommand(exoCmd* cmd, argList* tokens){
    if(strcmp(cmd->cmd_str, "SAVE") == 0 ){
        return saveCommand(DB_FILE_PATH);
    } else if(strcmp(cmd->cmd_str, "LOADDB") == 0){
        if(loadFromDB(DB_FILE_PATH) != -1){
            return returnOK();
        } else {
            return returnNull();
        }
    }

    if(cmd->skip_arg_test || \
        validArgs(cmd->args_count, tokens->size -1, cmd->variable_arg_count)){
        return cmd->f_ptr(tokens);
    } else {
        return returnError(WRONG_NUMBER_OF_ARGUMENTS);
    }
    return returnNull();
}

/*
Utility function used by executeCommand
***** HAVE TO MODIFY FOR ZADD.... think about minimum args and then dynamic arg nature
*/
bool validArgs(size_t arg_count, unsigned long args_passed, bool variable_args){
    printf("%s %zu %lu\n", WHT "validArgs called with: " RESET, arg_count, args_passed);
    if(arg_count == args_passed){
        return true;
    } else if(args_passed && variable_args){
        return args_passed % arg_count ? false : true;
    }
    return false;
}

/*
Function binded to inturrupt signal. It is excuted when user hits ctrl+C
*/
void sig_handler(int signo) {
    if(signo == SIGINT){
        printf("received SIGINT\n");
        if(-1 == shutdownServer()){
            perror("Cannot shutdown server properly.");
        } else {
            actionBeforeExit();
            printf("%s\n", "Server shutdown successfully.");
        }
    }
    exit(0);
}

/*
It shuts down the server after writing all the stored data in rdb file for persistence.
*/
int shutdownServer(){
    printf("%s\n","Shutting Down server...");
    // here we have to free any TCP port that we are holding << 1500 >>
    return 0;
}

/*
Function called by shutdownServer() for persistence.
It frees all the memory used by server after writing the rdb file. 
*/
void actionBeforeExit(){
    saveCommand(DB_FILE_PATH);
    freeHashTable(COMMANDS);
    freeHashTable(HASH_TABLE);
    freeGarbage();
    return;
}

void freeGarbage(){
    garbageNode *node = GARBAGE_LIST->head;
    garbageNode *tmp;
    exoVal *val;
    while(node){
        tmp = node->next;
        val = node->garbage;
        if(val){
            switch(val->ds_type){
            case BITMAP:
                freebitmapNode((bitmapNode*)val->val_obj);
                break;
            case SORTED_SET:
                freeSkipList((skipList*)val->val_obj);
                break;
            case BULKSTRING:
                freeExoString((exoString*)val->val_obj);
                break;
            }
            free(val);
        }
        free(node);
        node = tmp;
    }
    GARBAGE_LIST->head = GARBAGE_LIST->tail = NULL;
}