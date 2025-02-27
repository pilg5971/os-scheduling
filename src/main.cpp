#include <iostream>
#include <string>
#include <list>
#include <vector>
#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <unistd.h>
#include "configreader.h"
#include "process.h"

// Shared data for all cores
typedef struct SchedulerData {
    std::mutex mutex;
    std::condition_variable condition;
    ScheduleAlgorithm algorithm;
    uint32_t context_switch;
    uint32_t time_slice;
    std::list<Process*> ready_queue;
    bool all_terminated;
} SchedulerData;

void coreRunProcesses(uint8_t core_id, SchedulerData *data);
int printProcessOutput(std::vector<Process*>& processes, std::mutex& mutex);
void clearOutput(int num_lines);
uint64_t currentTime();
std::string processStateToString(Process::State state);

int main(int argc, char **argv)
{
    // Ensure user entered a command line parameter for configuration file name
    if (argc < 2)
    {
        std::cerr << "Error: must specify configuration file" << std::endl;
        exit(EXIT_FAILURE);
    }

    // Declare variables used throughout main
    int i;
    SchedulerData *shared_data;
    std::vector<Process*> processes;

    // Read configuration file for scheduling simulation
    SchedulerConfig *config = readConfigFile(argv[1]);

    // Store configuration parameters in shared data object
    uint8_t num_cores = config->cores;
    shared_data = new SchedulerData();
    shared_data->algorithm = config->algorithm;
    shared_data->context_switch = config->context_switch;
    shared_data->time_slice = config->time_slice;
    shared_data->all_terminated = false;

    // Create processes
    uint64_t start = currentTime();
    for (i = 0; i < config->num_processes; i++)
    {
        Process *p = new Process(config->processes[i], start);
        processes.push_back(p);
        // If process should be launched immediately, add to ready queue
        if (p->getState() == Process::State::Ready)
        {
            shared_data->ready_queue.push_back(p);
        }
    }

    // Free configuration data from memory
    deleteConfig(config);

    // Launch 1 scheduling thread per cpu core
    std::thread *schedule_threads = new std::thread[num_cores];
    for (i = 0; i < num_cores; i++)
    {
        schedule_threads[i] = std::thread(coreRunProcesses, i, shared_data);
    }

    // Main thread work goes here
    int num_lines = 0;
    //uint64_t currTime;
    while (!(shared_data->all_terminated))
    {
        // Clear output from previous iteration
        clearOutput(num_lines);

        // Do the following:
        //   - Get current time
        //   - *Check if any processes need to move from NotStarted to Ready (based on elapsed time), and if so put that process in the ready queue
        //   - *Check if any processes have finished their I/O burst, and if so put that process back in the ready queue
        //   - *Check if any running process need to be interrupted (RR time slice expires or newly ready process has higher priority)
        //   - *Sort the ready queue (if needed - based on scheduling algorithm)
        //   - Determine if all processes are in the terminated state
        //   - * = accesses shared data (ready queue), so be sure to use proper synchronization

       //[1]: Get Current Time
        uint64_t currTime = currentTime();

        //Scheduling Checks
        for(i = 0; i < processes.size(); i++){
            //[2]: NotStarted --> Ready Movement
            //std::cout << "Inside For Loop" << std::endl;
            if(processes[i]->getState() == Process::State::NotStarted){
                //std::cout << "Checkpoint #1" << std::endl;
                if((currTime - start) >= processes[i]->getStartTime()){
                    processes[i]->setState(Process::State::Ready, currTime);
                    //*** MUTEX WILL BE USED HERE
                    {
                        std::lock_guard<std::mutex> lock(shared_data->mutex);
                        shared_data->ready_queue.push_back(processes[i]);
                    }
                }
            
            //[3]: I/O burst check
            }else if(processes[i]->getState() == Process::State::IO){
                //std::cout << "Checkpoint #2" << std::endl;
                //if((currTime - processes[i]->getBurstStartTime()) >= (processes[i]->getBurstArray()[processes[i]->getCurrentBurst()])){ //burst_times[current_burst]
                if((processes[i]->getBurstStartTime() + processes[i]->getCurrentBurst()) <= currTime){
                //if(processes[i]->getIOBurstDone() == true){                    
                    //processes[i]->updateIOBurstDone(false);
                    //std::cout << "Changing the State" << std::endl;
                    processes[i]->setState(Process::State::Ready, currTime);
                    processes[i]->addCurrentBurst();
                    //std::cout << processes[i]->getState() << std::endl;

                    //*** MUTEX WILL BE USED HERE
                    {
                        std::lock_guard<std::mutex> lock(shared_data->mutex);
                        shared_data->ready_queue.push_back(processes[i]);
                    }
                }

            //[4]: Running Interruption
            }else if(processes[i]->getState() == Process::State::Running){
                {
                    //std::cout << "Checkpoint #3" << std::endl;
                    std::lock_guard<std::mutex> lock(shared_data->mutex);
                    //Time Slice Check
                    if(processes[i]->getCpuTime() > shared_data->time_slice){
                        processes[i]->interrupt();
                    }
                    //Priority Check ??***
                    if(shared_data->algorithm == PP){
                        //std::cout << "Checkpoint #5" << std::endl;
                        shared_data->ready_queue.sort(PpComparator());
                        for(std::list<Process*>::iterator it = shared_data->ready_queue.begin(); it != shared_data->ready_queue.end(); ++it){
                            if((*it)->getPriority() < processes[i]->getPriority()){
                                processes[i]->interrupt();
                            }
                        }
                    }
                }
            }
        }
        
        //[5]: Sorting the Ready Queue
        if(shared_data->algorithm == SJF){ //Sjf Sorting
            //std::cout << "Checkpoint #4" << std::endl;
            {
                std::lock_guard<std::mutex> lock(shared_data->mutex);
                shared_data->ready_queue.sort(SjfComparator());
            }
        }
        else if(shared_data->algorithm == PP){ //PP Sorting
            //std::cout << "Checkpoint #5" << std::endl;
            {
                std::lock_guard<std::mutex> lock(shared_data->mutex);
                shared_data->ready_queue.sort(PpComparator());
            }
        }
        
        //[6]: Check for completion
        bool completed = true;
        for(i = 0; i < processes.size(); i++){
            if(processes[i]->getState() != Process::State::Terminated){
                completed = false;
            }
        }
        if(completed == true){
            {
                //std::cout << "Closing Code" << std::endl;
                std::lock_guard<std::mutex> lock(shared_data->mutex);
                shared_data->all_terminated = true;
            }
        }


//---------------------------------------------------------------------------------------------------------------//
        // output process status table
        
        num_lines = printProcessOutput(processes, shared_data->mutex);

        // sleep 50 ms
        usleep(50000);
    }

    // wait for threads to finish
    for (i = 0; i < num_cores; i++)
    {
        schedule_threads[i].join();
    }

        // print final statistics
    int index=0;
    double turnaround = 0.0;
    double waiting = 0.0;
    double cpu_time = 0.0;

    for (index; index < processes.size(); index++)
    {
        turnaround = turnaround + processes[index]->getTurnaroundTime();
        waiting = waiting + processes[index]->getWaitTime();
        cpu_time = cpu_time + processes[index]->getCpuTime();
    }
    //  - CPU utilization
    cpu_time = cpu_time/turnaround;
    //  - Average turnaround time
    turnaround = turnaround/(double)i;
    //  - Average waiting time
    waiting = waiting/(double)i;
    
    std::cout<<"Average turnaround time:", turnaround;
    std::cout<<"Average waiting time:", waiting;
    std::cout<<"CPU utilization:", cpu_time;

    

    //  - Throughput
    //     - Average for first 50% of processes finished
    //     - Average for second 50% of processes finished
    //     - Overall average


    // Clean up before quitting program
    //std::cout << "Code Closed" << std::endl;
    processes.clear();

    return 0;
}

