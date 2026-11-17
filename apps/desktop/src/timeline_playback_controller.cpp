#include "timeline_playback_controller.h"

#include "timeline_document_controller.h"
#include "timeline_foreground_matte.h"
#include "timeline_hdr_sdr.h"
#include "timeline_processed_frame_cache.h"
#include "timeline_source_crop.h"
#include "timeline_video_grade_runtime.h"
#include "qimage_color_conversion.h"
#include "photo_editor/pipeline/color_adjustments.h"
#include "photo_editor/pipeline/color_lut_program.h"
#include "photo_editor/pipeline/color_pipeline.h"
#include "photo_editor/pipeline/photo_effects.h"
#include "photo_editor/pipeline/video_color_transform.h"

#include <QByteArrayView>
#include <QColorSpace>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QMetaObject>
#include <QMutexLocker>
#include <QQuickWindow>
#include <QSet>
#include <QStringList>
#include <QThread>
#include <QTransform>
#include <QUrl>
#include <qrgba64.h>
#include <QtMath>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <list>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

#ifdef Q_OS_UNIX
#include <sys/stat.h>
#endif

namespace photo_editor::desktop {

namespace {

constexpr qint64 kSeekToleranceMs = 20;
constexpr qint64 kPositionSyncToleranceMs = 250;
constexpr int kInactivePlaybackResourceReleaseGraceMs = 2000;
constexpr int kNativePausedPrerollTimeoutMs = 2000;
constexpr qint64 kNativePausedPrerollFrameToleranceMs = 100;
constexpr int kNativePausedPrerollRetargetIntervalMs = 50;
constexpr double kMaximumShuttleRate = 8.0;
constexpr int kFallbackPreviewWidth = 1920;
constexpr int kFallbackPreviewHeight = 1080;
constexpr double kRgba64Maximum = 65535.0;
constexpr std::size_t kParsedCubeLutCacheBudgetBytes = 64U * 1024U * 1024U;
constexpr std::size_t kParsedCubeLutCacheEntryLimit = 32U;
constexpr int kRec709ColorProfileSampleCount = 4097;
constexpr std::size_t kProcessedVideoFrameCacheBudgetBytes = 128U * 1024U * 1024U;
constexpr std::size_t kProcessedVideoFrameCacheEntryLimit = 240U;

QtVideo::Rotation video_surface_rotation(const QVideoFrameFormat& format) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
  return format.rotation();
#else
  Q_UNUSED(format)
  return QtVideo::Rotation::None;
#endif
}

QVideoFrame video_frame_from_image(const QImage& image) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
  return QVideoFrame(image);
#else
  if (image.isNull()) {
    return {};
  }
  const auto rgba = image.convertToFormat(QImage::Format_RGBA8888);
  QVideoFrame frame(QVideoFrameFormat(
      rgba.size(), QVideoFrameFormat::Format_RGBA8888));
  if (!frame.map(QVideoFrame::WriteOnly)) {
    return {};
  }
  const auto bytes_per_row = qMin(frame.bytesPerLine(0), rgba.bytesPerLine());
  for (auto y = 0; y < rgba.height(); ++y) {
    std::memcpy(
        frame.bits(0) + static_cast<qsizetype>(y) * frame.bytesPerLine(0),
        rgba.constScanLine(y),
        static_cast<std::size_t>(bytes_per_row));
  }
  frame.unmap();
  return frame;
#endif
}

TimelineProcessedFrameCache& processed_video_frame_cache() {
  static TimelineProcessedFrameCache cache(
      kProcessedVideoFrameCacheBudgetBytes,
      kProcessedVideoFrameCacheEntryLimit);
  return cache;
}

QString timeline_video_source_revision(const QString& source_url) {
  if (source_url.trimmed().isEmpty()) {
    return {};
  }
  const QUrl source(source_url);
  const auto path = source.isLocalFile()
                        ? source.toLocalFile()
                        : source.scheme().isEmpty() ? source_url : QString{};
  if (path.isEmpty()) {
    // Remote and virtual URLs have no portable local revision metadata. Keep
    // their full identity in the cache key and let decoder frame metadata
    // distinguish frames.
    return QStringLiteral("url:%1").arg(source_url);
  }

  const QFileInfo file(path);
  auto canonical_path = file.canonicalFilePath();
  if (canonical_path.isEmpty()) {
    canonical_path = file.absoluteFilePath();
  }
#ifdef Q_OS_UNIX
  struct stat info {};
  if (::stat(QFile::encodeName(path).constData(), &info) == 0) {
#ifdef Q_OS_MACOS
    const auto modified = info.st_mtimespec;
    const auto changed = info.st_ctimespec;
#else
    const auto modified = info.st_mtim;
    const auto changed = info.st_ctim;
#endif
    // Device/inode catch replacement or relinking at the same path; size,
    // mtime, and ctime catch content edits and metadata-preserving rewrites.
    return QStringLiteral("file:%1:%2:%3:%4:%5:%6:%7:%8")
        .arg(canonical_path)
        .arg(qulonglong(info.st_dev))
        .arg(qulonglong(info.st_ino))
        .arg(qlonglong(info.st_size))
        .arg(qlonglong(modified.tv_sec))
        .arg(qlonglong(modified.tv_nsec))
        .arg(qlonglong(changed.tv_sec))
        .arg(qlonglong(changed.tv_nsec));
  }
#endif
  return QStringLiteral("file:%1:%2:%3:%4:%5")
      .arg(canonical_path)
      .arg(file.size())
      .arg(file.lastModified().toMSecsSinceEpoch())
      .arg(file.metadataChangeTime().toMSecsSinceEpoch())
      .arg(file.birthTime().toMSecsSinceEpoch());
}

QPair<QImage, QString> load_still_photo_grade_source(
    const QString& path,
    const QSize& requested_size) {
  static const QSet<QString> kRawCameraExtensions{
      QStringLiteral("3fr"), QStringLiteral("arw"), QStringLiteral("cr2"),
      QStringLiteral("cr3"), QStringLiteral("dcr"), QStringLiteral("dng"),
      QStringLiteral("erf"), QStringLiteral("fff"), QStringLiteral("iiq"),
      QStringLiteral("kdc"), QStringLiteral("mef"), QStringLiteral("mos"),
      QStringLiteral("mrw"), QStringLiteral("nef"), QStringLiteral("nrw"),
      QStringLiteral("orf"), QStringLiteral("pef"), QStringLiteral("raf"),
      QStringLiteral("raw"), QStringLiteral("rw2"), QStringLiteral("rwl"),
      QStringLiteral("srw"), QStringLiteral("x3f")};
  if (kRawCameraExtensions.contains(QFileInfo(path).suffix().toLower())) {
    return {{}, QStringLiteral("RAW-camera decoding is not available in Timeline photo grading yet.")};
  }
  QImageReader reader(path);
  const auto source_size = reader.size();
  if (!reader.canRead() || !source_size.isValid() || source_size.isEmpty()) {
    return {{}, QStringLiteral("The still photo source could not be decoded for Timeline preview.")};
  }
  constexpr qint64 kMaximumStillSourcePixels = 100'000'000;
  if (static_cast<qint64>(source_size.width()) * source_size.height() >
      kMaximumStillSourcePixels) {
    return {{}, QStringLiteral("This still photo exceeds the current Timeline preview decode limit.")};
  }
  const auto preview_bounds = requested_size.isValid() && !requested_size.isEmpty()
                                  ? requested_size
                                  : QSize(1280, 720);
  const auto bounded_bounds = preview_bounds.scaled(
      QSize(1920, 1920), Qt::KeepAspectRatio);
  const auto decode_size = source_size.width() > bounded_bounds.width() ||
                                   source_size.height() > bounded_bounds.height()
                               ? source_size.scaled(bounded_bounds, Qt::KeepAspectRatio)
                               : source_size;
  reader.setScaledSize(decode_size);
  auto image = reader.read();
  if (image.isNull()) {
    return {{}, QStringLiteral("The still photo source could not be decoded for Timeline preview.")};
  }
  if (image.width() > bounded_bounds.width() ||
      image.height() > bounded_bounds.height()) {
    image = image.scaled(
        bounded_bounds, Qt::KeepAspectRatio, Qt::FastTransformation);
  }
  const auto rec709 = timeline_video_rec709_color_space();
  if (image.colorSpace().isValid() && image.colorSpace() != rec709) {
    image.convertToColorSpace(rec709);
  }
  image.setColorSpace(rec709);
  image = image.convertToFormat(QImage::Format_RGBA8888);
  if (image.isNull()) {
    return {{}, QStringLiteral("The still photo preview could not be normalized to Rec.709.")};
  }
  image.setColorSpace(rec709);
  return {std::move(image), {}};
}

std::uint16_t encode_rgba64_channel(double value) {
  return static_cast<std::uint16_t>(std::lround(
      std::clamp(value, 0.0, 1.0) * kRgba64Maximum));
}

double decode_rgba64_channel(std::uint16_t value) {
  return static_cast<double>(value) / kRgba64Maximum;
}

const QList<std::uint16_t>& rec709_video_transfer_table() {
  static const auto table = [] {
    QList<std::uint16_t> transfer_table;
    transfer_table.reserve(static_cast<qsizetype>(kRec709ColorProfileSampleCount));
    auto previous = std::uint16_t{0};
    for (int index = 0; index < kRec709ColorProfileSampleCount; ++index) {
      const auto encoded = static_cast<double>(index) /
                           static_cast<double>(kRec709ColorProfileSampleCount - 1);
      const auto linear = encoded < 0.081
                              ? encoded / 4.5
                              : std::pow((encoded + 0.099) / 1.099, 1.0 / 0.45);
      const auto sample = static_cast<std::uint16_t>(std::lround(
          std::clamp(linear, 0.0, 1.0) * 65535.0));
      previous = std::max(previous, sample);
      transfer_table.push_back(previous);
    }
    transfer_table.front() = 0;
    transfer_table.back() = std::numeric_limits<std::uint16_t>::max();
    return transfer_table;
  }();
  return table;
}

const QColorSpace& rec709_video_signal_color_space() {
  static const auto color_space = [] {
    auto result = QColorSpace(
        QColorSpace::Primaries::SRgb, rec709_video_transfer_table());
    result.setDescription(QStringLiteral("Rec.709 video signal"));
    return result;
  }();
  return color_space;
}

QColorSpace video_frame_rgb_color_space(const QVideoFrameFormat& format) {
  auto source_space = format.colorSpace();
  if (source_space == QVideoFrameFormat::ColorSpace_Undefined) {
    source_space = format.frameHeight() > 576
                       ? QVideoFrameFormat::ColorSpace_BT709
                       : QVideoFrameFormat::ColorSpace_BT601;
  }
  auto primaries = QColorSpace::Primaries::SRgb;
  auto use_bt601_primaries = false;
  auto use_bt2020_primaries = false;
  switch (source_space) {
    case QVideoFrameFormat::ColorSpace_BT2020:
      use_bt2020_primaries = true;
      break;
    case QVideoFrameFormat::ColorSpace_AdobeRgb:
      primaries = QColorSpace::Primaries::AdobeRgb;
      break;
    case QVideoFrameFormat::ColorSpace_BT601:
      use_bt601_primaries = true;
      break;
    case QVideoFrameFormat::ColorSpace_BT709:
    case QVideoFrameFormat::ColorSpace_Undefined:
      break;
  }
  auto custom_white_point = QPointF{};
  auto custom_red_point = QPointF{};
  auto custom_green_point = QPointF{};
  auto custom_blue_point = QPointF{};
  if (use_bt601_primaries) {
    custom_white_point = QPointF(0.3127, 0.3290);
    if (format.frameHeight() <= 576) {
      custom_red_point = QPointF(0.630, 0.340);
      custom_green_point = QPointF(0.310, 0.595);
      custom_blue_point = QPointF(0.155, 0.070);
    } else {
      custom_red_point = QPointF(0.640, 0.330);
      custom_green_point = QPointF(0.290, 0.600);
      custom_blue_point = QPointF(0.150, 0.060);
    }
  }
  if (use_bt2020_primaries) {
    custom_white_point = QPointF(0.3127, 0.3290);
    custom_red_point = QPointF(0.7080, 0.2920);
    custom_green_point = QPointF(0.1700, 0.7970);
    custom_blue_point = QPointF(0.1310, 0.0460);
  }
  const auto with_primaries = [&](QColorSpace::TransferFunction transfer,
                                  float gamma = 0.0F) {
    if (use_bt601_primaries || use_bt2020_primaries) {
      return QColorSpace(
          custom_white_point,
          custom_red_point,
          custom_green_point,
          custom_blue_point,
          transfer,
          gamma);
    }
    return QColorSpace(primaries, transfer, gamma);
  };
  const auto with_rec709_transfer_table = [&]() {
    const auto& transfer_table = rec709_video_transfer_table();
    if (use_bt601_primaries || use_bt2020_primaries) {
      return QColorSpace(custom_white_point,
                         custom_red_point,
                         custom_green_point,
                         custom_blue_point,
                         transfer_table);
    }
    return QColorSpace(primaries, transfer_table);
  };

  switch (format.colorTransfer()) {
    case QVideoFrameFormat::ColorTransfer_Linear:
      return with_primaries(QColorSpace::TransferFunction::Linear);
    case QVideoFrameFormat::ColorTransfer_Gamma22:
      return with_primaries(QColorSpace::TransferFunction::Gamma, 2.2F);
    case QVideoFrameFormat::ColorTransfer_Gamma28:
      return with_primaries(QColorSpace::TransferFunction::Gamma, 2.8F);
    case QVideoFrameFormat::ColorTransfer_ST2084:
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
      return QColorSpace::NamedColorSpace::Bt2100Pq;
#else
      return with_primaries(QColorSpace::TransferFunction::Linear);
#endif
    case QVideoFrameFormat::ColorTransfer_STD_B67:
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
      return QColorSpace::NamedColorSpace::Bt2100Hlg;
#else
      return with_primaries(QColorSpace::TransferFunction::Linear);
#endif
    case QVideoFrameFormat::ColorTransfer_Unknown:
      if (source_space == QVideoFrameFormat::ColorSpace_AdobeRgb) {
        return QColorSpace::NamedColorSpace::AdobeRgb;
      }
      if (source_space == QVideoFrameFormat::ColorSpace_BT2020) {
        return with_rec709_transfer_table();
      }
      return with_rec709_transfer_table();
    case QVideoFrameFormat::ColorTransfer_BT709:
    case QVideoFrameFormat::ColorTransfer_BT601:
      return with_rec709_transfer_table();
  }
  return with_rec709_transfer_table();
}

bool is_rgb_video_pixel_format(QVideoFrameFormat::PixelFormat format) {
  switch (format) {
    case QVideoFrameFormat::Format_ARGB8888:
    case QVideoFrameFormat::Format_ARGB8888_Premultiplied:
    case QVideoFrameFormat::Format_XRGB8888:
    case QVideoFrameFormat::Format_BGRA8888:
    case QVideoFrameFormat::Format_BGRA8888_Premultiplied:
    case QVideoFrameFormat::Format_BGRX8888:
    case QVideoFrameFormat::Format_ABGR8888:
    case QVideoFrameFormat::Format_XBGR8888:
    case QVideoFrameFormat::Format_RGBA8888:
    case QVideoFrameFormat::Format_RGBX8888:
      return true;
    default:
      return false;
  }
}

QString video_frame_color_space_name(QVideoFrameFormat::ColorSpace color_space) {
  switch (color_space) {
    case QVideoFrameFormat::ColorSpace_BT601:
      return QStringLiteral("BT.601");
    case QVideoFrameFormat::ColorSpace_BT709:
      return QStringLiteral("BT.709");
    case QVideoFrameFormat::ColorSpace_AdobeRgb:
      return QStringLiteral("Adobe RGB");
    case QVideoFrameFormat::ColorSpace_BT2020:
      return QStringLiteral("BT.2020");
    case QVideoFrameFormat::ColorSpace_Undefined:
      return QStringLiteral("Unknown");
  }
  return QStringLiteral("Unknown");
}

QString video_frame_transfer_name(QVideoFrameFormat::ColorTransfer transfer) {
  switch (transfer) {
    case QVideoFrameFormat::ColorTransfer_BT709:
      return QStringLiteral("BT.709");
    case QVideoFrameFormat::ColorTransfer_BT601:
      return QStringLiteral("BT.601");
    case QVideoFrameFormat::ColorTransfer_Linear:
      return QStringLiteral("Linear");
    case QVideoFrameFormat::ColorTransfer_Gamma22:
      return QStringLiteral("Gamma 2.2");
    case QVideoFrameFormat::ColorTransfer_Gamma28:
      return QStringLiteral("Gamma 2.8");
    case QVideoFrameFormat::ColorTransfer_ST2084:
      return QStringLiteral("PQ / ST 2084");
    case QVideoFrameFormat::ColorTransfer_STD_B67:
      return QStringLiteral("HLG / ARIB STD-B67");
    case QVideoFrameFormat::ColorTransfer_Unknown:
      return QStringLiteral("Unknown");
  }
  return QStringLiteral("Unknown");
}

QString video_frame_range_name(QVideoFrameFormat::ColorRange range) {
  switch (range) {
    case QVideoFrameFormat::ColorRange_Video:
      return QStringLiteral("Video range");
    case QVideoFrameFormat::ColorRange_Full:
      return QStringLiteral("Full range");
    case QVideoFrameFormat::ColorRange_Unknown:
      return QStringLiteral("Unknown range");
  }
  return QStringLiteral("Unknown range");
}

QVariantMap video_frame_color_metadata(const QVideoFrameFormat& format) {
  if (!format.isValid()) {
    return {};
  }
  auto pixel_format = QVideoFrameFormat::pixelFormatToString(format.pixelFormat());
  if (pixel_format.startsWith(QStringLiteral("Format_"))) {
    pixel_format.remove(0, 7);
  }
  return {
      {QStringLiteral("width"), format.frameWidth()},
      {QStringLiteral("height"), format.frameHeight()},
      {QStringLiteral("pixelFormat"), pixel_format},
      {QStringLiteral("colorSpace"),
       video_frame_color_space_name(format.colorSpace())},
      {QStringLiteral("transfer"), video_frame_transfer_name(format.colorTransfer())},
      {QStringLiteral("range"), video_frame_range_name(format.colorRange())},
      {QStringLiteral("maxLuminanceNits"), format.maxLuminance()},
  };
}

std::string video_frame_matrix_token(QVideoFrameFormat::ColorSpace color_space) {
  switch (color_space) {
    case QVideoFrameFormat::ColorSpace_BT601:
      return "smpte170m";
    case QVideoFrameFormat::ColorSpace_BT709:
      return "bt709";
    case QVideoFrameFormat::ColorSpace_BT2020:
      return "bt2020nc";
    case QVideoFrameFormat::ColorSpace_AdobeRgb:
      return "adobe-rgb";
    case QVideoFrameFormat::ColorSpace_Undefined:
      return {};
  }
  return {};
}

std::string video_frame_transfer_token(
    QVideoFrameFormat::ColorTransfer transfer) {
  switch (transfer) {
    case QVideoFrameFormat::ColorTransfer_BT709:
      return "bt709";
    case QVideoFrameFormat::ColorTransfer_BT601:
      return "smpte170m";
    case QVideoFrameFormat::ColorTransfer_Linear:
      return "linear";
    case QVideoFrameFormat::ColorTransfer_Gamma22:
      return "gamma22";
    case QVideoFrameFormat::ColorTransfer_Gamma28:
      return "gamma28";
    case QVideoFrameFormat::ColorTransfer_ST2084:
      return "smpte2084";
    case QVideoFrameFormat::ColorTransfer_STD_B67:
      return "arib-std-b67";
    case QVideoFrameFormat::ColorTransfer_Unknown:
      return {};
  }
  return {};
}

std::string video_frame_range_token(QVideoFrameFormat::ColorRange range) {
  switch (range) {
    case QVideoFrameFormat::ColorRange_Video:
      return "limited";
    case QVideoFrameFormat::ColorRange_Full:
      return "full";
    case QVideoFrameFormat::ColorRange_Unknown:
      return {};
  }
  return {};
}

photo_editor::pipeline::ResolvedVideoSourceSignalMetadata
resolved_video_frame_signal(
    const QVideoFrameFormat& format,
    bool camera_input_transform_selected) {
  auto primaries = std::string{};
  switch (format.colorSpace()) {
    case QVideoFrameFormat::ColorSpace_BT601:
      primaries = "smpte170m";
      break;
    case QVideoFrameFormat::ColorSpace_BT709:
      primaries = "bt709";
      break;
    case QVideoFrameFormat::ColorSpace_BT2020:
      primaries = "bt2020";
      break;
    case QVideoFrameFormat::ColorSpace_AdobeRgb:
      primaries = "jedec-p22";
      break;
    case QVideoFrameFormat::ColorSpace_Undefined:
      break;
  }
  return photo_editor::pipeline::resolve_video_source_signal_metadata(
      photo_editor::pipeline::VideoSourceSignalMetadata{
          .matrix = video_frame_matrix_token(format.colorSpace()),
          .range = video_frame_range_token(format.colorRange()),
          .transfer = video_frame_transfer_token(format.colorTransfer()),
          .primaries = std::move(primaries),
          .frame_height = format.frameHeight(),
      },
      camera_input_transform_selected);
}

struct VideoFrameAdjustmentGrade final {
  QString clip_id;
  QString recipe_json;
  double opacity = 1.0;
  QVariantMap animated_parameters;
  std::shared_ptr<TimelineVideoGradeRenderSession> session;
};

struct VideoFrameColorSettings final {
  QString clip_id;
  QString source_identity;
  QString source_revision;
  QString input_transform_key;
  QString grade_recipe_json;
  QVariantMap animated_grade_parameters;
  std::shared_ptr<TimelineVideoGradeRenderSession> grade_session;
  QVector<VideoFrameAdjustmentGrade> adjustment_grades;
  QString creative_lut_path;
  QString creative_lut_revision;
  double shot_match_red_gain = 1.0;
  double shot_match_green_gain = 1.0;
  double shot_match_blue_gain = 1.0;
  TimelinePlaybackEffectParameters effects;
  int sequence_width = 0;
  int sequence_height = 0;
  QSize preview_target_size;
};

bool timeline_effects_have_color_adjustments(
    const TimelinePlaybackEffectParameters& effects) {
  return effects.exposure != 0.0 || effects.contrast != 0.0 ||
         effects.saturation != 0.0 || effects.black_and_white != 0.0 ||
         effects.fade != 0.0 || effects.punch != 0.0 ||
         !effects.spatial_color_effects.isEmpty();
}

bool timeline_effects_are_active(
    const TimelinePlaybackEffectParameters& effects) {
  return timeline_effects_have_color_adjustments(effects) || effects.blur > 0.0;
}

struct CachedCubeLutExecution final {
  std::shared_ptr<const photo_editor::pipeline::CompiledUserLutStageProgram> program;
  std::shared_ptr<const photo_editor::pipeline::ColorLut3D> rec709_preview_lut;
};

struct ParsedCubeLutCacheEntry final {
  std::string key;
  std::shared_ptr<const CachedCubeLutExecution> execution;
  std::size_t byte_cost = 0U;
};

struct ParsedCubeLutCache final {
  std::mutex mutex;
  std::list<ParsedCubeLutCacheEntry> entries;
  std::unordered_map<
      std::string,
      std::list<ParsedCubeLutCacheEntry>::iterator> by_key;
  std::size_t resident_bytes = 0U;
  std::uint64_t hits = 0U;
  std::uint64_t misses = 0U;
  std::uint64_t evictions = 0U;
};

ParsedCubeLutCache& parsed_cube_lut_cache() {
  static ParsedCubeLutCache cache;
  return cache;
}

std::shared_ptr<const photo_editor::pipeline::ColorLut3D>
compile_rec709_preview_cube(
    const photo_editor::pipeline::CompiledUserLutStageProgram& program) {
  using photo_editor::pipeline::ColorLut3D;
  using photo_editor::pipeline::ColorPixel;
  using photo_editor::pipeline::TransferFunction;
  constexpr auto maximum_encoded_input = 2.0;
  constexpr auto p99_error_limit = 0.0015;
  constexpr auto maximum_error_limit = 0.005;
  for (const auto size : {33, 65}) {
    auto lut = ColorLut3D{};
    lut.size = size;
    lut.domain_min = {0.0, 0.0, 0.0};
    lut.domain_max = {
        maximum_encoded_input,
        maximum_encoded_input,
        maximum_encoded_input};
    lut.input_color_space = "Rec.709";
    lut.output_color_space = "Rec.709";
    const auto cube_size = static_cast<std::size_t>(size);
    lut.table.resize(cube_size * cube_size * cube_size);
    const auto table_coordinate = [size](int index) {
      return static_cast<float>(maximum_encoded_input *
                                static_cast<double>(index) /
                                static_cast<double>(size - 1));
    };
    const auto evaluate = [&program](const ColorPixel& encoded) {
      const auto linear = ColorPixel{
          .red = static_cast<float>(
              photo_editor::pipeline::decode_color_transfer_function(
                  encoded.red, TransferFunction::Bt709)),
          .green = static_cast<float>(
              photo_editor::pipeline::decode_color_transfer_function(
                  encoded.green, TransferFunction::Bt709)),
          .blue = static_cast<float>(
              photo_editor::pipeline::decode_color_transfer_function(
                  encoded.blue, TransferFunction::Bt709)),
          .alpha = encoded.alpha,
      };
      const auto graded = photo_editor::pipeline::apply_compiled_user_lut_stage_program(
          program, linear);
      return ColorPixel{
          .red = static_cast<float>(
              photo_editor::pipeline::encode_color_transfer_function(
                  graded.red, TransferFunction::Bt709)),
          .green = static_cast<float>(
              photo_editor::pipeline::encode_color_transfer_function(
                  graded.green, TransferFunction::Bt709)),
          .blue = static_cast<float>(
              photo_editor::pipeline::encode_color_transfer_function(
                  graded.blue, TransferFunction::Bt709)),
          .alpha = encoded.alpha,
      };
    };
    for (int red = 0; red < size; ++red) {
      for (int green = 0; green < size; ++green) {
        for (int blue = 0; blue < size; ++blue) {
          const auto index = static_cast<std::size_t>(red) * cube_size * cube_size +
                             static_cast<std::size_t>(green) * cube_size +
                             static_cast<std::size_t>(blue);
          lut.table[index] = evaluate(ColorPixel{
              .red = table_coordinate(red),
              .green = table_coordinate(green),
              .blue = table_coordinate(blue),
              .alpha = 1.0F,
          });
        }
      }
    }
    lut.valid = true;

    const auto validation_grid_size = size - 1;
    auto errors = std::vector<double>{};
    const auto validation_cube_size =
        static_cast<std::size_t>(validation_grid_size);
    errors.reserve(validation_cube_size * validation_cube_size *
                   validation_cube_size);
    auto maximum_error = 0.0;
    const auto validation_coordinate = [validation_grid_size](int index) {
      return static_cast<float>(maximum_encoded_input *
                                (static_cast<double>(index) + 0.5) /
                                static_cast<double>(validation_grid_size));
    };
    for (int red = 0; red < validation_grid_size; ++red) {
      for (int green = 0; green < validation_grid_size; ++green) {
        for (int blue = 0; blue < validation_grid_size; ++blue) {
          const auto input = ColorPixel{
              .red = validation_coordinate(red),
              .green = validation_coordinate(green),
              .blue = validation_coordinate(blue),
              .alpha = 1.0F,
          };
          const auto expected = evaluate(input);
          const auto actual = photo_editor::pipeline::apply_lut(lut, input);
          const auto error = std::max({
              std::abs(static_cast<double>(expected.red) - actual.red),
              std::abs(static_cast<double>(expected.green) - actual.green),
              std::abs(static_cast<double>(expected.blue) - actual.blue),
          });
          errors.push_back(error);
          maximum_error = std::max(maximum_error, error);
        }
      }
    }
    const auto p99_index = static_cast<std::size_t>(
        std::ceil(static_cast<double>(errors.size()) * 0.99) - 1.0);
    std::nth_element(errors.begin(), errors.begin() +
                         static_cast<std::ptrdiff_t>(p99_index), errors.end());
    if (errors[p99_index] <= p99_error_limit &&
        maximum_error <= maximum_error_limit) {
      return std::make_shared<const ColorLut3D>(std::move(lut));
    }
  }
  return {};
}

std::shared_ptr<const CachedCubeLutExecution> cached_cube_lut(
    const QString& path) {
  const QFileInfo file_info(path);
  const auto absolute_path = file_info.canonicalFilePath().isEmpty()
                                 ? file_info.absoluteFilePath()
                                 : file_info.canonicalFilePath();
  const auto cache_key = QStringLiteral("%1\n%2")
                             .arg(absolute_path,
                                  timeline_video_source_revision(absolute_path))
                             .toUtf8()
                             .toStdString();
  auto& cache = parsed_cube_lut_cache();
  {
    const std::lock_guard lock(cache.mutex);
    const auto found = cache.by_key.find(cache_key);
    if (found != cache.by_key.end()) {
      ++cache.hits;
      cache.entries.splice(cache.entries.begin(), cache.entries, found->second);
      return found->second->execution;
    }
    ++cache.misses;
  }

  std::shared_ptr<const photo_editor::pipeline::CompiledUserLutStageProgram>
      compiled_program;
  if (file_info.isFile() && file_info.isReadable()) {
    auto parsed = photo_editor::pipeline::load_cube_lut_file(
        absolute_path.toStdString());
    if (parsed.ok()) {
      auto lut_state = photo_editor::pipeline::ColorAdjustmentState{};
      lut_state.color_pipeline.working_profile =
          photo_editor::pipeline::working_color_profile_from_name(
              "Linear Rec.709");
      lut_state.lut_enabled = true;
      lut_state.lut_strength = 100.0;
      lut_state.lut_size = parsed.lut.size;
      lut_state.lut_domain_min = parsed.lut.domain_min;
      lut_state.lut_domain_max = parsed.lut.domain_max;
      lut_state.lut_input_color_space = parsed.lut.input_color_space;
      lut_state.lut_output_color_space = parsed.lut.output_color_space;
      lut_state.lut_table = std::move(parsed.lut.table);
      lut_state.lut_shaper_table = std::move(parsed.lut.shaper_table);
      lut_state.lut_shaper_size = parsed.lut.shaper_size;
      lut_state.lut_shaper_input_range = parsed.lut.shaper_input_range;
      auto program = photo_editor::pipeline::compile_user_lut_stage_program(
          lut_state);
      if (program.ready()) {
        compiled_program =
            std::make_shared<const photo_editor::pipeline::CompiledUserLutStageProgram>(
                std::move(program));
      }
    }
  }

  if (compiled_program == nullptr) {
    return {};
  }
  auto execution = std::make_shared<CachedCubeLutExecution>();
  execution->program = std::move(compiled_program);
  execution->rec709_preview_lut = compile_rec709_preview_cube(*execution->program);

  auto byte_cost = std::size_t{0};
  for (const auto& lut : execution->program->luts) {
    byte_cost += lut.lut.table.capacity() *
                     sizeof(photo_editor::pipeline::ColorPixel) +
                 lut.lut.shaper_table.capacity() *
                     sizeof(photo_editor::pipeline::ColorPixel);
  }
  if (execution->rec709_preview_lut != nullptr) {
    byte_cost += execution->rec709_preview_lut->table.capacity() *
                 sizeof(photo_editor::pipeline::ColorPixel);
  }
  if (byte_cost > kParsedCubeLutCacheBudgetBytes) {
    return execution;
  }

    const std::lock_guard lock(cache.mutex);
    const auto found = cache.by_key.find(cache_key);
    if (found != cache.by_key.end()) {
      cache.entries.splice(cache.entries.begin(), cache.entries, found->second);
      return found->second->execution;
  }
  cache.entries.push_front(
      ParsedCubeLutCacheEntry{cache_key, execution, byte_cost});
  cache.by_key.emplace(cache_key, cache.entries.begin());
  cache.resident_bytes += byte_cost;
  while (cache.resident_bytes > kParsedCubeLutCacheBudgetBytes ||
         cache.entries.size() > kParsedCubeLutCacheEntryLimit) {
    const auto last = std::prev(cache.entries.end());
    cache.resident_bytes -= last->byte_cost;
    cache.by_key.erase(last->key);
    cache.entries.erase(last);
    ++cache.evictions;
  }
  return cache.entries.front().execution;
}

QString video_grade_render_session_key(
    const QString& clip_id,
    const QString& recipe_json,
    const QSize& output_size) {
  auto payload = clip_id.toUtf8();
  payload.append('\n');
  payload.append(recipe_json.toUtf8());
  payload.append('\n');
  payload.append(QByteArray::number(output_size.width()));
  payload.append('x');
  payload.append(QByteArray::number(output_size.height()));
  return QString::fromLatin1(
      QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex());
}

