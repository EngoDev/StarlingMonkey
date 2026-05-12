#include "allocator.h"
#include "js/MemoryFunctions.h"

namespace {
JSContext *CURRENT_CONTEXT = nullptr;
}

void set_cabi_alloc_context(JSContext *cx) { CURRENT_CONTEXT = cx; }
JSContext *cabi_alloc_context() { return CURRENT_CONTEXT; }

extern "C" {

__attribute__((weak, export_name("cabi_realloc"))) void *
cabi_realloc(void *ptr, size_t orig_size, size_t _align, size_t new_size) {
  if (new_size == orig_size) {
    return ptr;
  }
  return JS_realloc(CURRENT_CONTEXT, ptr, orig_size, new_size);
}

void cabi_free(void *ptr) { JS_free(CURRENT_CONTEXT, ptr); }
}
