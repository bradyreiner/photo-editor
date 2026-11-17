#include "edit_recipe_mask_schema.h"
#include "brush_live_preview_item.h"
#include "editor_preview_controller.h"
#include <QApplication>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRectF>
#include <QSettings>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QThread>
#include <algorithm>
#include <atomic>
#include <iostream>

namespace photo_editor::desktop {
struct MaskInteractionPerformanceTestAccess final {
  static void seed(EditorPreviewController& c, int bytes, const QString& path) {
    auto active = default_mask_definition("brush");
    active["id"] = "active-brush";
    QVariantList masks{active};
    if (bytes > 0) {
      auto old = default_mask_definition("brush");
      old["id"] = "previous-brush";
      old["segmentationWidth"] = 2048;
      old["segmentationHeight"] = bytes / 2048;
      old["segmentationData"] = QByteArray(bytes, '\x7f');
      masks.append(old);
    }
    c.asset_id_ = "brush-dispatch";
    c.file_path_ = path;
    c.source_size_cache_path_ = c.file_path_;
    c.source_size_cache_bytes_ = QFileInfo(path).size();
    c.source_size_cache_modified_ms_ = QFileInfo(path).lastModified().toMSecsSinceEpoch();
    c.source_size_cache_ = QSize(2560, 1440);
    c.mask_session_.replaceStack(masks, 0);
    c.brush_stroke_active_ = true;
  }
  static qint64 dispatch(EditorPreviewController& c) {
    BrushPreviewPoint point;
    point.x = .31; point.y = .36; point.size = .08; point.flow = 1.;
    c.active_brush_live_points_.append(point);
    QElapsedTimer timer; timer.start();
    c.updateActiveBrushLivePreview({point});
    return timer.nsecsElapsed();
  }
  static QByteArray finish(EditorPreviewController& c) {
    c.auto_mask_live_preview_watcher_.waitForFinished();
    const auto frame = c.auto_mask_live_preview_watcher_.result();
    if (frame.raster.alpha.isEmpty() || std::none_of(frame.raster.alpha.begin(), frame.raster.alpha.end(),
        [](char byte) { return byte != 0; })) return {};
    const auto style = c.activeBrushPreviewStyle();
    const auto& image = frame.raster.image;
    if (image.isNull() || frame.raster.alpha.size() != image.width() * image.height()) {
      std::cerr << "brush raster image dimensions did not match its alpha coverage\n";
      return {};
    }
    for (int y = 0; y < image.height(); ++y) {
      for (int x = 0; x < image.width(); ++x) {
        const auto index = y * image.width() + x;
        const auto coverage = static_cast<unsigned char>(frame.raster.alpha.at(index));
        const auto alpha = qBound(0, qRound(coverage * style.opacity), 255);
        const auto expected = qPremultiply(qRgba(
            style.color.red(), style.color.green(), style.color.blue(), alpha));
        const auto actual = image.pixel(x, y);
        if (actual != expected) {
          std::cerr << "brush raster ARGB mismatch at " << x << ',' << y
                    << " expected=" << expected << " actual=" << actual << '\n';
          return {};
        }
      }
    }
    return QCryptographicHash::hash(frame.raster.alpha, QCryptographicHash::Sha256).toHex();
  }
  static void setStroke(
      EditorPreviewController& c,
      bool active,
      const QVector<BrushPreviewPoint>& points) {
    c.brush_stroke_active_ = active;
    c.active_brush_live_points_ = points;
    c.pending_brush_commit_preview_points_.clear();
    c.pending_brush_commit_preview_mask_id_.clear();
    emit c.activeBrushStrokeChanged();
  }
};
}

namespace {

using photo_editor::desktop::BrushLivePreviewItem;
using photo_editor::desktop::BrushPreviewPoint;
using photo_editor::desktop::EditorPreviewController;
using photo_editor::desktop::MaskInteractionPerformanceTestAccess;

QVector<BrushPreviewPoint> stroke_points(double first_x, double first_y, double second_x, double second_y) {
  BrushPreviewPoint first;
  first.x = first_x;
  first.y = first_y;
  first.size = 0.22;
  first.flow = 1.0;
  first.opacity = 1.0;
  BrushPreviewPoint second = first;
  second.x = second_x;
  second.y = second_y;
  return {first, second};
}

template <typename PaintWitness>
QImage capture_window_when(QQuickWindow& window, PaintWitness paint_witness) {
  QElapsedTimer timeout;
  timeout.start();
  QImage frame;
  while (timeout.elapsed() < 1500) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    window.update();
    frame = window.grabWindow();
    if (!frame.isNull() && paint_witness()) return frame;
    QThread::msleep(2);
  }
  return frame;
}