QString video_frame_color_settings_key(const VideoFrameColorSettings& settings) {
  auto values = QStringList{
             settings.clip_id,
             settings.source_identity,
             settings.source_revision,
             settings.input_transform_key,
             QString::fromLatin1(QCryptographicHash::hash(
             settings.grade_recipe_json.toUtf8(),
             QCryptographicHash::Sha256).toHex()),
             settings.creative_lut_path,
             settings.creative_lut_revision,
             QString::number(settings.shot_match_red_gain, 'g', 17),
             QString::number(settings.shot_match_green_gain, 'g', 17),
             QString::number(settings.shot_match_blue_gain, 'g', 17),
             QString::number(settings.effects.exposure, 'g', 17),
             QString::number(settings.effects.contrast, 'g', 17),
             QString::number(settings.effects.saturation, 'g', 17),
             QString::number(settings.effects.black_and_white, 'g', 17),
             QString::number(settings.effects.fade, 'g', 17),
             QString::number(settings.effects.punch, 'g', 17),
             QString::number(settings.effects.blur, 'g', 17),
             QString::number(settings.effects.blur_mask_enabled),
             settings.effects.blur_mask_shape,
             QString::number(settings.effects.blur_mask_center_x, 'g', 17),
             QString::number(settings.effects.blur_mask_center_y, 'g', 17),
             QString::number(settings.effects.blur_mask_width, 'g', 17),
             QString::number(settings.effects.blur_mask_height, 'g', 17),
             QString::number(settings.effects.blur_mask_feather, 'g', 17),
             QString::number(settings.effects.blur_mask_inverted),
             QString::number(settings.sequence_width),
             QString::number(settings.sequence_height),
             QStringLiteral("%1x%2")
                 .arg(settings.preview_target_size.width())
                 .arg(settings.preview_target_size.height())};
  for (const auto& effect : settings.effects.spatial_color_effects) {
    values.push_back(effect.name);
    values.push_back(QString::number(effect.amount, 'g', 17));
    values.push_back(effect.mask_shape);
    values.push_back(QString::number(effect.mask_center_x, 'g', 17));
    values.push_back(QString::number(effect.mask_center_y, 'g', 17));
    values.push_back(QString::number(effect.mask_width, 'g', 17));
    values.push_back(QString::number(effect.mask_height, 'g', 17));
    values.push_back(QString::number(effect.mask_feather, 'g', 17));
    values.push_back(QString::number(effect.mask_inverted));
  }
  for (auto parameter = settings.animated_grade_parameters.cbegin();
       parameter != settings.animated_grade_parameters.cend(); ++parameter) {
    values.push_back(parameter.key());
    values.push_back(QString::number(parameter.value().toDouble(), 'g', 17));
  }
  for (const auto& adjustment : settings.adjustment_grades) {
    values.push_back(adjustment.clip_id);
    values.push_back(QString::fromLatin1(QCryptographicHash::hash(
        adjustment.recipe_json.toUtf8(), QCryptographicHash::Sha256).toHex()));
    values.push_back(QString::number(adjustment.opacity, 'g', 17));
    for (auto parameter = adjustment.animated_parameters.cbegin();
         parameter != adjustment.animated_parameters.cend(); ++parameter) {
      values.push_back(parameter.key());
      values.push_back(QString::number(parameter.value().toDouble(), 'g', 17));
    }
  }
  return values.join(QChar(0x1f));
}

QString video_foreground_cutout_settings_key(const QVariantMap& clip) {
  // This identity also guards the per-clip snapshot captured by an async frame
  // worker. Include source crop edges so crop-only edits refresh that snapshot
  // and invalidate frames queued with the previous geometry.
  const auto metadata = clip.value(QStringLiteral("foregroundCutout")).toMap();
  const auto enabled = clip.contains(QStringLiteral("foregroundCutoutEnabled"))
                           ? clip.value(QStringLiteral("foregroundCutoutEnabled")).toBool()
                           : metadata.value(QStringLiteral("enabled")).toBool();
  const auto fields = QStringList{
      QString::number(enabled),
      QString::number(clip.value(QStringLiteral("foregroundCutoutReady")).toBool()),
      QString::number(metadata.value(QStringLiteral("enabled")).toBool()),
      clip.value(QStringLiteral("sourceIdentitySignature")).toString(),
      metadata.value(QStringLiteral("directory")).toString(),
      metadata.value(QStringLiteral("sourceSignature")).toString(),
      QString::number(metadata.value(QStringLiteral("fps")).toDouble(), 'g', 17),
      QString::number(metadata.value(QStringLiteral("trimStart")).toDouble(), 'g', 17),
      QString::number(metadata.value(QStringLiteral("trimEnd")).toDouble(), 'g', 17),
      QString::number(metadata.value(QStringLiteral("frameCount")).toInt()),
      QString::number(clip.value(QStringLiteral("sourceCropLeft"), 0.0).toDouble(), 'g', 17),
      QString::number(clip.value(QStringLiteral("sourceCropTop"), 0.0).toDouble(), 'g', 17),
      QString::number(clip.value(QStringLiteral("sourceCropRight"), 1.0).toDouble(), 'g', 17),
      QString::number(clip.value(QStringLiteral("sourceCropBottom"), 1.0).toDouble(), 'g', 17),
  };
  return QString::fromLatin1(QCryptographicHash::hash(
      fields.join(QLatin1Char('\n')).toUtf8(), QCryptographicHash::Sha256).toHex());
}

struct DecodedFramePlaneShape final {
  int row_bytes = 0;
  int rows = 0;
};

QByteArray decoded_video_frame_content_identity(const QVideoFrame& source) {
  const auto size = source.surfaceFormat().frameSize();
  const auto width = size.width();
  const auto height = size.height();
  if (!size.isValid() || size.isEmpty()) {
    return {};
  }

  std::array<DecodedFramePlaneShape, 3> plane_shapes{};
  auto plane_count = 0;
  const auto chroma_width = (width + 1) / 2;
  const auto chroma_height = (height + 1) / 2;
  const auto pixel_format = source.surfaceFormat().pixelFormat();
  switch (pixel_format) {
    case QVideoFrameFormat::Format_YUV420P:
    case QVideoFrameFormat::Format_YV12:
      plane_count = 3;
      plane_shapes[0] = {width, height};
      plane_shapes[1] = {chroma_width, chroma_height};
      plane_shapes[2] = {chroma_width, chroma_height};
      break;
    case QVideoFrameFormat::Format_YUV420P10:
      plane_count = 3;
      plane_shapes[0] = {width * 2, height};
      plane_shapes[1] = {chroma_width * 2, chroma_height};
      plane_shapes[2] = {chroma_width * 2, chroma_height};
      break;
    case QVideoFrameFormat::Format_YUV422P:
      plane_count = 3;
      plane_shapes[0] = {width, height};
      plane_shapes[1] = {chroma_width, height};
      plane_shapes[2] = {chroma_width, height};
      break;
    case QVideoFrameFormat::Format_NV12:
    case QVideoFrameFormat::Format_NV21:
      plane_count = 2;
      plane_shapes[0] = {width, height};
      plane_shapes[1] = {chroma_width * 2, chroma_height};
      break;
    case QVideoFrameFormat::Format_P010:
    case QVideoFrameFormat::Format_P016:
      plane_count = 2;
      plane_shapes[0] = {width * 2, height};
      plane_shapes[1] = {chroma_width * 4, chroma_height};
      break;
    case QVideoFrameFormat::Format_UYVY:
    case QVideoFrameFormat::Format_YUYV:
      plane_count = 1;
      plane_shapes[0] = {chroma_width * 4, height};
      break;
    case QVideoFrameFormat::Format_AYUV:
    case QVideoFrameFormat::Format_AYUV_Premultiplied:
      plane_count = 1;
      plane_shapes[0] = {width * 4, height};
      break;
    case QVideoFrameFormat::Format_ARGB8888:
    case QVideoFrameFormat::Format_ARGB8888_Premultiplied:
    case QVideoFrameFormat::Format_XRGB8888:
    case QVideoFrameFormat::Format_BGRA8888:
    case QVideoFrameFormat::Format_BGRA8888_Premultiplied:
    case QVideoFrameFormat::Format_BGRX8888:
    case QVideoFrameFormat::Format_ABGR8888:
    case QVideoFrameFormat::Format_XBGR8888:
    case QVideoFrameFormat::Format_RGBA8888:
    case QVideoFrameFormat::Format_RGBX8888:
      plane_count = 1;
      plane_shapes[0] = {width * 4, height};
      break;
    default:
      break;
  }

  QCryptographicHash hash(QCryptographicHash::Sha256);
  auto mapped_frame = source;
  auto mapped_planes_hashed = false;
  if (plane_count > 0 && mapped_frame.map(QVideoFrame::ReadOnly)) {
    auto valid_planes = mapped_frame.planeCount() >= plane_count;
    for (int plane = 0; valid_planes && plane < plane_count; ++plane) {
      const auto shape = plane_shapes[static_cast<std::size_t>(plane)];
      const auto stride = mapped_frame.bytesPerLine(plane);
      const auto required_bytes = static_cast<qint64>(stride) * shape.rows;
      valid_planes = mapped_frame.bits(plane) != nullptr &&
                     stride >= shape.row_bytes && shape.row_bytes > 0 &&
                     shape.rows > 0 && required_bytes >= 0 &&
                     mapped_frame.mappedBytes(plane) >= required_bytes;
    }
    if (valid_planes) {
      const auto header = QByteArrayLiteral("planes:") +
          QByteArray::number(static_cast<int>(pixel_format)) + ':' +
          QByteArray::number(width) + 'x' + QByteArray::number(height) + ':' +
          QByteArray::number(plane_count);
      hash.addData(header);
      for (int plane = 0; plane < plane_count; ++plane) {
        const auto shape = plane_shapes[static_cast<std::size_t>(plane)];
        const auto* bits = mapped_frame.bits(plane);
        const auto stride = mapped_frame.bytesPerLine(plane);
        hash.addData(QByteArray::number(shape.row_bytes));
        hash.addData(QByteArrayLiteral("x"));
        hash.addData(QByteArray::number(shape.rows));
        for (int row = 0; row < shape.rows; ++row) {
          const auto* row_bits = bits +
              static_cast<std::ptrdiff_t>(row) * stride;
          hash.addData(QByteArrayView(reinterpret_cast<const char*>(row_bits),
                                     static_cast<qsizetype>(shape.row_bytes)));
        }
      }
      mapped_planes_hashed = true;
    }
    mapped_frame.unmap();
  }
  if (mapped_planes_hashed) {
    return hash.result();
  }

  // Some platform decoders expose formats that cannot be safely described by
  // plane geometry here. Hash Qt's native image pixels and metadata without
  // reducing bit depth or trusting a guessed playhead position.
  const auto image = source.toImage();
  if (image.isNull()) {
    return {};
  }
  hash.reset();
  const auto image_depth = image.depth();
  const auto image_row_bits = static_cast<qint64>(image.width()) * image_depth;
  const auto row_bytes = (image_row_bits + 7) / 8;
  if (row_bytes <= 0 || row_bytes > std::numeric_limits<int>::max()) {
    return {};
  }
  hash.addData(QByteArrayLiteral("image:") + QByteArray::number(image.width()) +
               'x' + QByteArray::number(image.height()) + ':' +
               QByteArray::number(static_cast<int>(image.format())) + ':' +
               QByteArray::number(image_depth));
  const auto color_table = image.colorTable();
  hash.addData(QByteArray::number(color_table.size()));
  for (const auto color : color_table) {
    hash.addData(QByteArray::number(static_cast<quint32>(color), 16));
    hash.addData(QByteArrayLiteral(","));
  }
  hash.addData(image.colorSpace().iccProfile());
  for (int row = 0; row < image.height(); ++row) {
    if (image_depth % 8 == 0) {
      hash.addData(QByteArrayView(reinterpret_cast<const char*>(image.constScanLine(row)),
                                 static_cast<qsizetype>(row_bytes)));
      continue;
    }
    // Pack sub-byte indexed formats from valid pixels only so undefined bits
    // at the end of a padded row cannot change the identity.
    QByteArray indices(image.width(), '\0');
    for (int column = 0; column < image.width(); ++column) {
      indices[column] = static_cast<char>(image.pixelIndex(column, row));
    }
    hash.addData(indices);
  }
  return hash.result();
}

QString processed_video_frame_cache_key_ineligibility_reason(
    const QString& source_identity,
    const QVideoFrame& frame,
    double source_seconds,
    const QSize& target_size,
    const QByteArray& decoded_content_identity) {
  if (source_identity.isEmpty()) {
    return QStringLiteral("source identity unavailable");
  }
  if (!frame.isValid()) {
    return QStringLiteral("decoded frame invalid");
  }
  if (frame.startTime() < 0 && decoded_content_identity.isEmpty()) {
    return QStringLiteral("decoder timestamp and decoded content identity unavailable");
  }
  if (!std::isfinite(source_seconds)) {
    return QStringLiteral("source time is not finite");
  }
  if (!target_size.isValid() || target_size.isEmpty()) {
    return QStringLiteral("preview target size unavailable");
  }
  const auto source_size = frame.surfaceFormat().frameSize();
  if (!source_size.isValid() || source_size.isEmpty()) {
    return QStringLiteral("decoded frame dimensions unavailable");
  }
  return {};
}

QString processed_video_frame_cache_key(
    const QString& source_identity,
    const QVideoFrame& frame,
    const VideoFrameColorSettings& color_settings,
    const QVariantMap& clip,
    double source_seconds,
    const QSize& target_size,
    const QByteArray& decoded_content_identity) {
  if (!processed_video_frame_cache_key_ineligibility_reason(
           source_identity, frame, source_seconds, target_size,
           decoded_content_identity)
           .isEmpty()) {
    return {};
  }

  const auto format = frame.surfaceFormat();
  const auto source_size = format.frameSize();
  const auto fields = QStringList{
      source_identity,
      frame.startTime() >= 0
          ? QStringLiteral("pts:%1:%2")
                .arg(frame.startTime())
                .arg(frame.endTime())
          : QStringLiteral("content:%1")
                .arg(QString::fromLatin1(decoded_content_identity.toHex())),
      QString::number(source_seconds, 'g', 17),
      QStringLiteral("%1x%2").arg(target_size.width()).arg(target_size.height()),
      QStringLiteral("%1x%2").arg(source_size.width()).arg(source_size.height()),
      QString::number(static_cast<int>(format.pixelFormat())),
      QString::number(static_cast<int>(format.colorSpace())),
      QString::number(static_cast<int>(format.colorTransfer())),
      QString::number(static_cast<int>(format.colorRange())),
      QString::number(static_cast<int>(video_surface_rotation(format))),
      QString::number(static_cast<int>(frame.rotation())),
      QString::number(format.isMirrored()),
      QString::number(frame.mirrored()),
      video_frame_color_settings_key(color_settings),
      video_foreground_cutout_settings_key(clip),
      QString::number(clip.value(QStringLiteral("sourceCropLeft"), 0.0).toDouble(), 'g', 17),
      QString::number(clip.value(QStringLiteral("sourceCropTop"), 0.0).toDouble(), 'g', 17),
      QString::number(clip.value(QStringLiteral("sourceCropRight"), 1.0).toDouble(), 'g', 17),
      QString::number(clip.value(QStringLiteral("sourceCropBottom"), 1.0).toDouble(), 'g', 17),
  };
  return QString::fromLatin1(QCryptographicHash::hash(
      fields.join(QLatin1Char('\n')).toUtf8(), QCryptographicHash::Sha256).toHex());
}

struct CompiledVideoFrameTransforms final {
  std::optional<photo_editor::pipeline::CompiledVideoInputTransform> input;
  std::optional<photo_editor::pipeline::CompiledVideoRec709OutputTransform> output;
};

const CompiledVideoFrameTransforms& compiled_video_frame_transforms(
    photo_editor::pipeline::VideoInputTransformProfile profile) {
  using photo_editor::pipeline::VideoInputTransformProfile;
  static const std::array<CompiledVideoFrameTransforms, 3> transforms = [] {
    const auto working_space =
        photo_editor::pipeline::make_linear_rec709_color_space();
    const auto output =
        photo_editor::pipeline::compile_video_rec709_output_transform(working_space);
    return std::array<CompiledVideoFrameTransforms, 3>{
        CompiledVideoFrameTransforms{
            photo_editor::pipeline::compile_video_input_transform(
                VideoInputTransformProfile::SonySLog2SGamut, working_space),
            output},
        CompiledVideoFrameTransforms{
            photo_editor::pipeline::compile_video_input_transform(
                VideoInputTransformProfile::SonySLog3SGamut3, working_space),
            output},
        CompiledVideoFrameTransforms{
            photo_editor::pipeline::compile_video_input_transform(
                VideoInputTransformProfile::SonySLog3SGamut3Cine, working_space),
            output},
    };
  }();
  if (profile == VideoInputTransformProfile::SonySLog2SGamut) {
    return transforms[0];
  }
  return profile == VideoInputTransformProfile::SonySLog3SGamut3
             ? transforms[1]
             : transforms[2];
}

const photo_editor::pipeline::VideoRec709LutBuildResult&
compiled_video_rec709_lut(
    photo_editor::pipeline::VideoInputTransformProfile profile) {
  return photo_editor::pipeline::cached_sony_video_input_rec709_lut(profile);
}

int rotation_degrees(QtVideo::Rotation rotation) {
  switch (rotation) {
    case QtVideo::Rotation::Clockwise90:
      return 90;
    case QtVideo::Rotation::Clockwise180:
      return 180;
    case QtVideo::Rotation::Clockwise270:
      return 270;
    case QtVideo::Rotation::None:
    default:
      return 0;
  }
}

QImage flip_video_frame_horizontally(QImage image) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
  return image.flipped(Qt::Horizontal);
#else
  return image.mirrored(true, false);
#endif
}

QImage apply_frame_orientation(
    QImage image,
    QtVideo::Rotation surface_rotation,
    bool surface_mirrored,
    QtVideo::Rotation frame_rotation,
    bool frame_mirrored) {
  const auto rotate = [](QImage source, QtVideo::Rotation rotation) {
    const auto degrees = rotation_degrees(rotation);
    return degrees == 0
               ? source
               : source.transformed(QTransform().rotate(degrees), Qt::FastTransformation);
  };
  image = rotate(std::move(image), surface_rotation);
  if (surface_mirrored) {
    image = flip_video_frame_horizontally(std::move(image));
  }
  image = rotate(std::move(image), frame_rotation);
  if (frame_mirrored) {
    image = flip_video_frame_horizontally(std::move(image));
  }
  return image;
}

struct TimelineColorAdjustmentProgram final {
  double brightness = 0.0;
  double contrast = 1.0;
  double saturation = 1.0;
};

TimelineColorAdjustmentProgram compile_timeline_color_adjustments(
    const TimelinePlaybackEffectParameters& effects) {
  return {
      .brightness = std::clamp(
          effects.exposure * 0.40 + effects.fade * 0.08, -1.0, 1.0),
      .contrast = std::clamp(
          1.0 + effects.contrast * 0.80 - effects.fade * 0.35 +
              effects.punch * 0.35,
          0.0,
          2.0),
      .saturation = std::clamp(
          1.0 + effects.saturation - effects.black_and_white +
              effects.punch * 0.40,
          0.0,
          3.0),
  };
}

TimelineColorAdjustmentProgram compile_spatial_color_effect(
    const TimelinePlaybackEffectParameters::SpatialColorEffect& effect) {
  const auto amount = effect.amount;
  if (effect.name == QStringLiteral("Exposure")) {
    return {.brightness = std::clamp(amount * 0.40, -1.0, 1.0)};
  }
  if (effect.name == QStringLiteral("Contrast")) {
    return {.contrast = std::clamp(1.0 + amount * 0.80, 0.0, 2.0)};
  }
  if (effect.name == QStringLiteral("Saturation")) {
    return {.saturation = std::clamp(1.0 + amount, 0.0, 3.0)};
  }
  if (effect.name == QStringLiteral("Black & White")) {
    return {.saturation = std::clamp(1.0 - amount, 0.0, 3.0)};
  }
  if (effect.name == QStringLiteral("Fade")) {
    return {
        .brightness = std::clamp(amount * 0.08, -1.0, 1.0),
        .contrast = std::clamp(1.0 - amount * 0.35, 0.0, 2.0),
    };
  }
  if (effect.name == QStringLiteral("Punch")) {
    return {
        .contrast = std::clamp(1.0 + amount * 0.35, 0.0, 2.0),
        .saturation = std::clamp(1.0 + amount * 0.40, 0.0, 3.0),
    };
  }
  return {};
}

double timeline_effect_mask_amount(
    const TimelinePlaybackEffectParameters::SpatialColorEffect& effect,
    int x,
    int y,
    int width,
    int height) {
  const auto normalized_x = static_cast<double>(x) /
      static_cast<double>(qMax(1, width - 1));
  const auto normalized_y = static_cast<double>(y) /
      static_cast<double>(qMax(1, height - 1));
  double signed_distance = 0.0;
  if (effect.mask_shape == QStringLiteral("rectangle")) {
    const auto dx = (qAbs(normalized_x - effect.mask_center_x) -
                     effect.mask_width * 0.5) * width;
    const auto dy = (qAbs(normalized_y - effect.mask_center_y) -
                     effect.mask_height * 0.5) * height;
    const auto outside_x = qMax(0.0, dx);
    const auto outside_y = qMax(0.0, dy);
    signed_distance = outside_x > 0.0 || outside_y > 0.0
        ? std::hypot(outside_x, outside_y)
        : qMax(dx, dy);
  } else {
    const auto radius_x = qMax(1.0, effect.mask_width * 0.5 * width);
    const auto radius_y = qMax(1.0, effect.mask_height * 0.5 * height);
    const auto normalized_radius = std::hypot(
        (normalized_x - effect.mask_center_x) * width / radius_x,
        (normalized_y - effect.mask_center_y) * height / radius_y);
    signed_distance = (normalized_radius - 1.0) * qMin(radius_x, radius_y);
  }
  const auto feather = effect.mask_feather * qMin(width, height);
  auto amount = feather <= 1.0e-6
      ? (signed_distance <= 0.0 ? 1.0 : 0.0)
      : std::clamp(0.5 - signed_distance / feather, 0.0, 1.0);
  return effect.mask_inverted ? 1.0 - amount : amount;
}

std::array<double, 3> apply_timeline_color_adjustments(
    double red,
    double green,
    double blue,
    const TimelineColorAdjustmentProgram& program) {
  constexpr double kKr = 0.2126;
  constexpr double kKg = 0.7152;
  constexpr double kKb = 0.0722;
  constexpr double kCbScale = 2.0 * (1.0 - kKb);
  constexpr double kCrScale = 2.0 * (1.0 - kKr);
  const auto luma = kKr * red + kKg * green + kKb * blue;
  auto cb = (blue - luma) / kCbScale + 0.5;
  auto cr = (red - luma) / kCrScale + 0.5;
  const auto adjusted_luma = std::clamp(
      program.contrast * (luma - 0.5) + 0.5 + program.brightness,
      0.0,
      1.0);
  cb = std::clamp(program.saturation * (cb - 0.5) + 0.5, 0.0, 1.0);
  cr = std::clamp(program.saturation * (cr - 0.5) + 0.5, 0.0, 1.0);
  const auto output_red = adjusted_luma + kCrScale * (cr - 0.5);
  const auto output_blue = adjusted_luma + kCbScale * (cb - 0.5);
  const auto output_green =
      (adjusted_luma - kKr * output_red - kKb * output_blue) / kKg;
  return {output_red, output_green, output_blue};
}

void apply_timeline_color_effects(
    QImage* image,
    const TimelinePlaybackEffectParameters& effects,
    photo_editor::pipeline::CancellationToken cancellation) {
  const auto has_aggregate_adjustments =
      effects.exposure != 0.0 || effects.contrast != 0.0 ||
      effects.saturation != 0.0 || effects.black_and_white != 0.0 ||
      effects.fade != 0.0 || effects.punch != 0.0;
  if (image == nullptr || image->isNull() ||
      (!has_aggregate_adjustments && effects.spatial_color_effects.isEmpty())) {
    return;
  }
  const auto program = compile_timeline_color_adjustments(effects);
  const auto encode_channel = [](double value) {
    return encode_rgba64_channel(value);
  };

  for (int y = 0; y < image->height(); ++y) {
    if (cancellation.cancelled()) {
      return;
    }
    auto* row = reinterpret_cast<QRgba64*>(image->scanLine(y));
    for (int x = 0; x < image->width(); ++x) {
      auto red = decode_rgba64_channel(row[x].red());
      auto green = decode_rgba64_channel(row[x].green());
      auto blue = decode_rgba64_channel(row[x].blue());
      if (has_aggregate_adjustments) {
        const auto output = apply_timeline_color_adjustments(
            red, green, blue, program);
        red = output[0];
        green = output[1];
        blue = output[2];
      }
      for (const auto& effect : effects.spatial_color_effects) {
        const auto effect_output = apply_timeline_color_adjustments(
            red, green, blue, compile_spatial_color_effect(effect));
        const auto mask_amount = timeline_effect_mask_amount(
            effect, x, y, image->width(), image->height());
        red += (effect_output[0] - red) * mask_amount;
        green += (effect_output[1] - green) * mask_amount;
        blue += (effect_output[2] - blue) * mask_amount;
      }
      row[x].setRed(encode_channel(red));
      row[x].setGreen(encode_channel(green));
      row[x].setBlue(encode_channel(blue));
    }
  }
}

QString apply_timeline_blur(
    QImage* image,
    const TimelinePlaybackEffectParameters& effects,
    int sequence_width,
    int sequence_height,
    photo_editor::pipeline::CancellationToken cancellation) {
  using namespace photo_editor::pipeline;
  if (image == nullptr || image->isNull() || effects.blur <= 1e-6) {
    return {};
  }
  const auto width = image->width();
  const auto height = image->height();
  const auto pixel_count = static_cast<std::size_t>(width) *
                           static_cast<std::size_t>(height);
  if (width <= 0 || height <= 0 ||
      pixel_count / static_cast<std::size_t>(width) !=
          static_cast<std::size_t>(height)) {
    return QStringLiteral("Timeline blur received invalid frame dimensions.");
  }

  const auto masked = effects.blur_mask_enabled;
  const auto original_image = masked ? image->copy() : QImage{};

  try {
    std::vector<ColorPixel> pixels(pixel_count);
    for (int y = 0; y < height; ++y) {
      if (cancellation.cancelled()) {
        return QStringLiteral("Timeline blur was cancelled.");
      }
      const auto* row = reinterpret_cast<const QRgba64*>(
          image->constScanLine(y));
      for (int x = 0; x < width; ++x) {
        pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
               static_cast<std::size_t>(x)] = ColorPixel{
            .red = static_cast<float>(decode_rgba64_channel(row[x].red())),
            .green = static_cast<float>(decode_rgba64_channel(row[x].green())),
            .blue = static_cast<float>(decode_rgba64_channel(row[x].blue())),
            .alpha = static_cast<float>(decode_rgba64_channel(row[x].alpha()))};
      }
    }

    PhotoEffectInstance blur;
    blur.id = "timeline-blur";
    blur.type_id = "gaussian_blur";
    blur.gaussian.sigma = std::clamp(effects.blur * 18.0, 0.0, 18.0);
    blur.gaussian.edge_mode = PhotoEffectEdgeMode::Reflect;
    const std::array<PhotoEffectInstance, 1> blur_stack{blur};
    PhotoEffectRenderOptions options;
    options.scale_x = sequence_width > 0
                          ? static_cast<double>(width) / sequence_width
                          : 1.0;
    options.scale_y = sequence_height > 0
                          ? static_cast<double>(height) / sequence_height
                          : 1.0;
    options.max_scratch_bytes = 64U * 1024U * 1024U;
    options.cancellation = cancellation;
    const auto result = apply_photo_effect_stack_tiled(
        &pixels,
        width,
        height,
        blur_stack,
        options,
        std::span<const PhotoEffectMaskBinding>{},
        256);
    if (!result.ok()) {
      return QStringLiteral("Timeline blur could not be rendered: %1")
          .arg(QString::fromStdString(result.error));
    }

    const auto encode_channel = [](float value) {
      return encode_rgba64_channel(static_cast<double>(value));
    };
    for (int y = 0; y < height; ++y) {
      if (cancellation.cancelled()) {
        return QStringLiteral("Timeline blur was cancelled.");
      }
      auto* row = reinterpret_cast<QRgba64*>(image->scanLine(y));
      for (int x = 0; x < width; ++x) {
        const auto& pixel = pixels[
            static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
            static_cast<std::size_t>(x)];
        auto red = static_cast<double>(pixel.red);
        auto green = static_cast<double>(pixel.green);
        auto blue = static_cast<double>(pixel.blue);
        auto alpha = static_cast<double>(pixel.alpha);
        if (masked) {
          const auto normalized_x = static_cast<double>(x) /
              static_cast<double>(qMax(1, width - 1));
          const auto normalized_y = static_cast<double>(y) /
              static_cast<double>(qMax(1, height - 1));
          double signed_distance = 0.0;
          if (effects.blur_mask_shape == QStringLiteral("rectangle")) {
            const auto dx = (qAbs(normalized_x - effects.blur_mask_center_x) -
                             effects.blur_mask_width * 0.5) * width;
            const auto dy = (qAbs(normalized_y - effects.blur_mask_center_y) -
                             effects.blur_mask_height * 0.5) * height;
            const auto outside_x = qMax(0.0, dx);
            const auto outside_y = qMax(0.0, dy);
            signed_distance = outside_x > 0.0 || outside_y > 0.0
                ? std::hypot(outside_x, outside_y)
                : qMax(dx, dy);
          } else {
            const auto radius_x = qMax(1.0, effects.blur_mask_width * 0.5 * width);
            const auto radius_y = qMax(1.0, effects.blur_mask_height * 0.5 * height);
            const auto normalized_radius = std::hypot(
                (normalized_x - effects.blur_mask_center_x) * width / radius_x,
                (normalized_y - effects.blur_mask_center_y) * height / radius_y);
            signed_distance = (normalized_radius - 1.0) * qMin(radius_x, radius_y);
          }
          const auto feather =
              effects.blur_mask_feather * qMin(width, height);
          auto mask_amount = feather <= 1.0e-6
              ? (signed_distance <= 0.0 ? 1.0 : 0.0)
              : std::clamp(0.5 - signed_distance / feather, 0.0, 1.0);
          if (effects.blur_mask_inverted) {
            mask_amount = 1.0 - mask_amount;
          }
          const auto* original_row = reinterpret_cast<const QRgba64*>(
              original_image.constScanLine(y));
          const auto& original = original_row[x];
          const auto blend = [mask_amount](double source, double effected) {
            return source * (1.0 - mask_amount) + effected * mask_amount;
          };
          red = blend(decode_rgba64_channel(original.red()), red);
          green = blend(decode_rgba64_channel(original.green()), green);
          blue = blend(decode_rgba64_channel(original.blue()), blue);
          alpha = blend(decode_rgba64_channel(original.alpha()), alpha);
        }
        row[x] = QRgba64::fromRgba64(
            encode_channel(static_cast<float>(red)),
            encode_channel(static_cast<float>(green)),
            encode_channel(static_cast<float>(blue)),
            encode_channel(static_cast<float>(alpha)));
      }
    }
  } catch (const std::bad_alloc&) {
    return QStringLiteral("Timeline blur ran out of preview memory.");
  }
  return {};
}

enum class YuvFrameLayout {
  Planar420,
  Planar422,
  SemiPlanar420Uv,
  SemiPlanar420Vu,
  Packed422Uyvy,
  Packed422Yuyv,
};

