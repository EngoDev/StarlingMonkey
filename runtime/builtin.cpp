#include "builtin.h"

#include <memory>
#include <unordered_map>

namespace builtins {
namespace {
using RuntimeProtoMap =
    std::unordered_map<const JSClass *, std::unique_ptr<JS::PersistentRootedObject>>;
std::unordered_map<JSRuntime *, RuntimeProtoMap> proto_objs;
} // namespace

JS::HandleObject builtin_proto(JSContext *cx, const JSClass *cls) {
  auto runtime_it = proto_objs.find(JS_GetRuntime(cx));
  MOZ_RELEASE_ASSERT(runtime_it != proto_objs.end());
  auto proto_it = runtime_it->second.find(cls);
  MOZ_RELEASE_ASSERT(proto_it != runtime_it->second.end());
  return *proto_it->second;
}

bool set_builtin_proto(JSContext *cx, const JSClass *cls, JSObject *proto) {
  auto rooted = std::make_unique<JS::PersistentRootedObject>();
  rooted->init(cx, proto);
  proto_objs[JS_GetRuntime(cx)][cls] = std::move(rooted);
  return true;
}

void clear_builtin_protos(JSContext *cx) { proto_objs.erase(JS_GetRuntime(cx)); }

} // namespace builtins

static const JSErrorFormatString *GetErrorMessageFromRef(void *userRef, unsigned errorNumber) {
  auto *error = static_cast<JSErrorFormatString *>(userRef);

  JS::ConstUTF8CharsZ(error->format, strlen(error->format));
  return error;
}

bool api::throw_error(JSContext *cx, const JSErrorFormatString &error, const char *arg1,
                      const char *arg2, const char *arg3, const char *arg4) {
  const char **args = nullptr;
  const char *list[4] = {arg1, arg2, arg3, arg4};
  if (arg1) {
    args = list;
  }

  JS_ReportErrorNumberUTF8Array(cx, GetErrorMessageFromRef,
                                const_cast<JSErrorFormatString *>(&error), 0,
                                args); // NOLINT(cppcoreguidelines-pro-type-const-cast)
  return false;
}

std::optional<std::span<uint8_t>> value_to_buffer(JSContext *cx, JS::HandleValue val,
                                                  const char *val_desc) {
  if (!val.isObject() ||
      !(JS_IsArrayBufferViewObject(&val.toObject()) || JS::IsArrayBufferObject(&val.toObject()))) {
    api::throw_error(cx, api::Errors::InvalidBuffer, val_desc);
    return std::nullopt;
  }

  JS::RootedObject input(cx, &val.toObject());
  uint8_t *data = nullptr;
  bool is_shared = false;
  size_t len = 0;

  if (JS_IsArrayBufferViewObject(input)) {
    js::GetArrayBufferViewLengthAndData(input, &len, &is_shared, &data);
  } else {
    JS::GetArrayBufferLengthAndData(input, &len, &is_shared, &data);
  }

  return std::span(data, len);
}

bool RejectPromiseWithPendingError(JSContext *cx, HandleObject promise) {
  RootedValue exn(cx);
  if (!JS_IsExceptionPending(cx) || !JS_GetPendingException(cx, &exn)) {
    return false;
  }
  JS_ClearPendingException(cx);
  return JS::RejectPromise(cx, promise, exn);
}

JSObject *PromiseRejectedWithPendingError(JSContext *cx) {
  RootedObject promise(cx, JS::NewPromiseObject(cx, nullptr));
  if (!promise || !RejectPromiseWithPendingError(cx, promise)) {
    return nullptr;
  }
  return promise;
}
