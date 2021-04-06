#include "process.h"

// Process class methods
Process::Process(ProcessDetails details, uint64_t current_time)
{
    int i;
    pid = details.pid;
    start_time = details.start_time;
    num_bursts = details.num_bursts;
    current_burst = 0;
    burst_times = new uint32_t[num_bursts];
    for (i = 0; i < num_bursts; i++)
    {
        burst_times[i] = details.burst_times[i];
    }
    priority = details.priority;
    state = (start_time == 0) ? State::Ready : State::NotStarted;
    if (state == State::Ready)
    {
        launch_time = current_time;
    }
    is_interrupted = false;
    core = -1;
    turn_time = 0;
    wait_time = 0;
    cpu_time = 0;
    remain_time = 0;
    for (i = 0; i < num_bursts; i+=2)
    {
        remain_time += burst_times[i];
    }
}

Process::~Process()
{
    delete[] burst_times;
}

uint16_t Process::getPid() const
{
    return pid;
}

uint32_t Process::getStartTime() const
{
    return start_time;
}

uint8_t Process::getPriority() const
{
    return priority;
}

uint64_t Process::getBurstStartTime() const
{
    return burst_start_time;
}

uint32_t* Process::getBurstArray() const
{
    return burst_times;
}


uint16_t Process::getCurrentBurst() const
{
    return current_burst;
}

Process::State Process::getState() const
{
    return state;
}

bool Process::isInterrupted() const
{
    return is_interrupted;
}

int8_t Process::getCpuCore() const
{
    return core;
}

double Process::getTurnaroundTime() const
{
    return (double)turn_time / 1000.0;
}

double Process::getWaitTime() const
{
    return (double)wait_time / 1000.0;
}

double Process::getCpuTime() const
{
    return (double)cpu_time / 1000.0;
}

double Process::getRemainingTime() const
{
    return (double)remain_time / 1000.0;
}

void Process::setBurstStartTime(uint64_t current_time)
{
    burst_start_time = current_time;
}

void Process::setState(State new_state, uint64_t current_time)
{
    if (state == State::NotStarted && new_state == State::Ready)
    {
        launch_time = current_time;
    }
    state = new_state;
}

void Process::setCpuCore(int8_t core_num)
{
    core = core_num;
}

void Process::interrupt()
{
    is_interrupted = true;
}

void Process::interruptHandled()
{
    is_interrupted = false;
}

void Process::updateProcess(uint64_t current_time)
{
    // use `current_time` to update turnaround time, wait time, burst times, 
    // cpu time, and remaining time
    int32_t io_time = 0;

    //total time spent on a cpu core
    cpu_time = cpu_time + (current_time - burst_start_time); 

    //updates the array of burst times with remaining ms value of the current burst
    updateBurstTime(current_burst, burst_times[current_burst]-(current_time-burst_start_time));

    //total time since launch
    turn_time = current_time - launch_time;

    //time spent in the ready queue is equal to
    //total time taken by the process(turn_time) - the amount of time spent on bursts up to this point(io_time + cpu_time)
    for (int i = 1; i < current_burst; i=i+2)
    {
        io_time = io_time + burst_times[i];
    }
    
    wait_time = turn_time - (io_time + cpu_time);

    //remain_time is total time of remaining cpu bursts
    remain_time = 0;
    for (int i = 0; i < num_bursts; i=i+2)
    {
        remain_time += burst_times[i];
    }
    
}

void Process::updateBurstTime(int burst_idx, uint32_t new_time)
{
    burst_times[burst_idx] = new_time;
}


// Comparator methods: used in std::list sort() method
// No comparator needed for FCFS or RR (ready queue never sorted)

// SJF - comparator for sorting read queue based on shortest remaining CPU time
bool SjfComparator::operator ()(const Process *p1, const Process *p2)
{
    // your code here!
    //return false; // change this!

    //TRUE = SWAP
    //FALSE = DONT SWAP

    if(p1->getRemainingTime() > p2->getRemainingTime()){
        return true;
    }
    return false;

}

// PP - comparator for sorting read queue based on priority
bool PpComparator::operator ()(const Process *p1, const Process *p2)
{
    // your code here!
    //return false; // change this!

    //TRUE = SWAP
    //FALSE = DONT SWAP

    if(p1->getPriority() > p2->getPriority()){
        return true;
    }
    return false;
}
