#ifndef SPLATOON_SERVER_TASKSMANAGER_HPP
#define SPLATOON_SERVER_TASKSMANAGER_HPP

#include <cstdint>

// This is a simple class used in main to manage the time that the poll
// function should wait for a socket to be ready for any operation.
class TasksManager {
public:
    explicit TasksManager(uint64_t startTime);
    ~TasksManager() = default;

    // Push a new time to wait for the poll function.
    // This effectively changes the stored value if it is lower than the current one.
    void push(uint64_t time);

    // Get the time to wait for the poll function and reset the stored value.
    uint64_t get();

private:
    uint64_t startTime;
    uint64_t timeToWait;
};

#endif //SPLATOON_SERVER_TASKSMANAGER_HPP
