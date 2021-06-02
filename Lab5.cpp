#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <cinttypes>
#include <fstream>
#include <unistd.h>
#include <ctime>
#include <pthread.h>
#include <mutex>
#include <atomic>
#include <queue>
#include "pcb_queue.h"

// Each PCB is 38 bytes
#define PCB_SIZE 38

struct threadArgs {
    char * schedType;
    int loaderIndex;
};

// Set "timeRatioMilli" to how long 1 burst_time unit should be in milliseconds
// [Example] 100 milliseconds * 30 burst_time units = 3 seconds
int timeRatioMilli = 100;
int burstRatioNano = timeRatioMilli * 1000000; // converts to nano for nanosleep 
int rrTimeQuantum = 2;

int NUM_PROCESSORS;
int NUM_PRIOR_PROCS;
int NUM_PCBS;
std::vector<int> priorityIndices;
std::atomic<int> TOTAL_PCB_MEMORY;
std::vector<PCB*> pcbList;
std::vector<pcb_queue> procLoads;
std::vector<std::mutex> agingLocks;

std::string argsErrMsg = "\nInvalid arguments! Usage:\n<executable> <# processors (n)> "
                        "<proc 1 %> ... <proc N %> <proc 1 type> ... <proc N type> <pcbFile.bin>\n";

// Makes a copy bin function in case of corrupting or messing up the binary file
bool copyBinFile(FILE *binFile, char* fName) {
    FILE *copyFile = fopen("pcbFile.bin", "wb+");
    if (! copyFile) {
        printf("\nError: failed to open new copy file\n");
        return false;
    }

    fseek(binFile, 0, SEEK_END);
    long len = ftell(binFile);
    
    // Additionally checks to see if all PCB's have all their data
    if (len % PCB_SIZE != 0) {
        return false;
    }
    NUM_PCBS = len / PCB_SIZE;

    char buffer[len];
    fseek(binFile, 0, SEEK_SET);

    fread(buffer, sizeof(buffer), 1, binFile);
    fwrite(buffer, sizeof(buffer), 1, copyFile);
    fclose(binFile);
    fclose(copyFile);
    return true;
}

bool isValidArgs(int argc, char** argv) {

    // Doesn't meet the minimum number of arguments
    if (argc < 5) {
        std::cout << argsErrMsg << std::endl;
        return false;
    }

    // Number of processes must be at least 1
    if (((int)strtol(argv[1], NULL, 10)) < 1) {
        std::cout << argsErrMsg << std::endl;
        return false;
    }

    NUM_PROCESSORS = (int)strtol(argv[1], NULL, 10);
    if (argc != (NUM_PROCESSORS * 2) + 3) {
        std::cout << argsErrMsg << std::endl;
        return false;
    }

    // Check for valid floats, all of which should add up to 100% (1.0)
    float total = 0.0;
    for (int i = 2; i < (NUM_PROCESSORS+2); i++) {
        float percent = strtof(argv[i], NULL);
        if (percent <= 0.0 || percent > 1.0) {
            printf("\nError: an invalid float was entered at argument #%d\n", i+1);
            return false;
        }
        else
            total += percent;
    }

    if (total != 1.0) {
        printf("\nError: the supplied load percentages do not at up to 100%% (1.0)\n");
        return false;
    }

    // Check for valid string combinations:
    // sjf (shortest job first, rr (round robin), pr (priority), fcfs (first come first serve)
    std::string schedOptions[] = {"sjf", "rr", "pr", "fcfs"};
    for (int i = (NUM_PROCESSORS +2); i < argc-1; i++) {
        
        std::string schedType = argv[i];
        bool covered = false;
        for (int j = 0; j < 4; j++) {
            if (schedType == schedOptions[j])
                covered = true;
        }

        if (! covered) {
            printf("\nError: an invalid process scheduler type was entered at argument #%d\n", i+1);
            return false;
        }
    }

    // Lastly check if the file supplied exists and has a .bin extension
    FILE *file;
    if (! (file = fopen(argv[argc-1], "rb"))) {
        printf("\nError: failed to open file, please make sure the name is spelled properly and that it exists\n");
        return false;
    }

    std::string fName = argv[argc-1];
    int start = fName.length() - 4;
    if (fName.substr(start, 4) != ".bin") {
        printf("\nError: File does not have the right extension, must be a .bin\n");
        fclose(file);
        return false;
    }
        
    fclose(file);
    return true;
}

