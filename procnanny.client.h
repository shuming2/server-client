#include "memwatch.h"

typedef struct pids {  //pidof function result
  int pidarr[128];
  int numpid;
} PidList;

typedef struct record {
  char *name;
  int time;
} Record;

typedef struct records {
  Record *recordarr;
  int num;
} Records;

void killPreviousClient();
char *getCurrentTime();
void monitorProcess (int pp[]);
PidList pidof(char *);
void sendError(int);
void childExecution(char *, int, int);

int checkPidMonitored(int);
void addPidMonitored(int);
void delPidMonitored(int);
Records updateConfiguration(char*);
void initializeSocket(char*, int);
void clearClose();
void receiveMessage();
