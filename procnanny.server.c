#include <stdio.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/fcntl.h>
#include <sys/errno.h>
#include "procnanny.server.h"
#include "memwatch.h"

#define MY_PORT 9583


// shared data
char *currentTime, *configPath, *configMessage, *nodename[256] = {0}, *nodeString;
int clients[32] = {0}, numofClient = 0, sock, killcount, reread = 0, nodeno = 0;


int main(int argc, char *argv[]) {
  // check for input command
  if (argc != 2) sendError(1);

  // kill previous server processes
  killPreviousServer();

  // initialize pointers;
  nodeString = (char*) malloc(255);
  configMessage = (char*) malloc(255);
  currentTime = (char*) malloc(255);
  configPath = (char*) malloc(255);
  strcpy(configPath, argv[1]);

  // set up signal handler
  signal(SIGHUP, sighuphandler);
  signal(SIGINT, siginthandler);

  // print server info to a file
  printServerInfo();

  // read configuration file
  readConfigFile();

  // shared memory between parent process and child processes
  killcount = 0;

  // initialize the socket for connection
  initializeMasterSocket();


  int	client;
  struct	sockaddr_in from;
  socklen_t fromlength;
  fd_set fds, fds_full;
  struct timespec timeout={0,0};
  fromlength = sizeof (from);
  int i, j, maxfd = sock;

  // set up signal mask
  sigset_t sigmask;
  sigemptyset(&sigmask);
  sigaddset(&sigmask, SIGINT);
  sigaddset(&sigmask, SIGHUP);

  FD_SET(sock, &fds_full);
  // loop of receiving client connection
  for (;;) {
    fds = fds_full;
    if (reread == 1) {
      for (j = 0; j < numofClient; j++) {
        write(clients[j], configMessage, 256);
      }
      reread = 0;
    }

    // block the signals: SIGINT, SIGHUP
    if (pselect(maxfd + 1, &fds, NULL, NULL, &timeout, &sigmask) == -1) {
      printf("system errno = %d\n",errno);
      sendError(11);
    }
    for (i = 0; i <= maxfd; i++) {
      if (!FD_ISSET(i, &fds)) continue;
      if (i == sock) { // accept a new connection
        client = accept(sock, (struct sockaddr *) &from, &fromlength);
        if (client < 0) sendError(8);

        // send the configuration to client
        write(client, configMessage, 256);

        // add client to select watcher
        if (client > maxfd) maxfd = client;
        FD_SET(client, &fds_full);

        // save the client data
        clients[numofClient] = client;
        numofClient++;
      }
      else { // read messages sent from clients

        char *buffer = (char *) malloc(256);
        memset(buffer, 0, 256);

        // read the log message
        read(client, buffer, 256);

        // check if it is a kill message
        if (strstr(buffer, "after exceeding") != NULL) {
          killcount += 1;
        }

        // Check the node name
        checkNodeName(buffer);



        // write to the log file
        writeToLogFile(buffer);
      }
    }
  }
}



/**
 * kill previous procnanny.server process
 */
void killPreviousServer() {
  char *buffer = (char*) malloc(255);
  sprintf(buffer,"pidof procnanny.server -o %d | xargs kill -9", getpid()); //ignore parent process
  system(buffer);
  sprintf(buffer,"pidof procnanny.client | xargs kill -9");
  system(buffer);
  free(buffer);
}

/**
 * print server info
 */
void printServerInfo() {
  // environment variable error
  if (getenv("PROCNANNYSERVERINFO") == NULL) sendError(7);
  if (getenv("PROCNANNYLOGS") == NULL) sendError(2);

  // remove the previous files if they exist
  remove(getenv("PROCNANNYSERVERINFO"));
  remove(getenv("PROCNANNYLOGS"));

  // get server info
  int pidServer = (int) getpid();
  char hostname[255];
  gethostname(hostname,255);

  // print server info into server info file
  char *output = (char*) malloc(255);
  sprintf(output, "NODE %s PID %d PORT %d\n", hostname, pidServer, MY_PORT);
  FILE *serverInfoFile = fopen(getenv("PROCNANNYSERVERINFO"),"a");
  fprintf(serverInfoFile, "%s\n", output);
  fclose(serverInfoFile);

  // print server info into logfile
  sprintf(output, "%s  procnanny server: PID %d on node %s, port %d", getCurrentTime(), pidServer, hostname, MY_PORT);
  writeToLogFile(output);
}

/**
 * write a message into logfile
 */
void writeToLogFile(char *message) {
  FILE *logfile = fopen(getenv("PROCNANNYLOGS"),"a");
  fprintf(logfile, "%s\n", message);
  fclose(logfile);
  free(message);
}

/**
 * read configurion file
 */
void readConfigFile() {
  char *name = (char*) malloc(255);
  char *runtime = (char*) malloc(255);
  FILE *configfile = fopen(configPath,"r");
  if (configfile == NULL) sendError(4);
  else {
    strcpy(configMessage, "");
    while (fscanf(configfile, "%s %s", name, runtime) == 2) {  //get program name
      sprintf(configMessage, "%s(%s %s)", configMessage, name, runtime);
    }
  }
  fclose(configfile);
  free(name);
  free(runtime);
}


