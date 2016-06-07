/*************************************************************************\
\*************************************************************************/

/* ctty.c

   Implement our cttyOpen() function,
 */
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <termios.h>
#include <sys/select.h>
#include <stdlib.h>
#include <fcntl.h>
#include "pty_fork.h"      /* Declaration of ptyFork() */
#include "tty_functions.h" /* Declaration of ttySetRaw() */
#include "tlpi_hdr.h"

#define BUF_SIZE 256
#define MAX_SNAME 1000

struct termios ttyOrig;

static void /* Reset terminal mode on program exit */
ttyReset(void)
{
  if (tcsetattr(STDIN_FILENO, TCSANOW, &ttyOrig) == -1) errExit("tcsetattr");
}

int
main(int argc, char *argv[])
{
  char slaveName[MAX_SNAME];
  int masterFd;
  int indevFd;
  int outdevFd;
  fd_set  inFds;
  char    buf[BUF_SIZE];
  ssize_t numRead;
  struct winsize ws;
  struct timeval tv;

  // TODO investigate if poss /storage/extSdcard/com.luabot.luabot/tmp/fifo_in

  // ln -s /data/data/jackpal.androidterm/app_fifos/app_in /data/local/tmp/fifos/app_in 
  // ln -s /data/data/jackpal.androidterm/app_fifos/app_out /data/local/tmp/fifos/app_out 
  //char *pathIn  = "/data/data/jackpal.androidterm/app_fifos/app_in";
  //char *pathOut  = "/data/data/jackpal.androidterm/app_fifos/app_out";
  char *pathIn  = "/data/local/tmp/fifos/out";
  char *pathOut  = "/data/local/tmp/fifos/in";

  // unlink(path);
  // ret = mknod(fifoName, S_IFIFO | 0666, 0);
  //mkfifo(pathIn,  0666);
  //mkfifo(pathOut, 0666);

  /* Retrieve the attributes of terminal on which we are started */

  if (tcgetattr(STDIN_FILENO, &ttyOrig) == -1) errExit("tcgetattr");

  if (ioctl(STDIN_FILENO, TIOCGWINSZ, (char *)&ws) < 0) errExit(
      "TIOCGWINSZ error");


  // cfmakeraw(&ttyOrig);

  /* Create a child process, with parent and child connected via a
     pty pair. The child is connected to the pty slave and its terminal
     attributes are set to be the same as those retrieved above. */

  switch (ptyFork(&masterFd, slaveName, MAX_SNAME, &ttyOrig, &ws)) {
  case -1:
    errExit("ptyFork");

  case 0: /* Child executes command given in argv[1]... */
    execvp(argv[1], &argv[1]);
    errExit("execvp");

  default: /* Parent relays data between terminal and pty master */

    indevFd = open(pathIn, O_RDWR);

    if (indevFd == -1) errExit("indevFd");
    outdevFd = open(pathOut, O_RDWR);

    if (outdevFd == -1) errExit("outdevFd");

    if (dup2(indevFd, STDIN_FILENO) != STDIN_FILENO) err_exit(
        "indevFd:dup2-STDIN_FILENO");

    if (dup2(outdevFd, STDOUT_FILENO) != STDOUT_FILENO) err_exit(
        "outdevFd:dup2-STDOUT_FILENO");

    if (dup2(outdevFd, STDERR_FILENO) != STDERR_FILENO) err_exit(
        "outdevFd:dup2-STDERR_FILENO");

    ttySetRaw(STDIN_FILENO,  &ttyOrig);
    ttySetRaw(STDOUT_FILENO, &ttyOrig);
    ttySetRaw(STDERR_FILENO, &ttyOrig);

    setvbuf(stdin,  NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    if (indevFd > STDERR_FILENO) close(indevFd);

    if (outdevFd > STDERR_FILENO) close(outdevFd);


    /* Loop monitoring terminal and pty master for input. If the
       terminal is ready for input, read some bytes and write
       them to the pty master. If the pty master is ready for
       input, read some bytes and write them to the terminal. */

    tv.tv_sec  = 1;
    tv.tv_usec = 0;

    for (;;) {
      FD_ZERO(&inFds);
      FD_SET(STDIN_FILENO, &inFds);
      FD_SET(masterFd,     &inFds);

      // if (select(masterFd + 1, &inFds, NULL, NULL, NULL) == -1)
      if (select(masterFd + 1, &inFds, NULL, NULL, &tv) == -1) errExit("select");

      if (FD_ISSET(STDIN_FILENO, &inFds)) {
        numRead = read(STDIN_FILENO, buf, BUF_SIZE);

        // fprintf(stderr,"Read %d bytes from stdin: \n",numRead);
        if (numRead <= 0) exit(EXIT_SUCCESS);

        if (write(masterFd, buf, numRead) != numRead) fatal(
            "partial/failed write (masterFd)");
      }

      if (FD_ISSET(masterFd, &inFds)) {
        numRead = read(masterFd, buf, BUF_SIZE);

        // fprintf(stderr,"Read %d bytes from ptmx\n",numRead);
        if (numRead <= 0) exit(EXIT_SUCCESS);

        if (write(STDOUT_FILENO, buf, numRead) != numRead) fatal(
            "partial/failed write (STDOUT_FILENO)");
      }
    }

    if (atexit(ttyReset) != 0) errExit("atexit");
  }
}

