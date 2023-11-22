#include "user.h"
#include "kernel/types.h"

int main(int argc, char *argv[]){

    int historyId = atoi(argv[1]);
    int error = history(historyId);
    if(error != 0){
        return -1;
    }
    return 0;
}
