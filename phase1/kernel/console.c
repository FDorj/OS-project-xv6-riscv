//
// Console input and output, to the uart.
// Reads are line at a time.
// Implements special input characters:
//   newline -- end of line
//   control-h -- backspace
//   control-u -- kill line
//   control-d -- end of file
//   control-p -- print process list
//

#include <stdarg.h>

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "proc.h"
#include "console.h"

#define BACKSPACE 0x100
#define C(x)  ((x)-'@')  // Control-x

//
// send one character to the uart.
// called by printf(), and to echo input characters,
// but not from write().
//
void
consputc(int c)
{
  if(c == BACKSPACE){
    // if the user typed backspace, overwrite with a space.
    uartputc_sync('\b'); uartputc_sync(' '); uartputc_sync('\b');
  } else {
    uartputc_sync(c);
  }
}

struct {
  struct spinlock lock;
  
  // input
#define INPUT_BUF_SIZE 128
  char buf[INPUT_BUF_SIZE];
  uint r;  // Read index
  uint w;  // Write index
  uint e;  // Edit index
} cons;

struct {
  char bufferArr[MAX_HISTORY][INPUT_BUF_SIZE];
  uint lenghtsArr[MAX_HISTORY];
  uint lastCommandIndex;
  int numOfCommandsInMem;
  int currentHistory;
}historyBufferArray;

struct {
  int arrowIndex;
  int countUp;
  int countDown;
}arrowUpDown;

//
// user write()s to the console go here.
//
int
consolewrite(int user_src, uint64 src, int n)
{
  int i;

  for(i = 0; i < n; i++){
    char c;
    if(either_copyin(&c, user_src, src+i, 1) == -1)
      break;
    uartputc(c);
  }

  return i;
}

//
// user read()s from the console go here.
// copy (up to) a whole input line to dst.
// user_dist indicates whether dst is a user
// or kernel address.
//
int
consoleread(int user_dst, uint64 dst, int n)
{
  uint target;
  int c;
  char cbuf;

  target = n;
  acquire(&cons.lock);
  while(n > 0){
    // wait until interrupt handler has put some
    // input into cons.buffer.
    while(cons.r == cons.w){
      if(killed(myproc())){
        release(&cons.lock);
        return -1;
      }
      sleep(&cons.r, &cons.lock);
    }

    c = cons.buf[cons.r++ % INPUT_BUF_SIZE];

    if(c == C('D')){  // end-of-file
      if(n < target){
        // Save ^D for next time, to make sure
        // caller gets a 0-byte result.
        cons.r--;
      }
      break;
    }

    // copy the input byte to the user-space buffer.
    cbuf = c;
    if(either_copyout(user_dst, dst, &cbuf, 1) == -1)
      break;

    dst++;
    --n;

    if(c == '\n'){
      // a whole line has arrived, return to
      // the user-level read().
      break;
    }
  }
  release(&cons.lock);

  return target - n;
}

