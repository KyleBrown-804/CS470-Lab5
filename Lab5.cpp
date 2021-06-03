/* 
* Kyle Brown
* 6/3/2021
* CS470 Operating Systems Lab 5
*/
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

#define PCB_SIZE 38

struct threadArgs {
    char * schedType;
    int loaderIndex;
};

struct agingThreadArgs {
    int priorThreadIndex;
    int vectorIndex;
};

int rrTimeQuantum = 2;
int NUM_PROCESSORS;
int NUM_PRIOR_PROCS;
int NUM_PCBS;
int SUSPEND_FLAG = 0;
int IS_COMPLETE = 0;
int TOTAL_PCB_MEMORY = 0;

pthread_cond_t resumeCond;
pthread_mutex_t mutexSuspend;
pthread_mutex_t agingLock;
std::vector<PCB*> pcbList;
std::vector<pcb_queue> procLoads;
std::string argsErrMsg = "\nInvalid arguments! Usage:\n<executable> <# processors (n)> "
                        "<proc 1 %> ... <proc N %> <proc 1 type> ... <proc N type> <pcbFile.bin>\n";

/*  
*   -------------------------------------------
*           Utility Functions Below
*   -------------------------------------------
*/

// Makes a copy bin file to keep the integrity of the original
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

// Handles various command line errors and checks the file validity
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

// Reads in 38 bytes from a file stream to create a PCB
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

// The functions "suspendThreads", "resumeThreads", and "checkSuspend"
// are borrowed from the below stack overflow post:
// https://stackoverflow.com/questions/13662689/what-is-the-best-solution-to-pause-and-resume-pthreads
void suspendThreads() {
    pthread_mutex_lock(&mutexSuspend);
    SUSPEND_FLAG = 1;
    pthread_mutex_unlock(&mutexSuspend);
}

void resumeThreads() {
    pthread_mutex_lock(&mutexSuspend);
    SUSPEND_FLAG = 0;
    pthread_cond_broadcast(&resumeCond);
    pthread_mutex_unlock(&mutexSuspend);
}

void checkSuspend() {
    pthread_mutex_lock(&mutexSuspend);
    while (SUSPEND_FLAG != 0) {
        pthread_cond_wait(&resumeCond, &mutexSuspend);
    }
    pthread_mutex_unlock(&mutexSuspend);
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

    // Counts up all of the memory the PCB's use
    for (int i = 0; i < pcbList.size(); i++) {
        struct PCB* currPCB = pcbList.at(i);
        int pcbMem = currPCB->limit_register - currPCB->base_register;
        TOTAL_PCB_MEMORY += pcbMem;
    }
}


/*  
*   -------------------------------------------
*      Scheduler and Thread Functions Below
*   -------------------------------------------
*/

void shortestJobFirst(int loadIndex) {
    
    // Sorts pcb_queue by shortest to longest jobs
    procLoads[loadIndex].sortByBurst();

    while(IS_COMPLETE != 1) {

        // Waits to give loader a chance to assign more work
        if (procLoads[loadIndex].empty()) {
            sleep(2);
            continue;
        }

        // Checks for signal that load balancer is not moving PCB's
        checkSuspend();

        struct PCB *currPCB = procLoads[loadIndex].pop();
        printf("[Processor #%d] (SJF) Popped PCB off queue, PCB burst time: %d\n", loadIndex, currPCB->burst_time);

        // Decreases burst time and sleeps for proportional time to burst_time
        float secFormat = (float)currPCB->burst_time / 10;
        printf("[Processor #%d] (SJF) Sleeping for %.2f secs...\n", loadIndex, secFormat);
        currPCB->burst_time = 0;

        if (secFormat <= 0)
            sleep(1);
        else
            sleep((int)secFormat);

    }
}

