#ifndef JS_COMPUTE_RUNTIME_ALLOCATOR_H
#define JS_COMPUTE_RUNTIME_ALLOCATOR_H

#include <cstdint>

struct JSContext;

/// The C ABI allocator needs a JSContext in order to use JS_realloc/JS_free.
/// StarlingMonkey can host more than one JSContext, so this is the currently
/// active allocation context rather than a process-wide owner.
void set_cabi_alloc_context(JSContext *cx);
JSContext *cabi_alloc_context();

extern "C" {

/// A strong symbol to override the cabi_realloc defined by wit-bindgen. This
/// version of cabi_realloc uses JS_malloc under the hood.
void *cabi_realloc(void *ptr, size_t orig_size, size_t align, size_t new_size);

/// A more ergonomic version of cabi_realloc for fresh allocations.
inline void *cabi_malloc(size_t bytes, size_t align) {
  return cabi_realloc(nullptr, 0, align, bytes);
}

/// Not required by wit-bindgen generated code, but a usefully named version of
/// JS_free that can help with identifying where memory allocated by the c-abi.
void cabi_free(void *ptr);
}

#endif
