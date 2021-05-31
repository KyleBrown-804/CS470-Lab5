#include "pcb_queue.h"

pcb_queue::pcb_queue() {}
pcb_queue::~pcb_queue() {}

PCB* pcb_queue::at(int index) {
    return p_queue.at(index);
}

void pcb_queue::push(PCB *elem) {
    p_queue.push_back(elem);
}

PCB* pcb_queue::pop() {
    struct PCB *elem = p_queue.front();
    p_queue.pop_front();
    return elem;
}

// Comparators for different sorts needed for schedulers
bool pcb_queue::comparePID(const PCB* pcb1, const PCB* pcb2) {
    return pcb1->process_id < pcb2->process_id;
}

bool pcb_queue::compareBurst(const PCB* pcb1, const PCB* pcb2) {
    return pcb1->burst_time < pcb2->burst_time;
}

bool pcb_queue::comparePriority(const PCB* pcb1, const PCB* pcb2) {
    return pcb1->priority < pcb2->priority;
}

void pcb_queue::sortByPID() {
    std::sort(p_queue.begin(), p_queue.end(), comparePID);
}

void pcb_queue::sortByBurst() {
    std::sort(p_queue.begin(), p_queue.end(), compareBurst);
}

void pcb_queue::sortByPriority() {
    std::sort(p_queue.begin(), p_queue.end(), comparePriority);
}