void coreRunProcesses(uint8_t core_id, SchedulerData *shared_data)
{
    Process* currProcess = NULL;
    //std::cout << "Checkpoint #6" << std::endl;
    while (!(shared_data->all_terminated))
    {    
        // Work to be done by each core independent of the other cores
        // Repeat until all processes in terminated state:

        //   - *Get process at front of ready queue

        {
            std::lock_guard<std::mutex> lock(shared_data->mutex);
            if(!(shared_data->ready_queue.empty())){
                currProcess = shared_data->ready_queue.front();
                shared_data->ready_queue.pop_front();
                currProcess->setState(Process::State::Running, currentTime());
                //..Set to Running
            }
        }
        //std::cout << "Hello!" << std::endl;
        if(currProcess != NULL){
            //std::cout << "Checkpoint #9" << std::endl;
            //   - Simulate the processes running until one of the following:
            //     - CPU burst time has elapsed
            //     - Interrupted (RR time slice has elapsed or process preempted by higher priority process)
            //  - Place the process back in the appropriate queue
            //     - I/O queue if CPU burst finished (and process not finished) -- no actual queue, simply set state to IO
            //     - Terminated if CPU burst finished and no more bursts remain -- no actual queue, simply set state to Terminated
            //     - *Ready queue if interrupted (be sure to modify the CPU burst time to now reflect the remaining time)

            currProcess->setBurstStartTime(currentTime());

            //std::cout << "Checkpoint #11" << std::endl;
            uint32_t burstTime = currProcess->getBurstArray()[currProcess->getCurrentBurst()];

            //std::cout << "Checkpoint #10" << std::endl;
            //no need to actually do anything in the while loop
            while ((currentTime()-currProcess->getBurstStartTime() < burstTime) && (currProcess->isInterrupted() == false))
            {}

            currProcess->updateProcess(currentTime());

            if(currProcess->getRemainingTime() <= 0)
            {
                currProcess->setState(Process::State::Terminated, currentTime());
            }
            else if(currProcess->isInterrupted())
            {
                currProcess->updateBurstTime(currProcess->getCurrentBurst(), currProcess->getBurstArray()[currProcess->getCurrentBurst()]-(currentTime()-currProcess->getBurstStartTime()));                
                currProcess->setState(Process::State::Ready,currentTime());
                {
                    std::lock_guard<std::mutex> lock(shared_data->mutex);
                    shared_data->ready_queue.push_back(currProcess);                    
                }
                
            }
            else if (currProcess->getState() == Process::State::Running)
            {
                currProcess->addCurrentBurst();
                currProcess->setState(Process::State::IO, currentTime());

            }

            /*else if (currProcess->getState() == Process::State::IO)
            {
                currProcess->updateIOBurstDone(true);
            }*/

            //  - Wait context switching time
            usleep(shared_data->context_switch);
            
            //  - * = accesses shared data (ready queue), so be sure to use proper synchronization
            //std::cout << "Checkpoint #8" << std::endl;
        }
    }
}

