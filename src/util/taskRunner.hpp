#ifndef SPLATOON_SERVER_TASKRUNNER_HPP
#define SPLATOON_SERVER_TASKRUNNER_HPP

#include "task.hpp"

namespace async {

class TaskRunner {
public:
    virtual ~TaskRunner() = default;
    virtual void scheduleArbitraryFunction(Task<void>&& task) const = 0;
};

} // namespace async

#endif //SPLATOON_SERVER_TASKRUNNER_HPP