QImage capture_window(QQuickWindow& window) {
  return capture_window_when(window, [] { return true; });
}

void enqueue_geometry_burst(BrushLivePreviewItem* item, const QSize& final_size) {
  item->setWidth(94);
  item->setHeight(46);
  item->setWidth(62);
  item->setHeight(68);
  item->setWidth(final_size.width());
  item->setHeight(final_size.height());
}

QImage item_pixels(const QImage& frame, const QQuickWindow& window, const QPoint& position, const QSize& size) {
  if (frame.isNull() || window.width() <= 0 || window.height() <= 0) return {};
  const auto scale_x = static_cast<double>(frame.width()) / window.width();
  const auto scale_y = static_cast<double>(frame.height()) / window.height();
  const QRect pixels(
      qRound(position.x() * scale_x), qRound(position.y() * scale_y),
      qRound(size.width() * scale_x), qRound(size.height() * scale_y));
  return frame.copy(pixels.intersected(frame.rect()));
}

bool has_ink(const QImage& image, const QRectF& normalized_region) {
  if (image.isNull()) return false;
  const QRect region(
      qRound(normalized_region.x() * image.width()),
      qRound(normalized_region.y() * image.height()),
      qRound(normalized_region.width() * image.width()),
      qRound(normalized_region.height() * image.height()));
  const auto bounded = region.intersected(image.rect());
  for (int y = bounded.top(); y <= bounded.bottom(); ++y) {
    for (int x = bounded.left(); x <= bounded.right(); ++x) {
      const auto color = image.pixelColor(x, y);
      if (color.red() < 245 || color.green() < 245 || color.blue() < 245) return true;
    }
  }
  return false;
}

bool is_blank(const QImage& image) {
  if (image.isNull()) return false;
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      const auto color = image.pixelColor(x, y);
      if (color.red() < 250 || color.green() < 250 || color.blue() < 250) return false;
    }
  }
  return true;
}