struct PCB* parsePCB(FILE *file) {
    struct PCB* pcb = (struct PCB*) malloc(sizeof(struct PCB));
    if (pcb != nullptr) {
        fseek(file, 0, SEEK_CUR);
        fread(&pcb->priority, sizeof(int8_t), 1, file);
        fread(&pcb->process_name, sizeof(char), 16, file);
        fread(&pcb->process_id, sizeof(int), 1, file);
        fread(&pcb->activity_status, sizeof(int8_t), 1, file);
        fread(&pcb->base_register, sizeof(int), 1, file);
        fread(&pcb->limit_register, sizeof(long long int), 1, file);
        fread(&pcb->burst_time, sizeof(int), 1, file);
    }
    return pcb;
}

void printPCB(struct PCB* pcb) {
    if (pcb != nullptr) {
        printf("\nPriority:\t %d\n", pcb->priority);
        
        char buff[16];
        // clears memory to handle unused mem in process_name from file
        memset(buff, 0, sizeof(buff));
        strcpy(buff, pcb->process_name);

        printf("Process Name:\t ");
        for (int i = 0; i < 16; i++)
            printf("%c", buff[i]);
            
        printf("\nProcess ID:\t %d\n", pcb->process_id);
        printf("Activity Status: %d\n", pcb->activity_status);
        printf("Base Register:\t %d\n", pcb->base_register);
        printf("Limit Register:\t %lld\n", pcb->limit_register);
        printf("CPU Burst Time:\t %d\n\n", pcb->burst_time);
    }
}

// Calculates seconds and remainder nanoseconds for use with nanosleep
struct timespec calcWaitTime(int burst) {

    struct timespec waitTime;
    long long int sleepTime = burstRatioNano * burst * 10;
    int seconds = sleepTime / 1000000000;
    long long int remainderNano = sleepTime - (seconds * 1000000000);

    waitTime.tv_sec = seconds;
    waitTime.tv_nsec = remainderNano;

    return waitTime;
}

// Splits the load of PCB's for each processor's specified load percentage
void allocateProcLoads(FILE * file, char** argv) {
    
    // Reading in the PCB's and storing into a vector
    for (int i = 0; i < NUM_PCBS; i++)
        pcbList.push_back(parsePCB(file));
    
    // Creating load queues for each processor
    for (int i = 0; i < NUM_PROCESSORS; i++)
        procLoads.push_back(pcb_queue());

    // Splitting PCB's into load percentages for each processor
    float loadPercents[NUM_PROCESSORS];
    for (int i = 2, j = 0; i < (NUM_PROCESSORS+2); i++, j++) {
        float percent = strtof(argv[i], NULL);
        loadPercents[j] = percent;
    }

    int loadLimit[NUM_PROCESSORS];
    loadLimit[0] = ((int)(loadPercents[0] * (float)NUM_PCBS))-1;
    for (int i = 1; i < NUM_PROCESSORS; i++) {
        loadLimit[i] = (loadLimit[i-1] + (int)(loadPercents[i] * (float)NUM_PCBS));
    }

    // Should never occur but acts as a safety check with float arithmetic to prevent out of bounds
    if (loadLimit[NUM_PROCESSORS-1] >= NUM_PCBS)
        loadLimit[NUM_PROCESSORS-1] = NUM_PCBS-1;

    // Splitting PCB loads into seperate processor queues
    int p = 0;
    for (int i = 0; i < NUM_PROCESSORS; i++) {
        for (int j = p; j <= loadLimit[i]; j++) {
            procLoads[i].push(pcbList.at(j));
        }
        p = (loadLimit[i] + 1);
    }

    // [For Testing] Printing processor load queues
    // for (int i = 0, p = 0; i < NUM_PROCESSORS; i++) {
    //     for (int j = p; j <= loadLimit[i]; j++) {
    //         printPCB(procLoads[i].pop());
    //     }
    //     printf("----------------------------------\n");
    //     p = loadLimit[i] + 1;
    // }
}

long long int shortestJobFirst(int loadIndex) {
    
    long long int memoryUsed = 0;

    // Sorts pcb_queue by shortest to longest jobs
    procLoads[loadIndex].sortByBurst();

    // While the processor's job queue is non-empty will keep processing PCB's
    while(! procLoads[loadIndex].empty()) {
        struct PCB *currPCB = procLoads[loadIndex].pop();
        printf("[Processor #%d] (SJF) Popped PCB off queue, PCB burst time: %d\n", loadIndex, currPCB->burst_time);

        int memBlock = currPCB->limit_register - currPCB->base_register;
        memoryUsed += memBlock;

        // Decreases burst time and sleeps for proportional time to burst_time
        struct timespec timeWait = calcWaitTime(currPCB->burst_time);
        float secFormat = (float)currPCB->burst_time / 10;
        printf("[Processor #%d] (SJF) Sleeping for %.2f secs...\n", loadIndex, secFormat);
        currPCB->burst_time = 0;

        if (secFormat <= 0)
            sleep(1);
        else
            sleep((int)secFormat);
        //nanosleep(&timeWait, NULL);
    }

    return memoryUsed;
}

