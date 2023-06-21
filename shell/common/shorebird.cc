
#include "flutter/shell/common/shorebird.h"

#include <filesystem>
#include <optional>
#include <vector>

#include "flutter/fml/command_line.h"
#include "flutter/fml/file.h"
#include "flutter/fml/macros.h"
#include "flutter/fml/message_loop.h"
#include "flutter/fml/native_library.h"
#include "flutter/fml/paths.h"
#include "flutter/fml/size.h"
#include "flutter/lib/ui/plugins/callback_cache.h"
#include "flutter/runtime/dart_vm.h"
#include "flutter/shell/common/shell.h"
#include "flutter/shell/common/switches.h"
#include "third_party/dart/runtime/include/dart_tools_api.h"
#include "third_party/skia/include/core/SkFontMgr.h"

#include "third_party/updater/library/include/updater.h"

// Namespaced to avoid Google style warnings.
namespace flutter {

// Old Android versions (e.g. the v16 ndk Flutter uses) don't always include a
// getauxval symbol, but the Rust ring crate assumes it exists:
// https://github.com/briansmith/ring/blob/fa25bf3a7403c9fe6458cb87bd8427be41225ca2/src/cpu/arm.rs#L22
// It uses it to determine if the CPU supports AES instructions.
// Making this a weak symbol allows the linker to use a real version instead
// if it can find one.
// BoringSSL just reads from procfs instead, which is what we would do if
// we needed to implement this ourselves.  Implementation looks straightforward:
// https://lwn.net/Articles/519085/
// https://github.com/google/boringssl/blob/6ab4f0ae7f2db96d240eb61a5a8b4724e5a09b2f/crypto/cpu_arm_linux.c
#if defined(__ANDROID__) && defined(__arm__)
extern "C" __attribute__((weak)) unsigned long getauxval(unsigned long type) {
  return 0;
}
#endif

/// Checks whether the last item in the list of application library paths has a
/// filename that matches what we expect to see for a Flutter app on the current
/// platform.
///
/// On Android, this is "libapp.so".
/// On iOS, this is "App".
bool IsRunningApp(std::vector<std::string> libapp_paths) {
  std::filesystem::path path(libapp_paths.back());
  std::string filename = path.filename().string();

// Check for the filename we expect on the current platform.
#if defined(__ANDROID__)
  return filename == "libapp.so";
#elif defined(__APPLE__)
  return filename == "App";
#endif

  // If we aren't on Android or iOS, we're in an unfamiliar environment and
  // shouldn't assume we're running a Flutter app.
  return false;
}

void ConfigureShorebird(std::string cache_path,
                        flutter::Settings& settings,
                        const std::string& shorebird_yaml,
                        const std::string& version,
                        int64_t version_code) {
  auto cache_dir =
      fml::paths::JoinPaths({std::move(cache_path), "shorebird_updater"});

  fml::CreateDirectory(fml::paths::GetCachesDirectory(), {"shorebird_updater"},
                       fml::FilePermission::kReadWrite);

  // Using a block to make AppParameters lifetime explicit.
  {
    AppParameters app_parameters;
    // Combine version and version_code into a single string.
    // We could also pass these separately through to the updater if needed.
    auto release_version = version + "+" + std::to_string(version_code);
    app_parameters.release_version = release_version.c_str();
    app_parameters.cache_dir = cache_dir.c_str();

    // https://stackoverflow.com/questions/26032039/convert-vectorstring-into-char-c
    std::vector<const char*> c_paths{};
    for (const auto& string : settings.application_library_path) {
      c_paths.push_back(string.c_str());
    }
    // Do not modify application_library_path or c_strings will invalidate.

    // original_libapp_paths is a set of fallback paths to libapp.so.
    // The first item in original_libapp_paths will be "libapp.so".
    // This fails on some old versions of Android, and will fall back to the
    // second path, which is the absolute path to the libapp.so file.
    // FIXME: resolve the full path from "libapp.so" and pass that to Rust.
    // See updater.rs for more details.
    app_parameters.original_libapp_paths = c_paths.data();
    app_parameters.original_libapp_paths_size = c_paths.size();

    // If we don't believe that we're running a full Flutter app, warn the user
    // and don't try to initialize Shorebird.
    if (!IsRunningApp(settings.application_library_path)) {
      FML_LOG(WARNING)
          << "Shorebird updater: application library detected is not "
             "libapp.so, not running shorebird_init (detected "
          << settings.application_library_path.front() << ").";
      return;
    }

    // shorebird_init copies from app_parameters and shorebirdYaml.
    shorebird_init(&app_parameters, shorebird_yaml.c_str());
  }

  char* c_active_path = shorebird_next_boot_patch_path();
  if (c_active_path != NULL) {
    std::string active_path = c_active_path;
    shorebird_free_string(c_active_path);
    FML_LOG(INFO) << "Shorebird updater: active path: " << active_path;
    uintptr_t patch_number = shorebird_next_boot_patch_number();
    if (patch_number != 0) {
      FML_LOG(INFO) << "Shorebird updater: next patch number: " << patch_number;
    }

    settings.application_library_path.clear();
    settings.application_library_path.emplace_back(active_path);
    // Once start_update_thread is called, the next_boot_patch* functions may
    // change their return values if the shorebird_report_launch_failed
    // function is called.
    shorebird_report_launch_start();
  } else {
    FML_LOG(INFO) << "Shorebird updater: no active patch.";
  }

  FML_LOG(INFO) << "Starting Shorebird update";
  shorebird_start_update_thread();
}

}  // namespace flutter