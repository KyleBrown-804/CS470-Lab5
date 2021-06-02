#!/bin/bash

g++ -o lab5 pcb_queue.cpp Lab5.cpp -pthread
./lab5 1 1.0 pr processes_Spring2021.bin

# ./lab5 3 0.2 0.3 0.5 pr sjf fcfs processes_Spring2021.bin