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
#include <atomic>
#include <queue>
#include "pcb_queue.h"

// Each PCB is 38 bytes
#define PCB_SIZE 38
// struct PCB {
//     int8_t priority; // 1 byte
//     char process_name[16]; // 16 bytes
//     int process_id; // 4 bytes
//     int8_t activity_status; // 1 byte
//     int base_register; // 4 bytes
//     long long int limit_register; // 8 bytes
//     int burst_time; // 4 bytes
// };

struct threadArgs {
    char * schedType;
    int loaderIndex;
};

int NUM_PROCESSORS;
int NUM_PCBS;
std::vector<PCB*> pcbList;
std::vector<std::queue<PCB*>> procLoads;
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

// Splits the load of PCB's for each processor's specified load percentage
void allocateProcLoads(FILE * file, char** argv) {
    
    // Reading in the PCB's and storing into a vector
    for (int i = 0; i < NUM_PCBS; i++)
        pcbList.push_back(parsePCB(file));
    
    // Creating load queues for each processor
    for (int i = 0; i < NUM_PROCESSORS; i++)
        procLoads.push_back(std::queue<PCB*>());

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
    for (int i = 0, p = 0; i < NUM_PROCESSORS; i++) {
        for (int j = p; j <= loadLimit[i]; j++) {
            printPCB(procLoads[i].front());
            procLoads[i].pop();
        }
        printf("----------------------------------\n");
        p = loadLimit[i] + 1;
    }
}

void * processorThread(void * args) {

    struct threadArgs *t_arg = (struct threadArgs*) args;


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

    // Prepares the schedule type to be passed to each thread
    //char** scheduleType[NUM_PROCESSORS];
    std::vector<char *> scheduleType;
    for (int i = (NUM_PROCESSORS +2), j = 0; i < argc-1; i++, j++) {
        scheduleType.push_back(argv[i]);
    }

    // Allocates structs to pass as arguments to the processor threads    
    struct threadArgs *t_args = (struct threadArgs*) malloc(NUM_PROCESSORS * sizeof(*t_args));
    for (int i = 0; i < NUM_PROCESSORS; i++) {
        t_args[i].loaderIndex = i;
        t_args[i].schedType = scheduleType.at(i);
    }

    // Creating threads for the number of processors specified
    pthread_t processors[NUM_PROCESSORS];
    for (int i = 0; i < NUM_PROCESSORS; i++)
        pthread_create(&processors[i], NULL, processorThread, &t_args[i]);

    for (int i = 0; i < NUM_PROCESSORS; i++)
        pthread_join(processors[i], NULL);

    return 0;
}