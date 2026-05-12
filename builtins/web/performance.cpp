#include "performance.h"
#include <chrono>
#include <unordered_map>

namespace {
using FpMilliseconds = std::chrono::duration<float, std::chrono::milliseconds::period>;
std::unordered_map<JSRuntime *, std::chrono::steady_clock::time_point> time_origins;
} // namespace

namespace builtins::web::performance {

void Performance::set_time_origin(JSContext *cx,
                                  std::chrono::steady_clock::time_point time_origin) {
  time_origins[JS_GetRuntime(cx)] = time_origin;
}

std::chrono::steady_clock::time_point Performance::time_origin(JSContext *cx) {
  auto it = time_origins.find(JS_GetRuntime(cx));
  MOZ_RELEASE_ASSERT(it != time_origins.end());
  return it->second;
}

// https://w3c.github.io/hr-time/#dom-performance-now
bool Performance::now(JSContext *cx, unsigned argc, JS::Value *vp) {
  METHOD_HEADER(0);

  auto finish = std::chrono::high_resolution_clock::now();
  auto duration = FpMilliseconds(finish - time_origin(cx)).count();

  JS::RootedValue elapsed(cx, JS::Float32Value(duration));
  args.rval().set(elapsed);
  return true;
}

bool Performance::timeOrigin_get(JSContext *cx, unsigned argc, JS::Value *vp) {
  METHOD_HEADER(0);
  auto time = FpMilliseconds(time_origin(cx).time_since_epoch()).count();
  JS::RootedValue elapsed(cx, JS::Float32Value(time));
  args.rval().set(elapsed);
  return true;
}

const JSFunctionSpec Performance::methods[] = {JS_FN("now", now, 0, JSPROP_ENUMERATE), JS_FS_END};

const JSPropertySpec Performance::properties[] = {
    JS_PSG("timeOrigin", timeOrigin_get, JSPROP_ENUMERATE),
    JS_STRING_SYM_PS(toStringTag, "performance", JSPROP_READONLY), JS_PS_END};

const JSFunctionSpec Performance::static_methods[] = {JS_FS_END};
const JSPropertySpec Performance::static_properties[] = {JS_PS_END};

bool Performance::create(JSContext *cx, JS::HandleObject global) {
  JS::RootedObject performance(
      cx, JS_NewObjectWithGivenProto(cx, &Performance::class_, Performance::proto_obj(cx)));
  if (!performance) {
    return false;
  }
  if (!JS_DefineProperty(cx, global, "performance", performance, 0)) {
    return false;
  }
  if (!JS_DefineProperties(cx, performance, properties)) {
    return false;
  }
  return JS_DefineFunctions(cx, performance, methods);
}

bool Performance::init_class(JSContext *cx, JS::HandleObject global) {
  return init_class_impl(cx, global);
}

bool install(api::Engine *engine) {
  Performance::set_time_origin(engine->cx(), std::chrono::high_resolution_clock::now());
  if (!Performance::init_class(engine->cx(), engine->global())) {
    return false;
  }
  if (!Performance::create(engine->cx(), engine->global())) {
    return false;
  }
  return true;
}

} // namespace builtins::web::performance