QImage convert_yuv_video_frame_to_image(
    const QVideoFrame& source,
    bool camera_input_transform_selected,
    const photo_editor::pipeline::ResolvedVideoSourceSignalMetadata& signal,
    const QSize& maximum_output_size,
    bool* metadata_aware_conversion_used,
    QString* conversion_warning) {
  if (metadata_aware_conversion_used != nullptr) {
    *metadata_aware_conversion_used = false;
  }
  if (conversion_warning != nullptr) {
    conversion_warning->clear();
  }

  const auto format = source.surfaceFormat();
  const auto pixel_format = format.pixelFormat();
  switch (pixel_format) {
    case QVideoFrameFormat::Format_AYUV:
    case QVideoFrameFormat::Format_AYUV_Premultiplied:
    case QVideoFrameFormat::Format_YUV420P:
    case QVideoFrameFormat::Format_YUV420P10:
    case QVideoFrameFormat::Format_YUV422P:
    case QVideoFrameFormat::Format_YV12:
    case QVideoFrameFormat::Format_UYVY:
    case QVideoFrameFormat::Format_YUYV:
    case QVideoFrameFormat::Format_NV12:
    case QVideoFrameFormat::Format_NV21:
    case QVideoFrameFormat::Format_IMC1:
    case QVideoFrameFormat::Format_IMC2:
    case QVideoFrameFormat::Format_IMC3:
    case QVideoFrameFormat::Format_IMC4:
    case QVideoFrameFormat::Format_P010:
    case QVideoFrameFormat::Format_P016:
      break;
    default:
      return source.toImage();
  }

  const auto is_hdr_transfer =
      format.colorTransfer() == QVideoFrameFormat::ColorTransfer_ST2084 ||
      format.colorTransfer() == QVideoFrameFormat::ColorTransfer_STD_B67;
  auto layout = YuvFrameLayout::Planar420;
  auto bit_depth = 8;
  auto bytes_per_sample = 1;
  auto high_bit_aligned = false;
  switch (pixel_format) {
    case QVideoFrameFormat::Format_YUV420P:
    case QVideoFrameFormat::Format_YV12:
      layout = YuvFrameLayout::Planar420;
      break;
    case QVideoFrameFormat::Format_YUV422P:
      layout = YuvFrameLayout::Planar422;
      break;
    case QVideoFrameFormat::Format_NV12:
      layout = YuvFrameLayout::SemiPlanar420Uv;
      break;
    case QVideoFrameFormat::Format_NV21:
      layout = YuvFrameLayout::SemiPlanar420Vu;
      break;
    case QVideoFrameFormat::Format_UYVY:
      layout = YuvFrameLayout::Packed422Uyvy;
      break;
    case QVideoFrameFormat::Format_YUYV:
      layout = YuvFrameLayout::Packed422Yuyv;
      break;
    case QVideoFrameFormat::Format_P010:
      layout = YuvFrameLayout::SemiPlanar420Uv;
      bit_depth = 10;
      bytes_per_sample = 2;
      high_bit_aligned = true;
      break;
    case QVideoFrameFormat::Format_P016:
      layout = YuvFrameLayout::SemiPlanar420Uv;
      bit_depth = 16;
      bytes_per_sample = 2;
      break;
    case QVideoFrameFormat::Format_YUV420P10:
      layout = YuvFrameLayout::Planar420;
      bit_depth = 10;
      bytes_per_sample = 2;
      break;
    default:
      if (conversion_warning != nullptr) {
        *conversion_warning = QStringLiteral(
            "This video pixel layout uses Qt conversion; decoded matrix and range handling can vary by backend.");
      }
      return source.toImage();
  }

  const auto width = source.width();
  const auto height = source.height();
  if (width <= 0 || height <= 0) {
    return {};
  }
  if (!signal.metadata_aware_yuv_supported) {
    if (conversion_warning != nullptr) {
      *conversion_warning = QStringLiteral(
          "This YUV matrix uses Qt conversion because the shared CPU sampler does not support its coefficients.");
    }
    return source.toImage();
  }
  auto output_size = QSize(width, height);
  if (maximum_output_size.isValid() && !maximum_output_size.isEmpty() &&
      (width > maximum_output_size.width() ||
       height > maximum_output_size.height())) {
    output_size = output_size.scaled(
        maximum_output_size, Qt::KeepAspectRatio);
  }
  const auto output_width = output_size.width();
  const auto output_height = output_size.height();
  const auto chroma_width = (width + 1) / 2;
  const auto chroma_height = (height + 1) / 2;
  const auto sample_bytes = bytes_per_sample;

  QVideoFrame mapped_frame = source;
  if (!mapped_frame.map(QVideoFrame::ReadOnly)) {
    if (conversion_warning != nullptr) {
      *conversion_warning = QStringLiteral(
          "The frame could not be mapped for metadata-aware CPU color conversion; Qt conversion was used.");
    }
    return source.toImage();
  }

  const auto plane_count = mapped_frame.planeCount();
  const auto required_planes =
      layout == YuvFrameLayout::Planar420 ||
              layout == YuvFrameLayout::Planar422
          ? 3
          : layout == YuvFrameLayout::Packed422Uyvy ||
                    layout == YuvFrameLayout::Packed422Yuyv
                ? 1
                : 2;
  auto valid_planes = plane_count >= required_planes;
  for (int plane = 0; valid_planes && plane < required_planes; ++plane) {
    const auto* bits = mapped_frame.bits(plane);
    const auto stride = mapped_frame.bytesPerLine(plane);
    const auto is_packed = layout == YuvFrameLayout::Packed422Uyvy ||
                           layout == YuvFrameLayout::Packed422Yuyv;
    const auto is_semi_planar =
        layout == YuvFrameLayout::SemiPlanar420Uv ||
        layout == YuvFrameLayout::SemiPlanar420Vu;
    const auto chroma_plane = plane > 0 && !is_packed;
    const auto row_bytes = is_packed
                               ? chroma_width * 4
                           : chroma_plane && is_semi_planar
                               ? chroma_width * 2 * sample_bytes
                           : chroma_plane
                               ? chroma_width * sample_bytes
                               : width * sample_bytes;
    const auto row_count =
        chroma_plane && layout != YuvFrameLayout::Planar422
            ? chroma_height
            : height;
    valid_planes = bits != nullptr && stride >= row_bytes &&
                   mapped_frame.mappedBytes(plane) >= stride * row_count;
  }
  if (!valid_planes) {
    mapped_frame.unmap();
    if (conversion_warning != nullptr) {
      *conversion_warning = QStringLiteral(
          "The decoded YUV planes do not match their reported strides; Qt conversion was used.");
    }
    return source.toImage();
  }

  const auto high_precision_output =
      bit_depth > 8 || camera_input_transform_selected || is_hdr_transfer;
  auto image = QImage(
      output_width,
      output_height,
      high_precision_output ? QImage::Format_RGBA64
                            : QImage::Format_RGBA8888);
  if (image.isNull()) {
    mapped_frame.unmap();
    if (conversion_warning != nullptr) {
      *conversion_warning = QStringLiteral(
          "Preview memory was unavailable for metadata-aware YUV conversion.");
    }
    return {};
  }

  // The mapping is stable until unmap(). Resolve Qt's plane descriptors once,
  // rather than querying the frame for every luma/chroma sample of every pixel.
  std::array<const uchar*, 3> plane_data{};
  std::array<int, 3> plane_strides{};
  for (int plane = 0; plane < required_planes; ++plane) {
    plane_data[static_cast<std::size_t>(plane)] = mapped_frame.bits(plane);
    plane_strides[static_cast<std::size_t>(plane)] = mapped_frame.bytesPerLine(plane);
  }
  auto* output_bits = image.bits();
  const auto output_stride = image.bytesPerLine();

  const auto load_sample = [&](int plane, int x, int y) {
    const auto* row = plane_data[static_cast<std::size_t>(plane)] +
                      static_cast<std::ptrdiff_t>(y) *
                          plane_strides[static_cast<std::size_t>(plane)];
    if (sample_bytes == 1) {
      return static_cast<int>(row[x]);
    }
    std::uint16_t raw_sample = 0U;
    std::memcpy(&raw_sample, row + static_cast<std::ptrdiff_t>(x) * 2,
                sizeof(raw_sample));
    if (high_bit_aligned) {
      return static_cast<int>(raw_sample >> (16 - bit_depth));
    }
    const auto sample_mask = static_cast<std::uint16_t>(
        (std::uint32_t{1} << bit_depth) - 1U);
    return static_cast<int>(raw_sample & sample_mask);
  };
  const auto yuv_at = [&](int x, int y) {
    auto y_sample = 0;
    auto u_sample = 0;
    auto v_sample = 0;
    switch (layout) {
      case YuvFrameLayout::Planar420:
        y_sample = load_sample(0, x, y);
        if (pixel_format == QVideoFrameFormat::Format_YV12) {
          v_sample = load_sample(1, x / 2, y / 2);
          u_sample = load_sample(2, x / 2, y / 2);
        } else {
          u_sample = load_sample(1, x / 2, y / 2);
          v_sample = load_sample(2, x / 2, y / 2);
        }
        break;
      case YuvFrameLayout::Planar422:
        y_sample = load_sample(0, x, y);
        u_sample = load_sample(1, x / 2, y);
        v_sample = load_sample(2, x / 2, y);
        break;
      case YuvFrameLayout::SemiPlanar420Uv:
      case YuvFrameLayout::SemiPlanar420Vu: {
        y_sample = load_sample(0, x, y);
        const auto* chroma_row = plane_data[1] +
                                 static_cast<std::ptrdiff_t>(y / 2) * plane_strides[1];
        const auto offset = static_cast<std::ptrdiff_t>(x / 2) *
                            2 * sample_bytes;
        const auto first = sample_bytes == 1
                               ? static_cast<int>(chroma_row[offset])
                               : static_cast<int>([&]() {
                                   std::uint16_t sample = 0U;
                                   std::memcpy(&sample, chroma_row + offset, 2);
                                   if (high_bit_aligned) {
                                     sample >>= (16 - bit_depth);
                                   } else {
                                     sample &= static_cast<std::uint16_t>(
                                         (std::uint32_t{1} << bit_depth) - 1U);
                                   }
                                   return sample;
                                 }());
        const auto second = sample_bytes == 1
                                ? static_cast<int>(chroma_row[offset + sample_bytes])
                                : static_cast<int>([&]() {
                                    std::uint16_t sample = 0U;
                                    std::memcpy(&sample,
                                                chroma_row + offset + sample_bytes,
                                                2);
                                    if (high_bit_aligned) {
                                      sample >>= (16 - bit_depth);
                                    } else {
                                      sample &= static_cast<std::uint16_t>(
                                          (std::uint32_t{1} << bit_depth) - 1U);
                                    }
                                    return sample;
                                  }());
        if (layout == YuvFrameLayout::SemiPlanar420Uv) {
          u_sample = first;
          v_sample = second;
        } else {
          v_sample = first;
          u_sample = second;
        }
        break;
      }
      case YuvFrameLayout::Packed422Uyvy:
      case YuvFrameLayout::Packed422Yuyv: {
        const auto* row = plane_data[0] +
                          static_cast<std::ptrdiff_t>(y) * plane_strides[0];
        const auto offset = static_cast<std::ptrdiff_t>(x / 2) * 4;
        if (layout == YuvFrameLayout::Packed422Uyvy) {
          u_sample = row[offset];
          y_sample = row[offset + ((x % 2) == 0 ? 1 : 3)];
          v_sample = row[offset + 2];
        } else {
          y_sample = row[offset + ((x % 2) == 0 ? 0 : 2)];
          u_sample = row[offset + 1];
          v_sample = row[offset + 3];
        }
        break;
      }
    }
    return std::array<int, 3>{y_sample, u_sample, v_sample};
  };

  const auto kr = signal.ycbcr_kr;
  const auto kb = signal.ycbcr_kb;
  const auto full_range = signal.range == "full";
  const auto maximum_code = static_cast<double>(
      (std::uint32_t{1} << bit_depth) - 1U);
  const auto bit_depth_scale =
      bit_depth >= 8 ? static_cast<double>(std::uint32_t{1} << (bit_depth - 8))
                     : 1.0;
  const auto limited_camera_code_black =
      16.0 * bit_depth_scale / maximum_code;
  const auto limited_camera_code_span =
      219.0 * bit_depth_scale / maximum_code;
  const auto to_camera_code_value = [camera_input_transform_selected,
                                     restore_limited_code_values =
                                         signal.restore_limited_camera_code_values,
                                     limited_camera_code_black,
                                     limited_camera_code_span](double channel) {
    if (!camera_input_transform_selected || !restore_limited_code_values) {
      return channel;
    }
    return limited_camera_code_black + channel * limited_camera_code_span;
  };
  const auto encode = [](double value) {
    return static_cast<uchar>(std::lround(std::clamp(value, 0.0, 1.0) * 255.0));
  };
  const auto encode_16 = [](double value) {
    return encode_rgba64_channel(value);
  };
  // Select the monitor-resolution source samples before allocating RGBA, so
  // playback does not expand a large YUV frame only to discard those pixels.
  for (int y = 0; y < output_height; ++y) {
    auto* output_row = output_bits + static_cast<std::ptrdiff_t>(y) * output_stride;
    auto* output_16 = high_precision_output
                          ? reinterpret_cast<QRgba64*>(output_row)
                          : nullptr;
    auto* output_8 = high_precision_output ? nullptr : output_row;
    const auto source_y = std::min(
        height - 1,
        static_cast<int>((static_cast<std::int64_t>(y) * height) /
                         output_height));
    for (int x = 0; x < output_width; ++x) {
      const auto source_x = std::min(
          width - 1,
          static_cast<int>((static_cast<std::int64_t>(x) * width) /
                           output_width));
      const auto samples = yuv_at(source_x, source_y);
      const auto luma = full_range
                            ? static_cast<double>(samples[0]) / maximum_code
                            : (static_cast<double>(samples[0]) - 16.0 * bit_depth_scale) /
                                  (219.0 * bit_depth_scale);
      const auto cb = full_range
                          ? static_cast<double>(samples[1]) / maximum_code - 0.5
                          : (static_cast<double>(samples[1]) - 128.0 * bit_depth_scale) /
                                (224.0 * bit_depth_scale);
      const auto cr = full_range
                          ? static_cast<double>(samples[2]) / maximum_code - 0.5
                          : (static_cast<double>(samples[2]) - 128.0 * bit_depth_scale) /
                                (224.0 * bit_depth_scale);
      const auto red = luma + 2.0 * (1.0 - kr) * cr;
      const auto blue = luma + 2.0 * (1.0 - kb) * cb;
      const auto green =
          (luma - kr * red - kb * blue) / (1.0 - kr - kb);
      const auto output_red = to_camera_code_value(red);
      const auto output_green = to_camera_code_value(green);
      const auto output_blue = to_camera_code_value(blue);
      if (high_precision_output) {
        output_16[x] = QRgba64::fromRgba64(
            encode_16(output_red), encode_16(output_green),
            encode_16(output_blue), 65535U);
      } else {
        const auto offset = static_cast<std::ptrdiff_t>(x) * 4;
        output_8[offset] = encode(output_red);
        output_8[offset + 1] = encode(output_green);
        output_8[offset + 2] = encode(output_blue);
        output_8[offset + 3] = 255U;
      }
    }
  }
  mapped_frame.unmap();

  if (metadata_aware_conversion_used != nullptr) {
    *metadata_aware_conversion_used = true;
  }
  image.setColorSpace(video_frame_rgb_color_space(format));
  return image;
}

QImage convert_video_preview_format(
    const QImage& image,
    QImage::Format format) {
  auto converted = image.convertToFormat(format);
  if (converted.isNull()) {
    return {};
  }
  converted.setColorSpace(image.colorSpace());
  for (const auto& key : image.textKeys()) {
    converted.setText(key, image.text(key));
  }
  return converted;
}

TimelineProcessedVideoFrame process_video_frame(
    QVideoFrame frame,
    VideoFrameColorSettings color_settings,
    QVariantMap clip,
    double source_seconds,
    QSize target_size,
    bool continuous_playback,
    photo_editor::pipeline::CancellationToken cancellation) {
  QElapsedTimer processing_timer;
  processing_timer.start();
  QElapsedTimer stage_timer;
  stage_timer.start();
  auto decode_normalize_ms = 0.0;
  auto grade_compile_ms = 0.0;
  auto creative_lut_prepare_ms = 0.0;
  auto camera_transform_ms = 0.0;
  auto camera_and_base_grade_ms = 0.0;
  auto fused_color_pipeline_ms = 0.0;
  auto grade_apply_ms = 0.0;
  auto timeline_effects_ms = 0.0;
  auto creative_lut_ms = 0.0;
  auto frame_cache_lookup_ms = 0.0;
  // Without decoder timestamps, cache identity requires hashing every source
  // pixel. During continuous playback each new frame is normally a miss, so
  // render it directly. Paused seeks retain exact decoded-content reuse.
  const auto bypass_live_content_cache = continuous_playback && frame.startTime() < 0;
  const auto frame_cache_enabled =
      !qEnvironmentVariableIsSet("PHOTO_EDITOR_TIMELINE_FRAME_CACHE_DISABLE") &&
      !bypass_live_content_cache;
  const auto attach_processing_metrics = [&](QImage* output) {
    if (output == nullptr || output->isNull()) {
      return;
    }
    output->setText(QStringLiteral("timelineVideoMetricDecodeNormalizeMs"),
                    QString::number(decode_normalize_ms, 'f', 3));
    output->setText(QStringLiteral("timelineVideoMetricGradeCompileMs"),
                    QString::number(grade_compile_ms, 'f', 3));
    output->setText(QStringLiteral("timelineVideoMetricCreativeLutPrepareMs"),
                    QString::number(creative_lut_prepare_ms, 'f', 3));
    output->setText(QStringLiteral("timelineVideoMetricCameraTransformMs"),
                    QString::number(camera_transform_ms, 'f', 3));
    output->setText(
        QStringLiteral("timelineVideoMetricCameraAndBaseGradeMs"),
        QString::number(camera_and_base_grade_ms, 'f', 3));
    output->setText(
        QStringLiteral("timelineVideoMetricFusedColorPipelineMs"),
        QString::number(fused_color_pipeline_ms, 'f', 3));
    output->setText(QStringLiteral("timelineVideoMetricGradeApplyMs"),
                    QString::number(grade_apply_ms, 'f', 3));
    output->setText(QStringLiteral("timelineVideoMetricTimelineEffectsMs"),
                    QString::number(timeline_effects_ms, 'f', 3));
    output->setText(QStringLiteral("timelineVideoMetricCreativeLutMs"),
                    QString::number(creative_lut_ms, 'f', 3));
    output->setText(QStringLiteral("timelineVideoMetricFrameCacheLookupMs"),
                    QString::number(frame_cache_lookup_ms, 'f', 3));
    output->setText(QStringLiteral("timelineVideoMetricTotalMs"),
                    QString::number(
                        static_cast<double>(processing_timer.nsecsElapsed()) /
                            1'000'000.0,
                        'f',
                        3));
    output->setText(QStringLiteral("timelineVideoMetricWidth"),
                    QString::number(output->width()));
    output->setText(QStringLiteral("timelineVideoMetricHeight"),
                    QString::number(output->height()));
    output->setText(
        QStringLiteral("timelineVideoMetricSourceKey"),
        QStringLiteral("%1|%2x%3|%4")
            .arg(color_settings.clip_id)
            .arg(output->width())
            .arg(output->height())
            .arg(QString::fromLatin1(QCryptographicHash::hash(
                (video_frame_color_settings_key(color_settings) + QLatin1Char('|') +
                 video_foreground_cutout_settings_key(clip)).toUtf8(),
            QCryptographicHash::Sha256).toHex())));
  };
  using photo_editor::pipeline::VideoInputTransformProfile;
  const auto normalized_key = color_settings.input_transform_key.trimmed().toLower();
  const auto profile = normalized_key == QStringLiteral("slog2-sgamut")
                           ? std::optional{VideoInputTransformProfile::SonySLog2SGamut}
                       : normalized_key == QStringLiteral("slog3-sgamut3")
                           ? std::optional{VideoInputTransformProfile::SonySLog3SGamut3}
                       : normalized_key == QStringLiteral("slog3-sgamut3-cine")
                           ? std::optional{VideoInputTransformProfile::SonySLog3SGamut3Cine}
                           : std::nullopt;
  auto metadata_aware_conversion_used = false;
  auto conversion_warning = QString{};
  if (!target_size.isValid() || target_size.isEmpty()) {
    target_size = QSize(kFallbackPreviewWidth, kFallbackPreviewHeight);
  }
  const auto surface_format = frame.surfaceFormat();
  const auto surface_rotation = video_surface_rotation(surface_format);
  const auto frame_rotation = frame.rotation();
  const auto total_rotation =
      (rotation_degrees(surface_rotation) + rotation_degrees(frame_rotation)) % 360;
  // Native macOS decoders can omit PTS entirely. In that case key the cache by
  // the actual decoded planes (or Qt's normalized image for opaque formats),
  // never by an estimated playhead position alone.
  const auto decoded_content_identity = frame_cache_enabled &&
          frame.startTime() < 0
      ? decoded_video_frame_content_identity(frame)
      : QByteArray{};
  const auto cache_key_unavailable_reason = frame_cache_enabled
      ? processed_video_frame_cache_key_ineligibility_reason(
            color_settings.source_identity, frame, source_seconds, target_size,
            decoded_content_identity)
      : bypass_live_content_cache
          ? QStringLiteral("decoded-content cache bypassed during continuous playback")
          : QStringLiteral("processed frame cache disabled");
  const auto cache_key_eligible =
      frame_cache_enabled && cache_key_unavailable_reason.isEmpty();
  const auto cache_identity_kind = !frame_cache_enabled
      ? QStringLiteral("disabled")
      : frame.startTime() >= 0
      ? QStringLiteral("presentation-timestamp")
      : decoded_content_identity.isEmpty()
          ? QStringLiteral("unavailable")
          : QStringLiteral("decoded-content");
  auto frame_cache_key = QString{};
  if (frame_cache_enabled && cache_key_eligible) {
    frame_cache_key = processed_video_frame_cache_key(
        color_settings.source_identity,
        frame,
        color_settings,
        clip,
        source_seconds,
        target_size,
        decoded_content_identity);
  }
  if (!frame_cache_key.isEmpty()) {
    QElapsedTimer cache_lookup_timer;
    cache_lookup_timer.start();
    auto cached_image = processed_video_frame_cache().find(frame_cache_key);
    frame_cache_lookup_ms =
        static_cast<double>(cache_lookup_timer.nsecsElapsed()) / 1'000'000.0;
    if (!cached_image.isNull()) {
      return TimelineProcessedVideoFrame{
          .image = std::move(cached_image),
          .cache_enabled = frame_cache_enabled,
          .cache_hit = true,
          .cache_key_eligible = cache_key_eligible,
          .cache_lookup_performed = true,
          .frame_timestamp_us = frame.startTime(),
          .cache_identity_kind = cache_identity_kind,
          .cache_lookup_ms = frame_cache_lookup_ms,
          .total_ms = static_cast<double>(processing_timer.nsecsElapsed()) /
                      1'000'000.0,
      };
    }
  }
  auto pre_orientation_target = target_size;
  if (total_rotation == 90 || total_rotation == 270) {
    pre_orientation_target.transpose();
  }
  const auto source_signal = resolved_video_frame_signal(
      surface_format, profile.has_value());
  auto image = convert_yuv_video_frame_to_image(
      frame, profile.has_value(), source_signal, pre_orientation_target,
      &metadata_aware_conversion_used,
      &conversion_warning);
  if (image.isNull() || cancellation.cancelled()) {
    return {};
  }
  const auto source_is_rec709 = source_signal.source_is_rec709;
  const auto source_is_hdr = source_signal.source_is_hdr;
  const auto hdr_to_sdr_applied = source_is_hdr && !profile.has_value()
                                      ? timeline_hdr_sdr::apply_to_rec709_sdr(
                                            &image,
                                            source_signal.transfer == "arib-std-b67",
                                            source_signal.primaries == "bt2020",
                                            rec709_video_signal_color_space())
                                      : false;
  if (source_is_hdr) {
    conversion_warning = profile.has_value()
        ? QStringLiteral(
              "The decoder reports PQ/HLG while a manual Sony S-Log transform is selected; verify that the clip's tags are wrong before overriding them.")
        : hdr_to_sdr_applied
            ? conversion_warning
            : QStringLiteral("The shared PQ/HLG to SDR Rec.709 conversion could not be applied to this frame.");
  }
  const auto color_conversion_status = metadata_aware_conversion_used
      ? profile.has_value()
            ? source_is_hdr
                  ? QStringLiteral("Manual Sony S-Log transform overrides decoder PQ/HLG tags")
                  : QStringLiteral("Decoder matrix/range normalized before Sony S-Log transform")
            : QStringLiteral("Decoder matrix/range normalized to Rec.709")
      : source_is_hdr
            ? hdr_to_sdr_applied
                  ? QStringLiteral("PQ/HLG to SDR Rec.709 using shared Hable mapping")
                  : QStringLiteral("HDR frame conversion unavailable")
        : !conversion_warning.isEmpty()
            ? QStringLiteral("Qt frame conversion fallback")
            : QStringLiteral("Qt frame conversion");
  if (image.width() > pre_orientation_target.width() ||
      image.height() > pre_orientation_target.height()) {
    image = image.scaled(
        pre_orientation_target, Qt::KeepAspectRatio, Qt::FastTransformation);
  }
  image = apply_frame_orientation(
      std::move(image),
      surface_rotation,
      surface_format.isMirrored(),
      frame_rotation,
      frame.mirrored());
  const auto matte_state = evaluate_timeline_foreground_matte(clip);
  if (matte_state.enabled) {
    QString matte_error;
    const auto mask_path = timeline_foreground_matte_frame_path(
        matte_state, source_seconds, &matte_error);
    if (mask_path.isEmpty() ||
        !applyTimelineForegroundMask(&image, mask_path, &matte_error)) {
      image = QImage(target_size, QImage::Format_ARGB32_Premultiplied);
      image.fill(QColor(QStringLiteral("#211c22")));
      image.setText(
          QStringLiteral("timelineVideoColorDecodeWarning"),
          matte_error.isEmpty()
              ? QStringLiteral("Foreground cutout is enabled, but its matte could not be applied.")
              : matte_error);
      attach_processing_metrics(&image);
      const auto total_ms = image.text(QStringLiteral("timelineVideoMetricTotalMs")).toDouble();
      return TimelineProcessedVideoFrame{
          .image = std::move(image),
          .cache_enabled = frame_cache_enabled,
          .cache_hit = false,
          .cache_key_eligible = cache_key_eligible,
          .cache_lookup_performed = !frame_cache_key.isEmpty(),
          .frame_timestamp_us = frame.startTime(),
          .cache_identity_kind = cache_identity_kind,
          .cache_key_unavailable_reason = cache_key_unavailable_reason,
          .cache_lookup_ms = frame_cache_lookup_ms,
          .total_ms = total_ms,
      };
    }
  }
  const TimelineSourceCrop source_crop{
      clip.value(QStringLiteral("sourceCropLeft"), 0.0).toDouble(),
      clip.value(QStringLiteral("sourceCropTop"), 0.0).toDouble(),
      clip.value(QStringLiteral("sourceCropRight"), 1.0).toDouble(),
      clip.value(QStringLiteral("sourceCropBottom"), 1.0).toDouble()};
  if (source_crop.isValid() && !source_crop.isDefault()) {
    image = source_crop.apply(image);
    if (image.isNull()) {
      return {};
    }
  }
  decode_normalize_ms =
      static_cast<double>(stage_timer.nsecsElapsed()) / 1'000'000.0;

  const auto* transforms = profile.has_value()
                               ? &compiled_video_frame_transforms(*profile)
                               : nullptr;
  const auto has_video_transform = transforms != nullptr &&
                                   transforms->input.has_value() &&
                                   transforms->output.has_value();
  stage_timer.restart();
  const auto lut = color_settings.creative_lut_path.isEmpty()
                       ? std::shared_ptr<const CachedCubeLutExecution>{}
                       : cached_cube_lut(color_settings.creative_lut_path);
  creative_lut_prepare_ms =
      static_cast<double>(stage_timer.nsecsElapsed()) / 1'000'000.0;
  const auto grade_requested =
      !color_settings.grade_recipe_json.trimmed().isEmpty();
  const auto has_adjustment_grades =
      !color_settings.adjustment_grades.isEmpty();
  auto grade = TimelineVideoGradeLut{};
  stage_timer.restart();
  if (grade_requested && color_settings.animated_grade_parameters.isEmpty()) {
    QString lut_support_error;
    if (timeline_video_grade_can_use_lut(
            color_settings.grade_recipe_json,
            image.size(),
            &lut_support_error)) {
      grade = compile_timeline_video_grade_lut(
          color_settings.grade_recipe_json, cancellation);
    } else {
      grade.error = std::move(lut_support_error);
    }
  } else if (grade_requested && color_settings.grade_session == nullptr) {
    grade.error = QStringLiteral("Animated shared grade preview session is unavailable.");
  }
  grade_compile_ms =
      static_cast<double>(stage_timer.nsecsElapsed()) / 1'000'000.0;
  if (cancellation.cancelled()) {
    return {};
  }
  const auto grade_lut = grade.ok()
                             ? grade.lut
                             : std::shared_ptr<const photo_editor::pipeline::ColorLut3D>{};
  const auto has_shot_match =
      color_settings.shot_match_red_gain != 1.0 ||
      color_settings.shot_match_green_gain != 1.0 ||
      color_settings.shot_match_blue_gain != 1.0;
  const auto has_timeline_effects =
      timeline_effects_are_active(color_settings.effects);
  const auto has_timeline_color_adjustments =
      timeline_effects_have_color_adjustments(color_settings.effects);
  const auto has_spatial_color_effects =
      !color_settings.effects.spatial_color_effects.isEmpty();
  const auto color_adjustment_program = has_timeline_color_adjustments
      ? compile_timeline_color_adjustments(color_settings.effects)
      : TimelineColorAdjustmentProgram{};
  if (!has_video_transform && !source_is_hdr) {
    if (!image.colorSpace().isValid() &&
        (metadata_aware_conversion_used ||
         is_rgb_video_pixel_format(surface_format.pixelFormat()))) {
      image.setColorSpace(video_frame_rgb_color_space(surface_format));
    }
    if (image.colorSpace().isValid() &&
        image.colorSpace() != rec709_video_signal_color_space()) {
      auto rec709_image = convert_tagged_qimage_color_space(
          image,
          rec709_video_signal_color_space(),
          QImageColorConversionPurpose::DecodeNormalization);
      if (!rec709_image.isNull()) {
        image = std::move(rec709_image);
      } else if (conversion_warning.isEmpty()) {
        conversion_warning = QStringLiteral(
            "The decoded source color space could not be converted to Rec.709 for preview.");
      }
    }
    image.setColorSpace(rec709_video_signal_color_space());
  }
  if (!conversion_warning.isEmpty()) {
    image.setText(QStringLiteral("timelineVideoColorDecodeWarning"),
                  conversion_warning);
  }
  image.setText(QStringLiteral("timelineVideoColorDecodeStatus"),
                color_conversion_status);
  if (!has_video_transform && !grade_requested && !has_adjustment_grades &&
      lut == nullptr &&
      !has_shot_match && !has_timeline_effects) {
    if (source_is_rec709 || metadata_aware_conversion_used) {
      image.setColorSpace(rec709_video_signal_color_space());
    }
    auto output = convert_video_preview_format(image, QImage::Format_RGBA8888);
    attach_processing_metrics(&output);
    if (!output.isNull() && frame_cache_enabled && !frame_cache_key.isEmpty()) {
      static_cast<void>(processed_video_frame_cache().insert(frame_cache_key, output));
    }
    const auto total_ms = output.text(QStringLiteral("timelineVideoMetricTotalMs")).toDouble();
    return TimelineProcessedVideoFrame{
        .image = std::move(output),
        .cache_enabled = frame_cache_enabled,
        .cache_hit = false,
        .cache_key_eligible = cache_key_eligible,
        .cache_lookup_performed = !frame_cache_key.isEmpty(),
          .frame_timestamp_us = frame.startTime(),
          .cache_identity_kind = cache_identity_kind,
          .cache_key_unavailable_reason = cache_key_unavailable_reason,
        .cache_lookup_ms = frame_cache_lookup_ms,
        .total_ms = total_ms,
    };
  }

  image = convert_video_preview_format(image, QImage::Format_RGBA64);
  if (image.isNull()) {
    return {};
  }

  const auto encode_channel = [](float value) {
    return encode_rgba64_channel(static_cast<double>(value));
  };
  const auto decode_rec709_channel = [](double encoded) {
    return encoded < 0.081
               ? encoded / 4.5
               : std::pow((encoded + 0.099) / 1.099, 1.0 / 0.45);
  };
  stage_timer.restart();
  const auto has_static_base_grade =
      grade_requested && grade_lut != nullptr &&
      color_settings.animated_grade_parameters.isEmpty();
  const auto fuse_static_base_grade =
      has_video_transform && has_static_base_grade;
  const auto fuse_color_effects_with_camera =
      has_video_transform && has_timeline_color_adjustments &&
      !has_spatial_color_effects &&
      (!grade_requested || has_static_base_grade);
  const auto fuse_color_effects_with_grade =
      !has_video_transform && has_static_base_grade &&
      has_timeline_color_adjustments && !has_spatial_color_effects;
  const auto fuse_timeline_color_effects =
      fuse_color_effects_with_camera || fuse_color_effects_with_grade;
  const auto quantize_intermediate_channel = [&](float value) {
    return static_cast<float>(decode_rgba64_channel(encode_channel(value)));
  };
  if (has_video_transform) {
    const auto& rec709_lut = compiled_video_rec709_lut(*profile);
    const auto apply_camera_and_grade =
        [&](const photo_editor::pipeline::ColorPixel& encoded) {
          auto output = rec709_lut.ok()
              ? photo_editor::pipeline::apply_lut_tetrahedral(
                    rec709_lut.lut, encoded)
              : photo_editor::pipeline::apply_video_rec709_output_transform(
                    *transforms->output,
                    photo_editor::pipeline::apply_video_input_transform(
                        *transforms->input, encoded));
          if (fuse_static_base_grade || fuse_color_effects_with_camera) {
            // The unfused stages round through RGBA64 between camera
            // conversion, grade, and pointwise effects. Preserve those
            // boundaries while avoiding extra image scans.
            output.red = quantize_intermediate_channel(output.red);
            output.green = quantize_intermediate_channel(output.green);
            output.blue = quantize_intermediate_channel(output.blue);
          }
          if (fuse_static_base_grade) {
            const auto graded = photo_editor::pipeline::apply_lut_tetrahedral(
                *grade_lut, output);
            output.red = graded.red;
            output.green = graded.green;
            output.blue = graded.blue;
          }
          if (fuse_color_effects_with_camera) {
            if (fuse_static_base_grade) {
              output.red = quantize_intermediate_channel(output.red);
              output.green = quantize_intermediate_channel(output.green);
              output.blue = quantize_intermediate_channel(output.blue);
            }
            const auto adjusted = apply_timeline_color_adjustments(
                output.red, output.green, output.blue,
                color_adjustment_program);
            output.red = static_cast<float>(adjusted[0]);
            output.green = static_cast<float>(adjusted[1]);
            output.blue = static_cast<float>(adjusted[2]);
          }
          return output;
        };
    for (int y = 0; y < image.height(); ++y) {
      if (cancellation.cancelled()) {
        return {};
      }
      auto* row = reinterpret_cast<QRgba64*>(image.scanLine(y));
      for (int x = 0; x < image.width(); ++x) {
        auto& pixel = row[x];
        const auto encoded = photo_editor::pipeline::ColorPixel{
            .red = static_cast<float>(decode_rgba64_channel(pixel.red())),
            .green = static_cast<float>(decode_rgba64_channel(pixel.green())),
            .blue = static_cast<float>(decode_rgba64_channel(pixel.blue())),
            .alpha = static_cast<float>(decode_rgba64_channel(pixel.alpha())),
        };
        const auto output = apply_camera_and_grade(encoded);
        pixel.setRed(encode_channel(output.red));
        pixel.setGreen(encode_channel(output.green));
        pixel.setBlue(encode_channel(output.blue));
        pixel.setAlpha(encode_channel(output.alpha));
      }
    }
    image.setText(
        QStringLiteral("timelineVideoCameraTransformRoute"),
        rec709_lut.ok()
            ? QStringLiteral("validated-65-cube")
            : QStringLiteral("exact-photo-engine"));
    image.setText(
        QStringLiteral("timelineVideoCameraTransformCubeSize"),
        QString::number(rec709_lut.ok() ? rec709_lut.lut.size : 0));
    image.setText(
        QStringLiteral("timelineVideoCameraTransformP99Error"),
        QString::number(rec709_lut.measured_p99_absolute_error, 'g', 8));
    image.setText(
        QStringLiteral("timelineVideoCameraTransformMaximumError"),
        QString::number(rec709_lut.measured_maximum_absolute_error, 'g', 8));
  }
  const auto pointwise_color_stage_elapsed_ms =
      static_cast<double>(stage_timer.nsecsElapsed()) / 1'000'000.0;
  if (fuse_timeline_color_effects) {
    fused_color_pipeline_ms = pointwise_color_stage_elapsed_ms;
  } else if (fuse_static_base_grade) {
    camera_and_base_grade_ms = pointwise_color_stage_elapsed_ms;
  } else {
    camera_transform_ms = pointwise_color_stage_elapsed_ms;
  }
  if (has_video_transform || source_is_rec709 || grade_requested ||
      has_adjustment_grades || lut != nullptr || has_shot_match ||
      has_timeline_effects) {
    image.setColorSpace(rec709_video_signal_color_space());
  }

  stage_timer.restart();
  if (!fuse_static_base_grade && grade_requested && grade_lut != nullptr &&
      color_settings.animated_grade_parameters.isEmpty()) {
    for (int y = 0; y < image.height(); ++y) {
      if (cancellation.cancelled()) {
        return {};
      }
      auto* row = reinterpret_cast<QRgba64*>(image.scanLine(y));
      for (int x = 0; x < image.width(); ++x) {
        auto& pixel = row[x];
        const auto input = photo_editor::pipeline::ColorPixel{
            .red = static_cast<float>(decode_rgba64_channel(pixel.red())),
            .green = static_cast<float>(decode_rgba64_channel(pixel.green())),
            .blue = static_cast<float>(decode_rgba64_channel(pixel.blue())),
            .alpha = static_cast<float>(decode_rgba64_channel(pixel.alpha())),
        };
        const auto output = photo_editor::pipeline::apply_lut_tetrahedral(
            *grade_lut, input);
        if (fuse_color_effects_with_grade) {
          const auto adjusted = apply_timeline_color_adjustments(
              quantize_intermediate_channel(output.red),
              quantize_intermediate_channel(output.green),
              quantize_intermediate_channel(output.blue),
              color_adjustment_program);
          pixel.setRed(encode_rgba64_channel(adjusted[0]));
          pixel.setGreen(encode_rgba64_channel(adjusted[1]));
          pixel.setBlue(encode_rgba64_channel(adjusted[2]));
        } else {
          pixel.setRed(encode_channel(output.red));
          pixel.setGreen(encode_channel(output.green));
          pixel.setBlue(encode_channel(output.blue));
        }
      }
    }
  } else if (!fuse_static_base_grade && grade_requested) {
    const auto rendered_grade = color_settings.grade_session != nullptr
        ? render_timeline_video_grade_frame(
              image, *color_settings.grade_session,
              color_settings.animated_grade_parameters,
              rec709_video_signal_color_space(), cancellation)
        : color_settings.animated_grade_parameters.isEmpty()
              ? render_timeline_video_grade_frame(
                    image, color_settings.grade_recipe_json,
                    color_settings.clip_id,
                    rec709_video_signal_color_space(), cancellation)
              : TimelineVideoGradeFrame{
                    .error = QStringLiteral("Animated shared grade preview session is unavailable.")};
    if (rendered_grade.ok()) {
      image = rendered_grade.image;
      grade.error.clear();
    } else {
      grade.error = rendered_grade.error;
    }
  }
  const auto grade_stage_elapsed_ms =
      static_cast<double>(stage_timer.nsecsElapsed()) / 1'000'000.0;
  if (fuse_color_effects_with_grade) {
    fused_color_pipeline_ms = grade_stage_elapsed_ms;
  } else {
    grade_apply_ms = grade_stage_elapsed_ms;
  }

  stage_timer.restart();
  if (!fuse_timeline_color_effects) {
    apply_timeline_color_effects(
        &image, color_settings.effects, cancellation);
  }
  if (cancellation.cancelled()) {
    return {};
  }
  const auto blur_error = apply_timeline_blur(
      &image,
      color_settings.effects,
      color_settings.sequence_width,
      color_settings.sequence_height,
      cancellation);
  if (!blur_error.isEmpty()) {
    image.setText(QStringLiteral("timelineVideoEffectError"), blur_error);
  }
  timeline_effects_ms =
      static_cast<double>(stage_timer.nsecsElapsed()) / 1'000'000.0;

  stage_timer.restart();
  if (has_shot_match || lut != nullptr) {
    for (int y = 0; y < image.height(); ++y) {
      if (cancellation.cancelled()) {
        return {};
      }
      auto* row = reinterpret_cast<QRgba64*>(image.scanLine(y));
      for (int x = 0; x < image.width(); ++x) {
        auto& pixel = row[x];
        auto output = photo_editor::pipeline::ColorPixel{
            .red = static_cast<float>(decode_rgba64_channel(pixel.red())),
            .green = static_cast<float>(decode_rgba64_channel(pixel.green())),
            .blue = static_cast<float>(decode_rgba64_channel(pixel.blue())),
            .alpha = static_cast<float>(decode_rgba64_channel(pixel.alpha())),
        };
        output.red = static_cast<float>(
            static_cast<double>(output.red) * color_settings.shot_match_red_gain);
        output.green = static_cast<float>(
            static_cast<double>(output.green) * color_settings.shot_match_green_gain);
        output.blue = static_cast<float>(
            static_cast<double>(output.blue) * color_settings.shot_match_blue_gain);
        if (lut != nullptr) {
          if (lut->rec709_preview_lut != nullptr) {
            output = photo_editor::pipeline::apply_lut(
                *lut->rec709_preview_lut, output);
          } else {
            const auto working_pixel = photo_editor::pipeline::ColorPixel{
                .red = static_cast<float>(decode_rec709_channel(output.red)),
                .green = static_cast<float>(decode_rec709_channel(output.green)),
                .blue = static_cast<float>(decode_rec709_channel(output.blue)),
                .alpha = output.alpha,
            };
            const auto graded_pixel =
                photo_editor::pipeline::apply_compiled_user_lut_stage_program(
                    *lut->program, working_pixel);
            output.red = static_cast<float>(
                photo_editor::pipeline::encode_rec709_oetf(graded_pixel.red));
            output.green = static_cast<float>(
                photo_editor::pipeline::encode_rec709_oetf(graded_pixel.green));
            output.blue = static_cast<float>(
                photo_editor::pipeline::encode_rec709_oetf(graded_pixel.blue));
          }
        }
        pixel.setRed(encode_channel(output.red));
        pixel.setGreen(encode_channel(output.green));
        pixel.setBlue(encode_channel(output.blue));
      }
    }
  }
  creative_lut_ms =
      static_cast<double>(stage_timer.nsecsElapsed()) / 1'000'000.0;
  if (has_adjustment_grades) {
    QStringList adjustment_grade_errors;
    for (const auto& adjustment : color_settings.adjustment_grades) {
      if (cancellation.cancelled()) {
        return {};
      }
      const auto opacity = qBound(0.0, adjustment.opacity, 1.0);
      if (adjustment.clip_id.isEmpty() ||
          adjustment.recipe_json.trimmed().isEmpty() ||
          opacity <= 1.0e-6) {
        continue;
      }

      auto pointwise = false;
      if (adjustment.animated_parameters.isEmpty()) {
        QString lut_support_error;
        stage_timer.restart();
        pointwise = timeline_video_grade_can_use_lut(
            adjustment.recipe_json, image.size(), &lut_support_error);
        grade_compile_ms +=
            static_cast<double>(stage_timer.nsecsElapsed()) / 1'000'000.0;
      }
      auto adjustment_lut = TimelineVideoGradeLut{};
      if (pointwise && adjustment.animated_parameters.isEmpty()) {
        stage_timer.restart();
        adjustment_lut = compile_timeline_video_grade_lut(
            adjustment.recipe_json, cancellation);
        grade_compile_ms +=
            static_cast<double>(stage_timer.nsecsElapsed()) / 1'000'000.0;
      }

      stage_timer.restart();
      if (adjustment_lut.ok()) {
        for (int y = 0; y < image.height(); ++y) {
          if (cancellation.cancelled()) {
            return {};
          }
          auto* row = reinterpret_cast<QRgba64*>(image.scanLine(y));
          for (int x = 0; x < image.width(); ++x) {
            auto& pixel = row[x];
            const auto input = photo_editor::pipeline::ColorPixel{
                .red = static_cast<float>(decode_rgba64_channel(pixel.red())),
                .green = static_cast<float>(decode_rgba64_channel(pixel.green())),
                .blue = static_cast<float>(decode_rgba64_channel(pixel.blue())),
                .alpha = static_cast<float>(decode_rgba64_channel(pixel.alpha())),
            };
            const auto graded = photo_editor::pipeline::apply_lut_tetrahedral(
                *adjustment_lut.lut, input);
            pixel.setRed(encode_channel(static_cast<float>(std::lerp(
                static_cast<double>(input.red), static_cast<double>(graded.red), opacity))));
            pixel.setGreen(encode_channel(static_cast<float>(std::lerp(
                static_cast<double>(input.green), static_cast<double>(graded.green), opacity))));
            pixel.setBlue(encode_channel(static_cast<float>(std::lerp(
                static_cast<double>(input.blue), static_cast<double>(graded.blue), opacity))));
          }
        }
      } else {
        const auto rendered_adjustment = adjustment.session != nullptr
            ? render_timeline_video_grade_frame(
                  image, *adjustment.session,
                  adjustment.animated_parameters,
                  rec709_video_signal_color_space(), cancellation)
            : adjustment.animated_parameters.isEmpty()
                  ? render_timeline_video_grade_frame(
                        image, adjustment.recipe_json,
                        adjustment.clip_id,
                        rec709_video_signal_color_space(), cancellation)
                  : TimelineVideoGradeFrame{
                        .error = QStringLiteral("Animated adjustment grade preview session is unavailable.")};
        if (!rendered_adjustment.ok()) {
          adjustment_grade_errors.push_back(
              QStringLiteral("%1: %2")
                  .arg(adjustment.clip_id, rendered_adjustment.error));
          grade_apply_ms +=
              static_cast<double>(stage_timer.nsecsElapsed()) / 1'000'000.0;
          continue;
        }
        if (opacity >= 1.0 - 1.0e-6) {
          image = rendered_adjustment.image;
        } else {
          const auto& graded_image = rendered_adjustment.image;
          for (int y = 0; y < image.height(); ++y) {
            if (cancellation.cancelled()) {
              return {};
            }
            auto* destination = reinterpret_cast<QRgba64*>(image.scanLine(y));
            const auto* source = reinterpret_cast<const QRgba64*>(
                graded_image.constScanLine(y));
            for (int x = 0; x < image.width(); ++x) {
              auto& pixel = destination[x];
              pixel.setRed(static_cast<std::uint16_t>(qRound(std::lerp(
                  static_cast<double>(pixel.red()),
                  static_cast<double>(source[x].red()), opacity))));
              pixel.setGreen(static_cast<std::uint16_t>(qRound(std::lerp(
                  static_cast<double>(pixel.green()),
                  static_cast<double>(source[x].green()), opacity))));
              pixel.setBlue(static_cast<std::uint16_t>(qRound(std::lerp(
                  static_cast<double>(pixel.blue()),
                  static_cast<double>(source[x].blue()), opacity))));
            }
          }
        }
      }
      grade_apply_ms +=
          static_cast<double>(stage_timer.nsecsElapsed()) / 1'000'000.0;
    }
    if (!adjustment_grade_errors.isEmpty()) {
      if (!grade.error.isEmpty()) {
        adjustment_grade_errors.prepend(grade.error);
      }
      grade.error = adjustment_grade_errors.join(QStringLiteral("; "));
    }
  }
  if (!grade.error.isEmpty()) {
    image.setText(QStringLiteral("timelineVideoGradeError"), grade.error);
  }
  if (!conversion_warning.isEmpty()) {
    image.setText(QStringLiteral("timelineVideoColorDecodeWarning"),
                  conversion_warning);
  }
  image.setText(QStringLiteral("timelineVideoColorDecodeStatus"),
                color_conversion_status);
  auto output = convert_video_preview_format(image, QImage::Format_RGBA8888);
  attach_processing_metrics(&output);
  if (!output.isNull()) {
    if (frame_cache_enabled && !frame_cache_key.isEmpty()) {
      static_cast<void>(processed_video_frame_cache().insert(frame_cache_key, output));
    }
  }
  const auto total_ms = output.text(
      QStringLiteral("timelineVideoMetricTotalMs")).toDouble();
  return TimelineProcessedVideoFrame{
      .image = std::move(output),
      .cache_enabled = frame_cache_enabled,
      .cache_hit = false,
      .cache_key_eligible = cache_key_eligible,
      .cache_lookup_performed = !frame_cache_key.isEmpty(),
      .frame_timestamp_us = frame.startTime(),
      .cache_identity_kind = cache_identity_kind,
      .cache_key_unavailable_reason = cache_key_unavailable_reason,
      .cache_lookup_ms = frame_cache_lookup_ms,
      .total_ms = total_ms,
  };
}

}  // namespace

