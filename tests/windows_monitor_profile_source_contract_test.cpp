#include <cstdlib>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>

namespace {

[[nodiscard]] std::string read_file(const char* path) {
  std::ifstream stream(path, std::ios::binary);
  return std::string(
      std::istreambuf_iterator<char>(stream),
      std::istreambuf_iterator<char>());
}

[[nodiscard]] bool contains_all(
    const std::string& source,
    std::initializer_list<std::string_view> required_tokens) {
  for (const auto token : required_tokens) {
    if (source.find(token) == std::string::npos) {
      std::cerr << "missing Windows monitor-profile contract token: " << token
                << '\n';
      return false;
    }
  }
  return true;
}

}  // namespace

int main() {
  const auto discovery_source =
      read_file(PHOTO_EDITOR_DISPLAY_PROFILE_DISCOVERY_PATH);
  const auto controller_source =
      read_file(PHOTO_EDITOR_PREVIEW_CONTROLLER_PATH);
  if (discovery_source.empty()) {
    std::cerr << "failed to read display profile discovery source\n";
    return EXIT_FAILURE;
  }
  if (controller_source.empty()) {
    std::cerr << "failed to read preview controller source\n";
    return EXIT_FAILURE;
  }
  const auto discovery_contract =
      contains_all(
          discovery_source,
             {
                 "defined(Q_OS_WIN)",
                 "QueryDisplayConfig(",
                 "DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME",
                 "DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO",
                 "advancedColorEnabled",
                 "ColorProfileGetDisplayDefault",
                 "GetColorDirectoryW",
                 "WCS_PROFILE_MANAGEMENT_SCOPE_SYSTEM_WIDE",
                 "CPST_EXTENDED_DISPLAY_COLOR_MODE",
                 "CPST_STANDARD_DISPLAY_COLOR_MODE",
                 "windows-advanced-color-%1-icc-%2",
                 "CreateDCW(",
                 "GetICMProfileW(",
                 "DeleteDC(",
                 "kMaximumProfileBytes",
                 "QColorSpace::fromIccProfile(payload)",
                 "wcs-device:%1",
                 "windows-wcs-geticmprofile",
             });
  const auto controller_contract =
      contains_all(
          controller_source,
          {
                 "#include \"display_profile_discovery.h\"",
                 "display_profile_discovery::discover()",
                 "monitorProfileDiscovery",
                 "system_monitor_profile_discovery_",
                 "system_monitor_profile_timer_.setInterval(1000)",
                 "system_monitor_profile_change",
          });
  const auto controller_reclaims_discovery =
      controller_source.find("QueryDisplayConfig(") != std::string::npos ||
      controller_source.find("GetICMProfileW(") != std::string::npos ||
      controller_source.find("ColorProfileGetDisplayDefault") !=
          std::string::npos;
  if (controller_reclaims_discovery) {
    std::cerr << "preview controller reclaimed Windows profile discovery\n";
  }
  return discovery_contract && controller_contract &&
                 !controller_reclaims_discovery
             ? EXIT_SUCCESS
             : EXIT_FAILURE;
}
