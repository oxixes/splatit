#include "tasksManager.hpp"

TasksManager::TasksManager(uint64_t startTime) : startTime(startTime), timeToWait(startTime) {}

void TasksManager::push(uint64_t time) {
    if (time < timeToWait) timeToWait = time;
}

uint64_t TasksManager::get() {
    uint64_t time = timeToWait;
    timeToWait = startTime;
    return time;
}