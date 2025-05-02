#define _POSIX_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

int o_id = 0;
int id;
int p1;
int p2;


void process_order(char order[80]) {
    char begin[16], com[16], item[16];
    int num, p, size;
    int ret = sscanf(order, "%s %s %s %d %d;", begin, com, item, &num, &p);
    if (ret == 5) {
        if (num >= 1000) { 
            close(p1);
            close(p2);
            exit(0);
        }
        if (strcmp(com, "SELL") == 0) {
            char send[80];
            memset(send, 0, sizeof(send));
            size = sprintf(send, "BUY %d %s %d %d;", o_id, item, num, p);
            write(p2, send, size);
            kill(getppid(), SIGUSR1);
            o_id += 1;
        }
    }
}



void process_signal(int sig) {
    char order[80] = {0};
    read(p1, order, sizeof(order) - 1);
    process_order(order);
}

void open_fifo() {
    char name1[30] = {0}, name2[30] = {0};
    sprintf(name1, "/tmp/pe_exchange_%d", id);
    p1 = open(name1, 0);
    sprintf(name2, "/tmp/pe_trader_%d", id);
    p2 = open(name2, 1);
}


int main(int argc, char ** argv) {
    if (argc < 2) {
        // printf("Not enough arguments\n");
        exit(1);
    // } else { 
    //     printf("Launching trader pe_trader\n");
    }
    id = atoi(argv[1]);
    struct sigaction sa;
    sa.sa_handler = process_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(SIGUSR1, &sa, NULL) == -1) {
        perror("Error\n");
        exit(1);
    }
    open_fifo();
    while (1) {
        
    }
    return 0;
}
