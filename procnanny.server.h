#include "memwatch.h"

void killPreviousServer();
void printServerInfo();
void sendError(int);
char *getCurrentTime();
void writeToLogFile(char *);
void monitorClient(int);
void initializeMasterSocket();
char *readConfigFile(char *);
void sighuphandler(int);
void siginthandler(int);







