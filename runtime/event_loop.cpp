#include "event_loop.h"

#include "host_api.h"
#include "jsapi.h"
#include "jsfriendapi.h"

#include <iostream>
#include <vector>

namespace core {

void TaskQueue::trace(JSTracer *trc) const {
  for (const auto &task : tasks) {
    task->trace(trc);
  }
}

void EventLoop::queue_async_task(const RefPtr<api::AsyncTask> &task) {
  MOZ_ASSERT(task);
  queue_.get().tasks.emplace_back(task);
}

bool EventLoop::cancel_async_task(api::Engine *engine, const RefPtr<api::AsyncTask> &task) {
  auto *const tasks = &queue_.get().tasks;
  for (auto it = tasks->begin(); it != tasks->end(); ++it) {
    if (*it == task) {
      tasks->erase(it);
      task->cancel(engine);
      return true;
    }
  }
  return false;
}

bool EventLoop::has_pending_async_tasks() const { return !queue_.get().tasks.empty(); }

void EventLoop::incr_event_loop_interest() { queue_.get().interest_cnt++; }

void EventLoop::decr_event_loop_interest() {
  MOZ_ASSERT(queue_.get().interest_cnt > 0);
  queue_.get().interest_cnt--;
}

bool EventLoop::interest_complete() const { return queue_.get().interest_cnt == 0; }

void EventLoop::exit_event_loop() { queue_.get().event_loop_running = false; }

bool EventLoop::run_event_loop(api::Engine *engine, double total_compute) {
  if (queue_.get().event_loop_running) {
    fprintf(stderr, "cannot run event loop as it is already running");
    return false;
  }
  queue_.get().event_loop_running = true;
  JSContext *cx = engine->cx();

  while (true) {
    // Run a microtask checkpoint
    js::RunJobs(cx);

    if (JS_IsExceptionPending(cx)) {
      exit_event_loop();
      return false;
    }
    // if there is no interest in the event loop at all, just run one tick
    if (interest_complete()) {
      exit_event_loop();
      return true;
    }

    auto *const tasks = &queue_.get().tasks;
    size_t tasks_size = tasks->size();
    if (tasks_size == 0) {
      exit_event_loop();
      MOZ_ASSERT(!interest_complete());
      return false;
    }

    // Select the next task to run according to event-loop semantics of oldest-first.
    size_t task_idx = api::AsyncTask::select(*tasks);

    auto task = tasks->at(task_idx);
    tasks->erase(tasks->begin() + task_idx);
    bool success = task->run(engine);
    if (!success) {
      exit_event_loop();
      return false;
    }
  }
}

void EventLoop::init(JSContext *cx) { queue_.init(cx); }

} // namespace core