void TimelinePlaybackFrameImageState::setImage(QImage image) {
  const QMutexLocker lock(&mutex_);
  image_ = std::move(image);
}

QVariantMap TimelinePlaybackFrameImageState::resourceDiagnostics() const {
  const QMutexLocker lock(&mutex_);
  const auto image_bytes = image_.sizeInBytes();
  return {
      {QStringLiteral("processedFrameImagePresent"), !image_.isNull()},
      {QStringLiteral("processedFrameImageBytes"),
       static_cast<qulonglong>(image_bytes > 0 ? image_bytes : 0)},
      {QStringLiteral("processedFrameImageWidth"), image_.width()},
      {QStringLiteral("processedFrameImageHeight"), image_.height()},
  };
}

QImage TimelinePlaybackFrameImageState::requestImage(
    QSize* size,
    const QSize& requested_size) const {
  const QMutexLocker lock(&mutex_);
  auto result = image_;
  if (result.isNull()) {
    result = QImage(1, 1, QImage::Format_RGBA8888);
    result.fill(Qt::transparent);
  }
  if (size != nullptr) {
    *size = result.size();
  }
  const auto requested_downscale = requested_size.isValid() &&
      !requested_size.isEmpty() &&
      (requested_size.width() < result.width() ||
       requested_size.height() < result.height());
  if (requested_downscale && result.size() != requested_size) {
    result = result.scaled(requested_size, Qt::KeepAspectRatio, Qt::FastTransformation);
  }
  return result;
}

TimelinePlaybackImageProvider::TimelinePlaybackImageProvider(
    std::shared_ptr<TimelinePlaybackFrameImageState> image_state)
    : QQuickImageProvider(QQuickImageProvider::Image),
      image_state_(std::move(image_state)) {}

QImage TimelinePlaybackImageProvider::requestImage(
    const QString&,
    QSize* size,
    const QSize& requested_size) {
  return image_state_ == nullptr
             ? QImage{}
             : image_state_->requestImage(size, requested_size);
}

TimelinePlaybackController::TimelinePlaybackController(
    TimelineDocumentController* timeline_document,
    QObject* parent)
    : QObject(parent), timeline_document_(timeline_document) {
  native_media_enabled_ = !qEnvironmentVariableIsSet("PHOTO_EDITOR_TIMELINE_PLAYBACK_HEADLESS");
  if (native_media_enabled_) {
    audio_output_.attachPlayer(&player_);
    player_.setVideoSink(&video_sink_);
    native_video_frame_connection_ = connect(
        &video_sink_,
        &QVideoSink::videoFrameChanged,
        this,
        [this](const QVideoFrame& frame) {
          dispatchNativeVideoFrame(frame);
        },
        Qt::DirectConnection);
  }
  connect(
      &frame_processing_watcher_,
      &QFutureWatcher<TimelineProcessedVideoFrame>::finished,
      this,
      [this]() {
        const auto processed_frame = frame_processing_watcher_.result();
        const auto& image = processed_frame.image;
        const auto latest_work_generation =
            latest_frame_work_generation_->load(std::memory_order_relaxed);
        if (processing_work_generation_ == latest_work_generation &&
            processing_frame_generation_ == video_frame_generation_ &&
            processing_video_input_transform_ == active_video_input_transform_ &&
            processing_video_frame_color_key_ == active_video_frame_color_key_ &&
            processing_video_foreground_cutout_key_ ==
                active_video_foreground_cutout_key_ &&
            (processing_frame_is_scene_preview_ ||
             (!awaiting_media_source_ && !awaiting_position_sync_)) &&
            !video_frame_item_.isNull() && !image.isNull()) {
          auto playback_state_changed = false;
          const auto metric_source_key = image.text(
              QStringLiteral("timelineVideoMetricSourceKey"));
          const auto reset_frame_metrics =
              !metric_source_key.isEmpty() &&
              metric_source_key != frame_processing_metrics_
                                         .value(QStringLiteral("sourceKey"))
                                         .toString();
          auto next_frame_metrics = reset_frame_metrics
                                       ? QVariantMap{}
                                       : frame_processing_metrics_;
          if (!metric_source_key.isEmpty()) {
            next_frame_metrics.insert(QStringLiteral("sourceKey"),
                                      metric_source_key);
          }
          const auto previous_metric_sample_count =
              next_frame_metrics.value(QStringLiteral("sampleCount")).toLongLong();
          const auto metric_samples = {
              std::pair{
                  QStringLiteral("decodeNormalizeMs"),
                  QStringLiteral("timelineVideoMetricDecodeNormalizeMs")},
              std::pair{
                  QStringLiteral("gradeCompileMs"),
                  QStringLiteral("timelineVideoMetricGradeCompileMs")},
              std::pair{
                  QStringLiteral("creativeLutPrepareMs"),
                  QStringLiteral("timelineVideoMetricCreativeLutPrepareMs")},
              std::pair{
                  QStringLiteral("cameraTransformMs"),
                  QStringLiteral("timelineVideoMetricCameraTransformMs")},
              std::pair{
                  QStringLiteral("cameraAndBaseGradeMs"),
                  QStringLiteral("timelineVideoMetricCameraAndBaseGradeMs")},
              std::pair{
                  QStringLiteral("fusedColorPipelineMs"),
                  QStringLiteral("timelineVideoMetricFusedColorPipelineMs")},
              std::pair{
                  QStringLiteral("gradeApplyMs"),
                  QStringLiteral("timelineVideoMetricGradeApplyMs")},
              std::pair{
                  QStringLiteral("timelineEffectsMs"),
                  QStringLiteral("timelineVideoMetricTimelineEffectsMs")},
              std::pair{
                  QStringLiteral("creativeLutMs"),
                  QStringLiteral("timelineVideoMetricCreativeLutMs")},
              std::pair{
                  QStringLiteral("frameCacheLookupMs"),
                  QStringLiteral("timelineVideoMetricFrameCacheLookupMs")},
              std::pair{
                  QStringLiteral("totalMs"),
                  QStringLiteral("timelineVideoMetricTotalMs")},
          };
          for (const auto& [metric_key, image_key] : metric_samples) {
            auto value = 0.0;
            auto value_ok = processed_frame.cache_hit;
            if (processed_frame.cache_hit) {
              if (metric_key == QStringLiteral("totalMs")) {
                value = processed_frame.total_ms;
              } else if (metric_key == QStringLiteral("frameCacheLookupMs")) {
                value = processed_frame.cache_lookup_ms;
              }
            } else {
              value = image.text(image_key).toDouble(&value_ok);
            }
            if (!value_ok || !std::isfinite(value) || value < 0.0) {
              continue;
            }
            const auto previous_metric_value =
                next_frame_metrics.value(metric_key);
            const auto previous_value = previous_metric_value.isValid()
                                            ? previous_metric_value.toDouble()
                                            : value;
            const auto smoothed_value = previous_metric_sample_count == 0
                                            ? value
                                            : std::lerp(previous_value, value, 0.15);
            next_frame_metrics.insert(metric_key, smoothed_value);
          }
          const auto latest_total_ms = processed_frame.cache_hit
                                           ? processed_frame.total_ms
                                           : image.text(QStringLiteral(
                                                 "timelineVideoMetricTotalMs"))
                                                 .toDouble();
          const auto latest_total_ok = std::isfinite(latest_total_ms) &&
                                       latest_total_ms >= 0.0;
          if (latest_total_ok && std::isfinite(latest_total_ms) && latest_total_ms >= 0.0) {
            next_frame_metrics.insert(QStringLiteral("lastFrameTotalMs"), latest_total_ms);
          }
          const auto latest_cache_lookup_ms = processed_frame.cache_hit
                                                  ? processed_frame.cache_lookup_ms
                                                  : image.text(QStringLiteral(
                                                        "timelineVideoMetricFrameCacheLookupMs"))
                                                        .toDouble();
          const auto latest_cache_lookup_ok =
              std::isfinite(latest_cache_lookup_ms) &&
              latest_cache_lookup_ms >= 0.0;
          if (latest_cache_lookup_ok && std::isfinite(latest_cache_lookup_ms) &&
              latest_cache_lookup_ms >= 0.0) {
            next_frame_metrics.insert(
                QStringLiteral("lastFrameCacheLookupMs"), latest_cache_lookup_ms);
          }
          bool width_ok = false;
          const auto metric_width = image.text(
              QStringLiteral("timelineVideoMetricWidth")).toInt(&width_ok);
          if (width_ok) {
            next_frame_metrics.insert(QStringLiteral("width"), metric_width);
          }
          bool height_ok = false;
          const auto metric_height = image.text(
              QStringLiteral("timelineVideoMetricHeight")).toInt(&height_ok);
          if (height_ok) {
            next_frame_metrics.insert(QStringLiteral("height"), metric_height);
          }
          const auto cache_statistics = processed_video_frame_cache().statistics();
          next_frame_metrics.insert(QStringLiteral("frameCacheEnabled"),
                                    processed_frame.cache_enabled);
          next_frame_metrics.insert(QStringLiteral("frameCacheHit"),
                                    processed_frame.cache_hit);
          next_frame_metrics.insert(QStringLiteral("frameCacheKeyEligible"),
                                    processed_frame.cache_key_eligible);
          next_frame_metrics.insert(
              QStringLiteral("frameCacheLookupPerformed"),
              processed_frame.cache_lookup_performed);
          next_frame_metrics.insert(
              QStringLiteral("frameCacheFrameTimestampUs"),
              processed_frame.frame_timestamp_us);
          next_frame_metrics.insert(
              QStringLiteral("frameCacheIdentityKind"),
              processed_frame.cache_identity_kind);
          next_frame_metrics.insert(
              QStringLiteral("frameCacheKeyUnavailableReason"),
              processed_frame.cache_key_unavailable_reason);
          next_frame_metrics.insert(
              QStringLiteral("frameCacheEntries"),
              static_cast<qulonglong>(cache_statistics.entries));
          next_frame_metrics.insert(
              QStringLiteral("frameCacheBytes"),
              static_cast<qulonglong>(cache_statistics.resident_bytes));
          next_frame_metrics.insert(
              QStringLiteral("frameCacheHits"),
              static_cast<qulonglong>(cache_statistics.hits));
          next_frame_metrics.insert(
              QStringLiteral("frameCacheMisses"),
              static_cast<qulonglong>(cache_statistics.misses));
          next_frame_metrics.insert(
              QStringLiteral("frameCacheEvictions"),
              static_cast<qulonglong>(cache_statistics.evictions));
          const auto next_metric_sample_count =
              previous_metric_sample_count + 1;
          next_frame_metrics.insert(
              QStringLiteral("sampleCount"), next_metric_sample_count);
          frame_processing_metrics_ = std::move(next_frame_metrics);
          if (previous_metric_sample_count == 0 ||
              next_metric_sample_count % 15 == 0) {
            playback_state_changed = true;
          }
          const auto next_color_warning =
              image.text(QStringLiteral("timelineVideoColorDecodeWarning"));
          if (next_color_warning != color_signal_processing_warning_) {
            color_signal_processing_warning_ = next_color_warning;
            playback_state_changed = true;
          }
          const auto next_grade_error =
              image.text(QStringLiteral("timelineVideoGradeError"));
          if (next_grade_error != color_grade_error_) {
            color_grade_error_ = next_grade_error;
            playback_state_changed = true;
          }
          const auto next_conversion_status =
              image.text(QStringLiteral("timelineVideoColorDecodeStatus"));
          if (!next_conversion_status.isEmpty()) {
            auto completed_metadata = color_signal_metadata_;
            completed_metadata.insert(QStringLiteral("conversionStatus"),
                                      next_conversion_status);
            if (next_color_warning.isEmpty()) {
              completed_metadata.remove(QStringLiteral("conversionWarning"));
            } else {
              completed_metadata.insert(QStringLiteral("conversionWarning"),
                                        next_color_warning);
            }
            const std::array<std::pair<QString, QString>, 4> camera_transform_fields = {{
                {QStringLiteral("cameraTransformRoute"),
                 QStringLiteral("timelineVideoCameraTransformRoute")},
                {QStringLiteral("cameraTransformCubeSize"),
                 QStringLiteral("timelineVideoCameraTransformCubeSize")},
                {QStringLiteral("cameraTransformP99Error"),
                 QStringLiteral("timelineVideoCameraTransformP99Error")},
                {QStringLiteral("cameraTransformMaximumError"),
                 QStringLiteral("timelineVideoCameraTransformMaximumError")},
            }};
            for (const auto& [metadata_key, image_key] : camera_transform_fields) {
              const auto value = image.text(image_key);
              if (value.isEmpty()) {
                completed_metadata.remove(metadata_key);
              } else {
                completed_metadata.insert(metadata_key, value);
              }
            }
            setColorSignalMetadata(completed_metadata);
            if (timeline_document_ != nullptr) {
              timeline_document_->setPlaybackColorSignalMetadataForCurrentClip(
                  completed_metadata.value(QStringLiteral("clipId")).toString(),
                  std::move(completed_metadata));
            }
          }
          if (playback_state_changed) {
            emit stateChanged();
          }
          // Notifications above may synchronously seek or detach the output.
          if (processing_frame_generation_ == video_frame_generation_ &&
              (processing_frame_is_scene_preview_ ||
               (!awaiting_media_source_ && !awaiting_position_sync_)) &&
              !video_frame_item_.isNull()) {
            publishProcessedFrame(
                image,
                processing_frame_source_timestamp_us_,
                processing_frame_source_timestamp_estimated_);
          }
        }
        // Keep arrivals queued through publication callbacks; only one worker
        // may own the watcher, including during synchronous signal handlers.
        frame_processing_active_ = false;
        if (!timeline_document_.isNull() && pending_position_ms_ >= 0 &&
            !awaiting_media_source_ && !awaiting_position_sync_) {
          const auto next_position_ms = pending_position_ms_;
          pending_position_ms_ = -1;
          if (qAbs(player_.position() - next_position_ms) > kSeekToleranceMs) {
            invalidateFrameProcessing();
            awaited_position_ms_ = next_position_ms;
            awaiting_position_sync_ = true;
            player_.setPosition(next_position_ms);
            emit stateChanged();
            return;
          }
        }
        if (!has_pending_video_frame_) {
          if (processing_frame_generation_ != video_frame_generation_) {
            reprocessCurrentPausedFrameIfNeeded();
          }
          return;
        }
        auto next_frame = std::move(latest_pending_frame_);
        const auto next_source_timestamp_us = pending_frame_source_timestamp_us_;
        const auto next_source_timestamp_estimated =
            pending_frame_source_timestamp_estimated_;
        const auto next_is_scene_preview = pending_frame_is_scene_preview_;
        const auto next_scene_preview_identity = pending_scene_preview_identity_;
        const auto next_generation = pending_frame_generation_;
        const auto next_work_generation = pending_work_generation_;
        has_pending_video_frame_ = false;
        pending_frame_source_timestamp_us_ = -1;
        pending_frame_source_timestamp_estimated_ = false;
        pending_frame_is_scene_preview_ = false;
        pending_scene_preview_identity_.clear();
        if (next_generation == video_frame_generation_ &&
            (next_is_scene_preview ||
             (!awaiting_media_source_ && !awaiting_position_sync_))) {
          startFrameProcessing(
              next_frame,
              next_generation,
              next_work_generation,
              next_source_timestamp_us,
              next_source_timestamp_estimated,
              next_is_scene_preview,
              next_scene_preview_identity);
        }
      });
  connect(
      &still_photo_source_watcher_,
      &QFutureWatcher<QPair<QImage, QString>>::finished,
      this,
      [this]() {
        const auto decoded_source_key = still_photo_decoding_source_key_;
        const auto decoded = still_photo_source_watcher_.result();
        still_photo_decoding_source_key_.clear();
        if (decoded_source_key == still_photo_requested_source_key_) {
          if (decoded.first.isNull()) {
            still_photo_grade_preview_active_ = false;
            still_photo_grade_request_key_.clear();
            color_grade_error_ = decoded.second.isEmpty()
                                     ? QStringLiteral("The still photo preview could not be decoded.")
                                     : decoded.second;
            clearProcessedFrame();
            emit stateChanged();
            return;
          }
          still_photo_cached_source_key_ = decoded_source_key;
          still_photo_source_image_ = decoded.first;
          submitStillPhotoGradePreviewFrame();
          emit stateChanged();
          return;
        }
        syncStillPhotoGradePreview(current_program_clip_);
      });
  connect(&audio_output_, &TimelineAudioOutput::meterChanged, this, [this]() {
    emit stateChanged();
  });
  connect(
      &audio_output_,
      &TimelineAudioOutput::outputDeviceRecoveryRequested,
      this,
      [this](const QString& detail) {
        recoverRuntimeFailure(QStringLiteral("audio-device"), detail);
      });
  gap_timer_.setInterval(15);
  gap_timer_.setTimerType(Qt::PreciseTimer);
  connect(&gap_timer_, &QTimer::timeout, this, [this]() { tickGapPlayback(); });
  native_paused_preroll_timer_.setSingleShot(true);
  native_paused_preroll_timer_.setInterval(kNativePausedPrerollTimeoutMs);
  connect(&native_paused_preroll_timer_, &QTimer::timeout, this, [this]() {
    finishNativePausedPreroll(true);
  });
  prefetch_timer_.setSingleShot(true);
  prefetch_timer_.setInterval(18);
  connect(
      &prefetch_timer_,
      &QTimer::timeout,
      this,
      &TimelinePlaybackController::refreshPrefetchPlan);
  runtime_recovery_timer_.setSingleShot(true);
  runtime_recovery_timer_.setInterval(0);
  connect(&runtime_recovery_timer_, &QTimer::timeout, this, [this]() {
    if (!runtime_recovery_busy_) {
      return;
    }
    if (runtime_recovery_kind_ == QStringLiteral("decoder") ||
        runtime_recovery_kind_ == QStringLiteral("gpu")) {
      syncToDocument();
      refreshPrefetchPlan();
    }
    const auto state_preserved = timeline_document_.isNull() ||
                                 timeline_document_->deterministicDocumentHash() ==
                                     runtime_recovery_document_hash_;
    runtime_recovery_busy_ = false;
    runtime_recovery_status_ = state_preserved
                                   ? QStringLiteral("Recovered %1 without changing the edit.")
                                         .arg(runtime_recovery_kind_)
                                   : QStringLiteral("Recovery stopped because project state changed unexpectedly.");
    emit stateChanged();
  });

  connect(
      timeline_document_,
      &TimelineDocumentController::playbackStateChanged,
      this,
      [this]() {
        if (!workspace_active_) {
          if (timeline_document_->sequencePlaying())
            timeline_document_->pauseSequence();
          return;
        }
        syncToDocument();
        schedulePrefetchRefresh();
      });
  connect(
      timeline_document_,
      &TimelineDocumentController::selectionChanged,
      this,
      [this]() {
        syncToDocument();
        schedulePrefetchRefresh();
      });
  connect(
      timeline_document_,
      &TimelineDocumentController::renderCacheChanged,
      this,
      [this]() {
        syncToDocument();
        schedulePrefetchRefresh();
      });
  if (native_media_enabled_) {
    connect(
        &player_,
        &QMediaPlayer::mediaStatusChanged,
        this,
        [this](QMediaPlayer::MediaStatus status) {
          if (!workspace_active_) {
            if (status == QMediaPlayer::LoadedMedia || status == QMediaPlayer::BufferedMedia)
              awaiting_media_source_ = false;
            return;
          }
          if (status == QMediaPlayer::LoadedMedia || status == QMediaPlayer::BufferedMedia ||
              status == QMediaPlayer::BufferingMedia || status == QMediaPlayer::StalledMedia) {
            setErrorString(QString{});
            const auto paused_source_is_waiting_for_native_frame =
                awaiting_media_source_ && pending_position_ms_ >= 0 &&
                !timeline_document_.isNull() &&
                !timeline_document_->sequencePlaying();
            if (paused_source_is_waiting_for_native_frame) {
              const auto target_position_ms = pending_position_ms_;
              if (startNativePausedPreroll(target_position_ms)) {
                pending_position_ms_ = -1;
              } else {
                armNativePausedPrerollOutputRetry();
              }
              // Keep the source's target pending until a seekable native item,
              // known duration, and exposed output are all available. Darwin
              // can report BufferingMedia or LoadedMedia before those gates.
              emit stateChanged();
              return;
            }
            awaiting_media_source_ = false;
            if (scene_preview_cache_active_) {
              pending_position_ms_ = -1;
              awaiting_position_sync_ = false;
              const auto target_position_ms = timeline_document_.isNull()
                  ? 0
                  : qRound64(
                        (using_render_cache_
                             ? timeline_document_->sequencePlayheadSeconds() -
                                   render_cache_range_start_seconds_
                             : timeline_document_->programClipPlayheadSeconds()) *
                        1000.0);
              const auto scene_audio_should_play =
                  !timeline_document_.isNull() &&
                  timeline_document_->sequencePlaying() && shuttle_rate_ >= 0.0 &&
                  active_playback_rate_ > 0.0001;
              if (scene_audio_should_play) {
                attachAudioBufferOutputForPlayback();
                if (player_.playbackState() != QMediaPlayer::PlayingState &&
                    !play_when_ready_) {
                  player_.setPosition(target_position_ms);
                  awaited_position_ms_ = target_position_ms;
                }
                player_.play();
                play_when_ready_ =
                    player_.playbackState() != QMediaPlayer::PlayingState;
              } else {
                play_when_ready_ = false;
                if (player_.playbackState() == QMediaPlayer::PlayingState) {
                  player_.pause();
                }
                if (awaited_position_ms_ != target_position_ms) {
                  player_.setPosition(target_position_ms);
                  awaited_position_ms_ = target_position_ms;
                }
              }
            } else if (pending_position_ms_ >= 0) {
              const auto target_position_ms = pending_position_ms_;
              pending_position_ms_ = -1;
              // Seeking to the already loaded position may emit no positionChanged.
              const auto position_matches =
                  player_.position() == target_position_ms;
              clearPendingSeekFrame();
              awaited_position_ms_ = position_matches ? -1 : target_position_ms;
              awaiting_position_sync_ = !position_matches;
              player_.setPosition(target_position_ms);
            }
            if (!scene_preview_cache_active_ && !awaiting_position_sync_ &&
                !timeline_document_.isNull() &&
                !timeline_document_->sequencePlaying()) {
              reprocessCurrentPausedFrameIfNeeded();
            }
            if (play_when_ready_ && !scene_preview_cache_active_) {
              play_when_ready_ = false;
              attachAudioBufferOutputForPlayback();
              player_.play();
            }
            if (scene_preview_cache_active_) {
              syncScenePreviewFrame();
            }
          }
          if (status == QMediaPlayer::StalledMedia) {
            applyAdaptivePlaybackPressure(
                QStringLiteral("the media backend stalled"), true);
            publishPlaybackDiagnostic(
                QStringLiteral("stalled"), 1,
                QStringLiteral("The media backend reported stalled playback."));
          }
          emit stateChanged();
        });
    connect(
        &player_,
        &QMediaPlayer::bufferProgressChanged,
        this,
        [this](float progress) {
          if (progress < 0.25F) {
            applyAdaptivePlaybackPressure(
                QStringLiteral("the playback buffer is low"), false);
          }
          emit stateChanged();
        });
    connect(
        &player_,
        &QMediaPlayer::durationChanged,
        this,
        [this](qint64) {
          if (tryStartPendingNativePausedPreroll()) {
            emit stateChanged();
          }
        });
    connect(
        &player_,
        &QMediaPlayer::seekableChanged,
        this,
        [this](bool seekable) {
          if (seekable && tryStartPendingNativePausedPreroll()) {
            emit stateChanged();
          }
        });
    connect(
        &player_,
        &QMediaPlayer::positionChanged,
        this,
        [this](qint64 position) {
          if (!workspace_active_ || timeline_document_.isNull()) {
            return;
          }

          if (native_paused_preroll_active_) {
            observeNativePausedPrerollSeekPosition(position);
          }

          if (awaiting_media_source_) {
            return;
          }
          // Scene preview frames are selected from the original source-time
          // cache and the document gap clock. A Darwin backend position can
          // settle on a different frame, so it must not drive preview state.
          if (scene_preview_cache_active_) {
            return;
          }

          // QMediaPlayer can deliver a final positionChanged signal from the
          // previous source after setSource(). Do not let that old clip-local
          // timestamp advance the newly selected timeline clip.
          if (awaiting_position_sync_) {
            if (qAbs(position - awaited_position_ms_) > kPositionSyncToleranceMs) {
              return;
            }
            awaiting_position_sync_ = false;
            awaited_position_ms_ = -1;

            if (pending_position_ms_ >= 0) {
              const auto next_position_ms = pending_position_ms_;
              pending_position_ms_ = -1;
              if (qAbs(next_position_ms - position) >
                  kPositionSyncToleranceMs) {
                invalidateFrameProcessing();
                awaited_position_ms_ = next_position_ms;
                awaiting_position_sync_ = true;
                player_.setPosition(next_position_ms);
                emit stateChanged();
                return;
              }
              // The queued scrub converged on this acknowledgment. Reuse its
              // post-request candidate instead of setting the same position
              // again, which some paused backends acknowledge with no signal.
            }

            const auto pending_seek_frame = pending_seek_video_frame_;
            const auto pending_seek_generation = pending_seek_frame_generation_;
            pending_seek_video_frame_ = QVideoFrame{};
            pending_seek_frame_generation_ = 0U;
            if (pending_seek_frame.isValid() &&
                pending_seek_generation == position_sync_generation_) {
              // Some backends deliver the decoded seek frame before the
              // matching positionChanged signal. Consume only a frame that
              // arrived after this seek generation began. When the backend
              // supplies PTS metadata, reject frames from another position.
              const auto frame_position_us = pending_seek_frame.startTime();
              const auto frame_matches_position = frame_position_us < 0 ||
                  qAbs(frame_position_us / 1000 - position) <=
                      kPositionSyncToleranceMs;
              if (frame_matches_position) {
                handleVideoFrame(pending_seek_frame);
              }
            }
          }

          if (player_.playbackState() == QMediaPlayer::PlayingState &&
              !native_paused_preroll_active_) {
            if (playback_sample_clock_.isValid() &&
                last_playback_sample_position_ms_ >= 0) {
              recordPlaybackTimingSample(
                  qAbs(position - last_playback_sample_position_ms_),
                  playback_sample_clock_.restart());
            } else {
              playback_sample_clock_.start();
            }
            last_playback_sample_position_ms_ = position;
          }

          const auto program_audio_position = timeline_document_.isNull() ? position
              : qRound64((using_render_cache_
                    ? timeline_document_->sequencePlayheadSeconds() - render_cache_range_start_seconds_
                    : timeline_document_->programClipPlayheadSeconds()) * 1000.0);
          updateAudioVolume(program_audio_position);
        });
    connect(
        &player_,
        &QMediaPlayer::errorOccurred,
        this,
        [this](QMediaPlayer::Error, const QString& error_string) {
          cancelNativePausedPreroll();
          const auto detail = error_string.trimmed();
          setErrorString(detail);
          publishPlaybackDiagnostic(QStringLiteral("decoder"), 1, detail);
          recoverRuntimeFailure(QStringLiteral("decoder"), detail);
        });
  }

  syncToDocument();
  refreshPrefetchPlan();
}