/**
 * get time format string
 * Format: [Mon Oct 26 11:50:17 MST 2015]
 */
char *getCurrentTime () {
  time_t t;
  char *date = (char*) malloc(128);
  char *year = (char*) malloc(128);
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
  else if (error == 8) strcpy(errorMessage, "Error: Server: Failure of accepting client connection");
  else if (error == 9) strcpy(errorMessage, "Error: Server: Failure of opening master socket");
  else if (error == 10) strcpy(errorMessage, "Error: Server: Failure of binding master socket");
  else if (error == 11) strcpy(errorMessage, "Error: Server: Failure of selecting");
  fprintf(stderr,"%s  %s ,errorno = %d from server.\n", getCurrentTime(), errorMessage, error);
  free(errorMessage);
  exit(-1);
}

/**
 * signal handler for SIGHUP
 * called for re-reading the configuration file
 * command => kill -SIGHUP <pid>
 */
void sighuphandler(int signum) {
  int i;
  char *path = (char*) malloc(128);
  strcpy(path,configPath);
  char *configname = (char*) malloc(128);
  strcpy(configname,path);
  for (i = 0; *(path+i); i++){
    if (*(path+i) == '/') strcpy(configname, path++);
  }
  free(path);

  char *buffer = (char*) malloc(255);
  sprintf(buffer, "%s  Info: Caught SIGHUP. Configuration file '%s' re-read.", getCurrentTime(), configname);
  fprintf(stderr,"%s\n", buffer);
  writeToLogFile(buffer);
  free(configname);

  // re-read configuration file and send messages to clients
  readConfigFile();
  reread = 1;
}

/**
 * signal handler for SIGINT
 * called for cleanly termination
 * command => kill -SIGINT <pid>
 */
void siginthandler(int signum) {
  int i;

  // send "clearclose" message to every client and close the connection
  char *clearclose = "clearclose";
  for (i = 0; i < numofClient; i++) {
    write(clients[i], clearclose, 256);
    close(clients[i]);
  }

  // kill each of its children
  killPreviousServer();

  // free memory
  free(configMessage);
  free(configPath);

  // output to shell and logfile
  char *buffer = (char*) malloc(255);
  sprintf(buffer, "%s  Info: Caught SIGINT. Exiting cleanly. %d process(es) killed on node(s) %s.", getCurrentTime(), killcount, getNodeName());
  free(nodeString);
  free(currentTime);
  fprintf(stderr,"%s\n", buffer);
  writeToLogFile(buffer);

  // free pointer
  exit(0);
}



/**
 * initialize the socket between server and clients
 */
void initializeMasterSocket() {
  // stolen from sample code
  struct sockaddr_in	master;

  sock = socket (AF_INET, SOCK_STREAM, 0);
  if (sock < 0) {
    sendError(9);
  }

  master.sin_family = AF_INET;
  master.sin_addr.s_addr = INADDR_ANY;
  master.sin_port = htons (MY_PORT);

  if (bind (sock, (struct sockaddr*) &master, sizeof (master))) {
    sendError(10);
  }

  // max number of clients connection
  listen (sock, 32);
}

void checkNodeName(char *buffer) {
  char *str, *name = (char *) malloc(255);
  int i, j, startpos, endpos;

  // get node name from message
  // no process message check
  if ((str = strstr(buffer, "found on")) != NULL) {
    startpos = (int) (str - buffer + 9);
    endpos = (int) strlen(buffer) - 2;
    j = 0;
    for (i = startpos; i <= endpos; i++) {
      name[j] = buffer[i];
      j++;
    }
    name[j] = 0;
  }
    // initialize message
  else if ((str = strstr(buffer, "on node")) != NULL) {
    startpos = (int) (str - buffer + 8);
    endpos = (int) strlen(buffer) - 2;
    j = 0;
    for (i = startpos; i <= endpos; i++) {
      name[j] = buffer[i];
      j++;
    }
    name[j] = 0;
  }
    // kill message
  else if ((str = strstr(buffer, " killed after")) != NULL) {
    endpos = (int) (str - buffer - 1);
    for (i = 1; endpos - i > 0; i++) {
      if (buffer[endpos - i] == ' ') {
        startpos = endpos - i + 1;
        break;
      }
    }
    j = 0;
    for (i = startpos; i <= endpos; i++) {
      name[j] = buffer[i];
      j++;
    }
    name[j] = 0;
  }

  // check if the node name is in the node name list
  int new = 1;
  for (i = 0; i < nodeno; i++) {
    if (strcmp(nodename[i], name) == 0) {
      new = 0;
      break;
    }
  }

  if (new == 1) {
    nodename[nodeno] = malloc(255);
    strcpy(nodename[nodeno], name);
    nodeno += 1;
  }
  free(name);
}

char *getNodeName() {
  int i;
  for (i = 0; i < nodeno; i++) {
    strcpy(nodeString, nodename[i]);
    free(nodename[i]);
    if (i != nodeno) {
      sprintf(nodeString, "%s, ", nodeString);
    }
  }
  nodeString[strlen(nodeString)] = 0;
  return nodeString;
}
