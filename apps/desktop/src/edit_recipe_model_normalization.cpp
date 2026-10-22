#include "edit_recipe_state.h"

#include "photo_editor/pipeline/color_pipeline.h"

#include <QFileInfo>

namespace photo_editor::desktop {

QString normalized_photographic_rendering_model_name(
    const QString& value) {
  const auto normalized = value.trimmed().toLower();
  if (normalized == QStringLiteral("legacy-oklch-v1")) {
    return QStringLiteral("legacy-oklch-v1");
  }
  if (normalized == QStringLiteral("aces2-hellwig-jmh-v1") ||
      normalized == QStringLiteral("hellwig jmh")) {
    return QStringLiteral("aces2-hellwig-jmh-v1");
  }
  return QStringLiteral("aces2-ocio-v1");
}

QString normalized_renderer_transition_look_name(
    const QString& value) {
  return value.trimmed().compare(
             QStringLiteral("preserve-legacy-direct-luminance-v1"),
             Qt::CaseInsensitive) == 0
             ? QStringLiteral("preserve-legacy-direct-luminance-v1")
             : QStringLiteral("none");
}

QString normalized_camera_profile_rendering_model_name(
    const QString& value) {
  const auto normalized = value.trimmed().toLower();
  return normalized == QStringLiteral("matrix-only-v1") ||
                 normalized == QStringLiteral("matrix only v1") ||
                 normalized == QStringLiteral("legacy")
             ? QStringLiteral("matrix-only-v1")
             : QStringLiteral("dng-profile-v2");
}

QString normalized_highlight_reconstruction_model_name(
    const QString& value) {
  const auto normalized = value.trimmed().toLower();
  return normalized == QStringLiteral("legacy-working-rgb-v1") ||
                 normalized == QStringLiteral("legacy working rgb v1") ||
                 normalized == QStringLiteral("legacy")
             ? QStringLiteral("legacy-working-rgb-v1")
             : QStringLiteral("camera-linear-opposed-v2");
}

QString normalized_output_color_space_name(const QString& value) {
  const auto trimmed = value.trimmed();
  if (trimmed.compare(QStringLiteral("Display P3"), Qt::CaseInsensitive) == 0 ||
      trimmed.compare(QStringLiteral("display-p3"), Qt::CaseInsensitive) == 0 ||
      trimmed.compare(QStringLiteral("p3"), Qt::CaseInsensitive) == 0) {
    return QStringLiteral("Display P3");
  }
  if (trimmed.compare(QStringLiteral("Adobe RGB"), Qt::CaseInsensitive) == 0 ||
      trimmed.compare(QStringLiteral("adobe-rgb"), Qt::CaseInsensitive) == 0) {
    return QStringLiteral("Adobe RGB");
  }
  if (trimmed.compare(QStringLiteral("ProPhoto RGB"), Qt::CaseInsensitive) == 0 ||
      trimmed.compare(QStringLiteral("prophoto-rgb"), Qt::CaseInsensitive) == 0 ||
      trimmed.compare(QStringLiteral("prophoto"), Qt::CaseInsensitive) == 0) {
    return QStringLiteral("ProPhoto RGB");
  }
  if (trimmed.compare(QStringLiteral("Rec.709"), Qt::CaseInsensitive) == 0 ||
      trimmed.compare(QStringLiteral("rec709"), Qt::CaseInsensitive) == 0 ||
      trimmed.compare(QStringLiteral("rec-709"), Qt::CaseInsensitive) == 0) {
    return QStringLiteral("Rec.709");
  }
  if (trimmed.compare(QStringLiteral("Rec.2020"), Qt::CaseInsensitive) == 0 ||
      trimmed.compare(QStringLiteral("rec2020"), Qt::CaseInsensitive) == 0 ||
      trimmed.compare(QStringLiteral("rec-2020"), Qt::CaseInsensitive) == 0) {
    return QStringLiteral("Rec.2020");
  }
  return QStringLiteral("sRGB");
}

QString normalized_preview_output_mode_name(const QString& value) {
  const auto trimmed = value.trimmed();
  if (trimmed.compare(QStringLiteral("Match Export"), Qt::CaseInsensitive) == 0 ||
      trimmed.compare(QStringLiteral("match-export"), Qt::CaseInsensitive) == 0) {
    return QStringLiteral("Match Export");
  }
  if (trimmed.compare(QStringLiteral("Soft Proof"), Qt::CaseInsensitive) == 0 ||
      trimmed.compare(QStringLiteral("soft-proof"), Qt::CaseInsensitive) == 0) {
    return QStringLiteral("Soft Proof");
  }
  if (trimmed.compare(QStringLiteral("Reference Display Target"), Qt::CaseInsensitive) == 0 ||
      trimmed.compare(QStringLiteral("reference-display-target"), Qt::CaseInsensitive) == 0) {
    return QStringLiteral("Reference Display Target");
  }
  return QStringLiteral("Monitor");
}

QString normalized_input_color_profile_name(const QString& value) {
  const auto trimmed = value.trimmed();
  const auto lower = trimmed.toLower();
  const auto profile_label = [](const QString& input) {
    const auto separator = input.indexOf(QLatin1Char(':'));
    if (separator >= 0) {
      return input.mid(separator + 1).trimmed();
    }
    return QFileInfo(input).fileName().trimmed();
  };
  if (lower.startsWith(QStringLiteral("camera dcp:")) ||
      lower.startsWith(QStringLiteral("dcp:")) ||
      lower.endsWith(QStringLiteral(".dcp"))) {
    const auto label = profile_label(trimmed);
    return label.isEmpty() ? QStringLiteral("Camera DCP")
                           : QStringLiteral("Camera DCP: %1").arg(label);
  }
  if (lower.startsWith(QStringLiteral("camera icc:")) ||
      lower.startsWith(QStringLiteral("icc:")) ||
      lower.startsWith(QStringLiteral("camera icm:")) ||
      lower.startsWith(QStringLiteral("icm:")) ||
      lower.endsWith(QStringLiteral(".icc")) ||
      lower.endsWith(QStringLiteral(".icm"))) {
    const auto label = profile_label(trimmed);
    return label.isEmpty() ? QStringLiteral("Camera ICC")
                           : QStringLiteral("Camera ICC: %1").arg(label);
  }
  if (trimmed.compare(QStringLiteral("Camera Native"), Qt::CaseInsensitive) == 0 ||
      trimmed.compare(QStringLiteral("camera-native"), Qt::CaseInsensitive) == 0) {
    return QStringLiteral("Camera Native");
  }
  if (trimmed.compare(QStringLiteral("RAW Decoder Default"), Qt::CaseInsensitive) == 0 ||
      trimmed.compare(QStringLiteral("raw-decoder-default"), Qt::CaseInsensitive) == 0) {
    return QStringLiteral("RAW Decoder Default");
  }
  if (trimmed.compare(QStringLiteral("sRGB"), Qt::CaseInsensitive) == 0) {
    return QStringLiteral("sRGB");
  }
  if (trimmed.compare(QStringLiteral("Display P3"), Qt::CaseInsensitive) == 0 ||
      trimmed.compare(QStringLiteral("display-p3"), Qt::CaseInsensitive) == 0 ||
      trimmed.compare(QStringLiteral("p3"), Qt::CaseInsensitive) == 0) {
    return QStringLiteral("Display P3");
  }
  if (trimmed.compare(QStringLiteral("Adobe RGB"), Qt::CaseInsensitive) == 0 ||
      trimmed.compare(QStringLiteral("adobe-rgb"), Qt::CaseInsensitive) == 0) {
    return QStringLiteral("Adobe RGB");
  }
  if (trimmed.compare(QStringLiteral("ProPhoto RGB"), Qt::CaseInsensitive) == 0 ||
      trimmed.compare(QStringLiteral("prophoto-rgb"), Qt::CaseInsensitive) == 0 ||
      trimmed.compare(QStringLiteral("prophoto"), Qt::CaseInsensitive) == 0) {
    return QStringLiteral("ProPhoto RGB");
  }
  if (trimmed.compare(QStringLiteral("Rec.709"), Qt::CaseInsensitive) == 0 ||
      trimmed.compare(QStringLiteral("rec709"), Qt::CaseInsensitive) == 0 ||
      trimmed.compare(QStringLiteral("rec-709"), Qt::CaseInsensitive) == 0) {
    return QStringLiteral("Rec.709");
  }
  if (trimmed.compare(QStringLiteral("Rec.2020"), Qt::CaseInsensitive) == 0 ||
      trimmed.compare(QStringLiteral("rec2020"), Qt::CaseInsensitive) == 0 ||
      trimmed.compare(QStringLiteral("rec-2020"), Qt::CaseInsensitive) == 0) {
    return QStringLiteral("Rec.2020");
  }
  return QStringLiteral("Embedded");
}

QString normalized_working_color_profile_name(const QString& value) {
  return QString::fromStdString(
      photo_editor::pipeline::normalize_working_color_profile_name(
          value.toStdString()));
}

QString normalized_monitor_color_profile_name(const QString& value) {
  const auto trimmed = value.trimmed();
  if (trimmed.isEmpty() ||
      trimmed.compare(QStringLiteral("System Default"), Qt::CaseInsensitive) == 0 ||
      trimmed.compare(QStringLiteral("system-default"), Qt::CaseInsensitive) == 0) {
    return {};
  }
  return normalized_output_color_space_name(trimmed);
}

QString normalized_render_intent_name(const QString& value) {
  const auto trimmed = value.trimmed();
  if (trimmed.compare(QStringLiteral("Relative Colorimetric"), Qt::CaseInsensitive) == 0 ||
      trimmed.compare(QStringLiteral("relative-colorimetric"), Qt::CaseInsensitive) == 0) {
    return QStringLiteral("Relative Colorimetric");
  }
  return QStringLiteral("Perceptual");
}

QString normalized_gamut_mapping_policy_name(const QString& value) {
  const auto trimmed = value.trimmed();
  if (trimmed.compare(QStringLiteral("Clip"), Qt::CaseInsensitive) == 0 ||
      trimmed.compare(QStringLiteral("Clipping"), Qt::CaseInsensitive) == 0 ||
      trimmed.compare(QStringLiteral("relative-colorimetric"), Qt::CaseInsensitive) == 0) {
    return QStringLiteral("Clip");
  }
  if (trimmed.compare(QStringLiteral("Preserve Saturation"), Qt::CaseInsensitive) == 0 ||
      trimmed.compare(QStringLiteral("preserve-saturation"), Qt::CaseInsensitive) == 0) {
    return QStringLiteral("Preserve Saturation");
  }
  return QStringLiteral("Perceptual");
}

QString normalized_camera_matching_look_name(const QString& value) {
  const auto trimmed = value.trimmed();
  if (trimmed.compare(QStringLiteral("Neutral"), Qt::CaseInsensitive) == 0) {
    return QStringLiteral("Neutral");
  }
  if (trimmed.compare(QStringLiteral("Vivid"), Qt::CaseInsensitive) == 0) {
    return QStringLiteral("Vivid");
  }
  if (trimmed.compare(QStringLiteral("Portrait"), Qt::CaseInsensitive) == 0) {
    return QStringLiteral("Portrait");
  }
  if (trimmed.compare(QStringLiteral("Landscape"), Qt::CaseInsensitive) == 0) {
    return QStringLiteral("Landscape");
  }
  if (trimmed.compare(QStringLiteral("Monochrome"), Qt::CaseInsensitive) == 0 ||
      trimmed.compare(QStringLiteral("Black & White"), Qt::CaseInsensitive) == 0) {
    return QStringLiteral("Monochrome");
  }
  return QStringLiteral("Standard");
}

QString normalized_upright_mode_name(const QString& value) {
  const auto trimmed = value.trimmed();
  if (trimmed.compare(QStringLiteral("Vertical"), Qt::CaseInsensitive) == 0) {
    return QStringLiteral("Vertical");
  }
  if (trimmed.compare(QStringLiteral("Full"), Qt::CaseInsensitive) == 0) {
    return QStringLiteral("Full");
  }
  if (trimmed.compare(QStringLiteral("Guided"), Qt::CaseInsensitive) == 0) {
    return QStringLiteral("Guided");
  }
  return QStringLiteral("Off");
}

QString normalized_highlight_reconstruction_mode_name(const QString& value) {
  const auto trimmed = value.trimmed();
  if (trimmed.compare(QStringLiteral("Disabled"), Qt::CaseInsensitive) == 0) {
    return QStringLiteral("Disabled");
  }
  if (trimmed.compare(QStringLiteral("Camera Default"), Qt::CaseInsensitive) == 0 ||
      trimmed.compare(QStringLiteral("camera-default"), Qt::CaseInsensitive) == 0) {
    return QStringLiteral("Camera Default");
  }
  return QStringLiteral("Enabled");
}

QString normalized_chromatic_adaptation_method_name(const QString& value) {
  const auto trimmed = value.trimmed();
  if (trimmed.compare(QStringLiteral("CAT02"), Qt::CaseInsensitive) == 0) {
    return QStringLiteral("CAT02");
  }
  return QStringLiteral("Bradford");
}

QString normalized_raw_demosaic_quality_name(const QString& value) {
  const auto trimmed = value.trimmed();
  if (trimmed.compare(QStringLiteral("Preview"), Qt::CaseInsensitive) == 0 ||
      trimmed.compare(QStringLiteral("Draft"), Qt::CaseInsensitive) == 0) {
    return QStringLiteral("Preview");
  }
  if (trimmed.compare(QStringLiteral("Full"), Qt::CaseInsensitive) == 0 ||
      trimmed.compare(QStringLiteral("Best"), Qt::CaseInsensitive) == 0) {
    return QStringLiteral("Full");
  }
  return QStringLiteral("Balanced");
}

}  // namespace photo_editor::desktop
