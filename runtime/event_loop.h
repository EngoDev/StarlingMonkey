#ifndef JS_COMPUTE_RUNTIME_EVENT_LOOP_H
#define JS_COMPUTE_RUNTIME_EVENT_LOOP_H

#include "extension-api.h"
#include "jsapi.h"

#include <vector>

namespace core {

struct TaskQueue {
  std::vector<RefPtr<api::AsyncTask>> tasks;
  int interest_cnt = 0;
  bool event_loop_running = false;

  void trace(JSTracer *trc) const;
};

class EventLoop {
  PersistentRooted<TaskQueue> queue_;

  bool interest_complete() const;
  void exit_event_loop();

public:
  /**
   * Initialize the event loop
   */
  void init(JSContext *cx);

  /**
   * Check if there are any pending tasks (io requests or timers) to process.
   */
  bool has_pending_async_tasks() const;

  /**
   * Run the event loop until all interests are complete.
   * See run_event_loop in extension-api.h for the complete description.
   */
  bool run_event_loop(api::Engine *engine, double total_compute);

  void incr_event_loop_interest();
  void decr_event_loop_interest();

  /**
   * Select on the next async tasks
   */
  bool process_async_tasks(api::Engine *engine, double timeout);

  /**
   * Queue a new async task.
   */
  void queue_async_task(const RefPtr<api::AsyncTask> &task);

  /**
   * Remove a queued async task.
   */
  bool cancel_async_task(api::Engine *engine, const RefPtr<api::AsyncTask> &task);
};

} // namespace core

#endif
