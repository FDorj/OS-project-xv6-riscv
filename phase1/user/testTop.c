#include "user.h"
#include "kernel/types.h"
#include "kernel/proc.h"

char* printState(enum procstate state){
    switch (state)
    {
    case USED: return "USED";
    case SLEEPING: return "SLEEPING";
    case RUNNING: return "RUNNING";
    case RUNNABLE: return "RUNNABLE";
    case ZOMBIE: return "ZOMBIE";
    default: return "UNUSED"; 
    }
}

int main(){
    struct top *t = (struct top*)malloc(sizeof(struct top));
    int error = top(t);
    if(error != 0){
        return -1;
    }
    printf("uptime : %d seconds\n", t->uptime);
    printf("total process : %d\n", t->total_process);
    printf("running process : %d\n", t->running_process);
    printf("sleeping process : %d\n", t->sleeping_process);
    printf("process data : \n");
    printf("name        PID        PPID        state\n");
    for (int i = 0; i < (sizeof(t->p_list)/sizeof(t->p_list[0])); ++i) {
        if(t->p_list[i].state == 0){
            break;
        }
        printf("%s        %d        %d        %s\n", t->p_list[i].name, t->p_list[i].pid, t->p_list[i].ppid, printState(t->p_list[i].state));
    }
    free(t);
    return 0;
}
