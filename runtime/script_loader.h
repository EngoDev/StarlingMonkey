#ifndef SCRIPTLOADER_H
#define SCRIPTLOADER_H

#include <extension-api.h>

#include <js/CompileOptions.h>
#include <js/Modules.h>
#include <js/SourceText.h>
#include <memory>

class ScriptLoader {
  api::Engine *engine_;
  bool module_mode_ = true;
  std::string base_path_;
  JS::PersistentRootedObject module_registry_;
  JS::PersistentRootedObject builtin_modules_;
  std::unique_ptr<JS::CompileOptions> compile_opts_;
  mozilla::Maybe<std::string> path_prefix_;

public:
  ScriptLoader(api::Engine *engine, JS::CompileOptions *opts,
               mozilla::Maybe<std::string> path_prefix);

  ScriptLoader(const ScriptLoader &) = delete;
  ScriptLoader(ScriptLoader &&) = delete;

  ScriptLoader &operator=(const ScriptLoader &) = delete;
  ScriptLoader &operator=(ScriptLoader &&) = delete;

  ~ScriptLoader() = default;

  api::Engine *engine() const;
  HandleObject module_registry();
  HandleObject builtin_modules();
  const JS::CompileOptions &compile_options() const;
  const mozilla::Maybe<std::string> &path_prefix() const;

  bool define_builtin_module(const char *id, HandleValue builtin);
  void enable_module_mode(bool enable);

  bool eval_top_level_script(std::string_view path, JS::SourceText<mozilla::Utf8Unit> &source,
                             MutableHandleValue result, MutableHandleValue tla_promise);

  bool load_script(JSContext *cx, std::string_view script_path,
                   JS::SourceText<mozilla::Utf8Unit> &script);

  /**
   * Load a script without attempting to resolve its path relative to a base path.
   *
   * This is useful for loading ancillary scripts without interfering with, or depending on,
   * the script loader's state as determined by loading and running content scripts.
   */
  bool load_resolved_script(JSContext *cx, std::string_view specifier,
                            std::string_view resolved_path,
                            JS::SourceText<mozilla::Utf8Unit> &script);
};

#endif // SCRIPTLOADER_H