long long int roundRobin(int loadIndex) {

    long long int memoryUsed = 0;

    // While the processor's job queue is non-empty will keep processing PCB's
    while(! procLoads[loadIndex].empty()) {
        struct PCB *currPCB = procLoads[loadIndex].pop();
        printf("[Processor #%d] (RR) Popped PCB off queue, PCB burst time: %d\n", loadIndex, currPCB->burst_time);

        int memBlock = currPCB->limit_register - currPCB->base_register;
        memoryUsed += memBlock;

        // Simulates a round robin cycling after a given time quantum
        if (currPCB->burst_time >= 20) {
            currPCB->burst_time -= 20;
            printf("[Processor #%d] (RR) processing for 2 seconds...\n", loadIndex);
            sleep(rrTimeQuantum);
        }

        else {
            struct timespec waitTime = calcWaitTime(currPCB->burst_time);
            float secFormat = (float)currPCB->burst_time / 10;
            currPCB->burst_time = 0;
            printf("[Processor #%d] (RR) processing remaining burst time for %.2f seconds\n", loadIndex, secFormat);
            nanosleep(&waitTime, NULL);
        }


        if (currPCB->burst_time > 0) {
            printf("[Processor #%d] (RR) pushing PCB back to queue\n", loadIndex);
            procLoads[loadIndex].push(currPCB);
        }
    }

    return memoryUsed;
}

long long int prioritySchedule(int loadIndex) {
    
    long long int memoryUsed = 0;

    // Initially sorts PCB's by the highest priority first to lowest priority
    procLoads[loadIndex].sortByPriority();

    // While the processor's job queue is non-empty will keep processing PCB's
    while(! procLoads[loadIndex].empty()) {

        agingLocks[loadIndex].lock();
            struct PCB *currPCB = procLoads[loadIndex].pop();
            printf("[Processor #%d] (Priority) Popped PCB off queue, PCB burst time: %d\n", loadIndex, currPCB->burst_time);
            
            int memBlock = currPCB->limit_register - currPCB->base_register;
            memoryUsed += memBlock;

            // Decreases burst time and sleeps for proportional time to burst_time
            struct timespec timeWait = calcWaitTime(currPCB->burst_time);
            float secFormat = (float)currPCB->burst_time / 10;
            printf("[Processor #%d] (Priority) Sleeping for %.2f secs...\n", loadIndex, secFormat);
            currPCB->burst_time = 0;
            agingLocks[loadIndex].unlock();

        if (secFormat <= 0)
            sleep(1);
        else
            sleep((int)secFormat);
        //nanosleep(&timeWait, NULL);
    }

    return memoryUsed;

    return 1;
}

long long int firstComeFirstServe(int loadIndex) {

    long long int memoryUsed = 0;

    // While the processor's job queue is non-empty will keep processing PCB's
    while(! procLoads[loadIndex].empty()) {
        struct PCB *currPCB = procLoads[loadIndex].pop();
        printf("[Processor #%d] (FCFS) Popped PCB off queue, PCB burst time: %d\n", loadIndex, currPCB->burst_time);
        
        int memBlock = currPCB->limit_register - currPCB->base_register;
        memoryUsed += memBlock;

        // Decreases burst time and sleeps for proportional time to burst_time
        struct timespec timeWait = calcWaitTime(currPCB->burst_time);
        float secFormat = (float)currPCB->burst_time / 10;
        printf("[Processor #%d] (FCFS) Sleeping for %.2f secs...\n", loadIndex, secFormat);
        currPCB->burst_time = 0;

        if (secFormat <= 0)
            sleep(1);
        else
            sleep((int)secFormat);
        //nanosleep(&timeWait, NULL);
    }

    return memoryUsed;
}

