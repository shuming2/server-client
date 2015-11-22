#!/bin/bash
#CMPUT 379 Fall 2015 Assignment-2
#Test Case: 1
make

export PROCNANNYLOGS="./logfile.log"
export PROCNANNYSERVERINFO="./serverinfo"

./loop &
./loop &

./procnanny.server config &