bool preview_item_lifecycle(const QString& source_path) {
  auto* controller = new EditorPreviewController;
  MaskInteractionPerformanceTestAccess::seed(*controller, 0, source_path);

  QQuickWindow window;
  window.setColor(Qt::white);
  window.resize(320, 104);

  const QPoint hidden_parent_position{18, 18};
  const QPoint fresh_item_position{142, 18};
  auto* hidden_parent = new QQuickItem(window.contentItem());
  hidden_parent->setPosition(hidden_parent_position);
  hidden_parent->setSize(QSizeF(80, 64));
  auto* hidden_preview = new BrushLivePreviewItem(hidden_parent);
  hidden_preview->setSize(QSizeF(80, 64));
  hidden_preview->setImmediate(true);
  hidden_preview->setController(controller);

  const auto old_points = stroke_points(0.18, 0.20, 0.24, 0.23);
  MaskInteractionPerformanceTestAccess::setStroke(*controller, true, old_points);
  window.show();
  const auto old_frame = capture_window(window);
  const auto old_pixels = item_pixels(old_frame, window, hidden_parent_position, QSize(80, 64));
  if (!has_ink(old_pixels, QRectF(0, 0, 1, 1))) {
    std::cerr << "BrushLivePreviewItem did not paint its initial live stroke\n";
    delete controller;
    return false;
  }

  const auto new_points = stroke_points(0.72, 0.68, 0.80, 0.75);
  auto last_point = new_points.constLast();
  last_point.x = 0.76;
  last_point.y = 0.74;
  auto burst_points = new_points;
  burst_points.append(last_point);
  std::atomic_bool resized_item_painted{false};
  QObject::connect(hidden_preview, &BrushLivePreviewItem::framePainted,
      hidden_preview, [&resized_item_painted] {
        resized_item_painted.store(true, std::memory_order_relaxed);
      }, Qt::DirectConnection);
  MaskInteractionPerformanceTestAccess::setStroke(*controller, false, {});
  MaskInteractionPerformanceTestAccess::setStroke(*controller, true, burst_points);
  const QSize burst_size(70, 50);
  enqueue_geometry_burst(hidden_preview, burst_size);

  const QPoint burst_item_position{230, 18};
  auto* burst_preview = new BrushLivePreviewItem(window.contentItem());
  burst_preview->setPosition(burst_item_position);
  burst_preview->setSize(burst_size);
  burst_preview->setImmediate(true);
  std::atomic_bool fresh_burst_item_painted{false};
  QObject::connect(burst_preview, &BrushLivePreviewItem::framePainted,
      burst_preview, [&fresh_burst_item_painted] {
        fresh_burst_item_painted.store(true, std::memory_order_relaxed);
      }, Qt::DirectConnection);
  burst_preview->setController(controller);

  const auto burst_frame = capture_window_when(window, [&] {
    return resized_item_painted.load(std::memory_order_relaxed) &&
        fresh_burst_item_painted.load(std::memory_order_relaxed);
  });
  const auto resized_pixels = item_pixels(
      burst_frame, window, hidden_parent_position, burst_size);
  const auto fresh_burst_pixels = item_pixels(
      burst_frame, window, burst_item_position, burst_size);
  if (!resized_item_painted.load(std::memory_order_relaxed) ||
      !fresh_burst_item_painted.load(std::memory_order_relaxed) ||
      resized_pixels != fresh_burst_pixels ||
      !has_ink(resized_pixels, QRectF(0.55, 0.45, 0.42, 0.50))) {
    std::cerr << "Visible geometry burst did not paint the current stroke at its final size\n";
    delete controller;
    return false;
  }
  delete burst_preview;

  // End one stroke and begin an equal-sized replacement before any frame can
  // paint the empty state. The same point count must still rebuild from the
  // new stroke rather than reusing the preceding raster.
  auto replacement_points = old_points;
  auto replacement_point = replacement_points.constLast();
  replacement_point.x = 0.21;
  replacement_point.y = 0.26;
  replacement_points.append(replacement_point);
  std::atomic_bool replacement_item_painted{false};
  QObject::connect(hidden_preview, &BrushLivePreviewItem::framePainted,
      hidden_preview, [&replacement_item_painted] {
        replacement_item_painted.store(true, std::memory_order_relaxed);
      }, Qt::DirectConnection);
  MaskInteractionPerformanceTestAccess::setStroke(*controller, false, {});
  MaskInteractionPerformanceTestAccess::setStroke(*controller, true, replacement_points);

  auto* replacement_preview = new BrushLivePreviewItem(window.contentItem());
  replacement_preview->setPosition(burst_item_position);
  replacement_preview->setSize(burst_size);
  replacement_preview->setImmediate(true);
  std::atomic_bool fresh_replacement_painted{false};
  QObject::connect(replacement_preview, &BrushLivePreviewItem::framePainted,
      replacement_preview, [&fresh_replacement_painted] {
        fresh_replacement_painted.store(true, std::memory_order_relaxed);
      }, Qt::DirectConnection);
  replacement_preview->setController(controller);

  const auto replacement_frame = capture_window_when(window, [&] {
    return replacement_item_painted.load(std::memory_order_relaxed) &&
        fresh_replacement_painted.load(std::memory_order_relaxed);
  });
  const auto replacement_pixels = item_pixels(
      replacement_frame, window, hidden_parent_position, burst_size);
  const auto fresh_replacement_pixels = item_pixels(
      replacement_frame, window, burst_item_position, burst_size);
  if (!replacement_item_painted.load(std::memory_order_relaxed) ||
      !fresh_replacement_painted.load(std::memory_order_relaxed) ||
      replacement_pixels != fresh_replacement_pixels ||
      !has_ink(replacement_pixels, QRectF(0.05, 0.05, 0.45, 0.45)) ||
      has_ink(replacement_pixels, QRectF(0.58, 0.50, 0.42, 0.50))) {
    std::cerr << "Same-frame stroke replacement reused stale coverage or missed its paint witness\n";
    delete replacement_preview;
    delete controller;
    return false;
  }
  delete replacement_preview;

  hidden_parent->setVisible(false);
  MaskInteractionPerformanceTestAccess::setStroke(*controller, false, {});
  MaskInteractionPerformanceTestAccess::setStroke(*controller, true, new_points);
  hidden_preview->setSize(QSizeF(72, 52));

  auto* fresh_preview = new BrushLivePreviewItem(window.contentItem());
  fresh_preview->setPosition(fresh_item_position);
  fresh_preview->setSize(QSizeF(72, 52));
  fresh_preview->setImmediate(true);
  fresh_preview->setController(controller);
  const auto fresh_frame = capture_window(window);
  const auto fresh_pixels = item_pixels(fresh_frame, window, fresh_item_position, QSize(72, 52));
  if (!has_ink(fresh_pixels, QRectF(0.55, 0.45, 0.42, 0.50))) {
    std::cerr << "Fresh BrushLivePreviewItem did not paint the replacement live stroke\n";
    delete controller;
    return false;
  }

  hidden_parent->setVisible(true);
  const auto revealed_frame = capture_window(window);
  const auto revealed_pixels = item_pixels(revealed_frame, window, hidden_parent_position, QSize(72, 52));
  if (revealed_pixels != fresh_pixels ||
      !has_ink(revealed_pixels, QRectF(0.55, 0.45, 0.42, 0.50))) {
    std::cerr << "Re-shown BrushLivePreviewItem retained stale pixels after a hidden stroke change and resize\n";
    delete controller;
    return false;
  }

  hidden_parent->setVisible(false);
  MaskInteractionPerformanceTestAccess::setStroke(*controller, false, {});
  hidden_parent->setVisible(true);
  const auto empty_frame = capture_window(window);
  if (!is_blank(item_pixels(empty_frame, window, hidden_parent_position, QSize(72, 52)))) {
    std::cerr << "BrushLivePreviewItem retained pixels after an empty hidden stroke\n";
    delete controller;
    return false;
  }

  MaskInteractionPerformanceTestAccess::setStroke(*controller, true, new_points);
  const auto active_frame = capture_window(window);
  if (!has_ink(item_pixels(active_frame, window, hidden_parent_position, QSize(72, 52)),
          QRectF(0.55, 0.45, 0.42, 0.50))) {
    std::cerr << "BrushLivePreviewItem could not repaint after clearing a hidden stroke\n";
    delete controller;
    return false;
  }

  // Leave a geometry polish queued, then hide, clear the controller state,
  // and destroy the controller before the item can process that polish.
  enqueue_geometry_burst(hidden_preview, QSize(72, 52));
  hidden_parent->setVisible(false);
  MaskInteractionPerformanceTestAccess::setStroke(*controller, false, {});
  delete controller;
  hidden_parent->setVisible(true);
  const auto destroyed_frame = capture_window(window);
  const auto destroyed_pixels = item_pixels(destroyed_frame, window, hidden_parent_position, QSize(72, 52));
  if (!is_blank(destroyed_pixels)) {
    std::cerr << "BrushLivePreviewItem retained pixels after its controller was destroyed while hidden\n";
    return false;
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QTemporaryDir storage;
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", storage.filePath("data").toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", storage.filePath("cache").toUtf8());
  qputenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE", "1");
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, storage.filePath("settings"));
  QApplication app(argc, argv);
  QImage image(QSize(2560, 1440), QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::white);
  const auto source_path = storage.filePath("source.png");
  if (!image.save(source_path)) return 1;
  QJsonArray cases;
  using namespace photo_editor::desktop;
  for (const int bytes : {0, 4 * 1024 * 1024, 16 * 1024 * 1024}) {
    QVector<qint64> samples;
    QByteArray fingerprint;
    for (int trial = 0; trial < 9; ++trial) {
      EditorPreviewController controller;
      MaskInteractionPerformanceTestAccess::seed(controller, bytes, source_path);
      const auto ns = MaskInteractionPerformanceTestAccess::dispatch(controller);
      const auto hash = MaskInteractionPerformanceTestAccess::finish(controller);
      if (hash.isEmpty() || (!fingerprint.isEmpty() && fingerprint != hash)) {
        std::cerr << "missing or unstable brush raster bytes=" << bytes << " trial=" << trial << '\n';
        return 1;
      }
      fingerprint = hash;
      if (trial > 1) samples.append(ns);
    }
    std::sort(samples.begin(), samples.end());
    cases.append(QJsonObject{{"stored_mask_bytes", bytes}, {"median_ns", samples[samples.size() / 2]},
        {"alpha_sha256", QString::fromLatin1(fingerprint)}});
  }
  std::cout << QJsonDocument(QJsonObject{{"cases", cases}}).toJson(QJsonDocument::Compact).constData() << '\n';
  if (!preview_item_lifecycle(source_path)) return 1;
}
