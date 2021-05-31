#ifndef PCB_QUEUE_H
#define PCB_QUEUE_H

#include <deque>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <cinttypes>

struct PCB {
    int8_t priority;              // 1 byte
    char process_name[16];        // 16 bytes
    int process_id;               // 4 bytes
    int8_t activity_status;       // 1 byte
    int base_register;            // 4 bytes
    long long int limit_register; // 8 bytes
    int burst_time;               // 4 bytes
};

class pcb_queue {

    std::deque<PCB *> p_queue;

    public:

        pcb_queue();
        ~pcb_queue();

        PCB* at(int index);
        void push(PCB *elem);
        PCB* pop();

        // Comparators for different sorts needed for schedulers
        static bool comparePID(const PCB* pcb1, const PCB* pcb2);
        static bool compareBurst(const PCB* pcb1, const PCB* pcb2);
        static bool comparePriority(const PCB* pcb1, const PCB* pcb2);

        // Sorts needed for Fcfs, Sjf, and Priority
        void sortByPID();
        void sortByBurst();
        void sortByPriority();
};

#endif