bool TimelinePlaybackController::hasVideoSource() const {
  return !current_source_url_.isEmpty();
}

bool TimelinePlaybackController::mediaReady() const {
  if (!native_media_enabled_) {
    return hasVideoSource();
  }
  return player_.mediaStatus() == QMediaPlayer::LoadedMedia || player_.mediaStatus() == QMediaPlayer::BufferedMedia ||
         player_.mediaStatus() == QMediaPlayer::BufferingMedia || player_.mediaStatus() == QMediaPlayer::StalledMedia;
}

QString TimelinePlaybackController::errorString() const {
  return error_string_;
}

QString TimelinePlaybackController::previewQualityMode() const {
  return preview_quality_mode_;
}

QString TimelinePlaybackController::activeSourceUrl() const {
  return current_source_url_;
}

bool TimelinePlaybackController::usingProxy() const {
  return using_proxy_;
}

bool TimelinePlaybackController::usingRenderCache() const {
  return using_render_cache_;
}

QString TimelinePlaybackController::effectivePreviewQuality() const {
  if (current_source_url_.isEmpty()) {
    return QStringLiteral("none");
  }
  if (using_render_cache_) {
    return QStringLiteral("render-cache");
  }
  if (using_proxy_) {
    return QStringLiteral("proxy");
  }
  if (preview_quality_mode_ == QStringLiteral("half") ||
      preview_quality_mode_ == QStringLiteral("quarter")) {
    return preview_quality_mode_;
  }
  return QStringLiteral("original");
}

QString TimelinePlaybackController::playbackHealth() const {
  if (!error_string_.isEmpty()) {
    return QStringLiteral("error");
  }
  if (!hasVideoSource()) {
    return QStringLiteral("idle");
  }
  if (!native_media_enabled_) {
    return QStringLiteral("ready");
  }
  switch (player_.mediaStatus()) {
    case QMediaPlayer::StalledMedia:
      return QStringLiteral("stalled");
    case QMediaPlayer::LoadingMedia:
    case QMediaPlayer::BufferingMedia:
      return QStringLiteral("buffering");
    case QMediaPlayer::InvalidMedia:
      return QStringLiteral("error");
    default:
      return QStringLiteral("ready");
  }
}

double TimelinePlaybackController::bufferProgress() const {
  if (!native_media_enabled_) {
    return hasVideoSource() ? 1.0 : 0.0;
  }
  return qBound(0.0, static_cast<double>(player_.bufferProgress()), 1.0);
}

bool TimelinePlaybackController::adaptiveQualityActive() const {
  if (preview_quality_mode_ != QStringLiteral("auto") || timeline_document_.isNull()) {
    return false;
  }
  const auto responsiveness_needed =
      interaction_active_ || timeline_document_->sequencePlaying();
  return responsiveness_needed &&
         (using_render_cache_ || using_proxy_ ||
          adaptive_detail_level_ != QStringLiteral("full"));
}

QString TimelinePlaybackController::adaptiveDetailLevel() const {
  return adaptive_detail_level_;
}

QString TimelinePlaybackController::adaptiveQualityReason() const {
  return adaptive_quality_reason_;
}

double TimelinePlaybackController::effectivePreviewScale() const {
  if (preview_quality_mode_ == QStringLiteral("quarter")) {
    return 0.25;
  }
  if (preview_quality_mode_ == QStringLiteral("half")) {
    return 0.5;
  }
  if (preview_quality_mode_ == QStringLiteral("auto")) {
    if (adaptive_detail_level_ == QStringLiteral("quarter")) {
      return 0.25;
    }
    if (adaptive_detail_level_ == QStringLiteral("half")) {
      return 0.5;
    }
  }
  return 1.0;
}

bool TimelinePlaybackController::playbackRangeActive() const {
  return playback_range_active_;
}

bool TimelinePlaybackController::playbackRangeLooping() const {
  return playback_range_active_ && playback_range_looping_;
}

double TimelinePlaybackController::playbackRangeStart() const {
  return playback_range_active_ ? loop_start_seconds_ : -1.0;
}

double TimelinePlaybackController::playbackRangeEnd() const {
  return playback_range_active_ ? loop_end_seconds_ : -1.0;
}

bool TimelinePlaybackController::interactionActive() const {
  return interaction_active_;
}

double TimelinePlaybackController::activePlaybackRate() const {
  return active_playback_rate_;
}

double TimelinePlaybackController::shuttleRate() const {
  return shuttle_rate_;
}

bool TimelinePlaybackController::shuttling() const {
  return !qFuzzyIsNull(shuttle_rate_) && timeline_document_ != nullptr &&
         timeline_document_->sequencePlaying();
}

QVariantList TimelinePlaybackController::prefetchPlan() const {
  return prefetch_plan_;
}

QString TimelinePlaybackController::prefetchedSourceUrl() const {
  return prefetched_source_url_;
}

double TimelinePlaybackController::prefetchDirection() const {
  return prefetch_direction_;
}

double TimelinePlaybackController::audioPeakLeft() const {
  return audio_output_.peakLeft();
}

double TimelinePlaybackController::audioPeakRight() const {
  return audio_output_.peakRight();
}

double TimelinePlaybackController::audioTruePeakLeft() const {
  return audio_output_.truePeakLeft();
}

double TimelinePlaybackController::audioTruePeakRight() const {
  return audio_output_.truePeakRight();
}

double TimelinePlaybackController::audioRmsLeft() const {
  return audio_output_.rmsLeft();
}

double TimelinePlaybackController::audioRmsRight() const {
  return audio_output_.rmsRight();
}

bool TimelinePlaybackController::audioClipping() const {
  return audio_output_.clipping();
}

void TimelinePlaybackController::resetAudioMeters() {
  audio_output_.resetMeters();
  emit stateChanged();
}

QVariantMap TimelinePlaybackController::playbackDiagnostics() const {
  return playback_diagnostics_;
}

QVariantMap TimelinePlaybackController::playbackResourceDiagnostics() const {
  const auto sink_frame_retained = video_sink_.videoFrame().isValid();
  const auto latest_pending_frame_retained = latest_pending_frame_.isValid();
  const auto pending_seek_frame_retained = pending_seek_video_frame_.isValid();
  const auto processed_frame_cache = processed_video_frame_cache().statistics();
  auto diagnostics = QVariantMap{
      {QStringLiteral("hibernated"), playback_hibernated_},
      {QStringLiteral("resourceReleaseAttemptCount"),
       static_cast<qulonglong>(playback_resource_release_attempt_count_)},
      {QStringLiteral("resourceReleaseCompletedCount"),
       static_cast<qulonglong>(playback_resource_release_completed_count_)},
      {QStringLiteral("workspaceActive"), workspace_active_},
      {QStringLiteral("playerSourceLoaded"), !player_.source().isEmpty()},
      {QStringLiteral("playerMediaReady"), mediaReady()},
      {QStringLiteral("nativePrerollActive"), native_paused_preroll_active_},
      {QStringLiteral("nativePrerollFirstFrameSeen"),
       native_paused_preroll_first_frame_seen_},
      {QStringLiteral("nativePrerollSeekIssued"),
       native_paused_preroll_seek_issued_},
      {QStringLiteral("nativePrerollSeekQueued"),
       native_paused_preroll_seek_queued_},
      {QStringLiteral("nativePrerollRetargetPending"),
       native_paused_preroll_retarget_pending_},
      {QStringLiteral("nativePrerollSeekCount"),
       native_paused_preroll_seek_count_},
      {QStringLiteral("nativePrerollSeekAcknowledged"),
       native_paused_preroll_seek_acknowledged_},
      {QStringLiteral("nativePrerollAckPositionMs"),
       native_paused_preroll_ack_position_ms_},
      {QStringLiteral("nativePrerollFrameGeneration"),
       static_cast<qulonglong>(
           native_paused_preroll_frame_generation_.load(
               std::memory_order_acquire))},
      {QStringLiteral("nativePrerollPlayPending"),
       native_paused_preroll_play_pending_},
      {QStringLiteral("nativePrerollTargetMs"),
       native_paused_preroll_target_ms_},
      {QStringLiteral("nativePrerollLastTargetTimestampUs"),
       native_paused_preroll_last_target_timestamp_us_},
      {QStringLiteral("nativePrerollStartCount"),
       native_paused_preroll_start_count_},
      {QStringLiteral("nativePrerollTimeoutCount"),
       native_paused_preroll_timeout_count_},
      {QStringLiteral("nativePrerollStatus"),
       native_paused_preroll_status_},
      {QStringLiteral("audioBufferOutputDetached"),
       native_audio_buffer_output_detached_},
      {QStringLiteral("audioOutputSuspended"),
       audio_output_.outputSuspended()},
      {QStringLiteral("audioDecoder"), audio_output_.decoderDiagnostics()},
      {QStringLiteral("videoOutputWindowExposed"),
       !video_frame_item_.isNull() && video_frame_item_->window() != nullptr &&
           video_frame_item_->window()->isExposed()},
      {QStringLiteral("documentSequencePlaying"),
       !timeline_document_.isNull() && timeline_document_->sequencePlaying()},
      {QStringLiteral("scenePreviewCacheActive"), scene_preview_cache_active_},
      {QStringLiteral("prefetchPlayerSourceLoaded"),
       !prefetch_player_.source().isEmpty()},
      {QStringLiteral("videoSinkFrameRetained"), sink_frame_retained},
      {QStringLiteral("latestPendingFrameRetained"),
       latest_pending_frame_retained},
      {QStringLiteral("pendingSeekFrameRetained"), pending_seek_frame_retained},
      {QStringLiteral("retainedVideoFrameCount"),
       static_cast<int>(sink_frame_retained) +
           static_cast<int>(latest_pending_frame_retained) +
           static_cast<int>(pending_seek_frame_retained)},
      {QStringLiteral("frameProcessingInFlight"),
       frame_processing_watcher_.isRunning()},
      {QStringLiteral("videoGradeRenderSessionCacheCount"),
       video_grade_render_sessions_.size()},
      {QStringLiteral("activeGradeRenderSessionRetained"),
       active_video_grade_session_ != nullptr},
      {QStringLiteral("adjustmentGradeRenderSessionCount"),
       static_cast<int>(std::count_if(
           active_video_adjustment_grades_.cbegin(),
           active_video_adjustment_grades_.cend(),
           [](const auto& grade) { return grade.session != nullptr; }))},
      {QStringLiteral("sharedProcessedFrameCacheEntries"),
       static_cast<qulonglong>(processed_frame_cache.entries)},
      {QStringLiteral("sharedProcessedFrameCacheBytes"),
       static_cast<qulonglong>(processed_frame_cache.resident_bytes)},
      {QStringLiteral("resourceReleaseGraceMs"),
       kInactivePlaybackResourceReleaseGraceMs},
  };
  if (frame_image_state_ != nullptr) {
    const auto image_diagnostics = frame_image_state_->resourceDiagnostics();
    for (auto image_diagnostic = image_diagnostics.cbegin();
         image_diagnostic != image_diagnostics.cend();
         ++image_diagnostic) {
      diagnostics.insert(image_diagnostic.key(), image_diagnostic.value());
    }
  }
  return diagnostics;
}

QVariantMap TimelinePlaybackController::frameProcessingMetrics() const {
  return frame_processing_metrics_;
}

QVariantMap TimelinePlaybackController::colorCacheDiagnostics() const {
  auto metrics = timeline_video_grade_cache_metrics();
  auto& cache = parsed_cube_lut_cache();
  const std::lock_guard lock(cache.mutex);
  metrics.insert(QStringLiteral("creativeLutEntries"),
                 static_cast<qulonglong>(cache.entries.size()));
  metrics.insert(QStringLiteral("creativeLutAccountedPayloadBytes"),
                 static_cast<qulonglong>(cache.resident_bytes));
  metrics.insert(QStringLiteral("creativeLutPayloadBudgetBytes"),
                 static_cast<qulonglong>(kParsedCubeLutCacheBudgetBytes));
  metrics.insert(QStringLiteral("creativeLutEntryLimit"),
                 static_cast<qulonglong>(kParsedCubeLutCacheEntryLimit));
  metrics.insert(QStringLiteral("creativeLutHits"),
                 static_cast<qulonglong>(cache.hits));
  metrics.insert(QStringLiteral("creativeLutMisses"),
                 static_cast<qulonglong>(cache.misses));
  metrics.insert(QStringLiteral("creativeLutEvictions"),
                 static_cast<qulonglong>(cache.evictions));
  return metrics;
}

int TimelinePlaybackController::droppedFrameCount() const {
  return dropped_frame_count_;
}

void TimelinePlaybackController::resetPlaybackDiagnostics() {
  playback_diagnostics_.clear();
  frame_processing_metrics_.clear();
  dropped_frame_count_ = 0;
  playback_sample_clock_.invalidate();
  last_playback_sample_position_ms_ = -1;
  emit stateChanged();
}

void TimelinePlaybackController::recordPlaybackTimingSample(
    qint64 position_delta_ms,
    qint64 wall_delta_ms) {
  if (timeline_document_.isNull() || !timeline_document_->sequencePlaying() ||
      wall_delta_ms < 20) {
    return;
  }
  const auto timing_lag =
      wall_delta_ms >= 50 && position_delta_ms < wall_delta_ms * 3 / 5;
  if (preview_quality_mode_ == QStringLiteral("auto")) {
    if (timing_lag) {
      applyAdaptivePlaybackPressure(QStringLiteral("decoded frames are behind"), false);
    } else {
      recordAdaptiveHealthySample();
    }
  }
  if (!timing_lag) {
    return;
  }
  const auto frame_duration_ms =
      1000.0 / qMax(1.0, timeline_document_->framesPerSecond());
  const auto estimated = qMax(
      1,
      qRound((static_cast<double>(wall_delta_ms - position_delta_ms)) /
             frame_duration_ms));
  publishPlaybackDiagnostic(QStringLiteral("timing-lag"), estimated);
}

void TimelinePlaybackController::applyAdaptivePlaybackPressure(
    const QString& reason,
    bool severe) {
  if (preview_quality_mode_ != QStringLiteral("auto") ||
      timeline_document_.isNull() || !timeline_document_->sequencePlaying()) {
    return;
  }
  ++adaptive_slow_sample_count_;
  adaptive_healthy_sample_count_ = 0;
  auto next_level = adaptive_detail_level_;
  if (severe || adaptive_slow_sample_count_ >= 3) {
    next_level = QStringLiteral("quarter");
  } else if (adaptive_detail_level_ == QStringLiteral("full")) {
    next_level = QStringLiteral("half");
  }
  const auto normalized_reason = reason.simplified().left(160);
  if (next_level == adaptive_detail_level_ &&
      normalized_reason == adaptive_quality_reason_) {
    return;
  }
  adaptive_detail_level_ = next_level;
  adaptive_quality_reason_ = normalized_reason;
  emit stateChanged();
}

void TimelinePlaybackController::recordAdaptiveHealthySample() {
  if (preview_quality_mode_ != QStringLiteral("auto") ||
      adaptive_detail_level_ == QStringLiteral("full")) {
    adaptive_slow_sample_count_ = 0;
    return;
  }
  adaptive_slow_sample_count_ = 0;
  ++adaptive_healthy_sample_count_;
  if (adaptive_healthy_sample_count_ < 12) {
    return;
  }
  adaptive_healthy_sample_count_ = 0;
  adaptive_detail_level_ = adaptive_detail_level_ == QStringLiteral("quarter")
                               ? QStringLiteral("half")
                               : QStringLiteral("full");
  if (adaptive_detail_level_ == QStringLiteral("full")) {
    adaptive_quality_reason_.clear();
  } else {
    adaptive_quality_reason_ = QStringLiteral("playback load is recovering");
  }
  emit stateChanged();
}

void TimelinePlaybackController::resetAdaptivePlaybackDetail() {
  adaptive_slow_sample_count_ = 0;
  adaptive_healthy_sample_count_ = 0;
  adaptive_quality_reason_.clear();
  adaptive_detail_level_ = QStringLiteral("full");
}

void TimelinePlaybackController::publishPlaybackDiagnostic(
    const QString& trigger,
    int dropped_frames,
    const QString& detail) {
  if (timeline_document_.isNull()) {
    return;
  }
  const auto clip = timeline_document_->programClip();
  const auto media_online = clip.value(QStringLiteral("mediaOnline"), true).toBool();
  const auto effects = clip.value(QStringLiteral("effectStack")).toList();
  const auto source_path = clip.value(QStringLiteral("filePath")).toString();
  QString cause;
  QString summary;
  QString action;
  QString affected_detail;
  if (!media_online) {
    cause = QStringLiteral("storage");
    summary = QStringLiteral("Source media is offline or the storage volume disconnected.");
    action = QStringLiteral("Open Batch Relink and reconnect this source before playback.");
    affected_detail = source_path;
  } else if (using_render_cache_) {
    cause = QStringLiteral("cache");
    summary = QStringLiteral("The active render-cache range could not feed frames in time.");
    action = QStringLiteral("Clear and rebuild the preview cache for this range.");
    affected_detail = timeline_document_->renderCachePlaybackSourceUrl();
  } else if (!effects.isEmpty()) {
    cause = QStringLiteral("effect");
    const auto effect_name = effects.first().toString();
    summary = QStringLiteral("The active clip effect stack is the most likely playback bottleneck.");
    action = QStringLiteral("Temporarily bypass '%1' or render a preview cache.").arg(effect_name);
    affected_detail = effect_name;
  } else if (trigger == QStringLiteral("decoder") || !error_string_.isEmpty()) {
    cause = QStringLiteral("decoder");
    summary = QStringLiteral("The decoder failed while reading the active clip.");
    action = QStringLiteral("Retry playback; if it repeats, transcode or relink this source.");
    affected_detail = detail;
  } else if (bufferProgress() < 0.25 || trigger == QStringLiteral("stalled")) {
    cause = QStringLiteral("storage");
    summary = QStringLiteral("Media buffering fell behind the sequence clock.");
    action = QStringLiteral("Use a proxy or move this source to faster local storage.");
    affected_detail = source_path;
  } else {
    cause = QStringLiteral("decoder");
    summary = QStringLiteral("Decoded frames arrived too slowly for the sequence rate.");
    action = QStringLiteral("Use Auto or Proxy quality, or transcode the active source.");
    affected_detail = source_path;
  }
  dropped_frame_count_ += qMax(1, dropped_frames);
  playback_diagnostics_ = QVariantMap{
      {QStringLiteral("schema"), QStringLiteral("timeline-playback-diagnostics:v1")},
      {QStringLiteral("cause"), cause},
      {QStringLiteral("summary"), summary},
      {QStringLiteral("action"), action},
      {QStringLiteral("detail"), affected_detail},
      {QStringLiteral("clipId"), clip.value(QStringLiteral("clipId"))},
      {QStringLiteral("clipLabel"), clip.value(QStringLiteral("displayLabel"))},
      {QStringLiteral("trackLabel"), clip.value(QStringLiteral("trackLabel"))},
      {QStringLiteral("sourcePath"), source_path},
      {QStringLiteral("sequenceSeconds"), timeline_document_->sequencePlayheadSeconds()},
      {QStringLiteral("decoder"), QStringLiteral("Qt Multimedia / platform decoder")},
      {QStringLiteral("effectiveQuality"), effectivePreviewQuality()},
      {QStringLiteral("bufferProgress"), bufferProgress()},
      {QStringLiteral("droppedFrameCount"), dropped_frame_count_},
      {QStringLiteral("recoverable"), true},
  };
  emit stateChanged();
}

bool TimelinePlaybackController::runtimeRecoveryBusy() const {
  return runtime_recovery_busy_;
}

QString TimelinePlaybackController::runtimeRecoveryKind() const {
  return runtime_recovery_kind_;
}

QString TimelinePlaybackController::runtimeRecoveryStatus() const {
  return runtime_recovery_status_;
}

int TimelinePlaybackController::runtimeRecoveryCount() const {
  return runtime_recovery_count_;
}

QString TimelinePlaybackController::processedFrameSource() const {
  if (!processed_frame_available_) {
    return {};
  }
  return QStringLiteral("image://timeline-playback/frame?revision=%1")
      .arg(frame_source_revision_);
}

qint64 TimelinePlaybackController::processedFrameTimestampUs() const {
  return processed_frame_timestamp_us_;
}

bool TimelinePlaybackController::processedFrameTimestampEstimated() const {
  return processed_frame_timestamp_estimated_;
}

bool TimelinePlaybackController::stillPhotoGradePreviewActive() const {
  return still_photo_grade_preview_active_ && processed_frame_available_;
}

QString TimelinePlaybackController::colorGradeError() const {
  return color_grade_error_;
}

QString TimelinePlaybackController::colorSignalProcessingWarning() const {
  return color_signal_processing_warning_;
}

QVariantMap TimelinePlaybackController::colorSignalMetadata() const {
  return color_signal_metadata_;
}

QQuickImageProvider* TimelinePlaybackController::createImageProvider() {
  return new TimelinePlaybackImageProvider(frame_image_state_);
}

bool TimelinePlaybackController::sourceCropPreviewEnabled() const {
  return source_crop_preview_enabled_;
}

void TimelinePlaybackController::setSourceCropPreviewEnabled(bool enabled) {
  if (source_crop_preview_enabled_ == enabled) {
    return;
  }
  source_crop_preview_enabled_ = enabled;
  syncToDocument();
}

void TimelinePlaybackController::setPreviewQualityMode(const QString& mode) {
  const auto normalized = mode.trimmed().toLower();
  const auto accepted = normalized == QStringLiteral("full") ||
                        normalized == QStringLiteral("half") ||
                        normalized == QStringLiteral("quarter") ||
                        normalized == QStringLiteral("proxy") ||
                        normalized == QStringLiteral("clip")
                            ? normalized
                            : QStringLiteral("auto");
  if (preview_quality_mode_ == accepted) {
    return;
  }
  preview_quality_mode_ = accepted;
  resetAdaptivePlaybackDetail();
  syncToDocument();
}

void TimelinePlaybackController::setInteractionActive(bool active) {
  active = active && workspace_active_;
  if (interaction_active_ == active) {
    return;
  }
  interaction_active_ = active;
  if (!interaction_active_ && native_paused_preroll_active_ &&
      native_paused_preroll_seek_issued_) {
    // The decoder keeps moving during the gesture. Always seek once more to
    // freeze the exact final target before completing or handing off Play.
    native_paused_preroll_retarget_pending_ = true;
    native_paused_preroll_seek_acknowledged_ = false;
    native_paused_preroll_ack_position_ms_ = -1;
  }
  syncToDocument();
}

