#pragma once

#include "system_monitor_profile_state.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIODevice>
#include <QRectF>
#include <QScreen>
#include <QWindow>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#if defined(Q_OS_MACOS)
#include <CoreGraphics/CoreGraphics.h>
#elif defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <icm.h>
#endif

namespace photo_editor::desktop::display_profile_discovery {

inline constexpr auto kMaximumProfileBytes = qint64{64} * 1024 * 1024;

[[nodiscard]] inline SystemMonitorProfileSnapshot failure(
    const QString& display_identity,
    const QString& discovery_backend,
    const QString& failure_reason) {
  return {
      .display_identity = display_identity,
      .discovery_backend = discovery_backend,
      .last_discovery_backend = discovery_backend,
      .failure_reason = failure_reason,
  };
}

[[nodiscard]] inline SystemMonitorProfileSnapshot fromPayload(
    const QByteArray& payload,
    const QString& fallback_display_name,
    const QString& display_identity,
    const QString& discovery_backend) {
  if (payload.isEmpty() ||
      static_cast<qint64>(payload.size()) > kMaximumProfileBytes) {
    return failure(
        display_identity,
        discovery_backend,
        payload.isEmpty() ? QStringLiteral("monitor-profile-payload-empty")
                          : QStringLiteral("monitor-profile-payload-too-large"));
  }
  const auto color_space = QColorSpace::fromIccProfile(payload);
  if (!color_space.isValid()) {
    return failure(
        display_identity,
        discovery_backend,
        QStringLiteral("monitor-profile-payload-invalid"));
  }
  const auto digest =
      QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex();
  auto display_name = color_space.description().trimmed();
  if (display_name.isEmpty()) {
    display_name = fallback_display_name;
  }
  return {
      .color_space = color_space,
      .display_name = display_name,
      .identity = QStringLiteral("%1|icc-sha256:%2")
                      .arg(display_identity, QString::fromLatin1(digest)),
      .display_identity = display_identity,
      .discovery_backend = discovery_backend,
      .last_discovery_backend = discovery_backend,
      .display_target_identity =
          QStringLiteral("%1|reference-sdr-100nits").arg(display_identity),
      .display_target_provenance =
          QStringLiteral(
              "icc-verified-color-reference-luminance-defaults"),
  };
}

#if defined(Q_OS_WIN)

[[nodiscard]] inline SystemMonitorProfileSnapshot fromFile(
    const QString& profile_path,
    const QString& fallback_display_name,
    const QString& display_identity,
    const QString& discovery_backend) {
  const auto profile_info = QFileInfo(profile_path);
  if (!profile_info.isFile() || !profile_info.isReadable() ||
      profile_info.size() <= 0 ||
      profile_info.size() > kMaximumProfileBytes) {
    return failure(
        display_identity,
        discovery_backend,
        QStringLiteral("monitor-profile-file-unavailable"));
  }
  auto profile_file = QFile(profile_path);
  if (!profile_file.open(QIODevice::ReadOnly)) {
    return failure(
        display_identity,
        discovery_backend,
        QStringLiteral("monitor-profile-file-open-failed"));
  }
  const auto payload = profile_file.read(kMaximumProfileBytes + 1);
  if (static_cast<qint64>(payload.size()) != profile_info.size()) {
    return failure(
        display_identity,
        discovery_backend,
        QStringLiteral("monitor-profile-file-read-incomplete"));
  }
  return fromPayload(
      payload,
      fallback_display_name,
      display_identity,
      discovery_backend);
}

struct WindowsDisplayConfigIdentity final {
  bool valid = false;
  bool advanced_color_enabled = false;
  LUID target_adapter_id{};
  UINT32 source_id = 0U;
};

[[nodiscard]] inline WindowsDisplayConfigIdentity windowsDisplayIdentity(
    const QString& gdi_device_name) {
  if (gdi_device_name.isEmpty()) {
    return {};
  }
  for (auto attempt = 0; attempt < 3; ++attempt) {
    auto path_count = UINT32{0U};
    auto mode_count = UINT32{0U};
    if (GetDisplayConfigBufferSizes(
            QDC_ONLY_ACTIVE_PATHS, &path_count, &mode_count) !=
            ERROR_SUCCESS ||
        path_count == 0U) {
      return {};
    }
    auto paths = std::vector<DISPLAYCONFIG_PATH_INFO>(
        static_cast<std::size_t>(path_count));
    auto modes = std::vector<DISPLAYCONFIG_MODE_INFO>(
        static_cast<std::size_t>(mode_count));
    const auto query_result = QueryDisplayConfig(
        QDC_ONLY_ACTIVE_PATHS,
        &path_count,
        paths.data(),
        &mode_count,
        modes.empty() ? nullptr : modes.data(),
        nullptr);
    if (query_result == ERROR_INSUFFICIENT_BUFFER) {
      continue;
    }
    if (query_result != ERROR_SUCCESS) {
      return {};
    }
    paths.resize(static_cast<std::size_t>(path_count));
    for (const auto& path : paths) {
      auto source_name = DISPLAYCONFIG_SOURCE_DEVICE_NAME{};
      source_name.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
      source_name.header.size = static_cast<UINT32>(sizeof(source_name));
      source_name.header.adapterId = path.sourceInfo.adapterId;
      source_name.header.id = path.sourceInfo.id;
      if (DisplayConfigGetDeviceInfo(&source_name.header) != ERROR_SUCCESS ||
          QString::fromWCharArray(source_name.viewGdiDeviceName).compare(
              gdi_device_name,
              Qt::CaseInsensitive) != 0) {
        continue;
      }
      auto advanced_color = DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO{};
      advanced_color.header.type =
          DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO;
      advanced_color.header.size = static_cast<UINT32>(sizeof(advanced_color));
      advanced_color.header.adapterId = path.targetInfo.adapterId;
      advanced_color.header.id = path.targetInfo.id;
      const auto advanced_color_available =
          DisplayConfigGetDeviceInfo(&advanced_color.header) == ERROR_SUCCESS;
      return {
          .valid = true,
          .advanced_color_enabled =
              advanced_color_available &&
              advanced_color.advancedColorEnabled != 0U,
          .target_adapter_id = path.targetInfo.adapterId,
          .source_id = path.sourceInfo.id,
      };
    }
    return {};
  }
  return {};
}

[[nodiscard]] inline QString windowsColorProfilePath(
    HMODULE color_management_module,
    const QString& profile_name) {
  if (profile_name.isEmpty()) {
    return {};
  }
  if (QFileInfo(profile_name).isAbsolute()) {
    return profile_name;
  }
  using GetColorDirectory = BOOL(WINAPI*)(PCWSTR, PWSTR, PDWORD);
  const auto get_color_directory = reinterpret_cast<GetColorDirectory>(
      GetProcAddress(color_management_module, "GetColorDirectoryW"));
  if (get_color_directory == nullptr) {
    return {};
  }
  auto directory_size_bytes = DWORD{0U};
  static_cast<void>(
      get_color_directory(nullptr, nullptr, &directory_size_bytes));
  constexpr auto kMaximumColorDirectoryBytes = DWORD{64U * 1024U};
  if (directory_size_bytes < static_cast<DWORD>(sizeof(wchar_t)) ||
      directory_size_bytes > kMaximumColorDirectoryBytes) {
    return {};
  }
  const auto directory_character_count =
      static_cast<std::size_t>(directory_size_bytes) / sizeof(wchar_t) + 1U;
  auto color_directory =
      std::vector<wchar_t>(directory_character_count, L'\0');
  if (get_color_directory(
          nullptr, color_directory.data(), &directory_size_bytes) == FALSE) {
    return {};
  }
  const auto directory =
      QString::fromWCharArray(color_directory.data()).trimmed();
  return directory.isEmpty() ? QString{}
                             : QDir(directory).filePath(profile_name);
}

[[nodiscard]] inline SystemMonitorProfileSnapshot
windowsAdvancedColorProfile(
    const QString& gdi_device_name,
    const QString& fallback_display_name) {
  const auto display_identity = windowsDisplayIdentity(gdi_device_name);
  if (!display_identity.valid) {
    return {};
  }
  const auto module = LoadLibraryW(L"Mscms.dll");
  if (module == nullptr) {
    return {};
  }
  using GetDisplayDefaultProfile = HRESULT(WINAPI*)(
      WCS_PROFILE_MANAGEMENT_SCOPE,
      LUID,
      UINT32,
      COLORPROFILETYPE,
      COLORPROFILESUBTYPE,
      LPWSTR*);
  const auto get_display_default =
      reinterpret_cast<GetDisplayDefaultProfile>(
          GetProcAddress(module, "ColorProfileGetDisplayDefault"));
  if (get_display_default == nullptr) {
    FreeLibrary(module);
    return {};
  }
  const auto preferred_subtype =
      display_identity.advanced_color_enabled
          ? CPST_EXTENDED_DISPLAY_COLOR_MODE
          : CPST_STANDARD_DISPLAY_COLOR_MODE;
  auto* profile_name = static_cast<LPWSTR>(nullptr);
  auto selected_scope = WCS_PROFILE_MANAGEMENT_SCOPE_CURRENT_USER;
  auto result = get_display_default(
      selected_scope,
      display_identity.target_adapter_id,
      display_identity.source_id,
      CPT_ICC,
      preferred_subtype,
      &profile_name);
  if (FAILED(result) || profile_name == nullptr) {
    if (profile_name != nullptr) {
      LocalFree(profile_name);
      profile_name = nullptr;
    }
    selected_scope = WCS_PROFILE_MANAGEMENT_SCOPE_SYSTEM_WIDE;
    result = get_display_default(
        selected_scope,
        display_identity.target_adapter_id,
        display_identity.source_id,
        CPT_ICC,
        preferred_subtype,
        &profile_name);
  }
  if (FAILED(result) || profile_name == nullptr) {
    if (profile_name != nullptr) {
      LocalFree(profile_name);
    }
    FreeLibrary(module);
    return {};
  }
  const auto profile_path = windowsColorProfilePath(
      module, QString::fromWCharArray(profile_name));
  LocalFree(profile_name);
  FreeLibrary(module);
  return fromFile(
      profile_path,
      fallback_display_name,
      QStringLiteral("wcs-device:%1").arg(gdi_device_name),
      QStringLiteral("windows-advanced-color-%1-icc-%2")
          .arg(
              display_identity.advanced_color_enabled
                  ? QStringLiteral("extended")
                  : QStringLiteral("standard"),
              selected_scope == WCS_PROFILE_MANAGEMENT_SCOPE_CURRENT_USER
                  ? QStringLiteral("current-user")
                  : QStringLiteral("system-wide")));
}

#endif

[[nodiscard]] inline SystemMonitorProfileSnapshot discover() {
  auto* const application =
      qobject_cast<QGuiApplication*>(QCoreApplication::instance());
  if (application == nullptr) {
    return failure({}, {}, QStringLiteral("gui-application-unavailable"));
  }
  auto* screen = application->focusWindow() != nullptr
                     ? application->focusWindow()->screen()
                     : application->primaryScreen();
  if (screen == nullptr) {
    return failure({}, {}, QStringLiteral("monitor-screen-unavailable"));
  }
#if defined(Q_OS_MACOS)
  std::array<CGDirectDisplayID, 32> displays{};
  std::uint32_t display_count = 0U;
  if (CGGetActiveDisplayList(
          static_cast<std::uint32_t>(displays.size()),
          displays.data(),
          &display_count) != kCGErrorSuccess ||
      display_count == 0U) {
    return failure(
        QStringLiteral("screen:%1").arg(screen->name()),
        QStringLiteral("macos-coregraphics-display-icc"),
        QStringLiteral("active-display-list-unavailable"));
  }
  const auto screen_geometry = QRectF(screen->geometry());
  auto selected_display = CGMainDisplayID();
  auto selected_overlap = -1.0;
  for (std::uint32_t index = 0U; index < display_count; ++index) {
    const auto bounds = CGDisplayBounds(displays[index]);
    const auto display_geometry = QRectF(
        bounds.origin.x,
        bounds.origin.y,
        bounds.size.width,
        bounds.size.height);
    const auto overlap = display_geometry.intersected(screen_geometry);
    const auto overlap_area = overlap.isValid()
                                  ? overlap.width() * overlap.height()
                                  : 0.0;
    if (overlap_area > selected_overlap) {
      selected_overlap = overlap_area;
      selected_display = displays[index];
    }
  }
  auto* const native_color_space = CGDisplayCopyColorSpace(selected_display);
  const auto display_identity =
      QStringLiteral("display:%1").arg(QString::number(selected_display));
  if (native_color_space == nullptr) {
    return failure(
        display_identity,
        QStringLiteral("macos-coregraphics-display-icc"),
        QStringLiteral("display-color-space-unavailable"));
  }
  auto* const profile_data = CGColorSpaceCopyICCData(native_color_space);
  CGColorSpaceRelease(native_color_space);
  if (profile_data == nullptr) {
    return failure(
        display_identity,
        QStringLiteral("macos-coregraphics-display-icc"),
        QStringLiteral("display-icc-payload-unavailable"));
  }
  const auto profile_size = CFDataGetLength(profile_data);
  const auto* const profile_bytes = CFDataGetBytePtr(profile_data);
  if (profile_size <= 0 || profile_bytes == nullptr ||
      static_cast<qint64>(profile_size) > kMaximumProfileBytes) {
    CFRelease(profile_data);
    return failure(
        display_identity,
        QStringLiteral("macos-coregraphics-display-icc"),
        QStringLiteral("display-icc-payload-invalid-size"));
  }
  const auto payload = QByteArray(
      reinterpret_cast<const char*>(profile_bytes),
      static_cast<qsizetype>(profile_size));
  CFRelease(profile_data);
  return fromPayload(
      payload,
      screen->name(),
      display_identity,
      QStringLiteral("macos-coregraphics-display-icc"));
#elif defined(Q_OS_WIN)
  const auto screen_name = screen->name().trimmed();
  if (screen_name.isEmpty()) {
    return failure(
        {},
        QStringLiteral("windows-display-profile"),
        QStringLiteral("windows-screen-name-unavailable"));
  }
  const auto advanced_profile =
      windowsAdvancedColorProfile(screen_name, screen_name);
  if (advanced_profile.color_space.isValid()) {
    return advanced_profile;
  }
  const auto device_name = screen_name.toStdWString();
  const auto display_dc =
      CreateDCW(L"DISPLAY", device_name.c_str(), nullptr, nullptr);
  if (display_dc == nullptr) {
    return failure(
        QStringLiteral("wcs-device:%1").arg(screen_name),
        QStringLiteral("windows-wcs-geticmprofile"),
        QStringLiteral("windows-display-context-unavailable"));
  }
  auto profile_name_size = DWORD{0U};
  static_cast<void>(GetICMProfileW(display_dc, &profile_name_size, nullptr));
  if (profile_name_size == 0U || profile_name_size > 32768U) {
    DeleteDC(display_dc);
    return failure(
        QStringLiteral("wcs-device:%1").arg(screen_name),
        QStringLiteral("windows-wcs-geticmprofile"),
        QStringLiteral("windows-icc-profile-name-invalid"));
  }
  auto profile_name = std::vector<wchar_t>(profile_name_size);
  if (GetICMProfileW(display_dc, &profile_name_size, profile_name.data()) ==
      FALSE) {
    DeleteDC(display_dc);
    return failure(
        QStringLiteral("wcs-device:%1").arg(screen_name),
        QStringLiteral("windows-wcs-geticmprofile"),
        QStringLiteral("windows-icc-profile-query-failed"));
  }
  DeleteDC(display_dc);
  return fromFile(
      QString::fromWCharArray(profile_name.data()),
      screen_name,
      QStringLiteral("wcs-device:%1").arg(screen_name),
      QStringLiteral("windows-wcs-geticmprofile"));
#else
  return failure(
      QStringLiteral("screen:%1").arg(screen->name()),
      QStringLiteral("unsupported-platform"),
      QStringLiteral("system-monitor-profile-discovery-unsupported"));
#endif
}

}  // namespace photo_editor::desktop::display_profile_discovery
