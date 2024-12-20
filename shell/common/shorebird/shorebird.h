#ifndef FLUTTER_SHELL_COMMON_SHOREBIRD_SHOREBIRD_H_
#define FLUTTER_SHELL_COMMON_SHOREBIRD_SHOREBIRD_H_

#include "flutter/common/settings.h"
#include "shell/platform/embedder/embedder.h"

namespace flutter {

struct ShorebirdConfigArgs { 
  std::string code_cache_path;
  std::string app_storage_path;
  std::string release_app_library_path;
  const std::string& shorebird_yaml;
  const std::string& version;
  const std::string& version_code;

  // constructor
  ShorebirdConfigArgs(
    std::string code_cache_path, 
    std::string app_storage_path,
    std::string release_app_library_path, 
    const std::string& shorebird_yaml,
    const std::string& version, 
    const std::string& version_code) : 
  code_cache_path(code_cache_path), app_storage_path(app_storage_path), release_app_library_path(release_app_library_path), shorebird_yaml(shorebird_yaml), version(version), version_code(version_code) {}
};

std::string ConfigureShorebird(const ShorebirdConfigArgs& args);

void ConfigureShorebird(const ShorebirdFlutterProjectArgs& args,
                        flutter::Settings& settings);

void ConfigureShorebird(std::string code_cache_path,
                        std::string app_storage_path,
                        flutter::Settings& settings,
                        const std::string& shorebird_yaml,
                        const std::string& version,
                        const std::string& version_code);

}  // namespace flutter

#endif  // FLUTTER_SHELL_COMMON_SHOREBIRD_SHOREBIRD_H_