void TimelinePlaybackController::attachVideoOutput(QObject* output) {
  const auto output_changed = video_frame_item_.data() != qobject_cast<QQuickItem*>(output);
  if (output_changed) {
    const auto preroll_needs_restart = native_paused_preroll_active_ ||
        native_paused_preroll_resume_pending_;
    cancelNativePausedPreroll();
    native_paused_preroll_resume_pending_ = preroll_needs_restart;
    invalidateFrameProcessing();
  }
  video_frame_item_ = qobject_cast<QQuickItem*>(output);
  if (!video_frame_item_.isNull() && active_program_clip_id_.isEmpty()) {
    syncToDocument();
  }
  if (video_frame_item_.isNull()) {
    clearProcessedFrame();
  } else {
    if (awaiting_media_source_ && pending_position_ms_ >= 0 &&
        tryStartPendingNativePausedPreroll()) {
      return;
    }
    if (!awaiting_media_source_ &&
        (awaiting_position_sync_ || native_paused_preroll_resume_pending_) &&
        !timeline_document_.isNull() &&
        !timeline_document_->sequencePlaying() && mediaReady()) {
      const auto target_position_ms = nativePausedTargetPositionMs();
      if (startNativePausedPreroll(target_position_ms)) {
        return;
      }
    }
    if (native_paused_preroll_resume_pending_ ||
        (awaiting_media_source_ && pending_position_ms_ >= 0)) {
      armNativePausedPrerollOutputRetry();
    }
    if (output_changed && !native_paused_preroll_resume_pending_ &&
        !awaiting_media_source_ &&
        !awaiting_position_sync_ && canReplayCachedVideoFrame()) {
      // Asynchronous QML can attach after the decoder has delivered its paused frame.
      handleVideoFrame(video_sink_.videoFrame());
    }
  }
  if (!video_frame_item_.isNull()) {
    syncScenePreviewFrame();
    syncStillPhotoGradePreview(current_program_clip_);
  }
}

bool TimelinePlaybackController::workspaceActive() const {
  return workspace_active_;
}

bool TimelinePlaybackController::playbackStartPending() const {
  return native_paused_preroll_play_pending_;
}

void TimelinePlaybackController::setWorkspaceActive(bool active) {
  if (workspace_active_ == active)
    return;
  const auto suspend_generation = ++playback_suspend_generation_;
  // Gate callbacks before pausing: pauseSequence emits playbackStateChanged.
  // Keep the document and playhead, but never synchronize or grade a hidden
  // workspace. Native decode resources are released after a short grace period
  // so quick workspace switches can reuse the already loaded source.
  workspace_active_ = active;
  if (!active) {
    native_paused_preroll_resume_pending_ = native_paused_preroll_active_ ||
        (native_media_enabled_ && !current_source_url_.isEmpty() &&
         !processed_frame_available_ && !scene_preview_cache_active_);
    cancelNativePausedPreroll(false);
    interaction_active_ = false;
    shuttle_rate_ = 0.0;
    play_when_ready_ = false;
    pending_position_ms_ = -1;
    awaited_position_ms_ = -1;
    awaiting_position_sync_ = false;
    if (native_paused_preroll_resume_pending_) {
      awaited_position_ms_ = nativePausedTargetPositionMs();
      awaiting_position_sync_ = awaited_position_ms_ >= 0;
    }
    invalidateFrameProcessing();
    stopGapPlayback();
    prefetch_timer_.stop();
    prefetch_plan_.clear();
    prefetched_source_url_.clear();
    if (!timeline_document_.isNull())
      timeline_document_->pauseSequence();
    if (native_media_enabled_) {
      audio_output_.setOutputSuspended(true);
      if (!native_audio_buffer_output_detached_) {
        player_.setAudioBufferOutput(nullptr);
        native_audio_buffer_output_detached_ = true;
      }
      player_.pause();
      prefetch_player_.stop();
      prefetch_player_.setSource(QUrl());
    }
    // The grace is a minimum retention period. A coarse timer may expire
    // before it, discarding the decoder during a quick workspace return.
    QTimer::singleShot(
        kInactivePlaybackResourceReleaseGraceMs,
        Qt::PreciseTimer,
        this,
        [this, suspend_generation]() {
          releaseSuspendedPlaybackResources(suspend_generation);
        });
  } else {
    syncToDocument();
    if (native_paused_preroll_resume_pending_ &&
        !timeline_document_.isNull() &&
        !timeline_document_->sequencePlaying()) {
      if (!startNativePausedPreroll(nativePausedTargetPositionMs())) {
        armNativePausedPrerollOutputRetry();
      }
    }
    schedulePrefetchRefresh();
    if (!awaiting_media_source_ && !awaiting_position_sync_ &&
        canReplayCachedVideoFrame()) {
      handleVideoFrame(video_sink_.videoFrame());
    }
  }
  emit stateChanged();
}

void TimelinePlaybackController::releaseSuspendedPlaybackResources(
    quint64 generation) {
  ++playback_resource_release_attempt_count_;
  if (workspace_active_ || generation != playback_suspend_generation_) {
    return;
  }

  invalidateFrameProcessing();
  stopGapPlayback();
  prefetch_timer_.stop();
  prefetch_plan_.clear();
  prefetched_source_url_.clear();
  scene_preview_frame_key_.clear();
  scene_preview_cache_active_ = false;

  clearProcessedFrame();
  video_sink_.setVideoFrame(QVideoFrame{});
  active_video_grade_session_.reset();
  active_video_adjustment_grades_.clear();
  video_grade_render_sessions_.clear();
  active_video_frame_color_key_.clear();
  active_video_foreground_cutout_key_.clear();

  player_.pause();
  player_.stop();
  player_.setSource(QUrl());
  prefetch_player_.stop();
  prefetch_player_.setSource(QUrl());
  audio_output_.resetForDiscontinuity();

  pending_position_ms_ = -1;
  awaited_position_ms_ = -1;
  awaiting_media_source_ = false;
  awaiting_position_sync_ = false;
  play_when_ready_ = false;
  playback_hibernated_ = true;
  ++playback_resource_release_completed_count_;
  emit stateChanged();
}

QSize TimelinePlaybackController::frameTargetSize() const {
  if (!video_frame_item_.isNull() && video_frame_item_->width() > 0.0 &&
      video_frame_item_->height() > 0.0) {
    const auto window = video_frame_item_->window();
    const auto device_scale = window == nullptr ? 1.0 : window->devicePixelRatio();
    const auto render_scale = device_scale * effectivePreviewScale();
    return QSize(
        qMax(1, qRound(video_frame_item_->width() * render_scale)),
        qMax(1, qRound(video_frame_item_->height() * render_scale)));
  }
  return QSize(kFallbackPreviewWidth, kFallbackPreviewHeight);
}

void TimelinePlaybackController::dispatchNativeVideoFrame(
    const QVideoFrame& frame) {
  const auto decoder_frame_generation =
      native_paused_preroll_frame_generation_.load(
          std::memory_order_acquire);
  const auto callback_work_generation =
      latest_frame_work_generation_->load(std::memory_order_relaxed);
  const auto deliver_frame =
      [this, frame, decoder_frame_generation,
       callback_work_generation]() {
        if (callback_work_generation !=
            latest_frame_work_generation_->load(std::memory_order_relaxed)) {
          // A seek, source change, output detach, or suspension invalidated
          // this decoder callback while it waited in the controller queue.
          return;
        }
        handleVideoFrame(frame, decoder_frame_generation);
      };
  if (QThread::currentThread() == thread()) {
    deliver_frame();
    return;
  }
  QMetaObject::invokeMethod(
      this, deliver_frame, Qt::QueuedConnection);
}

void TimelinePlaybackController::handleVideoFrame(
    const QVideoFrame& frame,
    quint64 decoder_frame_generation) {
  if (decoder_frame_generation != 0U &&
      decoder_frame_generation !=
          native_paused_preroll_frame_generation_.load(
              std::memory_order_acquire)) {
    // videoFrameChanged can already be queued in the multimedia pipeline when
    // a scrub, source switch, or hibernation invalidates the corresponding
    // decoder output generation.
    return;
  }
  if (!workspace_active_ && playback_hibernated_ && frame.isValid()) {
    // A queued decoder callback can arrive after setSource() has released its
    // media. Do not let QVideoSink retain that late frame during hibernation.
    video_sink_.setVideoFrame(QVideoFrame{});
    return;
  }
  const auto is_scene_preview_frame = injecting_scene_preview_frame_;
  if (!workspace_active_ || (!native_media_enabled_ && !is_scene_preview_frame) ||
      video_frame_item_.isNull() || !frame.isValid() ||
      (scene_preview_cache_active_ && !is_scene_preview_frame) ||
      (awaiting_media_source_ && !is_scene_preview_frame)) {
    return;
  }
  auto start_user_play_after_frame = false;
  if (native_paused_preroll_active_ && !is_scene_preview_frame) {
    const auto preroll_is_current =
        workspace_active_ && timeline_document_ != nullptr &&
        !timeline_document_->sequencePlaying() &&
        current_source_url_ == native_paused_preroll_source_url_ &&
        !current_source_url_.isEmpty() &&
        player_.source() == QUrl(native_paused_preroll_source_url_);
    if (!preroll_is_current) {
      cancelNativePausedPreroll();
      return;
    }
    const auto latest_target_ms = nativePausedTargetPositionMs();
    if (latest_target_ms < 0) {
      cancelNativePausedPreroll();
      return;
    }
    if (latest_target_ms != native_paused_preroll_target_ms_) {
      native_paused_preroll_target_ms_ = latest_target_ms;
      native_paused_preroll_seek_acknowledged_ = false;
      native_paused_preroll_ack_position_ms_ = -1;
      if (native_paused_preroll_seek_issued_) {
        native_paused_preroll_retarget_pending_ = true;
      }
      // Keep long drags alive while retaining a timeout for a stalled gesture.
      if (interaction_active_) {
        native_paused_preroll_timer_.start();
      }
    }
    if (!native_paused_preroll_seek_issued_) {
      if (!native_paused_preroll_first_frame_seen_) {
        native_paused_preroll_first_frame_seen_ = true;
        native_paused_preroll_status_ = QStringLiteral("first decoder frame received");
      }
      queueNativePausedPrerollSeek(native_paused_preroll_generation_);
      // The initial decoder frame only warms AVFoundation. It is never
      // published as the paused Program frame.
      return;
    }
    if (native_paused_preroll_retarget_pending_) {
      queueNativePausedPrerollSeek(native_paused_preroll_generation_);
      return;
    }

    const auto frame_timestamp_ms = frame.startTime() < 0
        ? -1
        : frame.startTime() / 1000;
    const auto frame_position_ms = player_.position();
    if (frame_timestamp_ms >= 0) {
      if (qAbs(frame_timestamp_ms - native_paused_preroll_target_ms_) >
          kNativePausedPrerollFrameToleranceMs) {
        return;
      }
    } else {
      if (!native_paused_preroll_seek_acknowledged_) {
        observeNativePausedPrerollSeekPosition(frame_position_ms);
      }
      const auto current_frame_generation =
          native_paused_preroll_frame_generation_.load(
              std::memory_order_acquire);
      if (!native_paused_preroll_seek_acknowledged_ ||
          decoder_frame_generation == 0U ||
          decoder_frame_generation != current_frame_generation ||
          frame_position_ms <= native_paused_preroll_target_ms_ ||
          frame_position_ms - native_paused_preroll_target_ms_ >
              kNativePausedPrerollFrameToleranceMs) {
        return;
      }
    }
    // With unknown PTS, handleVideoFrame records the backend's acknowledged
    // frame position as an estimated source timestamp. During a scrub, publish
    // matching target frames but keep the silent decoder warm; release reissues
    // the final target before completing the preroll.
    native_paused_preroll_last_target_timestamp_us_ = frame.startTime() >= 0
        ? frame.startTime()
        : frame_position_ms * 1000;
    if (interaction_active_) {
      native_paused_preroll_status_ =
          QStringLiteral("live scrub target frame received");
    } else {
      start_user_play_after_frame = native_paused_preroll_play_pending_;
      finishNativePausedPreroll(false);
      awaiting_media_source_ = false;
      awaiting_position_sync_ = false;
      awaited_position_ms_ = -1;
      pending_position_ms_ = -1;
    }
  }
  if (awaiting_position_sync_ && !is_scene_preview_frame) {
    pending_seek_video_frame_ = frame;
    pending_seek_frame_generation_ = position_sync_generation_;
    return;
  }
  const auto frame_timestamp_us = frame.startTime();
  const auto source_timestamp_estimated = frame_timestamp_us < 0;
  auto source_timestamp_us = frame_timestamp_us;
  if (source_timestamp_estimated) {
    // Prefer the decoder clock for native frames; use the document clock only
    // when no matching native source is loaded (including injected tests).
    const auto native_source_clock_available = native_media_enabled_ &&
        mediaReady() && !current_source_url_.isEmpty() &&
        current_source_url_ == preferredSourceUrl() &&
        player_.source() == QUrl(current_source_url_);
    const auto source_clock_seconds = timeline_document_.isNull()
        ? 0.0
        : native_source_clock_available
              ? static_cast<double>(player_.position()) / 1000.0
              : timeline_document_->programClipPlayheadSeconds();
    source_timestamp_us = std::isfinite(source_clock_seconds)
        ? qRound64(qMax(0.0, source_clock_seconds) * 1'000'000.0)
        : 0;
  }
  auto signal_metadata = using_render_cache_
                             ? QVariantMap{}
                             : video_frame_color_metadata(frame.surfaceFormat());
  if (!signal_metadata.isEmpty()) {
    signal_metadata.insert(
        QStringLiteral("clipId"),
        timeline_document_ != nullptr
            ? timeline_document_->currentClip()
                  .value(QStringLiteral("clipId"))
                  .toString()
            : QString{});
    auto previous_signal_metadata = color_signal_metadata_;
    const auto previous_conversion_status =
        previous_signal_metadata.take(QStringLiteral("conversionStatus"));
    const auto previous_conversion_warning =
        previous_signal_metadata.take(QStringLiteral("conversionWarning"));
    if (previous_signal_metadata == signal_metadata) {
      if (previous_conversion_status.isValid()) {
        signal_metadata.insert(QStringLiteral("conversionStatus"),
                               previous_conversion_status);
      }
      if (previous_conversion_warning.isValid()) {
        signal_metadata.insert(QStringLiteral("conversionWarning"),
                               previous_conversion_warning);
      }
    }
    if (timeline_document_ != nullptr) {
      timeline_document_->setPlaybackColorSignalMetadataForCurrentClip(
          signal_metadata.value(QStringLiteral("clipId")).toString(),
          signal_metadata);
    }
    setColorSignalMetadata(std::move(signal_metadata));
  } else {
    if (timeline_document_ != nullptr) {
      timeline_document_->setPlaybackColorSignalMetadataForCurrentClip(
          QString{}, {});
    }
    setColorSignalMetadata({});
  }
  // Ordinary frame arrival does not invalidate work already in progress. When
  // processing is slower than decoding, present that frame and then the newest
  // pending one instead of cancelling every frame before it can be displayed.
  const auto work_generation =
      latest_frame_work_generation_->load(std::memory_order_relaxed);
  if (frame_processing_active_) {
    const auto replaced_pending = has_pending_video_frame_;
    latest_pending_frame_ = frame;
    pending_frame_source_timestamp_us_ = source_timestamp_us;
    pending_frame_source_timestamp_estimated_ = source_timestamp_estimated;
    pending_frame_is_scene_preview_ = is_scene_preview_frame;
    pending_scene_preview_identity_ = is_scene_preview_frame
                                          ? scene_preview_frame_key_
                                          : QString{};
    pending_frame_generation_ = video_frame_generation_;
    pending_work_generation_ = work_generation;
    has_pending_video_frame_ = true;
    if (replaced_pending) {
      ++dropped_frame_count_;
      emit stateChanged();
    }
    return;
  }
  startFrameProcessing(
      frame,
      video_frame_generation_,
      work_generation,
      source_timestamp_us,
      source_timestamp_estimated,
      is_scene_preview_frame,
      is_scene_preview_frame ? scene_preview_frame_key_ : QString{});
  if (start_user_play_after_frame && native_paused_preroll_play_pending_ &&
      workspace_active_ && !timeline_document_.isNull() &&
      !timeline_document_->sequencePlaying()) {
    native_paused_preroll_play_pending_ = false;
    play();
  }
}

void TimelinePlaybackController::reprocessCurrentPausedFrameIfNeeded() {
  if (!workspace_active_ || !native_media_enabled_ || timeline_document_.isNull() ||
      timeline_document_->sequencePlaying() || video_frame_item_.isNull() ||
      awaiting_media_source_ || awaiting_position_sync_ ||
      current_source_url_.isEmpty() || current_source_url_ != preferredSourceUrl()) {
    return;
  }
  if (has_pending_video_frame_ ||
      (frame_processing_active_ &&
       processing_frame_generation_ == video_frame_generation_)) {
    return;
  }

  const auto expected_position_ms = qRound64(
      (using_render_cache_
           ? timeline_document_->sequencePlayheadSeconds() -
                 render_cache_range_start_seconds_
           : timeline_document_->programClipPlayheadSeconds()) *
      1000.0);
  if (qAbs(player_.position() - expected_position_ms) > kSeekToleranceMs) {
    return;
  }

  const auto frame = video_sink_.videoFrame();
  if (!frame.isValid()) {
    const auto published_frame_matches_position =
        processed_frame_available_ && processed_frame_timestamp_us_ >= 0 &&
        qAbs(processed_frame_timestamp_us_ / 1000 - expected_position_ms) <=
            kPositionSyncToleranceMs;
    if (!published_frame_matches_position) {
      // A paused backend can be loaded at the right position without ever
      // delivering its first QVideoSink frame. Reassert the same position
      // without entering the positionChanged handshake; a matching position
      // may produce no positionChanged notification, but can still decode.
      player_.setPosition(expected_position_ms);
    }
    return;
  }
  const auto timestamp_us = frame.startTime();
  if (timestamp_us >= 0 &&
      qAbs(timestamp_us / 1000 - player_.position()) >
          kPositionSyncToleranceMs) {
    return;
  }
  const auto timestamp_estimated = timestamp_us < 0;
  // Use the same clock selection as publication so unknown-PTS frames can be
  // deduplicated against the exact frame time captured at arrival.
  const auto native_source_clock_available = native_media_enabled_ &&
      mediaReady() && !current_source_url_.isEmpty() &&
      current_source_url_ == preferredSourceUrl() &&
      player_.source() == QUrl(current_source_url_);
  const auto source_clock_seconds = timeline_document_.isNull()
      ? 0.0
      : native_source_clock_available
            ? static_cast<double>(player_.position()) / 1000.0
            : timeline_document_->programClipPlayheadSeconds();
  const auto source_timestamp_us = timestamp_estimated
      ? std::isfinite(source_clock_seconds)
            ? qRound64(qMax(0.0, source_clock_seconds) * 1'000'000.0)
            : 0
      : timestamp_us;
  if (processed_frame_available_ &&
      processed_frame_timestamp_us_ == source_timestamp_us &&
      processed_frame_timestamp_estimated_ == timestamp_estimated) {
    return;
  }

  handleVideoFrame(frame);
}

qint64 TimelinePlaybackController::nativePausedTargetPositionMs() const {
  if (timeline_document_.isNull()) {
    return -1;
  }
  const auto source_position_seconds = using_render_cache_
      ? timeline_document_->sequencePlayheadSeconds() -
            render_cache_range_start_seconds_
      : timeline_document_->programClipPlayheadSeconds();
  return std::isfinite(source_position_seconds)
      ? qRound64(qMax(0.0, source_position_seconds) * 1000.0)
      : -1;
}

bool TimelinePlaybackController::canStartNativePausedPreroll(
    qint64 target_position_ms) const {
  const auto media_status = player_.mediaStatus();
  if (!native_media_enabled_ || !workspace_active_ || timeline_document_.isNull() ||
      timeline_document_->sequencePlaying() || scene_preview_cache_active_ ||
      target_position_ms < 0 || current_source_url_.isEmpty() ||
      player_.source() != QUrl(current_source_url_) || !mediaReady() ||
      (media_status != QMediaPlayer::LoadedMedia &&
       media_status != QMediaPlayer::BufferedMedia) ||
      !player_.isSeekable() || player_.duration() <= target_position_ms ||
      video_frame_item_.isNull()) {
    return false;
  }
  const auto* window = video_frame_item_->window();
  return window != nullptr && window->isExposed();
}

void TimelinePlaybackController::observeNativePausedPrerollSeekPosition(
    qint64 position_ms) {
  if (!native_paused_preroll_active_ ||
      !native_paused_preroll_seek_issued_ ||
      current_source_url_ != native_paused_preroll_source_url_ ||
      player_.source() != QUrl(native_paused_preroll_source_url_) ||
      native_paused_preroll_target_ms_ < 0 ||
      position_ms <= native_paused_preroll_target_ms_) {
    return;
  }

  const auto delta_ms = position_ms - native_paused_preroll_target_ms_;
  native_paused_preroll_ack_position_ms_ = position_ms;
  if (delta_ms > kNativePausedPrerollFrameToleranceMs) {
    // AVFoundation's public clock now proves that the requested seek finished,
    // but it advanced too far to truthfully timestamp an unknown-PTS frame.
    // Reissue the latest target while playback is still warming the decoder.
    native_paused_preroll_seek_acknowledged_ = false;
    native_paused_preroll_retarget_pending_ = true;
    native_paused_preroll_status_ =
        QStringLiteral("seek passed frame tolerance; retrying target");
    queueNativePausedPrerollSeek(native_paused_preroll_generation_);
    emit stateChanged();
    return;
  }

  if (!native_paused_preroll_seek_acknowledged_) {
    native_paused_preroll_seek_acknowledged_ = true;
    // Invalidate frame callbacks emitted before AVFoundation's live clock
    // advanced beyond its optimistic requested-position value. Only a later
    // QVideoSink emission can supply the unknown-PTS image for this seek.
    native_paused_preroll_frame_generation_.fetch_add(
        1U, std::memory_order_acq_rel);
    native_paused_preroll_status_ =
        QStringLiteral("native seek acknowledged past target");
    emit stateChanged();
  }
}

bool TimelinePlaybackController::startNativePausedPreroll(
    qint64 target_position_ms) {
  if (native_paused_preroll_active_) {
    const auto latest_target_ms = nativePausedTargetPositionMs();
    if (latest_target_ms >= 0 &&
        latest_target_ms != native_paused_preroll_target_ms_) {
      native_paused_preroll_target_ms_ = latest_target_ms;
      native_paused_preroll_seek_acknowledged_ = false;
      native_paused_preroll_ack_position_ms_ = -1;
      native_paused_preroll_retarget_pending_ =
          native_paused_preroll_seek_issued_;
    }
    return true;
  }
  ++native_paused_preroll_start_count_;
  const auto latest_target_position_ms = nativePausedTargetPositionMs();
  if (target_position_ms < 0 || latest_target_position_ms < 0) {
    native_paused_preroll_status_ = QStringLiteral("skipped: invalid target");
    return false;
  }
  target_position_ms = latest_target_position_ms;
  if (!canStartNativePausedPreroll(target_position_ms)) {
    if (!native_media_enabled_) {
      native_paused_preroll_status_ = QStringLiteral("skipped: native media disabled");
    } else if (!workspace_active_) {
      native_paused_preroll_status_ = QStringLiteral("skipped: workspace inactive");
    } else if (timeline_document_.isNull() || timeline_document_->sequencePlaying()) {
      native_paused_preroll_status_ = QStringLiteral("skipped: no paused document");
    } else if (scene_preview_cache_active_) {
      native_paused_preroll_status_ = QStringLiteral("skipped: scene preview active");
    } else if (current_source_url_.isEmpty() ||
               player_.source() != QUrl(current_source_url_)) {
      native_paused_preroll_status_ = QStringLiteral("skipped: source mismatch");
    } else if (!mediaReady() ||
               (player_.mediaStatus() != QMediaPlayer::LoadedMedia &&
                player_.mediaStatus() != QMediaPlayer::BufferedMedia)) {
      native_paused_preroll_status_ = QStringLiteral("skipped: media not loaded");
    } else if (!player_.isSeekable() ||
               player_.duration() <= target_position_ms) {
      native_paused_preroll_status_ = QStringLiteral("skipped: duration or seekability not ready");
    } else if (video_frame_item_.isNull()) {
      native_paused_preroll_status_ = QStringLiteral("skipped: video output missing");
    } else if (video_frame_item_->window() == nullptr) {
      native_paused_preroll_status_ = QStringLiteral("skipped: output has no window");
    } else {
      native_paused_preroll_status_ = QStringLiteral("skipped: output window not exposed");
      armNativePausedPrerollOutputRetry();
    }
    return false;
  }

  disarmNativePausedPrerollOutputRetry();
  native_paused_preroll_active_ = true;
  native_paused_preroll_first_frame_seen_ = false;
  native_paused_preroll_seek_issued_ = false;
  native_paused_preroll_seek_queued_ = false;
  native_paused_preroll_retarget_pending_ = false;
  native_paused_preroll_seek_acknowledged_ = false;
  native_paused_preroll_seek_count_ = 0;
  native_paused_preroll_target_ms_ = target_position_ms;
  native_paused_preroll_ack_position_ms_ = -1;
  native_paused_preroll_last_target_timestamp_us_ = -1;
  native_paused_preroll_elapsed_.restart();
  native_paused_preroll_last_seek_elapsed_ms_ = -1;
  native_paused_preroll_source_url_ = current_source_url_;
  native_paused_preroll_resume_pending_ = false;
  native_paused_preroll_status_ = QStringLiteral("playing first decoder frame");
  ++native_paused_preroll_generation_;
  native_paused_preroll_frame_generation_.fetch_add(
      1U, std::memory_order_acq_rel);

  pending_position_ms_ = -1;
  awaited_position_ms_ = -1;
  awaiting_media_source_ = false;
  awaiting_position_sync_ = false;
  clearPendingSeekFrame();
  invalidateFrameProcessing();
  video_sink_.setVideoFrame(QVideoFrame{});
  if (!native_audio_buffer_output_detached_) {
    audio_output_.setOutputSuspended(true);
    player_.setAudioBufferOutput(nullptr);
    native_audio_buffer_output_detached_ = true;
  } else {
    audio_output_.setOutputSuspended(true);
  }
  player_.setPlaybackRate(1.0);
  native_paused_preroll_timer_.start();
  player_.play();
  emit stateChanged();
  return true;
}

void TimelinePlaybackController::cancelNativePausedPreroll(
    bool clear_resume_request) {
  disarmNativePausedPrerollOutputRetry();
  native_paused_preroll_timer_.stop();
  const auto was_active = native_paused_preroll_active_;
  native_paused_preroll_active_ = false;
  native_paused_preroll_first_frame_seen_ = false;
  native_paused_preroll_seek_issued_ = false;
  native_paused_preroll_seek_queued_ = false;
  native_paused_preroll_retarget_pending_ = false;
  native_paused_preroll_play_pending_ = false;
  native_paused_preroll_seek_count_ = 0;
  native_paused_preroll_seek_acknowledged_ = false;
  native_paused_preroll_last_seek_elapsed_ms_ = -1;
  native_paused_preroll_target_ms_ = -1;
  native_paused_preroll_ack_position_ms_ = -1;
  native_paused_preroll_source_url_.clear();
  ++native_paused_preroll_generation_;
  native_paused_preroll_frame_generation_.fetch_add(
      1U, std::memory_order_acq_rel);
  if (clear_resume_request) {
    native_paused_preroll_resume_pending_ = false;
  }
  if (was_active && player_.playbackState() == QMediaPlayer::PlayingState &&
      (timeline_document_.isNull() || !timeline_document_->sequencePlaying())) {
    player_.pause();
  }
  if (was_active) {
    native_paused_preroll_status_ = QStringLiteral("cancelled");
  }
}

void TimelinePlaybackController::finishNativePausedPreroll(bool timed_out) {
  if (!native_paused_preroll_active_) {
    return;
  }
  const auto target_position_ms = native_paused_preroll_target_ms_;
  native_paused_preroll_timer_.stop();
  native_paused_preroll_active_ = false;
  native_paused_preroll_first_frame_seen_ = false;
  native_paused_preroll_seek_issued_ = false;
  native_paused_preroll_seek_queued_ = false;
  native_paused_preroll_retarget_pending_ = false;
  native_paused_preroll_seek_acknowledged_ = false;
  native_paused_preroll_target_ms_ = -1;
  native_paused_preroll_ack_position_ms_ = -1;
  native_paused_preroll_source_url_.clear();
  ++native_paused_preroll_generation_;
  native_paused_preroll_frame_generation_.fetch_add(
      1U, std::memory_order_acq_rel);
  native_paused_preroll_status_ = timed_out
      ? QStringLiteral("timed out")
      : QStringLiteral("decoded target frame");
  if (player_.playbackState() == QMediaPlayer::PlayingState) {
    player_.pause();
  }
  if (timed_out) {
    native_paused_preroll_play_pending_ = false;
    ++native_paused_preroll_timeout_count_;
    invalidateFrameProcessing();
    video_sink_.setVideoFrame(QVideoFrame{});
    clearPendingSeekFrame();
    pending_position_ms_ = -1;
    awaited_position_ms_ = target_position_ms;
    awaiting_position_sync_ = target_position_ms >= 0;
    awaiting_media_source_ = false;
  }
  emit stateChanged();
}

void TimelinePlaybackController::attachAudioBufferOutputForPlayback() {
  if (!native_media_enabled_) {
    return;
  }
  if (native_audio_buffer_output_detached_) {
    audio_output_.setOutputSuspended(true);
    player_.setAudioBufferOutput(audio_output_.bufferOutput());
    native_audio_buffer_output_detached_ = false;
  }
  if (!audio_output_.outputSuspended()) {
    return;
  }
  const auto source_url = current_source_url_;
  const auto resume_generation = ++native_audio_output_resume_generation_;
  QTimer::singleShot(0, this, [this, resume_generation, source_url]() {
    if (native_audio_output_resume_generation_ != resume_generation ||
        native_audio_buffer_output_detached_ || !workspace_active_ ||
        timeline_document_.isNull() ||
        !timeline_document_->sequencePlaying() ||
        player_.playbackState() != QMediaPlayer::PlayingState ||
        current_source_url_ != source_url ||
        player_.source() != QUrl(source_url)) {
      return;
    }
    audio_output_.setOutputSuspended(false);
  });
}

void TimelinePlaybackController::queueNativePausedPrerollSeek(
    quint64 generation) {
  if (!native_paused_preroll_active_ ||
      native_paused_preroll_seek_queued_ ||
      (native_paused_preroll_seek_count_ > 0 &&
       !native_paused_preroll_retarget_pending_)) {
    return;
  }
  native_paused_preroll_seek_queued_ = true;
  const auto source_url = native_paused_preroll_source_url_;
  auto queue_delay_ms = 0;
  if (native_paused_preroll_seek_count_ > 0 &&
      native_paused_preroll_last_seek_elapsed_ms_ >= 0) {
    const auto elapsed_since_last_seek =
        native_paused_preroll_elapsed_.elapsed() -
        native_paused_preroll_last_seek_elapsed_ms_;
    queue_delay_ms = qMax(
        0,
        kNativePausedPrerollRetargetIntervalMs -
            static_cast<int>(elapsed_since_last_seek));
    const auto remaining_preroll_ms = native_paused_preroll_timer_.remainingTime();
    if (remaining_preroll_ms > 0) {
      // Flush the last coalesced target before the current idle deadline.
      queue_delay_ms = qMin(
          queue_delay_ms,
          qMax(0, static_cast<int>(remaining_preroll_ms - 1)));
    }
  }
  QTimer::singleShot(queue_delay_ms, this, [this, generation, source_url]() {
    if (!native_paused_preroll_active_ ||
        native_paused_preroll_generation_ != generation ||
        !workspace_active_ || timeline_document_.isNull() ||
        timeline_document_->sequencePlaying() ||
        current_source_url_ != source_url || player_.source() != QUrl(source_url) ||
        !native_paused_preroll_first_frame_seen_) {
      if (native_paused_preroll_generation_ == generation) {
        native_paused_preroll_seek_queued_ = false;
      }
      return;
    }
    const auto latest_target_ms = nativePausedTargetPositionMs();
    if (!canStartNativePausedPreroll(latest_target_ms)) {
      native_paused_preroll_seek_queued_ = false;
      finishNativePausedPreroll(true);
      return;
    }
    if (native_paused_preroll_seek_count_ > 0 &&
        !native_paused_preroll_retarget_pending_) {
      native_paused_preroll_seek_queued_ = false;
      return;
    }
    native_paused_preroll_target_ms_ = latest_target_ms;
    native_paused_preroll_seek_issued_ = true;
    native_paused_preroll_seek_queued_ = false;
    native_paused_preroll_retarget_pending_ = false;
    ++native_paused_preroll_seek_count_;
    native_paused_preroll_seek_acknowledged_ = false;
    native_paused_preroll_ack_position_ms_ = -1;
    native_paused_preroll_last_seek_elapsed_ms_ =
        native_paused_preroll_elapsed_.elapsed();
    native_paused_preroll_status_ = native_paused_preroll_seek_count_ == 1
        ? QStringLiteral("seek issued while prerolling")
        : QStringLiteral("latest scrub target issued while prerolling");
    clearPendingSeekFrame();
    native_paused_preroll_frame_generation_.fetch_add(
        1U, std::memory_order_acq_rel);
    // AVFoundation can reset a seek issued from LoadedMedia while it is still
    // constructing its paused item. The first playing frame marks the end of
    // that construction; a single seek while warm-up playback is active then
    // lands on the requested frame without advancing the document clock.
    player_.setPosition(latest_target_ms);
    emit stateChanged();
  });
}

