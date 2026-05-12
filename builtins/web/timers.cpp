#include "timers.h"

#include "event_loop.h"

#include <algorithm>
#include <ctime>
#include <host_api.h>
#include <iostream>
#include <list>
#include <map>
#include <vector>

#define S_TO_NS(s) ((s) * 1000000000)
#define NS_TO_MS(ns) ((ns) / 1000000)

namespace {

class TimersMap {
public:
  std::map<int32_t, api::AsyncTask *> timers_;
  int32_t next_timer_id = 1;
  void trace(JSTracer *trc) {
    for (auto &[id, timer] : timers_) {
      timer->trace(trc);
    }
  }
};

static builtins::RuntimePersistentRooted<js::UniquePtr<TimersMap>> TIMERS_MAP;

TimersMap *timers_map(JSContext *cx) { return TIMERS_MAP.rooted(cx).get().get(); }

} // namespace

class TimerTask final : public api::AsyncTask {
  using TimerArgumentsVector = std::vector<JS::Heap<JS::Value>>;

  int32_t timer_id_;
  int64_t delay_;
  int64_t deadline_;
  bool repeat_;

  Heap<JSObject *> callback_;
  TimerArgumentsVector arguments_;

public:
  explicit TimerTask(JSContext *cx, const int64_t delay_ns, const bool repeat,
                     HandleObject callback, JS::HandleValueVector args)
      : timer_id_(timers_map(cx)->next_timer_id++), delay_(delay_ns),
        deadline_(host_api::MonotonicClock::now() + delay_ns), repeat_(repeat),
        callback_(callback) {

    arguments_.reserve(args.length());
    for (const auto &arg : args) {
      arguments_.emplace_back(arg);
    }

    handle_ = host_api::MonotonicClock::subscribe(deadline_, true);

    timers_map(cx)->timers_.emplace(timer_id_, this);
  }

  [[nodiscard]] bool run(api::Engine *engine) override {
    JSContext *cx = engine->cx();

    const RootedObject callback(cx, callback_);
    JS::RootedValueVector argv(cx);
    if (!argv.initCapacity(arguments_.size())) {
      JS_ReportOutOfMemory(cx);
      return false;
    }

    for (auto &arg : arguments_) {
      argv.infallibleAppend(arg);
    }

    RootedValue rval(cx);
    if (!Call(cx, NullHandleValue, callback, argv, &rval)) {
      return false;
    }

    // The task might've been canceled during the callback.
    if (handle_ != INVALID_POLLABLE_HANDLE) {
      host_api::MonotonicClock::unsubscribe(handle_);
    }

    auto *map = timers_map(cx);
    if (map->timers_.contains(timer_id_)) {
      if (repeat_) {
        deadline_ = host_api::MonotonicClock::now() + delay_;
        handle_ = host_api::MonotonicClock::subscribe(deadline_, true);
        engine->queue_async_task(this);
      } else {
        map->timers_.erase(timer_id_);
      }
    }

    return true;
  }

  [[nodiscard]] bool cancel(api::Engine *engine) override {
    if (!timers_map(engine->cx())->timers_.contains(timer_id_)) {
      return false;
    }

    host_api::MonotonicClock::unsubscribe(id());
    handle_ = -1;
    return true;
  }

  [[nodiscard]] uint64_t deadline() override { return deadline_; }

  [[nodiscard]] int32_t timer_id() const { return timer_id_; }

  void trace(JSTracer *trc) override {
    TraceEdge(trc, &callback_, "Timer callback");
    for (auto &arg : arguments_) {
      TraceEdge(trc, &arg, "Timer callback arguments");
    }
  }

  static bool clear(api::Engine *engine, int32_t timer_id) {
    auto *map = timers_map(engine->cx());
    if (!map->timers_.contains(timer_id)) {
      return false;
    }

    engine->cancel_async_task(map->timers_[timer_id]);
    map->timers_.erase(timer_id);
    return true;
  }
};

