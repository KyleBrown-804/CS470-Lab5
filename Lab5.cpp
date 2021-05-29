#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <regex>
#include <unistd.h>
#include <ctime>
#include <pthread.h>
#include <atomic>

int NUM_PROCS;
std::string argsErrMsg = "\nInvalid arguments! Usage:\n<executable> <# processors = n> <proc1 %> <procN %> <proc1 type> <procN type> <pcbFile.bin>\n";

// Each PCB is 38 bytes
struct PCB {
    char priority; // 1 byte
    int16_t process_name; // 16 bytes
    int process_id; // 4 bytes
    char activity_status; // 1 byte
    int base_register; // 4 bytes
    int8_t limit_register; // 8 bytes
    int burst_time; // 4 bytes
};

// Makes a copy bin function in case of corrupting or messing up the binary file
void copyBinFile(FILE *binFile, char* fName) {
    FILE *copyFile = fopen("pcbFile.bin", "wb+");
    if (! copyFile)
        printf("\nError: failed to open new copy file\n");

    fseek(binFile, 0, SEEK_END);
    long len = ftell(binFile);
    char buffer[len];
    fseek(binFile, 0, SEEK_SET);

    fread(buffer, sizeof(buffer), 1, binFile);
    fwrite(buffer, sizeof(buffer), 1, copyFile);
    fclose(binFile);
    fclose(copyFile);
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

    NUM_PROCS = (int)strtol(argv[1], NULL, 10);

    // [file] [2] 0.2 0.3 rr rr file.bin
    if (argc != (NUM_PROCS * 2) + 3) {
        std::cout << argsErrMsg << std::endl;
        return false;
    }

    // Check for valid floats, all of which should add up to 100% (1.0)
    float total = 0.0;
    for (int i = 2; i < (NUM_PROCS+2); i++) {
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
    for (int i = (NUM_PROCS +2); i < argc-1; i++) {
        
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

int main(int argc, char** argv) {

    if (! isValidArgs(argc, argv))
        return -1;
    
    // Copies the original bin file to keep its integrity
    FILE *procFile = fopen(argv[argc-1], "rb");
    copyBinFile(procFile, argv[argc-1]);

    // Opening newly created bin file to read in PCBs.
    FILE *pcbFile = fopen("pcbFile.bin", "rb+");
    if (! pcbFile) {
        printf("\nError: failed to open newly copied PCB file\n");
        return -1;
    }

    printf("\nOkay now you can start this dumb lab\n\n");
    return 0;
}