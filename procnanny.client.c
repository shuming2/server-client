#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "procnanny.client.h"
#include <stdlib.h>
#include "memwatch.h"


PidList *pidmonitored;
Records records;
int childnum = 0;
char *message, *pname, *getmessage;
int sock, sendPipe[2];
char *currentTime;

int main(int argc, char *argv[]) {
  currentTime = (char*) malloc(255);

  // check for input command
  if (argc != 3) sendError(1);
  int my_port;
  sscanf(argv[2], "%d", &my_port);

  // shared data
  puts("shared data");
  PidList pl;
  pidmonitored = &pl;
  pidmonitored = mmap(NULL, sizeof(*pidmonitored), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  (*pidmonitored).numpid = 0;

  // kill previous procnanny.client
  puts("kill previous procnanny.client");
  killPreviousClient();

  // initialize pointers
  puts("initialize pointers");
  message = (char*) malloc(128);
  getmessage = (char*) malloc(128);
  pname = (char*) malloc(128);
  records.recordarr = malloc(128*(256+4));
  records.num = 0;


  // create pipes
  puts("create pipes");
  if (pipe(sendPipe) == -1) sendError(5);

  // initialize the socket
  puts("initialize the socket");
  initializeSocket(argv[1], my_port);

  // loop for check message received
  puts("loop for check message received");
  receiveMessage();

  return 1;
}

/**
 * monitor processes
 */
void monitorProcess (int sendPipe[2]) {
  int i, j, forkpid, runtime, pid;

  for (i = 0; i < records.num; i++) {
    //Get all pid of the same program
    PidList pidresult = pidof(records.recordarr[i].name);
    int no = pidresult.numpid; //Memorize # of pid

    //No process found
    if (no == 0) {
      char *buffer = malloc(255);
      sprintf(buffer, "%s  Info: No '%s' processes found.", getCurrentTime(), records.recordarr[i].name);
      write(sock, buffer, 256);
      free(buffer);
    }
    //Monitoring process
    for (j = 0; j < no; j++) {

      //Check if the process is monitored currently
      if (checkPidMonitored(pidresult.pidarr[j]) == 0) {

        addPidMonitored(pidresult.pidarr[j]);

        sprintf(message,"%s %d %d",records.recordarr[i].name, records.recordarr[i].time, pidresult.pidarr[j]);

        if (childnum < ((*pidmonitored).numpid)) {
          if ((forkpid = fork()) < 0) sendError(6);
          else if (forkpid > 0) {
            childnum++;
          }
          else if (forkpid == 0) {  //child process
            for(;;) {
              read(sendPipe[0], getmessage, 128);
              sscanf(getmessage, "%s %d %d", pname, &runtime, &pid);
              childExecution(pname, runtime, pid);
            }
          }
        }
        write(sendPipe[1], message, 128);
      }
    }
  }
}

/**
 * receive messages from server
 */
void receiveMessage() {
  fd_set fds;
  struct timeval timeout={0,0};
  int maxfd = sock;
  // loop of receiving messages
  for (;;) {
    FD_ZERO(&fds); //clear it
    FD_SET(sock, &fds);

    if (select(maxfd+1, &fds, NULL, NULL, &timeout) == -1) {
      sendError(11);
    } else {
      if (FD_ISSET(sock, &fds)) { // test if socket is readable
        char *buffer = (char*) malloc(256);

        read(sock, buffer, 256);

        if (strcmp(buffer, "clearclose")==0) {
          clearClose();
          free(buffer);
        } else {
          int i;
          for (i = 0; i < records.num; i++) free(records.recordarr[i].name);
          updateConfiguration(buffer);
          monitorProcess(sendPipe);
        }
      }
    }
  }
}

/**
 * initialize the socket
 */
void initializeSocket(char *hostname, int port) {
  // stolen from sample code
  struct	sockaddr_in	server;
  struct	hostent		*host;

  /* Put here the name of the sun on which the server is executed */
  host = gethostbyname (hostname);

  if (host == NULL) sendError(8);

  sock = socket (AF_INET, SOCK_STREAM, 0);
  if (sock < 0) sendError(9);

  bzero (&server, sizeof (server));
  bcopy (host->h_addr, & (server.sin_addr), (size_t)host->h_length);
  server.sin_family = (sa_family_t) host->h_addrtype;
  server.sin_port = htons (port);

  if (connect(sock, (struct sockaddr*) &server, sizeof (server))) sendError(10);
}

/**
 * clearly exit when receiving "clearclose" message
 */
void clearClose() {
  int i;

  // free memory
  for (i = 0; i < records.num; i++) free(records.recordarr[i].name);
  free(records.recordarr);
  free(message);
  free(pname);
  free(getmessage);
  free(currentTime);

  // clear all the child processes
  killPreviousClient();

  // close the connection
  close(sock);
}

/**
 * update configuration message
 */
Records updateConfiguration(char *buffer) {
  char *name = (char*) malloc(255);
  int runtime, i = 0, j;
  for (j = 0; *(buffer+j); j++){
    if (*(buffer+j) == '(') {
      sscanf(buffer+j, "(%s %d)", name, &runtime);
      records.recordarr[i].name = (char *) malloc(255);
      strcpy(records.recordarr[i].name, name);
      records.recordarr[i].time = runtime;
      i++;
    }
  }
  records.num = i;
  free(name);
  free(buffer);
  return records;
}

/**
 * kill previous procnanny.client process
 */
void killPreviousClient() {
  char *buffer = (char*) malloc(255);
  sprintf(buffer,"pidof procnanny.client -o %d | xargs kill -9", getpid()); //ignore parent process
  system(buffer);
  free(buffer);
}

/**
 * get time format string
 * Format: [Mon Oct 26 11:50:17 MST 2015]
 */
char *getCurrentTime () {
  time_t t;
  char *date = malloc(128), *year = malloc(128);
  tzset(); //tzname[0] gets the timezone name
  time(&t); //get current time
  strftime(date, 128, "%a %b %d %T", localtime(&t));
  strftime(year, 128, "%Y", localtime(&t));
  // currentTime = date + timezone + year
  sprintf(currentTime, "[%s %s %s]", date, tzname[0],year);
  free(date);
  free(year);
  return currentTime;
}

/**
 * get all pid by program name
 */
PidList pidof(char *name) {
  PidList pids;
  int i = 0, pid;
  char *buffer = (char*) malloc(255);
  sprintf(buffer,"pidof %s", name);
  FILE *pidofOut = popen(buffer,"r");
  while (fscanf(pidofOut, "%d", &pid) == 1) {
    pids.pidarr[i] = pid;  //convert from char* type to int
    i++;
  }
  pclose(pidofOut);
  free(buffer);
  pids.numpid = i;
  return pids;
}

/**
 * child process monitors the process
 */
void childExecution(char *pname, int runtime, int pid) {
  char *buffer = (char*) malloc(255);
  sprintf(buffer, "%s  Info: Initializing monitoring of process '%s' (PID %d).", getCurrentTime(), pname, pid);
  write(sock, buffer, 256);;

  sleep(runtime);  //sleep time
  if (kill(pid, SIGKILL) == 0) {  //check if process monitored is still alive
    sprintf(buffer, "%s  Action: PID %d (%s) killed after exceeding %d seconds.", getCurrentTime(), pid, pname, runtime);
    write(sock, buffer, 256);;
  }
  free(buffer);
  delPidMonitored(pid);
}

/**
 * send error message to stdout
 */
void sendError(int error) {
  char *errorMessage = (char*) malloc(255);
  if (error == 1) strcpy(errorMessage, "Error: Wrong command.($./procnanny.server <configfile>)");
  else if (error == 2) strcpy(errorMessage, "Error: Failure of getting path of log file");
  else if (error == 3) strcpy(errorMessage, "Error: Failure of killing previous process");
  else if (error == 4) strcpy(errorMessage, "Error: Failure of opening config file");
  else if (error == 5) strcpy(errorMessage, "Error: Failure of creating pipes");
  else if (error == 6) strcpy(errorMessage, "Error: Failure of forking");
  else if (error == 7) strcpy(errorMessage, "Error: Failure of getting path of server info file");
  else if (error == 8) strcpy(errorMessage, "Error: Client: cannot get host description");
  else if (error == 9) strcpy(errorMessage, "Error: Client: cannot open socket");
  else if (error == 10) strcpy(errorMessage, "Error: Client: cannot connect to server");
  else if (error == 11) strcpy(errorMessage, "Error: Client: Failure of selecting");
  fprintf(stderr,"%s  %s \n", getCurrentTime(), errorMessage);
  free(errorMessage);
  exit(-1);
}


/**
 * check if the pid is in the pids monitored
 */
int checkPidMonitored(int pid) {
  int i;
  for (i = 0; i < (*pidmonitored).numpid; i++) {
    if ((*pidmonitored).pidarr[i] == pid) return 1;
  }
  return 0;
}

/**
 * add pid to pids monitored
 */
void addPidMonitored(int pid) {
  (*pidmonitored).pidarr[(*pidmonitored).numpid] = pid;
  (*pidmonitored).numpid += 1;
}

/**
 * delete pid from pids monitored
 */
void delPidMonitored(int pid) {
  int i, index;
  for (i = 0; i < (*pidmonitored).numpid; i++) {
    if ((*pidmonitored).pidarr[i] == pid) {
      index = i;
      break;
    }
  }
  for (i = index; i < (*pidmonitored).numpid - 1; i++)
  {
    (*pidmonitored).pidarr[i] = (*pidmonitored).pidarr[i + 1];
  }
  (*pidmonitored).numpid -= 1;
}
