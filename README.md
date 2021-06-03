# CS470-Lab5
[Compiling & Execution]
    
    To compile the program enter:
    'g++ -o lab5 pcb_queue.cpp Lab5.cpp -pthread'

    To run the program you can test many different combinations
    of processor types and numbers the only requirements are that
    the load percentages for processors add up to 1.0 (100%), that
    the binary file entered is valid and is divisible by 38 bytes and
    that either "fcfs", "pr", "rr", or "sjf" is entered as the scheduler
    type. In addition the number of processors specified must match the
    number of load percentages and the number of scheudler types specified.

    A few examples include the following:
    ./lab5 1 1.0 pr processes_Spring2021.bin
    ./lab5 4 0.25 0.25 0.25 0.25 pr sjf fcfs rr processes_Spring2021.bin
    ./lab5 4 0.25 0.25 0.25 0.25 pr pr pr pr processes_Spring2021.bin
    ./lab5 4 0.25 0.25 0.25 0.25 pr sjf rr pr processes_Spring2021.bin
    ./lab5 5 0.25 0.1 0.15 0.25 0.25 pr sjf fcfs rr pr processes_Spring2021.bin


[Terminal Output]
    The terminal output will display every push/pop operation for each processor
    when they are pulling or pushing to their individual PCB work pools. The output
    will show the processor number, schedule type, and the action it performed as
    well as the PCB burst time it had and how long it will sleep.

    Example:
        [Processor #2] (FCFS) Popped PCB off queue, PCB burst time: 52
        [Processor #2] (FCFS) Sleeping for 5.20 secs...

    When an aging event or a load balancing event occurs an obvious indicator will
    be displayed to the screen like so:

        [ ------------------------------------------------------------------------------------ ]
        Aging priorities for [Processor #4]
        Priorities have been aged for [Processor #4] aging again in 20 seconds
        [ ------------------------------------------------------------------------------------ ]

        [ ------------------------------------------------------------------------------------ ]
        [Load Balancing] Calculating the processor with the most jobs and reallocating half...
        [Load Balancing] Complete: [Processor #1] has taken 7 PCB's from [Processor #0]
        [ ------------------------------------------------------------------------------------ ]

    [Note] On finishing all PCB's processes will as shown below, however note they may take
    a few seconds to do so as any aging thread to it's matching priority thread may still be asleep
    from it's 20 second interval. Simply wait a bit longer and all processors will exit and the
    total number of bytes the PCB's used will be diplayed at the end as shown in the example below:

        All processors have completed processing their allocated PCB's
        ~~~ [PROCESSOR #0] now exiting ~~~
        ~~~ [PROCESSOR #3] now exiting ~~~
        ~~~ [PROCESSOR #1] now exiting ~~~
        ~~~ [PROCESSOR #2] now exiting ~~~
        ~~~ [PROCESSOR #4] now exiting ~~~
        The total number of memory used by all PCB's was 52769 bytes

[Runtime]
    Each processor will execute a PCB relative to their burst times (except round robin)
    which is set to a time quantum of 2 seconds. In this simulation 100 milliseconds
    represents 1 burst time unit which works nicely in the sense that 10 burst units 
    equals 1 full second, for example some of the slowest burst times around 100
    burst units take up to 10 seconds. This means the program usually takes around 
    1-2 minutes to execute.


[Aging Mechanism]
    All processors are seperate threads which execute in parallel, priority threads
    each have their own corresponding aging threads which kick in every 20 seconds
    to lock the priority threads then increase priority and resort by priority per 
    instructions.


[Load Balancing]
    During execution the main thread of execution will idle and occasionally check
    if any threads have finished their jobs. If any thread has the main thread will 
    begin to load balance which signals for all other threads to halt execution as it
    grabs half of the jobs from whichever thread currently has the most. The load
    balancer will then broadcast out to all waiting threads that they may resume.