int printProcessOutput(std::vector<Process*>& processes, std::mutex& mutex)
{
    int i;
    int num_lines = 2;
    std::lock_guard<std::mutex> lock(mutex);
    printf("|   PID | Priority |      State | Core | Turn Time | Wait Time | CPU Time | Remain Time |\n");
    printf("+-------+----------+------------+------+-----------+-----------+----------+-------------+\n");
    for (i = 0; i < processes.size(); i++)
    {
        if (processes[i]->getState() != Process::State::NotStarted)
        {
            uint16_t pid = processes[i]->getPid();
            uint8_t priority = processes[i]->getPriority();
            std::string process_state = processStateToString(processes[i]->getState());
            int8_t core = processes[i]->getCpuCore();
            std::string cpu_core = (core >= 0) ? std::to_string(core) : "--";
            double turn_time = processes[i]->getTurnaroundTime();
            double wait_time = processes[i]->getWaitTime();
            double cpu_time = processes[i]->getCpuTime();
            double remain_time = processes[i]->getRemainingTime();
            printf("| %5u | %8u | %10s | %4s | %9.1lf | %9.1lf | %8.1lf | %11.1lf |\n", 
                   pid, priority, process_state.c_str(), cpu_core.c_str(), turn_time, 
                   wait_time, cpu_time, remain_time);
            num_lines++;
        }
    }
    return num_lines;
}

void clearOutput(int num_lines)
{
    int i;
    for (i = 0; i < num_lines; i++)
    {
        fputs("\033[A\033[2K", stdout);
    }
    rewind(stdout);
    fflush(stdout);
}

uint64_t currentTime()
{
    uint64_t ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::system_clock::now().time_since_epoch()).count();
    return ms;
}

std::string processStateToString(Process::State state)
{
    std::string str;
    switch (state)
    {
        case Process::State::NotStarted:
            str = "not started";
            break;
        case Process::State::Ready:
            str = "ready";
            break;
        case Process::State::Running:
            str = "running";
            break;
        case Process::State::IO:
            str = "i/o";
            break;
        case Process::State::Terminated:
            str = "terminated";
            break;
        default:
            str = "unknown";
            break;
    }
    return str;
}