namespace builtins::web::timers {

template <bool repeat>
bool set_timeout_or_interval(JSContext *cx, HandleObject handler, JS::HandleValueVector handle_args,
                             int32_t delay_ms, int32_t *timer_id) {
  delay_ms = std::max(delay_ms, 0);

  // Convert delay from milliseconds to nanoseconds, as that's what Timers operate on.
  const int64_t delay = static_cast<int64_t>(delay_ms) * 1000000;
  auto *const timer = js_new<TimerTask>(cx, delay, repeat, handler, handle_args);
  api::Engine::get(cx)->queue_async_task(timer);

  *timer_id = timer->timer_id();
  return true;
}

bool set_timeout(JSContext *cx, HandleObject handler, JS::HandleValueVector handle_args,
                 int32_t delay_ms, int32_t *timer_id) {
  return set_timeout_or_interval<false>(cx, handler, handle_args, delay_ms, timer_id);
}

bool set_interval(JSContext *cx, HandleObject handler, JS::HandleValueVector handle_args,
                  int32_t delay_ms, int32_t *timer_id) {
  return set_timeout_or_interval<true>(cx, handler, handle_args, delay_ms, timer_id);
}

/**
 * The `setTimeout` and `setInterval` global functions
 * https://html.spec.whatwg.org/multipage/timers-and-user-prompts.html#dom-settimeout
 * https://html.spec.whatwg.org/multipage/timers-and-user-prompts.html#dom-setinterval
 */
template <bool repeat> bool setTimeout_or_interval(JSContext *cx, const unsigned argc, Value *vp) {
  REQUEST_HANDLER_ONLY(repeat ? "setInterval" : "setTimeout");
  CallArgs args = CallArgsFromVp(argc, vp);
  if (!args.requireAtLeast(cx, repeat ? "setInterval" : "setTimeout", 1)) {
    return false;
  }

  if (!(args[0].isObject() && JS::IsCallable(&args[0].toObject()))) {
    return api::throw_error(cx, api::Errors::TypeError, repeat ? "setInterval" : "setTimeout",
                            "first argument", "be a function");
  }
  const RootedObject handler(cx, &args[0].toObject());

  int32_t delay_ms = 0;
  if (args.length() > 1 && !JS::ToInt32(cx, args.get(1), &delay_ms)) {
    return false;
  }

  JS::RootedValueVector handler_args(cx);
  if (args.length() > 2 && !handler_args.initCapacity(args.length() - 2)) {
    JS_ReportOutOfMemory(cx);
    return false;
  }
  for (size_t i = 2; i < args.length(); i++) {
    handler_args.infallibleAppend(args[i]);
  }

  int32_t timer_id = 0;
  if (!set_timeout_or_interval<repeat>(cx, handler, handler_args, delay_ms, &timer_id)) {
    return false;
  }

  args.rval().setInt32(timer_id);
  return true;
}

/**
 * The `clearTimeout` and `clearInterval` global functions
 * https://html.spec.whatwg.org/multipage/timers-and-user-prompts.html#dom-cleartimeout
 * https://html.spec.whatwg.org/multipage/timers-and-user-prompts.html#dom-clearinterval
 */
template <bool interval> bool clearTimeout_or_interval(JSContext *cx, unsigned argc, Value *vp) {
  const CallArgs args = CallArgsFromVp(argc, vp);
  if (!args.requireAtLeast(cx, interval ? "clearInterval" : "clearTimeout", 1)) {
    return false;
  }

  int32_t id = 0;
  if (!ToInt32(cx, args[0], &id)) {
    return false;
  }

  clear_timeout_or_interval(cx, id);
  args.rval().setUndefined();
  return true;
}

void clear_timeout_or_interval(JSContext *cx, int32_t timer_id) {
  TimerTask::clear(api::Engine::get(cx), timer_id);
}

constexpr JSFunctionSpec methods[] = {
    JS_FN("setInterval", setTimeout_or_interval<true>, 1, JSPROP_ENUMERATE),
    JS_FN("setTimeout", setTimeout_or_interval<false>, 1, JSPROP_ENUMERATE),
    JS_FN("clearInterval", clearTimeout_or_interval<true>, 1, JSPROP_ENUMERATE),
    JS_FN("clearTimeout", clearTimeout_or_interval<false>, 1, JSPROP_ENUMERATE), JS_FS_END};

bool install(api::Engine *engine) {
  TIMERS_MAP.init(engine->cx(), js::MakeUnique<TimersMap>());
  return JS_DefineFunctions(engine->cx(), engine->global(), methods);
}

} // namespace builtins::web::timers