void roundRobin(int loadIndex) {

    while(IS_COMPLETE != 1) {

        // Waits to give loader a chance to assign more work
        if (procLoads[loadIndex].empty()) {
            sleep(2);
            continue;
        }

        // Checks for signal that load balancer is not moving PCB's
        checkSuspend();

        struct PCB *currPCB = procLoads[loadIndex].pop();
        printf("[Processor #%d] (RR) Popped PCB off queue, PCB burst time: %d\n", loadIndex, currPCB->burst_time);

        // Simulates a round robin cycling after a given time quantum
        if (currPCB->burst_time >= 20) {
            currPCB->burst_time -= 20;
            printf("[Processor #%d] (RR) processing for 2 seconds...\n", loadIndex);
            sleep(rrTimeQuantum);
        }

        else {
            float secFormat = (float)currPCB->burst_time / 10;
            currPCB->burst_time = 0;
            printf("[Processor #%d] (RR) processing remaining burst time for %.2f seconds\n", loadIndex, secFormat);
            sleep((int)secFormat);
        }


        if (currPCB->burst_time > 0) {
            printf("[Processor #%d] (RR) pushing PCB back to queue\n", loadIndex);
            procLoads[loadIndex].push(currPCB);
        }
    }

}

void prioritySchedule(int loadIndex) {
    
    // Initially sorts PCB's by the highest priority first to lowest priority
    procLoads[loadIndex].sortByPriority();

    // While the processor's job queue is non-empty will keep processing PCB's
    while(IS_COMPLETE != 1) {

        // Waits to give loader a chance to assign more work
        if (procLoads[loadIndex].empty()) {
            sleep(2);
            continue;
        }

        // Checks for signal that load balancer is not moving PCB's
        checkSuspend();

        pthread_mutex_lock(&agingLock);
            struct PCB *currPCB = procLoads[loadIndex].pop();
        pthread_mutex_unlock(&agingLock);
            
            printf("[Processor #%d] (Priority) Popped PCB off queue, PCB burst time: %d\n", loadIndex, currPCB->burst_time);

            // Decreases burst time and sleeps for proportional time to burst_time
            float secFormat = (float)currPCB->burst_time / 10;
            printf("[Processor #%d] (Priority) Sleeping for %.2f secs...\n", loadIndex, secFormat);
            currPCB->burst_time = 0;

        if (secFormat <= 0)
            sleep(1);
        else
            sleep((int)secFormat);

    }
}

void firstComeFirstServe(int loadIndex) {

    while(IS_COMPLETE != 1) {

        // Waits to give loader a chance to assign more work
        if (procLoads[loadIndex].empty()) {
            sleep(2);
            continue;
        }

        // Checks for signal that load balancer is not moving PCB's
        checkSuspend();

        struct PCB *currPCB = procLoads[loadIndex].pop();
        printf("[Processor #%d] (FCFS) Popped PCB off queue, PCB burst time: %d\n", loadIndex, currPCB->burst_time);

        // Decreases burst time and sleeps for proportional time to burst_time
        float secFormat = (float)currPCB->burst_time / 10;
        printf("[Processor #%d] (FCFS) Sleeping for %.2f secs...\n", loadIndex, secFormat);

        currPCB->burst_time = 0;
        if (secFormat <= 0)
            sleep(1);
        else
            sleep((int)secFormat);

    }

}

void * agingThread(void * args) {
   
    int loaderIndex = *((int*) args);

    while(IS_COMPLETE != 1) {

        // Waits to give loader a chance to assign more work to priority threads
        if (procLoads[loaderIndex].empty()) {
            sleep(2);
            continue;
        }

        sleep(20);

        // Safety check to exit early if the priority process finished while sleeping
        if (procLoads[loaderIndex].empty())
            continue;

        // Checks for signal that load balancer is not moving PCB's
        checkSuspend();

        // Mutex locking avoids array out of bounds when indexing/resorting
        // while the priority scheduler may be trying to pop off and resize the queue
        pthread_mutex_lock(&agingLock);
            printf("\n[ ------------------------------------------------------------------------------------ ]\n");
            printf("Aging priorities for [Processor #%d]\n", loaderIndex);

            for (int i = 0 ; i < procLoads[loaderIndex].size(); i++) {
                procLoads[loaderIndex].at(i)->priority += 1;
            }
            procLoads[loaderIndex].sortByPriority();

            printf("Priorities have been aged for [Processor #%d] aging again in 20 seconds\n", loaderIndex);
            printf("[ ------------------------------------------------------------------------------------ ]\n\n");
            fflush(stdout);
        pthread_mutex_unlock(&agingLock);
    }
    
    return nullptr;
}