//
// the console input interrupt handler.
// uartintr() calls this for input character.
// do erase/kill processing, append to cons.buf,
// wake up consoleread() if a whole line has arrived.
//
void
consoleintr(int c)
{
  if (c == '[' && arrowUpDown.arrowIndex == 1) {
    arrowUpDown.arrowIndex = 2;
    return;
  } else if ((c == 'A' || c == 'B') && arrowUpDown.arrowIndex == 2)
  {
    arrowKey(c == 'A');
    return;
  }
  
  
  acquire(&cons.lock);

  switch(c){
    case '\e': 

  arrowUpDown.arrowIndex = 1;
  break;
  case C('P'):  // Print process list.
    procdump();
    break;
  case C('U'):  // Kill line.
    while(cons.e != cons.w &&
          cons.buf[(cons.e-1) % INPUT_BUF_SIZE] != '\n'){
      cons.e--;
      consputc(BACKSPACE);
    }
    break;
  case C('H'): // Backspace
  case '\x7f': // Delete key
    if(cons.e != cons.w){
      cons.e--;
      consputc(BACKSPACE);
    }
    break;
  default:
    if(c != 0 && cons.e-cons.r < INPUT_BUF_SIZE){
      c = (c == '\r') ? '\n' : c;
      // echo back to the user.
      consputc(c);
      // store for consumption by consoleread().
      cons.buf[cons.e++ % INPUT_BUF_SIZE] = c;
      if(c == '\n' || c == C('D') || cons.e-cons.r == INPUT_BUF_SIZE){
        char historyInArray[7] = "history";
        char commandString[INPUT_BUF_SIZE];
        int j = 0;
        int i = 0;
        for(i = cons.w; i < cons.e; i++){ //copy command in commandString till before \n
          commandString[j] = cons.buf[i];
          j++;
        }
        commandString[j] = 0;
        if(strncmp(commandString, historyInArray, 7) != 0){  // check if the command is not history
          // Assuming commandString is a string and we want to store each character in the buffer
          for(int i = 0; i < strlen(commandString); ++i) {
            historyBufferArray.bufferArr[historyBufferArray.lastCommandIndex][i] = commandString[i];
          }
          historyBufferArray.lenghtsArr[historyBufferArray.lastCommandIndex] = strlen(commandString);
          historyBufferArray.lastCommandIndex++;
          historyBufferArray.currentHistory = historyBufferArray.lastCommandIndex -1;

          if(historyBufferArray.lastCommandIndex == 16){
            historyBufferArray.lastCommandIndex = 0;

          }
          
          if(historyBufferArray.numOfCommandsInMem < 16){
            historyBufferArray.numOfCommandsInMem++;
          }

          arrowUpDown.countUp = 0;
        }
        // wake up consoleread() if a whole line (or end-of-file)
        // has arrived.
        cons.w = cons.e;
        wakeup(&cons.r);
      }
    }
    break;
  }
  
  release(&cons.lock);
}

void
consoleinit(void)
{
  initlock(&cons.lock, "cons");

  uartinit();

  // connect read and write system calls
  // to consoleread and consolewrite.
  devsw[CONSOLE].read = consoleread;
  devsw[CONSOLE].write = consolewrite;
}

int history(int historyId){
  if(historyId < 16){
      for(int i = 0; i < historyBufferArray.numOfCommandsInMem; i++){
      printf("%s", historyBufferArray.bufferArr[i]);
      }
      printf("requested command: %s\n", historyBufferArray.bufferArr[historyId]);
  }else{
    return -1;
  }
  return 0;
}

int arrowKey(int isUp){
  char commandToPrint[INPUT_BUF_SIZE];
  while (cons.w != cons.e && cons.buf[(cons.e - 1) % INPUT_BUF_SIZE] != '\n'){
        cons.e--;
    consputc(BACKSPACE);
  }

  if (isUp){
    // printf("UP");
    if(historyBufferArray.numOfCommandsInMem > 0){
      if((historyBufferArray.numOfCommandsInMem < 16 && historyBufferArray.currentHistory > -1) || (historyBufferArray.numOfCommandsInMem == 16 && arrowUpDown.countUp < historyBufferArray.numOfCommandsInMem)){

      strncpy(commandToPrint, historyBufferArray.bufferArr[historyBufferArray.currentHistory], historyBufferArray.lenghtsArr[historyBufferArray.currentHistory]-1);
      
      for (int i = 0; i < INPUT_BUF_SIZE && commandToPrint[i] != 0; i++)
      {
        consoleintr(commandToPrint[i]);
      }
      arrowUpDown.countUp++;
      arrowUpDown.countDown = arrowUpDown.countUp;
      historyBufferArray.currentHistory--;
      if(historyBufferArray.numOfCommandsInMem == 16 && historyBufferArray.currentHistory < 0){
        historyBufferArray.currentHistory = 15;
      }
    return 0;
      }
    }
  }else {
    if(arrowUpDown.countDown > 0){
    // printf("down");
    strncpy(commandToPrint, historyBufferArray.bufferArr[historyBufferArray.currentHistory], historyBufferArray.lenghtsArr[historyBufferArray.currentHistory]-1);

      for (int i = 0; i < INPUT_BUF_SIZE && commandToPrint[i] != 0; i++)
      {
        consoleintr(commandToPrint[i]);
      }
      arrowUpDown.countDown--;
    historyBufferArray.currentHistory++;
    if(historyBufferArray.numOfCommandsInMem == 16 && historyBufferArray.currentHistory > 15){
      historyBufferArray.currentHistory = 0;
    }
    return 0;
  }
  }
  return -1;
}