bool TimelinePlaybackController::tryStartPendingNativePausedPreroll() {
  const auto pending_source_load = awaiting_media_source_ &&
      pending_position_ms_ >= 0;
  const auto pending_workspace_resume = native_paused_preroll_resume_pending_;
  if (!workspace_active_ || timeline_document_.isNull() ||
      timeline_document_->sequencePlaying() ||
      (!pending_source_load && !pending_workspace_resume) ||
      scene_preview_cache_active_ ||
      current_source_url_.isEmpty() ||
      player_.source() != QUrl(current_source_url_) ||
      (player_.mediaStatus() != QMediaPlayer::LoadedMedia &&
       player_.mediaStatus() != QMediaPlayer::BufferedMedia) ||
      !player_.isSeekable() ||
      player_.duration() <= (pending_source_load
                                 ? pending_position_ms_
                                 : nativePausedTargetPositionMs())) {
    return false;
  }
  const auto target_position_ms = pending_source_load
      ? pending_position_ms_
      : nativePausedTargetPositionMs();
  if (!startNativePausedPreroll(target_position_ms)) {
    return false;
  }
  if (pending_source_load) {
    pending_position_ms_ = -1;
  }
  return true;
}

void TimelinePlaybackController::armNativePausedPrerollOutputRetry() {
  if (native_paused_preroll_output_connection_ || video_frame_item_.isNull()) {
    return;
  }
  auto* const window = video_frame_item_->window();
  if (window == nullptr || window->isExposed()) {
    return;
  }
  native_paused_preroll_output_connection_ = connect(
      window,
      &QQuickWindow::frameSwapped,
      this,
      [this, window]() {
        if (video_frame_item_.isNull() || video_frame_item_->window() != window) {
          disarmNativePausedPrerollOutputRetry();
          return;
        }
        if (tryStartPendingNativePausedPreroll()) {
          emit stateChanged();
          return;
        }
        if (!awaiting_media_source_ &&
            !native_paused_preroll_resume_pending_) {
          disarmNativePausedPrerollOutputRetry();
        }
      });
}

void TimelinePlaybackController::disarmNativePausedPrerollOutputRetry() {
  if (!native_paused_preroll_output_connection_) {
    return;
  }
  QObject::disconnect(native_paused_preroll_output_connection_);
  native_paused_preroll_output_connection_ = {};
}

void TimelinePlaybackController::startFrameProcessing(
    const QVideoFrame& frame,
    quint64 generation,
    int work_generation,
    qint64 source_timestamp_us,
    bool source_timestamp_estimated,
    bool scene_preview_frame,
    const QString& scene_preview_identity) {
  processing_frame_generation_ = generation;
  processing_work_generation_ = work_generation;
  processing_frame_source_timestamp_us_ = source_timestamp_us;
  processing_frame_source_timestamp_estimated_ = source_timestamp_estimated;
  processing_frame_is_scene_preview_ = scene_preview_frame;
  processing_scene_preview_identity_ = scene_preview_identity;
  processing_video_input_transform_ = active_video_input_transform_;
  processing_video_frame_color_key_ = active_video_frame_color_key_;
  processing_video_foreground_cutout_key_ = active_video_foreground_cutout_key_;
  frame_processing_active_ = true;
  QVector<VideoFrameAdjustmentGrade> adjustment_grades;
  adjustment_grades.reserve(active_video_adjustment_grades_.size());
  for (const auto& adjustment : active_video_adjustment_grades_) {
    adjustment_grades.push_back({
        adjustment.clip_id,
        adjustment.recipe_json,
        adjustment.opacity,
        adjustment.animated_parameters,
        adjustment.session,
    });
  }
  const auto target_size = frameTargetSize();
  const auto color_settings = VideoFrameColorSettings{
      .clip_id = timeline_document_.isNull()
                     ? QString{}
                     : active_program_clip_id_,
      .source_identity = scene_preview_frame
          ? active_video_source_identity_ + QStringLiteral("|scene-preview:") +
                scene_preview_identity
          : active_video_source_identity_,
      .source_revision = active_video_source_revision_,
      .input_transform_key = active_video_input_transform_,
      .grade_recipe_json = active_video_grade_recipe_json_,
      .animated_grade_parameters = active_video_grade_parameters_,
      .grade_session = active_video_grade_session_,
      .adjustment_grades = std::move(adjustment_grades),
      .creative_lut_path = active_video_creative_lut_path_,
      .creative_lut_revision =
          timeline_video_source_revision(active_video_creative_lut_path_),
      .shot_match_red_gain = active_video_shot_match_red_gain_,
      .shot_match_green_gain = active_video_shot_match_green_gain_,
      .shot_match_blue_gain = active_video_shot_match_blue_gain_,
      .effects = active_video_effects_,
      .sequence_width = active_sequence_width_,
      .sequence_height = active_sequence_height_,
      .preview_target_size = target_size,
  };
  const auto clip = active_video_foreground_cutout_clip_;
  const auto source_seconds =
      static_cast<double>(source_timestamp_us) / 1'000'000.0;
  const auto work_generation_state = latest_frame_work_generation_;
  const auto continuous_playback = !timeline_document_.isNull() && timeline_document_->sequencePlaying();
  frame_processing_watcher_.setFuture(QtConcurrent::run(
      [frame, color_settings, clip, source_seconds, target_size, work_generation,
       work_generation_state, continuous_playback]() mutable {
        const auto cancellation = photo_editor::pipeline::CancellationToken{
            .latest_generation = work_generation_state.get(),
            .generation = work_generation,
        };
        return process_video_frame(
            frame, color_settings, clip, source_seconds, target_size, continuous_playback, cancellation);
      }));
}

TimelinePlaybackController::~TimelinePlaybackController() {
  native_paused_preroll_frame_generation_.fetch_add(
      1U, std::memory_order_acq_rel);
  QObject::disconnect(native_video_frame_connection_);
  player_.stop();
  prefetch_player_.stop();
  player_.setAudioBufferOutput(nullptr);
  player_.setVideoSink(nullptr);
  invalidateFrameProcessing();
}

void TimelinePlaybackController::invalidateFrameProcessing() {
  ++video_frame_generation_;
  latest_frame_work_generation_->fetch_add(1, std::memory_order_relaxed);
  has_pending_video_frame_ = false;
  latest_pending_frame_ = QVideoFrame{};
  pending_frame_source_timestamp_us_ = -1;
  pending_frame_source_timestamp_estimated_ = false;
  pending_frame_is_scene_preview_ = false;
  pending_scene_preview_identity_.clear();
  clearPendingSeekFrame();
}

void TimelinePlaybackController::clearPendingSeekFrame() {
  pending_seek_video_frame_ = QVideoFrame{};
  pending_seek_frame_generation_ = 0U;
  ++position_sync_generation_;
}

void TimelinePlaybackController::publishProcessedFrame(
    QImage image,
    qint64 timestamp_us,
    bool timestamp_estimated) {
  if (frame_image_state_ == nullptr) {
    return;
  }
  processed_frame_timestamp_us_ = timestamp_us;
  processed_frame_timestamp_estimated_ = timestamp_estimated;
  processed_frame_available_ = !image.isNull();
  frame_image_state_->setImage(std::move(image));
  ++frame_source_revision_;
  emit processedFrameSourceChanged();
}

void TimelinePlaybackController::clearProcessedFrame() {
  processed_frame_available_ = false;
  processed_frame_timestamp_us_ = -1;
  processed_frame_timestamp_estimated_ = false;
  if (frame_image_state_ != nullptr) {
    frame_image_state_->setImage(QImage{});
  }
  if (!color_signal_processing_warning_.isEmpty()) {
    color_signal_processing_warning_.clear();
    emit stateChanged();
  }
  if (timeline_document_ != nullptr) {
    timeline_document_->setPlaybackColorSignalMetadataForCurrentClip(
        QString{}, {});
  }
  setColorSignalMetadata({});
  ++frame_source_revision_;
  emit processedFrameSourceChanged();
}

void TimelinePlaybackController::setColorSignalMetadata(QVariantMap metadata) {
  if (color_signal_metadata_ == metadata) {
    return;
  }
  color_signal_metadata_ = std::move(metadata);
  emit colorSignalMetadataChanged();
}

void TimelinePlaybackController::play() {
  if (!workspace_active_ || timeline_document_.isNull()) {
    return;
  }

  const auto native_source_load_pending = native_media_enabled_ &&
      awaiting_media_source_ && pending_position_ms_ >= 0;
  if (native_paused_preroll_active_ ||
      native_paused_preroll_resume_pending_ || native_source_load_pending) {
    native_paused_preroll_play_pending_ = true;
    native_paused_preroll_status_ =
        QStringLiteral("user Play waits for the requested decoded frame");
    if (!native_paused_preroll_active_) {
      tryStartPendingNativePausedPreroll();
    }
    emit stateChanged();
    return;
  }
  attachAudioBufferOutputForPlayback();
  shuttle_rate_ = 1.0;
  timeline_document_->playSequence();
  gap_start_sequence_seconds_ = timeline_document_->sequencePlayheadSeconds();
  gap_elapsed_.restart();
}

void TimelinePlaybackController::pause() {
  if (timeline_document_.isNull()) {
    return;
  }

  const auto previous_pause_frame_preservation =
      preserve_last_processed_frame_during_pause_;
  preserve_last_processed_frame_during_pause_ =
      timeline_document_->sequencePlaying();
  if (native_paused_preroll_play_pending_) {
    native_paused_preroll_play_pending_ = false;
    native_paused_preroll_status_ =
        QStringLiteral("pending user Play cancelled");
    emit stateChanged();
  } else {
    cancelNativePausedPreroll();
  }
  shuttle_rate_ = 0.0;
  timeline_document_->pauseSequence();
  resetAdaptivePlaybackDetail();
  syncToDocument();
  preserve_last_processed_frame_during_pause_ =
      previous_pause_frame_preservation;
}

void TimelinePlaybackController::togglePlayback() {
  if (timeline_document_.isNull()) {
    return;
  }

  if (timeline_document_->sequencePlaying() ||
      native_paused_preroll_play_pending_) {
    stopShuttle();
  } else {
    play();
  }
}

void TimelinePlaybackController::shuttleReverse() {
  if (timeline_document_.isNull() || timeline_document_->clipCount() == 0) {
    return;
  }

  if (timeline_document_->sequencePlayheadSeconds() >=
      timeline_document_->totalDurationSecondsValue() - 0.0001) {
    timeline_document_->seekSequenceSeconds(
        qMax(0.0,
             timeline_document_->totalDurationSecondsValue() -
                 1.0 / qMax(1.0, timeline_document_->framesPerSecond())));
  }
  shuttle_rate_ = timeline_document_->sequencePlaying() && shuttle_rate_ < 0.0
                      ? -qMin(kMaximumShuttleRate, qAbs(shuttle_rate_) * 2.0)
                      : -1.0;
  timeline_document_->playSequence();
  gap_start_sequence_seconds_ = timeline_document_->sequencePlayheadSeconds();
  gap_elapsed_.restart();
  syncToDocument();
}

void TimelinePlaybackController::stopShuttle() {
  pause();
}

void TimelinePlaybackController::shuttleForward() {
  if (timeline_document_.isNull() || timeline_document_->clipCount() == 0) {
    return;
  }

  shuttle_rate_ = timeline_document_->sequencePlaying() && shuttle_rate_ > 0.0
                      ? qMin(kMaximumShuttleRate, shuttle_rate_ * 2.0)
                      : 1.0;
  timeline_document_->playSequence();
  gap_start_sequence_seconds_ = timeline_document_->sequencePlayheadSeconds();
  gap_elapsed_.restart();
  syncToDocument();
}

void TimelinePlaybackController::stepFrames(int direction) {
  if (timeline_document_.isNull()) {
    return;
  }

  invalidateFrameProcessing();
  shuttle_rate_ = 0.0;
  timeline_document_->pauseSequence();
  timeline_document_->stepSequenceFrames(direction == 0 ? 0 : direction > 0 ? 1 : -1);
  syncToDocument();
}

void TimelinePlaybackController::seekSequenceSeconds(double seconds) {
  audio_output_.resetForDiscontinuity();
  if (timeline_document_.isNull()) {
    return;
  }

  if (!interaction_active_) {
    invalidateFrameProcessing();
  }
  shuttle_rate_ = 0.0;
  timeline_document_->seekSequenceSeconds(seconds);
  // seekSequenceSeconds always emits playbackStateChanged; that connection
  // synchronizes the decoder once. Repeating it here rebuilds the Program
  // projection and can issue the same native seek twice per scrub sample.
}

void TimelinePlaybackController::seekCurrentClipSeconds(double seconds) {
  audio_output_.resetForDiscontinuity();
  if (timeline_document_.isNull()) {
    return;
  }

  invalidateFrameProcessing();
  shuttle_rate_ = 0.0;
  timeline_document_->seekCurrentClipSeconds(seconds);
  syncToDocument();
}

void TimelinePlaybackController::setPlaybackLoop(double start_seconds, double end_seconds) {
  if (timeline_document_.isNull()) {
    return;
  }
  const auto duration = timeline_document_->totalDurationSecondsValue();
  const auto start = qBound(0.0, start_seconds, duration);
  const auto end = qBound(0.0, end_seconds, duration);
  if (end <= start) {
    clearPlaybackLoop();
    return;
  }
  loop_start_seconds_ = start;
  loop_end_seconds_ = end;
  playback_range_active_ = true;
  playback_range_looping_ = true;
  schedulePrefetchRefresh();
  emit stateChanged();
}

void TimelinePlaybackController::clearPlaybackLoop() {
  loop_start_seconds_ = -1.0;
  loop_end_seconds_ = -1.0;
  playback_range_active_ = false;
  playback_range_looping_ = false;
  schedulePrefetchRefresh();
  emit stateChanged();
}

bool TimelinePlaybackController::playReviewRange(
    double start_seconds,
    double end_seconds,
    double preroll_seconds,
    double postroll_seconds,
    bool loop) {
  if (timeline_document_.isNull()) {
    return false;
  }
  const auto duration = timeline_document_->totalDurationSecondsValue();
  const auto range_start = qBound(0.0, start_seconds, duration);
  const auto range_end = qBound(0.0, end_seconds, duration);
  if (range_end <= range_start) {
    return false;
  }
  loop_start_seconds_ = qMax(0.0, range_start - qMax(0.0, preroll_seconds));
  loop_end_seconds_ = qMin(duration, range_end + qMax(0.0, postroll_seconds));
  playback_range_active_ = loop_end_seconds_ > loop_start_seconds_;
  playback_range_looping_ = loop;
  timeline_document_->seekSequenceSeconds(loop_start_seconds_);
  shuttle_rate_ = 1.0;
  timeline_document_->playSequence();
  gap_start_sequence_seconds_ = loop_start_seconds_;
  gap_elapsed_.restart();
  schedulePrefetchRefresh();
  emit stateChanged();
  return playback_range_active_;
}

bool TimelinePlaybackController::playInToOut(
    double start_seconds,
    double end_seconds) {
  return playReviewRange(start_seconds, end_seconds, 0.0, 0.0, false);
}

bool TimelinePlaybackController::reviewFromEditPoint(
    double edit_seconds,
    double preroll_seconds,
    double postroll_seconds) {
  if (timeline_document_.isNull()) {
    return false;
  }
  const auto duration = timeline_document_->totalDurationSecondsValue();
  const auto edit = qBound(0.0, edit_seconds, duration);
  const auto start = qMax(0.0, edit - qMax(0.0, preroll_seconds));
  const auto end = qMin(duration, edit + qMax(0.0, postroll_seconds));
  return playReviewRange(start, end, 0.0, 0.0, false);
}

void TimelinePlaybackController::stopReviewPlayback() {
  pause();
  clearPlaybackLoop();
}

void TimelinePlaybackController::setLikelyNextEditSeconds(double seconds) {
  const auto normalized = seconds < 0.0 ? -1.0 : seconds;
  if (qFuzzyCompare(likely_next_edit_seconds_ + 1.0, normalized + 1.0)) {
    return;
  }
  likely_next_edit_seconds_ = normalized;
  schedulePrefetchRefresh();
}

void TimelinePlaybackController::setTimelineEffectsBypassed(bool bypassed) {
  if (timeline_effects_bypassed_ == bypassed) {
    return;
  }
  timeline_effects_bypassed_ = bypassed;
  syncToDocument();
  schedulePrefetchRefresh();
}

void TimelinePlaybackController::recoverRuntimeFailure(
    const QString& kind,
    const QString& detail) {
  const auto normalized = kind.trimmed().toLower();
  const auto supported = normalized == QStringLiteral("decoder") ||
                         normalized == QStringLiteral("gpu") ||
                         normalized == QStringLiteral("audio-device") ||
                         normalized == QStringLiteral("external-display");
  if (!supported || runtime_recovery_busy_) {
    return;
  }

  runtime_recovery_busy_ = true;
  runtime_recovery_kind_ = normalized;
  runtime_recovery_status_ = detail.trimmed().isEmpty()
                                 ? QStringLiteral("Recovering %1…").arg(normalized)
                                 : QStringLiteral("Recovering %1: %2")
                                       .arg(normalized, detail.trimmed());
  runtime_recovery_document_hash_ = timeline_document_.isNull()
                                        ? QString{}
                                        : timeline_document_->deterministicDocumentHash();
  ++runtime_recovery_count_;

  if (native_media_enabled_ &&
      (normalized == QStringLiteral("decoder") ||
       normalized == QStringLiteral("gpu"))) {
    invalidateFrameProcessing();
    player_.pause();
    player_.setSource(QUrl());
    prefetch_player_.stop();
    prefetch_player_.setSource(QUrl());
    current_source_url_.clear();
    prefetched_source_url_.clear();
    awaiting_media_source_ = false;
    awaiting_position_sync_ = false;
  }
  if (normalized == QStringLiteral("audio-device")) {
    audio_output_.recoverOutputDevice();
  }
  emit runtimeRecoveryRequested(normalized);
  emit stateChanged();
  runtime_recovery_timer_.start();
}

void TimelinePlaybackController::schedulePrefetchRefresh() {
  if (!workspace_active_)
    return;
  // Playback ticks arrive every ~15 ms. Restarting an 18 ms debounce on every
  // tick would starve prefetch forever, so retain the first pending deadline.
  if (!prefetch_timer_.isActive()) {
    prefetch_timer_.start();
  }
}

void TimelinePlaybackController::refreshPrefetchPlan() {
  if (!workspace_active_ || timeline_document_.isNull()) {
    prefetch_plan_.clear();
    prefetched_source_url_.clear();
    return;
  }
  prefetch_direction_ = shuttle_rate_ < 0.0 ? -1.0 : 1.0;
  auto next_plan = timeline_document_->playbackPrefetchCandidates(
      timeline_document_->sequencePlayheadSeconds(),
      prefetch_direction_,
      loop_start_seconds_,
      loop_end_seconds_,
      likely_next_edit_seconds_,
      6);
  QVariantList protected_plan;
  for (const auto& value : next_plan) {
    auto candidate = value.toMap();
    const auto source_url = candidate.value(QStringLiteral("sourceUrl")).toString();
    if (source_url.isEmpty() || source_url == current_source_url_) {
      continue;
    }
    candidate.insert(QStringLiteral("protectedSourceUrl"), current_source_url_);
    protected_plan.push_back(candidate);
  }
  prefetch_plan_ = protected_plan;
  const auto next_source = prefetch_plan_.isEmpty()
                               ? QString{}
                               : prefetch_plan_.first()
                                     .toMap()
                                     .value(QStringLiteral("sourceUrl"))
                                     .toString();
  if (prefetched_source_url_ != next_source) {
    prefetched_source_url_ = next_source;
    if (native_media_enabled_) {
      prefetch_player_.stop();
      prefetch_player_.setSource(QUrl(prefetched_source_url_));
      if (!prefetch_plan_.isEmpty()) {
        const auto position_ms = qRound64(
            prefetch_plan_.first()
                .toMap()
                .value(QStringLiteral("sourceSeconds"))
                .toDouble() *
            1000.0);
        prefetch_player_.setPosition(qMax<qint64>(0, position_ms));
      }
    }
  }
  emit stateChanged();
}

void TimelinePlaybackController::syncToDocument() {
  if (!workspace_active_ || timeline_document_.isNull()) {
    return;
  }
  if (!timeline_document_->sequencePlaying() &&
      adaptive_detail_level_ != QStringLiteral("full")) {
    resetAdaptivePlaybackDetail();
  }

  const auto next_source_url = preferredSourceUrl();
  // Resolve media revision at the document/source synchronization boundary.
  // Worker frames consume this immutable value and never stat media files.
  const auto next_source_revision =
      timeline_video_source_revision(next_source_url);
  const auto proxy_source_url = timeline_document_->proxyPlaybackSourceUrl();
  const auto render_cache_source_url = timeline_document_->renderCachePlaybackSourceUrl();
  using_render_cache_ = !render_cache_source_url.isEmpty() &&
                        next_source_url == render_cache_source_url;
  render_cache_range_start_seconds_ = using_render_cache_
                                          ? timeline_document_->renderCachePlaybackStartSeconds()
                                          : 0.0;
  using_proxy_ = !proxy_source_url.isEmpty() && next_source_url == proxy_source_url;
  // Decoder frames follow Program. Inspector selection can be an audio or
  // adjustment clip, so it must not choose the color pipeline for these frames.
  const auto program_clip = timeline_document_->programClip();
  current_program_clip_ = program_clip;
  const auto program_media_type =
      program_clip.value(QStringLiteral("mediaType")).toString();
  // programClip() builds a QVariant projection that includes the clip's
  // keyframes. Playback synchronization already resolves that projection; keep
  // its stable ID for decoded frames instead of rebuilding it at frame rate.
  active_program_clip_id_ =
      program_clip.value(QStringLiteral("clipId")).toString();
  VideoFrameColorSettings next_color_settings;
  next_color_settings.source_identity = next_source_url;
  next_color_settings.source_revision = next_source_revision;
  QSet<QString> active_render_session_keys;
  auto grade_session_error = QString{};
  const auto output_size = frameTargetSize();
  next_color_settings.preview_target_size = output_size;
  const auto session_for = [&](const QString& clip_id,
                               const QString& recipe_json,
                               const QVariantMap& animated_parameters) {
    if (animated_parameters.isEmpty() || recipe_json.trimmed().isEmpty()) {
      return std::shared_ptr<TimelineVideoGradeRenderSession>{};
    }
    const auto key = video_grade_render_session_key(
        clip_id, recipe_json, output_size);
    active_render_session_keys.insert(key);
    auto session = video_grade_render_sessions_.value(key);
    if (session == nullptr) {
      auto prepared = create_timeline_video_grade_render_session(
          recipe_json, clip_id, output_size);
      if (!prepared.ok()) {
        grade_session_error = prepared.error;
        return std::shared_ptr<TimelineVideoGradeRenderSession>{};
      }
      session = std::make_shared<TimelineVideoGradeRenderSession>(
          std::move(prepared));
      video_grade_render_sessions_.insert(key, session);
    }
    return session;
  };
  if (!using_render_cache_ &&
      (program_media_type == QStringLiteral("video") ||
       program_media_type == QStringLiteral("photo"))) {
    next_color_settings.clip_id =
        program_clip.value(QStringLiteral("clipId")).toString();
    if (program_media_type == QStringLiteral("photo")) {
      const auto still_source_path =
          program_clip.value(QStringLiteral("filePath")).toString().trimmed();
      next_color_settings.source_identity = still_source_path;
      next_color_settings.source_revision =
          timeline_video_source_revision(still_source_path);
    } else {
      next_color_settings.input_transform_key =
          program_clip.value(QStringLiteral("videoInputTransform"))
              .toString()
              .trimmed()
              .toLower();
    }
    next_color_settings.grade_recipe_json =
        timeline_document_->previewColorGradeRecipeJsonForClip(
            program_clip.value(QStringLiteral("clipId")).toString());
    next_color_settings.animated_grade_parameters =
        program_clip.value(QStringLiteral("animatedColorGradeParameters")).toMap();
    next_color_settings.grade_session = session_for(
        next_color_settings.clip_id,
        next_color_settings.grade_recipe_json,
        next_color_settings.animated_grade_parameters);
    const auto adjustment_grades =
        timeline_document_->activeProgramAdjustmentColorGrades(
            program_clip.value(QStringLiteral("clipId")).toString());
    next_color_settings.adjustment_grades.reserve(adjustment_grades.size());
    for (const auto& value : adjustment_grades) {
      const auto adjustment = value.toMap();
      next_color_settings.adjustment_grades.push_back({
          adjustment.value(QStringLiteral("clipId")).toString(),
          adjustment.value(QStringLiteral("recipeJson")).toString(),
          adjustment.value(QStringLiteral("opacity"), 1.0).toDouble(),
          adjustment.value(QStringLiteral("animatedColorGradeParameters")).toMap(),
          session_for(
              adjustment.value(QStringLiteral("clipId")).toString(),
              adjustment.value(QStringLiteral("recipeJson")).toString(),
              adjustment.value(QStringLiteral("animatedColorGradeParameters")).toMap()),
      });
    }
    if (program_clip.value(QStringLiteral("colorLutEnabled"), true).toBool()) {
      next_color_settings.creative_lut_path =
          program_clip.value(QStringLiteral("colorLutPath")).toString().trimmed();
    }
    next_color_settings.creative_lut_revision =
        timeline_video_source_revision(next_color_settings.creative_lut_path);
    next_color_settings.shot_match_red_gain =
        program_clip.value(QStringLiteral("shotMatchRedGain"), 1.0).toDouble();
    next_color_settings.shot_match_green_gain =
        program_clip.value(QStringLiteral("shotMatchGreenGain"), 1.0).toDouble();
    next_color_settings.shot_match_blue_gain =
        program_clip.value(QStringLiteral("shotMatchBlueGain"), 1.0).toDouble();
    next_color_settings.sequence_width = timeline_document_->sequenceWidth();
    next_color_settings.sequence_height = timeline_document_->sequenceHeight();
    if (!timeline_effects_bypassed_ &&
        program_clip.value(QStringLiteral("effectsEnabled"), true).toBool()) {
      const auto effect_details =
          program_clip.value(QStringLiteral("effectDetails")).toList();
      for (const auto& effect_value : effect_details) {
        const auto effect = effect_value.toMap();
        if (!effect.value(QStringLiteral("enabled"), true).toBool()) {
          continue;
        }
        const auto amount = effect.value(QStringLiteral("amount")).toDouble();
        const auto name = effect.value(QStringLiteral("name")).toString();
        const auto mask = effect.value(QStringLiteral("mask")).toMap();
        const auto append_spatial_color_effect = [&](const QString& effect_name) {
          if (!mask.value(QStringLiteral("enabled")).toBool()) {
            return false;
          }
          TimelinePlaybackEffectParameters::SpatialColorEffect spatial_effect;
          spatial_effect.name = effect_name;
          spatial_effect.amount = amount;
          spatial_effect.mask_shape =
              mask.value(QStringLiteral("shape"), QStringLiteral("ellipse"))
                  .toString();
          spatial_effect.mask_center_x =
              mask.value(QStringLiteral("centerX"), 0.5).toDouble();
          spatial_effect.mask_center_y =
              mask.value(QStringLiteral("centerY"), 0.5).toDouble();
          spatial_effect.mask_width =
              mask.value(QStringLiteral("width"), 0.65).toDouble();
          spatial_effect.mask_height =
              mask.value(QStringLiteral("height"), 0.65).toDouble();
          spatial_effect.mask_feather =
              mask.value(QStringLiteral("feather"), 0.04).toDouble();
          spatial_effect.mask_inverted =
              mask.value(QStringLiteral("inverted")).toBool();
          next_color_settings.effects.spatial_color_effects.push_back(
              std::move(spatial_effect));
          return true;
        };
        if (name == QStringLiteral("Exposure")) {
          if (!append_spatial_color_effect(name)) {
            next_color_settings.effects.exposure = amount;
          }
        } else if (name == QStringLiteral("Contrast")) {
          if (!append_spatial_color_effect(name)) {
            next_color_settings.effects.contrast = amount;
          }
        } else if (name == QStringLiteral("Saturation")) {
          if (!append_spatial_color_effect(name)) {
            next_color_settings.effects.saturation = amount;
          }
        } else if (name == QStringLiteral("Black & White")) {
          if (!append_spatial_color_effect(name)) {
            next_color_settings.effects.black_and_white = amount;
          }
        } else if (name == QStringLiteral("Fade")) {
          if (!append_spatial_color_effect(name)) {
            next_color_settings.effects.fade = amount;
          }
        } else if (name == QStringLiteral("Punch")) {
          if (!append_spatial_color_effect(name)) {
            next_color_settings.effects.punch = amount;
          }
        } else if (name == QStringLiteral("Blur")) {
          next_color_settings.effects.blur = amount;
          next_color_settings.effects.blur_mask_enabled =
              mask.value(QStringLiteral("enabled")).toBool();
          next_color_settings.effects.blur_mask_shape =
              mask.value(QStringLiteral("shape"), QStringLiteral("ellipse"))
                  .toString();
          next_color_settings.effects.blur_mask_center_x =
              mask.value(QStringLiteral("centerX"), 0.5).toDouble();
          next_color_settings.effects.blur_mask_center_y =
              mask.value(QStringLiteral("centerY"), 0.5).toDouble();
          next_color_settings.effects.blur_mask_width =
              mask.value(QStringLiteral("width"), 0.65).toDouble();
          next_color_settings.effects.blur_mask_height =
              mask.value(QStringLiteral("height"), 0.65).toDouble();
          next_color_settings.effects.blur_mask_feather =
              mask.value(QStringLiteral("feather"), 0.04).toDouble();
          next_color_settings.effects.blur_mask_inverted =
              mask.value(QStringLiteral("inverted")).toBool();
        }
      }
    }
  }
  for (auto session = video_grade_render_sessions_.begin();
       session != video_grade_render_sessions_.end();) {
    if (!active_render_session_keys.contains(session.key())) {
      session = video_grade_render_sessions_.erase(session);
    } else {
      ++session;
    }
  }
  const auto next_color_key = video_frame_color_settings_key(next_color_settings);
  auto next_foreground_cutout_clip = using_render_cache_
                                         ? QVariantMap{}
                                         : program_clip;
  // Source-crop authoring shows the full source, independent of the final
  // framing. Keep the document crop intact and key the preview with this copy.
  if (source_crop_preview_enabled_ && !timeline_document_->sequencePlaying()) {
    next_foreground_cutout_clip.insert(QStringLiteral("sourceCropLeft"), 0.0);
    next_foreground_cutout_clip.insert(QStringLiteral("sourceCropTop"), 0.0);
    next_foreground_cutout_clip.insert(QStringLiteral("sourceCropRight"), 1.0);
    next_foreground_cutout_clip.insert(QStringLiteral("sourceCropBottom"), 1.0);
  }
  const auto next_foreground_cutout_key =
      video_foreground_cutout_settings_key(next_foreground_cutout_clip);
  const auto color_settings_changed =
      next_color_key != active_video_frame_color_key_ ||
      next_foreground_cutout_key != active_video_foreground_cutout_key_;
  if (color_settings_changed) {
    active_video_source_identity_ = next_color_settings.source_identity;
    active_video_source_revision_ = next_color_settings.source_revision;
    active_video_input_transform_ = next_color_settings.input_transform_key;
    active_video_grade_recipe_json_ = next_color_settings.grade_recipe_json;
    active_video_grade_parameters_ =
        next_color_settings.animated_grade_parameters;
    active_video_grade_session_ = next_color_settings.grade_session;
    active_video_adjustment_grades_.clear();
    for (const auto& adjustment : next_color_settings.adjustment_grades) {
      active_video_adjustment_grades_.push_back({
          adjustment.clip_id,
          adjustment.recipe_json,
          adjustment.opacity,
          adjustment.animated_parameters,
          adjustment.session,
      });
    }
    color_grade_error_ = using_render_cache_
                             ? QString{}
                             : program_clip.value(QStringLiteral("colorGradeRecipeError"))
                                   .toString();
    if (!grade_session_error.isEmpty()) {
      color_grade_error_ = QStringLiteral("Animated grade preview could not be prepared: %1")
                               .arg(grade_session_error);
    }
    active_video_creative_lut_path_ = next_color_settings.creative_lut_path;
    active_video_shot_match_red_gain_ = next_color_settings.shot_match_red_gain;
    active_video_shot_match_green_gain_ = next_color_settings.shot_match_green_gain;
    active_video_shot_match_blue_gain_ = next_color_settings.shot_match_blue_gain;
    active_video_effects_ = next_color_settings.effects;
    active_sequence_width_ = next_color_settings.sequence_width;
    active_sequence_height_ = next_color_settings.sequence_height;
    active_video_frame_color_key_ = next_color_key;
    active_video_foreground_cutout_clip_ = std::move(next_foreground_cutout_clip);
    active_video_foreground_cutout_key_ = next_foreground_cutout_key;
    invalidateFrameProcessing();
    // Keep the last complete Program image during the synchronous transition
    // from playback to pause while replacement settings render. A paused edit
    // still clears the prior image so it cannot be mistaken for the edited
    // frame. Source changes still clear the image below.
    if (!preserve_last_processed_frame_during_pause_ ||
        !processed_frame_available_) {
      clearProcessedFrame();
    }
  }
  active_playback_rate_ = using_render_cache_
                              ? 1.0
                              : qBound(
                                    -8.0,
                                    program_clip
                                        .value(
                                            QStringLiteral("effectivePlaybackRate"),
                                            program_clip.value(QStringLiteral("playbackRate"), 1.0))
                                        .toDouble(),
                                    8.0);
  const auto target_position_ms =
      qRound64((using_render_cache_
                    ? timeline_document_->sequencePlayheadSeconds() -
                          render_cache_range_start_seconds_
                    : timeline_document_->programClipPlayheadSeconds()) *
               1000.0);
  if (native_paused_preroll_active_) {
    if (timeline_document_->sequencePlaying() ||
        next_source_url != native_paused_preroll_source_url_ ||
        player_.source() != QUrl(native_paused_preroll_source_url_)) {
      // A direct document Play or Program source switch invalidates the
      // silent warm-up and any user Play request waiting on its frame.
      cancelNativePausedPreroll();
    } else {
      if (target_position_ms != native_paused_preroll_target_ms_) {
        native_paused_preroll_target_ms_ = target_position_ms;
        native_paused_preroll_seek_acknowledged_ = false;
        native_paused_preroll_ack_position_ms_ = -1;
        native_paused_preroll_retarget_pending_ =
            native_paused_preroll_seek_issued_;
        native_paused_preroll_status_ =
            QStringLiteral("latest scrub target pending");
        if (interaction_active_) {
          native_paused_preroll_timer_.start();
        }
      }
      if (native_paused_preroll_first_frame_seen_ &&
          (!native_paused_preroll_seek_issued_ ||
           native_paused_preroll_retarget_pending_)) {
        queueNativePausedPrerollSeek(native_paused_preroll_generation_);
      }
      emit stateChanged();
      return;
    }
  }
  const auto player_was_already_at_target =
      qAbs(player_.position() - target_position_ms) <= kSeekToleranceMs;

  if (!native_media_enabled_) {
    current_source_url_ = next_source_url;
    if (timeline_document_->sequencePlaying()) {
      startGapPlayback();
    } else {
      stopGapPlayback();
    }
    setErrorString(QString{});
    syncScenePreviewFrame();
    syncStillPhotoGradePreview(program_clip);
    emit stateChanged();
    return;
  }

  updateAudioVolume(target_position_ms);
  // QAudioBufferOutput is unavailable in Qt's Darwin backend. Its PCM
  // fallback follows the same Program source/clock and never paused preroll.
  const auto forward_audio_rate = qAbs(active_playback_rate_) *
      (shuttle_rate_ > 0.0 ? shuttle_rate_ : 1.0);
  audio_output_.synchronizeDecodedAudio(
      next_source_url, target_position_ms, forward_audio_rate,
      using_render_cache_ ? 2 : qMax(1, program_clip.value(
          QStringLiteral("sourceAudioChannels"), 2).toInt()),
      timeline_document_->sequencePlaying() && shuttle_rate_ >= 0.0 &&
          active_playback_rate_ > 0.0001 && !native_paused_preroll_active_ &&
          player_.source() == QUrl(next_source_url) && player_.hasAudio() &&
          player_.playbackState() == QMediaPlayer::PlayingState);
  const auto audio_decode_error = audio_output_.decoderDiagnostics()
      .value(QStringLiteral("pcmFallbackError")).toString();
  if (!audio_decode_error.isEmpty()) {
    setErrorString(QStringLiteral("Audio preview is unavailable: %1").arg(audio_decode_error));
  } else if (error_string_.startsWith(QStringLiteral("Audio preview is unavailable:"))) {
    setErrorString(QString{});
  }
  if (timeline_document_->sequencePlaying()) {
    attachAudioBufferOutputForPlayback();
  }
  const auto forward_shuttle_multiplier = shuttle_rate_ > 0.0 ? shuttle_rate_ : 1.0;
  player_.setPlaybackRate(
      qMax(0.01, qAbs(active_playback_rate_)) * forward_shuttle_multiplier);
  if (timeline_document_->sequencePlaying()) {
    startGapPlayback();
  } else {
    stopGapPlayback();
  }

  if (current_source_url_ != next_source_url || playback_hibernated_) {
    audio_output_.resetForDiscontinuity();
    last_audio_source_position_ms_ = target_position_ms;
    current_source_url_ = next_source_url;
    playback_hibernated_ = false;
    invalidateFrameProcessing();
    clearProcessedFrame();
    playback_sample_clock_.invalidate();
    last_playback_sample_position_ms_ = -1;
    pending_position_ms_ = target_position_ms;
    awaited_position_ms_ = target_position_ms;
    awaiting_media_source_ = !current_source_url_.isEmpty();
    awaiting_position_sync_ = !current_source_url_.isEmpty();
    play_when_ready_ = false;
    setErrorString(QString{});

    if (current_source_url_.isEmpty()) {
      awaited_position_ms_ = -1;
      awaiting_media_source_ = false;
      awaiting_position_sync_ = false;
      player_.stop();
      player_.setSource(QUrl());
      if (timeline_document_->sequencePlaying()) {
        startGapPlayback();
      } else {
        stopGapPlayback();
      }
    } else {
      player_.setSource(QUrl(current_source_url_));
    }

    syncScenePreviewFrame();
    if (scene_preview_cache_active_) {
      pending_position_ms_ = -1;
      awaited_position_ms_ = -1;
      awaiting_position_sync_ = false;
    }
    syncStillPhotoGradePreview(program_clip);
    play_when_ready_ = timeline_document_->sequencePlaying() &&
                       shuttle_rate_ >= 0.0 &&
                       !scene_preview_cache_active_;
    emit stateChanged();
    return;
  }

  if (current_source_url_.isEmpty()) {
    awaited_position_ms_ = -1;
    awaiting_media_source_ = false;
    awaiting_position_sync_ = false;
    player_.stop();
    if (timeline_document_->sequencePlaying()) {
      startGapPlayback();
    } else {
      stopGapPlayback();
    }
    setErrorString(QString{});
    syncScenePreviewFrame();
    syncStillPhotoGradePreview(program_clip);
    emit stateChanged();
    return;
  }

  // Resolve the scene frame before touching the native seek clock. Scene
  // frames come from an exact source-time cache, while Qt's Darwin position
  // can report a different decoded frame for the same seek.
  syncScenePreviewFrame();
  syncStillPhotoGradePreview(program_clip);
  if (awaiting_media_source_) {
    if (scene_preview_cache_active_) {
      pending_position_ms_ = -1;
      awaited_position_ms_ = -1;
      awaiting_position_sync_ = false;
      play_when_ready_ = false;
    } else {
      pending_position_ms_ = target_position_ms;
      awaited_position_ms_ = target_position_ms;
      awaiting_position_sync_ = true;
      play_when_ready_ = timeline_document_->sequencePlaying() &&
                         shuttle_rate_ >= 0.0;
    }
    emit stateChanged();
    return;
  }

  if (scene_preview_cache_active_) {
    pending_position_ms_ = -1;
    awaiting_position_sync_ = false;
  }
  const auto scrub_seek_in_flight = awaiting_position_sync_ ||
      (interaction_active_ && !timeline_document_->sequencePlaying() &&
       frame_processing_active_);
  const auto native_forward_playback = timeline_document_->sequencePlaying() &&
      shuttle_rate_ >= 0.0 && active_playback_rate_ > 0.0001;
  if (scrub_seek_in_flight) {
    pending_position_ms_ = target_position_ms;
  } else if (!scene_preview_cache_active_ && !native_forward_playback &&
             qAbs(player_.position() - target_position_ms) > kSeekToleranceMs) {
    // The sequence clock emits ordinary playback updates every 15 ms. Seeking
    // the running decoder on those updates can continually cancel frame
    // delivery and leave a late seek completion racing the next paused seek.
    // Source changes have their own position handshake above; paused seeks and
    // reverse shuttle still need explicit decoder positioning here.
    invalidateFrameProcessing();
    pending_position_ms_ = -1;
    awaited_position_ms_ = target_position_ms;
    awaiting_position_sync_ = true;
    player_.setPosition(target_position_ms);
  }

  if (scene_preview_cache_active_) {
    const auto scene_audio_should_play = timeline_document_->sequencePlaying() &&
        shuttle_rate_ >= 0.0 && active_playback_rate_ > 0.0001;
    if (scene_audio_should_play) {
      if (player_.playbackState() == QMediaPlayer::PlayingState) {
        play_when_ready_ = false;
      } else {
        if (!play_when_ready_) {
          player_.setPosition(target_position_ms);
          awaited_position_ms_ = target_position_ms;
        }
        if (mediaReady()) {
          player_.play();
          play_when_ready_ =
              player_.playbackState() != QMediaPlayer::PlayingState;
        } else {
          play_when_ready_ = true;
        }
      }
    } else {
      play_when_ready_ = false;
      if (player_.playbackState() == QMediaPlayer::PlayingState) {
        player_.pause();
      }
      if (mediaReady() && awaited_position_ms_ != target_position_ms) {
        player_.setPosition(target_position_ms);
        awaited_position_ms_ = target_position_ms;
      }
    }
  } else if (timeline_document_->sequencePlaying() && shuttle_rate_ >= 0.0 &&
      active_playback_rate_ > 0.0001) {
    if (player_.mediaStatus() == QMediaPlayer::LoadedMedia || player_.mediaStatus() == QMediaPlayer::BufferedMedia ||
        player_.mediaStatus() == QMediaPlayer::BufferingMedia || player_.mediaStatus() == QMediaPlayer::StalledMedia) {
      player_.play();
    } else {
      play_when_ready_ = true;
    }
  } else if (player_.playbackState() == QMediaPlayer::PlayingState ||
             (player_.playbackState() == QMediaPlayer::StoppedState &&
              mediaReady())) {
    player_.pause();
  }

  // Refresh only after source/position synchronization has been resolved.
  // A same-position paused seek can invalidate in-flight work without asking
  // the decoder for another frame, so also check the sink's current frame.
  if (!timeline_document_->sequencePlaying() && player_was_already_at_target) {
    reprocessCurrentPausedFrameIfNeeded();
  }
  syncStillPhotoGradePreview(program_clip);
  emit stateChanged();
}