void * processorThread(void * args) {

    struct threadArgs *t_arg = (struct threadArgs*) args;
    std::string schedType = t_arg->schedType;

    if (schedType == "sjf")
        shortestJobFirst(t_arg->loaderIndex);

    else if (schedType == "fcfs")
        firstComeFirstServe(t_arg->loaderIndex);

    else if (schedType == "rr")
        roundRobin(t_arg->loaderIndex);

    else if (schedType == "pr") {
        prioritySchedule(t_arg->loaderIndex);
    }

    printf("\n~~~ [PROCESSOR #%d] now exiting ~~~\n", t_arg->loaderIndex);
    return nullptr;
}

bool loadBalance(int loadIndex) {

    int max = 0, target = 0;
    for (int i = 0; i < NUM_PROCESSORS; i++) {
        if (i == loadIndex)
            continue;

        if (procLoads[i].size() >= max) {
            target = i;
            break;
        }
    }

    // Don't suspend and lock if threads are almost done executing
    if (procLoads[target].size() <= 5)
        return false;

    // Signal a halt of execution to all other threads to prevent interference
    // when moving loads and wait for them to finish their current jobs
    suspendThreads();
    sleep(2);

    printf("\n[ ------------------------------------------------------------------------------------ ]\n");
    printf("[Load Balancing] Calculating the processor with the most jobs and reallocating half...\n");

    int split = procLoads[target].size() / 2;
    for (int i = 0; i < split; i++) {
        struct PCB* currPCB = procLoads[target].pop();

        if (currPCB != nullptr)
            procLoads[loadIndex].push(currPCB);
    }

    printf("[Load Balancing] Complete: [Processor #%d] has taken %d PCB's from [Processor #%d]", loadIndex, split, target);
    printf("\n[ ------------------------------------------------------------------------------------ ]\n\n");
    resumeThreads();
    return true;
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
        }
    }

    // Allocates structs to pass as arguments to the processor threads   
    struct threadArgs *t_args = (struct threadArgs*) malloc(NUM_PROCESSORS * sizeof(*t_args));
    for (int i = 0; i < NUM_PROCESSORS; i++) {
        t_args[i].loaderIndex = i;
        t_args[i].schedType = scheduleType.at(i);
    }

    // Sets up load index for aging threads to access correspsonding priority thread's pool
    std::vector<int> priorityIndices;
    for (int i = 0; i < NUM_PROCESSORS; i++) {
        std::string currType = scheduleType.at(i);
        if (currType == "pr") {
            priorityIndices.push_back(i);
        }
    }

    // Initializes condition variable for load balancing and mutex locks
    pthread_mutex_init(&mutexSuspend, NULL);
    pthread_mutex_init(&agingLock, NULL);
    pthread_cond_init(&resumeCond, NULL);

    // Creating threads for the number of processors specified
    pthread_t processors[NUM_PROCESSORS];
    for (int i = 0; i < NUM_PROCESSORS; i++)
        pthread_create(&processors[i], NULL, processorThread, &t_args[i]);

    pthread_t agingThreads[NUM_PRIOR_PROCS];
    for (int i = 0; i < NUM_PRIOR_PROCS; i++)
        pthread_create(&agingThreads[i], NULL, agingThread, (void*)&priorityIndices[i]);
    
    
    // Main thread loop which checks if all processors are done and load balances
    int numEmpty = NUM_PROCESSORS;
    while (numEmpty != 0) {
        numEmpty = NUM_PROCESSORS;
        sleep(2);

        // Checks if load balancing is possible, if not marks a finished processor
        for (int i = 0; i < NUM_PROCESSORS; i++) {
            if (procLoads[i].empty()) {
                if (! loadBalance(i)) {
                    numEmpty--;
                }
            }
        }
    }

    // Signals threads to wrap up as all processor jobs have been completed
    IS_COMPLETE = 1;
    printf("\nAll processors have completed processing their allocated PCB's\n");

    for (int i = 0; i < NUM_PROCESSORS; i++)
        pthread_join(processors[i], NULL);

    for (int i = 0; i < NUM_PRIOR_PROCS; i++)
        pthread_join(agingThreads[i], NULL);

    // [ ----- Deallocations ----- ]
    for (int i = 0; i < NUM_PCBS; i++)
        free(pcbList[i]);

    free(t_args);
    pthread_cond_destroy(&resumeCond);
    printf("The total number of memory used by all PCB's was %d bytes\n", TOTAL_PCB_MEMORY);
    return 0;
}