void * agingThread(void * args) {
   
    int loaderIndex = *((int*) args);
    while (! procLoads[loaderIndex].empty()) {
        sleep(20);

        // Mutex locking avoids array out of bounds when indexing/resorting
        // while the priority scheduler may be trying to pop off and resize the queue
        agingLocks[loaderIndex].lock();
            printf("Aging priorities for [Processor #%d]\n", loaderIndex);

            for (int i = 0 ; i < procLoads[loaderIndex].size(); i++)
                procLoads[loaderIndex].at(i)->priority += 1;

            procLoads[loaderIndex].sortByPriority();

            printf("Priorities have been aged for [Processor #%d] aging again in 20 seconds\n", loaderIndex);
        agingLocks[loaderIndex].unlock();
    }
    
    return nullptr;
}

void * processorThread(void * args) {

    struct threadArgs *t_arg = (struct threadArgs*) args;
    std::string schedType = t_arg->schedType;
    int poolMemTotal = 0;

    if (schedType == "sjf")
        poolMemTotal = shortestJobFirst(t_arg->loaderIndex);

    else if (schedType == "fcfs")
        poolMemTotal = firstComeFirstServe(t_arg->loaderIndex);

    else if (schedType == "rr")
        poolMemTotal = roundRobin(t_arg->loaderIndex);

    else if (schedType == "pr")
        poolMemTotal = prioritySchedule(t_arg->loaderIndex);

    TOTAL_PCB_MEMORY += poolMemTotal;
    return nullptr;
}

int main(int argc, char** argv) {

    if (! isValidArgs(argc, argv))
        return -1;
    
    // Copies the original bin file to keep its integrity
    FILE *procFile = fopen(argv[argc-1], "rb");
    int exitStat = 0;
    if ((exitStat = copyBinFile(procFile, argv[argc-1])) != 1) {
        if (exitStat == 0)
            printf("\nError: failed to copy original bin file");
        else if (exitStat == -1)
            printf("\nError: bin file missing PCB data (file size is not divisible by PCB size)\n");
        return -1;
    }

    // Opening newly created bin file to read in PCBs.
    FILE *pcbFile = fopen("pcbFile.bin", "rb+");
    if (! pcbFile) {
        printf("\nError: failed to open newly copied PCB file\n");
        return -1;
    }

    // Handles splitting the processor loads specified
    allocateProcLoads(pcbFile, argv);

    // Prepares the schedule type to be passed to each thread and tracks
    // the number of processors with a priority scheduling type
    std::vector<char *> scheduleType;
    NUM_PRIOR_PROCS = 0;
    for (int i = (NUM_PROCESSORS +2), j = 0; i < argc-1; i++, j++) {
        scheduleType.push_back(argv[i]);
        std::string currType = argv[i];
        if (currType == "pr") {
            NUM_PRIOR_PROCS++;
            priorityIndices.push_back(j);
        }
    }

    // Allocates structs to pass as arguments to the processor threads    
    struct threadArgs *t_args = (struct threadArgs*) malloc(NUM_PROCESSORS * sizeof(*t_args));
    for (int i = 0; i < NUM_PROCESSORS; i++) {
        t_args[i].loaderIndex = i;
        t_args[i].schedType = scheduleType.at(i);
    }

    // Reallocates mutexes for each aging thread to use with corresponding
    // priority threads now that the number of priority threads is known
    agingLocks = std::vector<std::mutex>(NUM_PRIOR_PROCS);

    // Creating threads for the number of processors specified
    pthread_t processors[NUM_PROCESSORS];
    for (int i = 0; i < NUM_PROCESSORS; i++)
        pthread_create(&processors[i], NULL, processorThread, &t_args[i]);

    pthread_t agingThreads[NUM_PRIOR_PROCS];
    for (int i = 0; i < NUM_PRIOR_PROCS; i++)
        pthread_create(&agingThreads[i], NULL, agingThread,(void*)&priorityIndices[i]);

    bool queuePool[NUM_PROCESSORS];
    for (int i = 0; i < NUM_PROCESSORS; i++)
        queuePool[i] = false;

    while (true) {
        sleep(2);

        for (int i = 0; i < NUM_PROCESSORS; i++) {
            if (procLoads[i].empty())
                queuePool[i] = true;
        }

        for (int i = 0 ; i < NUM_PROCESSORS; i++) {
            if (! queuePool[i])
                continue;
        }

        break;
    }
    
    for (int i = 0; i < NUM_PROCESSORS; i++)
        pthread_join(processors[i], NULL);

    for (int i = 0; i < NUM_PRIOR_PROCS; i++)
        pthread_join(agingThreads[i], NULL);

    // [ ----- Deallocations ----- ]
    for (int i = 0; i < NUM_PCBS; i++)
        free(pcbList[i]);

    printf("\nDone\n");
    return 0;
}