void TimelinePlaybackController::syncStillPhotoGradePreview(
    const QVariantMap& program_clip) {
  if (timeline_document_.isNull() || !workspace_active_) {
    return;
  }
  const auto clear_still_request = [this]() {
    const auto was_active = still_photo_grade_preview_active_;
    still_photo_grade_preview_active_ = false;
    still_photo_requested_source_key_.clear();
    still_photo_grade_request_key_.clear();
    if (was_active && !using_render_cache_) {
      clearProcessedFrame();
    } else if (was_active) {
      emit processedFrameSourceChanged();
    }
  };
  if (using_render_cache_ ||
      program_clip.value(QStringLiteral("mediaType")).toString() !=
          QStringLiteral("photo")) {
    clear_still_request();
    return;
  }

  const auto clip_id = program_clip.value(QStringLiteral("clipId")).toString().trimmed();
  const auto source_path = program_clip.value(QStringLiteral("filePath")).toString().trimmed();
  const auto recipe = timeline_document_->previewColorGradeRecipeJsonForClip(clip_id);
  if (clip_id.isEmpty() || source_path.isEmpty() || recipe.trimmed().isEmpty()) {
    clear_still_request();
    return;
  }
  const auto animated_parameters =
      program_clip.value(QStringLiteral("animatedColorGradeParameters")).toMap();
  QString pointwise_error;
  if (!animated_parameters.isEmpty() ||
      !timeline_video_grade_can_use_lut(recipe, QSize(33, 33), &pointwise_error)) {
    const auto unsupported = animated_parameters.isEmpty()
        ? QStringLiteral("Still-photo Timeline preview currently requires a static pointwise grade. %1")
              .arg(pointwise_error)
        : QStringLiteral("Animated shared grades on still photos are not supported yet.");
    const auto had_request = still_photo_grade_preview_active_ ||
                             !still_photo_grade_request_key_.isEmpty();
    still_photo_grade_preview_active_ = false;
    still_photo_requested_source_key_.clear();
    still_photo_grade_request_key_.clear();
    color_grade_error_ = unsupported;
    if (had_request && !using_render_cache_) {
      clearProcessedFrame();
    } else {
      emit processedFrameSourceChanged();
      emit stateChanged();
    }
    return;
  }
  if (video_frame_item_.isNull()) {
    return;
  }

  const auto target_size = frameTargetSize();
  const auto source_revision = timeline_video_source_revision(source_path);
  const auto source_key = QStringLiteral("%1|%2|%3x%4")
                              .arg(source_path, source_revision)
                              .arg(target_size.width())
                              .arg(target_size.height());
  const auto request_key = QStringLiteral("%1|%2|%3")
                               .arg(clip_id, source_key, active_video_frame_color_key_);
  const auto request_changed = request_key != still_photo_grade_request_key_;
  if (!request_changed && processed_frame_available_ &&
      still_photo_grade_preview_active_) {
    return;
  }
  still_photo_requested_source_key_ = source_key;
  still_photo_grade_request_key_ = request_key;
  if (request_changed) {
    still_photo_grade_preview_active_ = true;
    clearProcessedFrame();
  }
  if (still_photo_cached_source_key_ == source_key &&
      !still_photo_source_image_.isNull()) {
    submitStillPhotoGradePreviewFrame();
    return;
  }
  if (still_photo_source_watcher_.isRunning()) {
    return;
  }
  still_photo_decoding_source_key_ = source_key;
  still_photo_source_watcher_.setFuture(QtConcurrent::run(
      load_still_photo_grade_source, source_path, target_size));
}

void TimelinePlaybackController::submitStillPhotoGradePreviewFrame() {
  if (!workspace_active_ || video_frame_item_.isNull() ||
      still_photo_source_image_.isNull() ||
      still_photo_requested_source_key_.isEmpty() ||
      still_photo_grade_request_key_.isEmpty() ||
      still_photo_cached_source_key_ != still_photo_requested_source_key_ ||
      using_render_cache_ || timeline_document_.isNull() ||
      timeline_document_->scenePreviewFrame().value(QStringLiteral("active")).toBool()) {
    return;
  }
  auto frame = QVideoFrame(
      still_photo_source_image_.convertToFormat(QImage::Format_RGBA8888));
  if (!frame.isValid()) {
    color_grade_error_ = QStringLiteral("The still photo could not be placed in the shared Timeline preview pipeline.");
    still_photo_grade_preview_active_ = false;
    clearProcessedFrame();
    emit stateChanged();
    return;
  }
  frame.setStartTime(qRound64(
      qMax(0.0, timeline_document_->programClipPlayheadSeconds()) * 1'000'000.0));
  scene_preview_frame_key_ = still_photo_grade_request_key_;
  const auto was_injecting = injecting_scene_preview_frame_;
  injecting_scene_preview_frame_ = true;
  handleVideoFrame(frame);
  injecting_scene_preview_frame_ = was_injecting;
}

void TimelinePlaybackController::syncScenePreviewFrame() {
  if (timeline_document_.isNull() || !workspace_active_) {
    return;
  }
  const auto preview = timeline_document_->scenePreviewFrame();
  const auto active = preview.value(QStringLiteral("active")).toBool();
  if (!active) {
    if (scene_preview_cache_active_) {
      scene_preview_cache_active_ = false;
      play_when_ready_ = false;
      scene_preview_frame_key_.clear();
      invalidateFrameProcessing();
      clearProcessedFrame();
    }
    if (!scene_preview_error_.isEmpty()) {
      const auto prior_scene_error = scene_preview_error_;
      scene_preview_error_.clear();
      if (error_string_ == prior_scene_error) {
        setErrorString(QString{});
      }
    }
    return;
  }

  if (native_paused_preroll_active_) {
    cancelNativePausedPreroll();
  }

  if (!scene_preview_cache_active_) {
    scene_preview_cache_active_ = true;
    play_when_ready_ = false;
    awaited_position_ms_ = -1;
    scene_preview_frame_key_.clear();
    invalidateFrameProcessing();
    clearProcessedFrame();
  }

  auto failure = preview.value(QStringLiteral("error")).toString().trimmed();
  const auto frame_path = preview.value(QStringLiteral("path")).toString().trimmed();
  bool source_seconds_ok = false;
  const auto source_seconds = preview.value(QStringLiteral("sourceSeconds"))
                                  .toDouble(&source_seconds_ok);
  const auto source_timestamp_us = source_seconds_ok && std::isfinite(source_seconds) &&
                                           source_seconds >= 0.0 && source_seconds <= 21600.0
                                       ? qRound64(source_seconds * 1'000'000.0)
                                       : -1;
  if (failure.isEmpty() &&
      (frame_path.isEmpty() || source_timestamp_us < 0 ||
       !QFileInfo::exists(frame_path))) {
    failure = QStringLiteral(
        "Scene preview frame is unavailable. Solve this shot again to rebuild its local frame cache.");
  }
  if (!failure.isEmpty()) {
    if (scene_preview_error_ != failure) {
      scene_preview_frame_key_.clear();
      invalidateFrameProcessing();
      clearProcessedFrame();
      scene_preview_error_ = failure;
      setErrorString(failure);
    } else if (error_string_ != failure) {
      setErrorString(failure);
    }
    return;
  }

  if (!scene_preview_error_.isEmpty()) {
    const auto prior_scene_error = scene_preview_error_;
    scene_preview_error_.clear();
    if (error_string_ == prior_scene_error) {
      setErrorString(QString{});
    }
  }
  const auto frame_key = frame_path + QLatin1Char('|') +
                         QString::number(source_timestamp_us) + QLatin1Char('|') +
                         active_video_frame_color_key_;
  if (scene_preview_frame_key_ == frame_key) {
    const auto same_frame_is_pending =
        (frame_processing_active_ && processing_frame_is_scene_preview_ &&
         processing_frame_source_timestamp_us_ == source_timestamp_us &&
         processing_scene_preview_identity_ == frame_key) ||
        (has_pending_video_frame_ && pending_frame_is_scene_preview_ &&
         pending_frame_source_timestamp_us_ == source_timestamp_us &&
         pending_scene_preview_identity_ == frame_key);
    if (same_frame_is_pending ||
        (processed_frame_available_ &&
         processed_frame_timestamp_us_ == source_timestamp_us &&
         !processed_frame_timestamp_estimated_)) {
      return;
    }
  }

  QImage image(frame_path);
  if (image.isNull() || image.width() > 1920 || image.height() > 1920 ||
      static_cast<qint64>(image.width()) * image.height() > 2'000'000) {
    const auto image_failure = QStringLiteral(
        "Scene preview frame could not be loaded within the preview size limit.");
    if (scene_preview_error_ != image_failure) {
      scene_preview_frame_key_.clear();
      invalidateFrameProcessing();
      clearProcessedFrame();
      scene_preview_error_ = image_failure;
      setErrorString(image_failure);
    } else if (error_string_ != image_failure) {
      setErrorString(image_failure);
    }
    return;
  }
  auto frame = video_frame_from_image(image);
  if (!frame.isValid()) {
    setErrorString(QStringLiteral("Scene preview frame could not be prepared for playback."));
    return;
  }
  frame.setStartTime(source_timestamp_us);
  scene_preview_frame_key_ = frame_key;
  const auto was_injecting_scene_preview_frame = injecting_scene_preview_frame_;
  injecting_scene_preview_frame_ = true;
  handleVideoFrame(frame);
  injecting_scene_preview_frame_ = was_injecting_scene_preview_frame;
}

QString TimelinePlaybackController::preferredSourceUrl() const {
  if (timeline_document_.isNull()) {
    return {};
  }

  const auto original = timeline_document_->originalPlaybackSourceUrl();
  if (timeline_document_->hasColorGradePreview()) return original;
  // Scene-cache images are ungraded source frames. Keep the original clip's
  // color/effect pipeline active instead of treating them as a rendered cache.
  if (timeline_document_->scenePreviewFrame().value(QStringLiteral("active")).toBool()) {
    return original;
  }
  if (source_crop_preview_enabled_ && !timeline_document_->sequencePlaying()) {
    return original;
  }
  const auto proxy = timeline_document_->proxyPlaybackSourceUrl();
  if (preview_quality_mode_ == QStringLiteral("full")) {
    return original;
  }
  if (preview_quality_mode_ == QStringLiteral("proxy")) {
    return proxy.isEmpty() ? original : proxy;
  }
  if (preview_quality_mode_ == QStringLiteral("clip")) {
    return timeline_document_->playbackSourceUrl();
  }
  const auto needs_responsive_source = interaction_active_ || timeline_document_->sequencePlaying();
  const auto render_cache = timeline_document_->renderCachePlaybackSourceUrl();
  const auto reduced_resolution = preview_quality_mode_ == QStringLiteral("half") ||
                                  preview_quality_mode_ == QStringLiteral("quarter");
  // Resolution controls the published Program size, not whether the authored
  // composite can use a ready cache. Retain that composite when paused too, so
  // lowering detail does not force original decoding or lose cached effects.
  if ((needs_responsive_source || reduced_resolution) && !render_cache.isEmpty()) {
    return render_cache;
  }
  if (reduced_resolution) {
    return original;
  }
  return needs_responsive_source && !proxy.isEmpty() ? proxy : original;
}

bool TimelinePlaybackController::canReplayCachedVideoFrame() const {
  return !timeline_document_.isNull() && !current_source_url_.isEmpty() &&
      !active_program_clip_id_.isEmpty() &&
      current_program_clip_.value(QStringLiteral("mediaType")).toString() ==
          QStringLiteral("video") &&
      current_program_clip_.value(QStringLiteral("clipId")).toString() ==
          active_program_clip_id_;
}

void TimelinePlaybackController::startGapPlayback() {
  if (timeline_document_.isNull() || gap_timer_.isActive()) {
    return;
  }

  gap_start_sequence_seconds_ = timeline_document_->sequencePlayheadSeconds();
  gap_elapsed_.restart();
  gap_timer_.start();
}

void TimelinePlaybackController::stopGapPlayback() {
  gap_timer_.stop();
  gap_elapsed_.invalidate();
}

void TimelinePlaybackController::tickGapPlayback() {
  if (timeline_document_.isNull() || !timeline_document_->sequencePlaying()) {
    stopGapPlayback();
    return;
  }

  const auto elapsed_seconds = static_cast<double>(gap_elapsed_.elapsed()) / 1000.0;
  const auto effective_rate = qFuzzyIsNull(shuttle_rate_) ? 1.0 : shuttle_rate_;
  const auto target_seconds = gap_start_sequence_seconds_ + elapsed_seconds * effective_rate;
  if (playback_range_active_ && effective_rate > 0.0 &&
      target_seconds >= loop_end_seconds_) {
    if (playback_range_looping_) {
      timeline_document_->syncGapPlaybackPosition(loop_start_seconds_);
      gap_start_sequence_seconds_ = loop_start_seconds_;
      gap_elapsed_.restart();
      emit stateChanged();
      return;
    }
    timeline_document_->syncGapPlaybackPosition(loop_end_seconds_);
    shuttle_rate_ = 0.0;
    timeline_document_->pauseSequence();
    playback_range_active_ = false;
    playback_range_looping_ = false;
    loop_start_seconds_ = -1.0;
    loop_end_seconds_ = -1.0;
    stopGapPlayback();
    schedulePrefetchRefresh();
    emit stateChanged();
    return;
  }
  const auto bounded_seconds = qBound(
      0.0, target_seconds, timeline_document_->totalDurationSecondsValue());
  timeline_document_->syncGapPlaybackPosition(bounded_seconds);
  if ((effective_rate < 0.0 && target_seconds <= 0.0) ||
      (effective_rate > 0.0 &&
       target_seconds >= timeline_document_->totalDurationSecondsValue())) {
    shuttle_rate_ = 0.0;
    timeline_document_->pauseSequence();
    stopGapPlayback();
    emit stateChanged();
  }
}

void TimelinePlaybackController::updateAudioVolume(qint64 source_position_ms) {
  if (timeline_document_.isNull()) {
    return;
  }
  if (source_position_ms >= 0) {
    // Ordinary playback notifications can be more than one frame apart. They
    // are not seeks and must not flush DSP/sink state or restart PCM decoding.
    // Explicit seek entrypoints and source changes reset independently.
    if (!timeline_document_->sequencePlaying() &&
        last_audio_source_position_ms_ >= 0 &&
        qAbs(source_position_ms - last_audio_source_position_ms_) > kSeekToleranceMs) {
      audio_output_.resetForDiscontinuity();
    }
    last_audio_source_position_ms_ = source_position_ms;
  }
  if (using_render_cache_) {
    audio_output_.setVolume(1.0);
    audio_output_.setPanEnabled(false);
    audio_output_.setProcessingEnabled(false);
    audio_output_.setEqualizerEnabled(false);
    audio_output_.setEqLowDb(0.0);
    audio_output_.setEqMidDb(0.0);
    audio_output_.setEqHighDb(0.0);
    audio_output_.setCompressorEnabled(false);
    audio_output_.setLimiterEnabled(false);
    audio_output_.setGateEnabled(false);
    audio_output_.setDeEsserEnabled(false);
    audio_output_.setHighPassEnabled(false);
    audio_output_.setLowPassEnabled(false);
    audio_output_.setDialogueCleanupEnabled(false);
    audio_output_.setDuckingGainDb(0.0);
    return;
  }
  audio_output_.setPanEnabled(true);
  // QMediaPlayer is decoding the Program video source. The inspector may be
  // focused on a voiceover or another audio clip, so its clip and track state
  // cannot control this decoded buffer's output.
  const auto clip = timeline_document_->programClip();
  const auto track_settings = timeline_document_->audioTrackPlaybackSettings(
      clip.value(QStringLiteral("audioTrackId")).toString(),
      timeline_document_->sequencePlayheadSeconds());
  audio_output_.setProcessingEnabled(
      !track_settings.value(QStringLiteral("audioProcessingBypassed"), false).toBool());
  audio_output_.setEqualizerEnabled(
      track_settings.value(QStringLiteral("audioEqEnabled"), true).toBool());
  audio_output_.setEqLowDb(
      track_settings.value(QStringLiteral("audioEqLowDb"), 0.0).toDouble());
  audio_output_.setEqMidDb(
      track_settings.value(QStringLiteral("audioEqMidDb"), 0.0).toDouble());
  audio_output_.setEqHighDb(
      track_settings.value(QStringLiteral("audioEqHighDb"), 0.0).toDouble());
  audio_output_.setCompressorEnabled(
      track_settings.value(QStringLiteral("audioCompressorEnabled"), false).toBool());
  audio_output_.setCompressorThresholdDb(
      track_settings.value(QStringLiteral("audioCompressorThresholdDb"), -18.0).toDouble());
  audio_output_.setCompressorRatio(
      track_settings.value(QStringLiteral("audioCompressorRatio"), 4.0).toDouble());
  audio_output_.setCompressorMakeupDb(
      track_settings.value(QStringLiteral("audioCompressorMakeupDb"), 0.0).toDouble());
  audio_output_.setLimiterEnabled(
      track_settings.value(QStringLiteral("audioLimiterEnabled"), false).toBool());
  audio_output_.setLimiterCeilingDb(
      track_settings.value(QStringLiteral("audioLimiterCeilingDb"), -1.0).toDouble());
  audio_output_.setGateEnabled(
      track_settings.value(QStringLiteral("audioGateEnabled"), false).toBool());
  audio_output_.setGateThresholdDb(
      track_settings.value(QStringLiteral("audioGateThresholdDb"), -48.0).toDouble());
  audio_output_.setDeEsserEnabled(
      track_settings.value(QStringLiteral("audioDeEsserEnabled"), false).toBool());
  audio_output_.setDeEsserAmount(
      track_settings.value(QStringLiteral("audioDeEsserAmount"), 0.35).toDouble());
  audio_output_.setHighPassEnabled(
      track_settings.value(QStringLiteral("audioHighPassEnabled"), false).toBool());
  audio_output_.setHighPassHz(
      track_settings.value(QStringLiteral("audioHighPassHz"), 80.0).toDouble());
  audio_output_.setLowPassEnabled(
      track_settings.value(QStringLiteral("audioLowPassEnabled"), false).toBool());
  audio_output_.setLowPassHz(
      track_settings.value(QStringLiteral("audioLowPassHz"), 16000.0).toDouble());
  const auto cleanup_enabled =
      clip.value(QStringLiteral("trackDialogueCleanupEnabled"), false).toBool();
  audio_output_.setDialogueCleanupEnabled(cleanup_enabled);
  audio_output_.setVoiceIsolationAmount(
      clip.value(QStringLiteral("trackDialogueVoiceIsolationAmount"), 0.65).toDouble());
  audio_output_.setNoiseReductionAmount(
      clip.value(QStringLiteral("trackDialogueNoiseReductionAmount"), 0.35).toDouble());
  audio_output_.setDereverbAmount(
      clip.value(QStringLiteral("trackDialogueDereverbAmount"), 0.25).toDouble());
  audio_output_.setHumRemovalAmount(
      clip.value(QStringLiteral("trackDialogueHumRemovalAmount"), 0.50).toDouble());
  audio_output_.setHumFrequencyHz(
      clip.value(QStringLiteral("trackDialogueHumFrequencyHz"), 60).toInt());
  audio_output_.setArtifactSmoothing(
      clip.value(QStringLiteral("trackDialogueArtifactSmoothing"), 0.65).toDouble());
  audio_output_.setAmbiencePreservation(
      clip.value(QStringLiteral("trackDialogueAmbiencePreservation"), 0.35).toDouble());
  audio_output_.setDuckingGainDb(
      track_settings.value(
          QStringLiteral("audioDuckingGainDb"),
          clip.value(QStringLiteral("trackAudioDuckingGainDb"), 0.0)).toDouble());
  const auto enabled = (!clip.contains(QStringLiteral("audioPlaybackEnabled")) ||
                        clip.value(QStringLiteral("audioPlaybackEnabled")).toBool()) &&
                       !clip.value(QStringLiteral("retimeAudioMuted"), false).toBool() &&
                       track_settings.value(QStringLiteral("visible"), true).toBool() &&
                       !track_settings.value(QStringLiteral("muted"), false).toBool() &&
                       !(track_settings.value(QStringLiteral("anyAudioSolo"), false).toBool() &&
                         !track_settings.value(QStringLiteral("solo"), false).toBool());
  if (!enabled || clip.isEmpty()) {
    audio_output_.setVolume(0.0);
    return;
  }

  const auto fade_in = clip.value(QStringLiteral("fadeInSeconds")).toDouble();
  const auto fade_out = clip.value(QStringLiteral("fadeOutSeconds")).toDouble();
  const auto timeline_offset = qMax(
      0.0,
      timeline_document_->sequencePlayheadSeconds() -
          clip.value(QStringLiteral("startSeconds")).toDouble());
  const auto timeline_duration = qMax(
      0.0, clip.value(QStringLiteral("trimmedDurationSeconds")).toDouble());
  auto envelope = 1.0;
  if (fade_in > 0.0) {
    envelope = qMin(envelope, qBound(0.0, timeline_offset / fade_in, 1.0));
  }
  if (fade_out > 0.0) {
    envelope = qMin(
        envelope, qBound(0.0, (timeline_duration - timeline_offset) / fade_out, 1.0));
  }
  const auto matched_bypass_gain_db =
      track_settings.value(QStringLiteral("audioProcessingBypassed"), false).toBool() &&
              track_settings.value(
                  QStringLiteral("audioLoudnessMatchedBypass"), false).toBool()
          ? track_settings.value(
                QStringLiteral("audioLoudnessMatchedBypassGainDb"), 0.0).toDouble()
          : 0.0;
  const auto total_gain_db = clip.value(QStringLiteral("audioGainDb")).toDouble() +
                             track_settings.value(
                                 QStringLiteral("audioGainDb"),
                                 clip.value(QStringLiteral("trackAudioGainDb"))).toDouble() +
                             matched_bypass_gain_db;
  const auto linear_gain = qPow(10.0, total_gain_db / 20.0);
  audio_output_.setPan(
      track_settings.value(
          QStringLiteral("audioPan"), clip.value(QStringLiteral("trackAudioPan"))).toDouble());
  audio_output_.setVolume(qBound(0.0, linear_gain * envelope, 4096.0));
}

void TimelinePlaybackController::setErrorString(const QString& error_string) {
  if (error_string_ == error_string) {
    return;
  }

  error_string_ = error_string;
  emit stateChanged();
}

}  // namespace photo_editor::desktop
