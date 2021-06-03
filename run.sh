#!/bin/bash

g++ -o lab5 pcb_queue.cpp Lab5.cpp -pthread
# ./lab5 1 1.0 pr processes_Spring2021.bin
# ./lab5 4 0.25 0.25 0.25 0.25 pr sjf fcfs rr processes_Spring2021.bin
# ./lab5 3 0.2 0.3 0.5 sjf rr pr processes_Spring2021.bin
# ./lab5 4 0.25 0.25 0.25 0.25 pr pr pr pr processes_Spring2021.bin
# ./lab5 4 0.25 0.25 0.25 0.25 pr sjf rr pr processes_Spring2021.bin
./lab5 5 0.25 0.1 0.15 0.25 0.25 pr sjf fcfs rr pr processes_Spring2021.bin