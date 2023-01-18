#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int main(int argc, char *argv[]){
    // TODO
    int i;
    if (argc < 2){
        fprintf(2, "Usage: mkdir files...\n");
        exit(1);
    }
    for (i = 1; i < argc; i++){
        if (mkdirat(AT_FDCWD,argv[i],0) < 0){
            fprintf(2, "mkdir: %s failed to create\n", argv[i]);
            break;
        }
    }
    exit(0);
}