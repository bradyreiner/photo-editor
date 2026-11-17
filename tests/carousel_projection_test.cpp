#include "carousel_document_controller.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QSettings>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace photo_editor::desktop {

struct CarouselHistoryTestAccess {
  struct SelectionAdmissionCase final {
    QStringList input_ids;
    QStringList normalized_ids;
    QString primary_id;
    QStringList admitted_ids;
  };

  static void require(bool value, const char* message) {
    if (!value) {
      throw std::runtime_error(message);
    }
  }

  static QString itemId(int board, int local_index) {
    return QStringLiteral("layer-%1-%2").arg(board, 2, 10, QLatin1Char('0'))
        .arg(local_index, 4, 10, QLatin1Char('0'));
  }

  static void seed(CarouselDocumentController& controller,
                   int item_count,
                   int slide_count,
                   int selection_count) {
    require(item_count > 0 && slide_count > 0 && item_count % slide_count == 0,
            "Fixture item count must be evenly divisible by slide count");
    const auto items_per_slide = item_count / slide_count;
    require(selection_count >= 1 && selection_count <= items_per_slide,
            "Fixture selection count must fit on the first board");

    controller.slides_.clear();
    controller.items_.clear();
    controller.selected_item_ids_.clear();
    controller.selected_slide_indices_ = {0, 8, slide_count - 1};
    controller.has_active_carousel_ = true;
    controller.current_index_ = 0;

    for (int slide_index = 0; slide_index < slide_count; ++slide_index) {
      CarouselDocumentController::Slide slide;
      slide.edit_board_id = QStringLiteral("board-%1").arg(slide_index, 2, 10, QLatin1Char('0'));
      slide.board_name = QStringLiteral("Board %1").arg(slide_index, 2, 10, QLatin1Char('0'));
      slide.background_color = slide_index % 2 == 0 ? QStringLiteral("#f7f3ed")
                                                    : QStringLiteral("#ffffff");
      controller.slides_.push_back(std::move(slide));
    }

    controller.items_.reserve(item_count);
    for (int slide_index = 0; slide_index < slide_count; ++slide_index) {
      const auto board = controller.slideRectInDocument(slide_index);
      const int columns = qMax(1, static_cast<int>(std::floor((board.width() - 48.0) / 40.0)));
      const int rows = (items_per_slide + columns - 1) / columns;
      const auto step_x = (board.width() - 48.0) / static_cast<double>(columns);
      const auto step_y = (board.height() - 40.0) / static_cast<double>(rows);
      for (int local_index = 0; local_index < items_per_slide; ++local_index) {
        CarouselDocumentController::CanvasItem item;
        item.id = itemId(slide_index, local_index);
        item.kind = CarouselDocumentController::ItemKind::Shape;
        item.shape_kind = QStringLiteral("rectangle");
        item.display_label = QStringLiteral("Board %1 layer %2")
                                 .arg(slide_index, 2, 10, QLatin1Char('0'))
                                 .arg(local_index, 4, 10, QLatin1Char('0'));
        const auto column = local_index % columns;
        const auto row = local_index / columns;
        item.bounds = QRectF(board.left() + 24.0 + static_cast<double>(column) * step_x,
                             board.top() + 20.0 + static_cast<double>(row) * step_y,
                             qMin(26.0, qMax(2.0, step_x - 4.0)),
                             qMin(22.0, qMax(2.0, step_y - 4.0)));
        item.hidden = local_index % 19 == 0;
        item.locked = local_index % 7 == 6;
        if (slide_index == 0 && local_index < 12) {
          item.object_group_id = QStringLiteral("group-feature-card");
        }
        if (slide_index == 0 && local_index < 8) {
          item.constraint_stack_id = QStringLiteral("stack-feature-card");
          item.constraint_stack_order = local_index;
          item.constraint_stack_axis = QStringLiteral("horizontal");
          item.constraint_stack_padding = 12.0;
          item.constraint_stack_gap = 8.0;
        }
        if (slide_index == 0 && local_index < selection_count) {
          controller.selected_item_ids_.push_back(item.id);
        }
        controller.items_.push_back(std::move(item));
      }
    }

    controller.selected_item_id_ = itemId(0, selection_count > 6 ? 6 : 0);
    controller.document_dirty_ = false;
  }

  static QRectF selectionBounds(const CarouselDocumentController& controller, int selection_count) {
    QRectF bounds;
    bool has_bounds = false;
    for (int local_index = 0; local_index < selection_count; ++local_index) {
      const auto& item = controller.items_.at(local_index);
      bounds = has_bounds ? bounds.united(item.bounds) : item.bounds;
      has_bounds = true;
    }
    return bounds;
  }

  static QStringList selectedIds(const CarouselDocumentController& controller) {
    return controller.selected_item_ids_;
  }

  static SelectionAdmissionCase selectionAdmissionCase(int item_count,
                                                        int selection_count,
                                                        bool distributed) {
    SelectionAdmissionCase result;
    const auto items_per_slide = item_count / 20;
    QSet<QString> requested_ids;
    requested_ids.reserve(selection_count);
    for (int index = 0; index < selection_count; ++index) {
      const auto global_index = distributed && selection_count > 1
                                    ? static_cast<int>((static_cast<qint64>(index) * (item_count - 1)) /
                                                       (selection_count - 1))
                                    : distributed ? item_count - 1 - index : index;
      const auto id = itemId(global_index / items_per_slide, global_index % items_per_slide);
      requested_ids.insert(id);
      result.normalized_ids.push_back(id);
      result.input_ids.push_back(QStringLiteral("  %1  ").arg(id));
      result.input_ids.push_back(id);
    }
    result.input_ids.push_back(QString{});
    result.input_ids.push_back(QStringLiteral("   "));
    result.input_ids.push_back(QStringLiteral(" missing-layer-id "));

    for (int global_index = item_count - 1; global_index >= 0; --global_index) {
      const auto id = itemId(global_index / items_per_slide, global_index % items_per_slide);
      if (!requested_ids.contains(id)) {
        result.primary_id = id;
        break;
      }
    }
    require(!result.primary_id.isEmpty(), "Could not choose an unselected primary layer");
    result.admitted_ids = result.normalized_ids;
    result.admitted_ids.push_back(result.primary_id);
    return result;
  }

  static QStringList normalizeSelection(const CarouselDocumentController& controller,
                                       const QStringList& item_ids) {
    return controller.normalizedSelectedItemIds(item_ids);
  }

  static void setSelection(CarouselDocumentController& controller,
                           const QStringList& item_ids,
                           const QString& primary_id) {
    controller.setSelectedItemIds(item_ids, primary_id);
  }
};

}  // namespace photo_editor::desktop

namespace {

using photo_editor::desktop::CarouselDocumentController;
using photo_editor::desktop::CarouselHistoryTestAccess;

void require(bool value, const char* message) {
  CarouselHistoryTestAccess::require(value, message);
}

QString fingerprint(const QVariant& value) {
  const auto json = QJsonDocument::fromVariant(value).toJson(QJsonDocument::Compact);
  require(!json.isEmpty(), "Getter result could not be encoded as JSON");
  return QString::fromLatin1(QCryptographicHash::hash(json, QCryptographicHash::Sha256).toHex());
}

QString fingerprint(const QJsonArray& value) {
  const auto json = QJsonDocument(value).toJson(QJsonDocument::Compact);
  require(!json.isEmpty(), "Selection result could not be encoded as JSON");
  return QString::fromLatin1(QCryptographicHash::hash(json, QCryptographicHash::Sha256).toHex());
}

double median(std::vector<double> values) {
  require(!values.empty(), "No timing samples were captured");
  std::sort(values.begin(), values.end());
  return values.at(values.size() / 2);
}

double percentile95(std::vector<double> values) {
  require(!values.empty(), "No timing samples were captured");
  std::sort(values.begin(), values.end());
  const auto index = static_cast<std::size_t>(std::ceil(static_cast<double>(values.size()) * 0.95)) - 1;
  return values.at(index);
}

QVariantList timedSlides(CarouselDocumentController& controller, std::vector<double>& samples) {
  QElapsedTimer timer;
  timer.start();
  auto result = controller.slides();
  samples.push_back(static_cast<double>(timer.nsecsElapsed()) / 1'000'000.0);
  return result;
}

QVariantList timedStoryboardEntries(CarouselDocumentController& controller, std::vector<double>& samples) {
  QElapsedTimer timer;
  timer.start();
  auto result = controller.storyboardEntries();
  samples.push_back(static_cast<double>(timer.nsecsElapsed()) / 1'000'000.0);
  return result;
}

QVariantMap timedSelectionDetails(CarouselDocumentController& controller, std::vector<double>& samples) {
  QElapsedTimer timer;
  timer.start();
  auto result = controller.selectionDetails();
  samples.push_back(static_cast<double>(timer.nsecsElapsed()) / 1'000'000.0);
  return result;
}

QJsonObject summarize(const std::vector<double>& samples) {
  return {{QStringLiteral("median_ms"), median(samples)},
          {QStringLiteral("p95_ms"), percentile95(samples)}};
}

QJsonArray jsonArray(const QStringList& values) {
  QJsonArray array;
  for (const auto& value : values) {
    array.push_back(value);
  }
  return array;
}

QJsonObject measureSelectionAdmission(CarouselDocumentController& controller,
                                     int item_count,
                                     int selection_count,
                                     int repeats) {
  const auto measure_case = [&](bool distributed) {
    const auto fixture = CarouselHistoryTestAccess::selectionAdmissionCase(
        item_count, selection_count, distributed);
    std::vector<double> normalize_times;
    std::vector<double> setter_times;
    QStringList normalized;
    QStringList admitted;
    for (int iteration = 0; iteration < repeats + 1; ++iteration) {
      QElapsedTimer timer;
      timer.start();
      normalized = CarouselHistoryTestAccess::normalizeSelection(controller, fixture.input_ids);
      const auto normalize_elapsed = static_cast<double>(timer.nsecsElapsed()) / 1'000'000.0;

      timer.restart();
      CarouselHistoryTestAccess::setSelection(controller, fixture.input_ids, fixture.primary_id);
      const auto setter_elapsed = static_cast<double>(timer.nsecsElapsed()) / 1'000'000.0;
      admitted = CarouselHistoryTestAccess::selectedIds(controller);
      if (iteration > 0) {
        normalize_times.push_back(normalize_elapsed);
        setter_times.push_back(setter_elapsed);
      }
    }
    require(normalized == fixture.normalized_ids,
            "Selection normalizer changed trim, de-duplication, missing-ID, or order semantics");
    require(admitted == fixture.admitted_ids,
            "Selection setter changed primary append or selection ordering semantics");
    const auto input_json = jsonArray(fixture.input_ids);
    const auto normalized_json = jsonArray(normalized);
    const auto admitted_json = jsonArray(admitted);
    return QJsonObject{
        {QStringLiteral("layout"), distributed ? QStringLiteral("distributed")
                                               : QStringLiteral("clustered")},
        {QStringLiteral("requested_valid_ids"), selection_count},
        {QStringLiteral("input_ids_including_duplicates"), static_cast<int>(fixture.input_ids.size())},
        {QStringLiteral("contract_parity"), true},
        {QStringLiteral("normalizer"), summarize(normalize_times)},
        {QStringLiteral("set_selected_item_ids"), summarize(setter_times)},
        {QStringLiteral("input_sha256"), fingerprint(input_json)},
        {QStringLiteral("normalized_sha256"), fingerprint(normalized_json)},
        {QStringLiteral("admitted_sha256"), fingerprint(admitted_json)}};
  };
  return {{QStringLiteral("items"), item_count},
          {QStringLiteral("selected_items"), selection_count},
          {QStringLiteral("repeats"), repeats},
          {QStringLiteral("clustered"), measure_case(false)},
          {QStringLiteral("distributed"), measure_case(true)}};
}

void verifyProjection(const CarouselDocumentController& controller,
                      const QVariantList& slides,
                      const QVariantList& storyboard,
                      const QVariantMap& selection,
                      int item_count,
                      int slide_count,
                      int selection_count) {
  require(slides.size() == static_cast<qsizetype>(slide_count), "slides() changed board order or count");
  require(storyboard.size() == static_cast<qsizetype>(slide_count),
          "storyboardEntries() changed board order or count");

  const auto items_per_slide = item_count / slide_count;
  const auto hidden_count = (items_per_slide + 18) / 19;
  const auto visible_count = items_per_slide - hidden_count;
  for (int slide_index = 0; slide_index < slide_count; ++slide_index) {
    const auto slide = slides.at(slide_index).toMap();
    const auto board = storyboard.at(slide_index).toMap();
    const auto first_visible_label = QStringLiteral("Board %1 layer %2")
                                         .arg(slide_index, 2, 10, QLatin1Char('0'))
                                         .arg(1, 4, 10, QLatin1Char('0'));
    const auto expected_label = QStringLiteral("%1 + %2 more")
                                    .arg(first_visible_label)
                                    .arg(visible_count - 1);
    require(slide.value(QStringLiteral("index")).toInt() == slide_index,
            "slides() index order changed");
    require(board.value(QStringLiteral("index")).toInt() == slide_index,
            "storyboardEntries() index order changed");
    require(slide.value(QStringLiteral("boardId")).toString() ==
                QStringLiteral("board-%1").arg(slide_index, 2, 10, QLatin1Char('0')),
            "slides() board identity changed");
    require(slide.value(QStringLiteral("itemCount")).toInt() == visible_count,
            "slides() visible layer count changed");
    require(slide.value(QStringLiteral("label")).toString() == expected_label,
            "slides() summary label changed");
    require(board.value(QStringLiteral("itemCount")).toInt() == visible_count,
            "storyboardEntries() visible layer count changed");
  }

  require(selection.value(QStringLiteral("selectedCount")).toInt() == selection_count,
          "Selection count changed");
  int locked_selected_count = 0;
  for (int local_index = 0; local_index < selection_count; ++local_index) {
    locked_selected_count += local_index % 7 == 6 ? 1 : 0;
  }
  require(selection.value(QStringLiteral("editableSelectionCount")).toInt() ==
              selection_count - locked_selected_count,
          "Locked layer filtering changed editable selection count");
  require(selection.value(QStringLiteral("hasUnlockedSelection")).toBool(),
          "Unlocked selection presence changed");
  require(selection.value(QStringLiteral("hasMultipleSelection")).toBool() == (selection_count > 1),
          "Multiple selection state changed");
  require(selection.value(QStringLiteral("locked")).toBool() == (selection_count > 6),
          "Current locked layer state changed");
  require(selection.value(QStringLiteral("objectGroupId")).toString() ==
              QStringLiteral("group-feature-card"),
          "Selected object group identity changed");
  require(selection.value(QStringLiteral("objectGroupCount")).toInt() == 12,
          "Selected object group count changed");
  require(selection.value(QStringLiteral("constraintStackId")).toString() ==
              QStringLiteral("stack-feature-card"),
          "Selected constraint stack identity changed");
  require(selection.value(QStringLiteral("constraintStackCount")).toInt() == 8,
          "Selected constraint stack count changed");
  require(selection.value(QStringLiteral("constraintStackAxis")).toString() == QStringLiteral("horizontal"),
          "Constraint stack axis changed");

  const auto ids = selection.value(QStringLiteral("selectedItemIds")).toList();
  require(ids.size() == selection_count, "Selected layer ID count changed");
  for (qsizetype index = 0; index < ids.size(); ++index) {
    require(ids.at(index).toString() == CarouselHistoryTestAccess::itemId(0, static_cast<int>(index)),
            "Selected layer ordering changed");
  }

  const auto expected_bounds = CarouselHistoryTestAccess::selectionBounds(controller, selection_count);
  require(selection.value(QStringLiteral("selectionX")).toDouble() == expected_bounds.x() &&
              selection.value(QStringLiteral("selectionY")).toDouble() == expected_bounds.y() &&
              selection.value(QStringLiteral("selectionWidth")).toDouble() == expected_bounds.width() &&
              selection.value(QStringLiteral("selectionHeight")).toDouble() == expected_bounds.height(),
          "Selection bounds changed");
}

}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QTemporaryDir workspace;
  if (!workspace.isValid()) {
    return 2;
  }
  qputenv("PHOTO_EDITOR_APP_DATA_ROOT", workspace.path().toUtf8());
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", workspace.path().toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", workspace.filePath(QStringLiteral("cache")).toUtf8());
  qputenv("PHOTO_EDITOR_CAROUSEL_DOCUMENTS_PATH",
          workspace.filePath(QStringLiteral("carousels")).toUtf8());
  qputenv("XDG_CONFIG_HOME", workspace.filePath(QStringLiteral("config")).toUtf8());
  qputenv("XDG_CACHE_HOME", workspace.filePath(QStringLiteral("cache")).toUtf8());
  qputenv("XDG_DATA_HOME", workspace.filePath(QStringLiteral("data")).toUtf8());
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, workspace.path());

  QApplication app(argc, argv);
  QCoreApplication::setOrganizationName(QStringLiteral("LooksyPerformanceProbe"));
  QCoreApplication::setApplicationName(QStringLiteral("CarouselProjection"));

  bool item_count_ok = true;
  bool repeat_count_ok = true;
  bool selection_count_ok = true;
  const int item_count = argc > 1 ? QString::fromLocal8Bit(argv[1]).toInt(&item_count_ok) : 10000;
  const int repeats = argc > 2 ? QString::fromLocal8Bit(argv[2]).toInt(&repeat_count_ok) : 7;
  const int selection_count = argc > 4
                                  ? QString::fromLocal8Bit(argv[4]).toInt(&selection_count_ok)
                                  : 256;
  const auto mode = argc > 5 ? QString::fromLocal8Bit(argv[5]) : QStringLiteral("projection");
  if (!item_count_ok || !repeat_count_ok || item_count < 5120 || item_count % 20 != 0 ||
      repeats < 3 || repeats > 101 || !selection_count_ok || selection_count < 1 ||
      selection_count > item_count / 20 || (mode != QStringLiteral("projection") && mode != QStringLiteral("admission")) ||
      argc > 6) {
    std::cerr << "usage: carousel_projection_test [item-count-divisible-by-20] [repeats>=3] [output.json] [selection-count] [projection|admission]\n";
    return 2;
  }

  constexpr int slide_count = 20;
  try {
    CarouselDocumentController controller;
    CarouselHistoryTestAccess::seed(controller, item_count, slide_count, selection_count);

    if (mode == QStringLiteral("admission")) {
      const auto report = measureSelectionAdmission(controller, item_count, selection_count, repeats);
      const auto json = QJsonDocument(report).toJson(QJsonDocument::Indented);
      if (argc >= 4) {
        QFile output(QString::fromLocal8Bit(argv[3]));
        if (!output.open(QIODevice::WriteOnly) || output.write(json) != json.size()) {
          throw std::runtime_error("Could not write JSON output file");
        }
      }
      std::cout << QJsonDocument(report).toJson(QJsonDocument::Compact).constData() << '\n';
      return 0;
    }

    std::vector<double> slide_times;
    std::vector<double> storyboard_times;
    std::vector<double> selection_times;
    auto slides = timedSlides(controller, slide_times);
    auto storyboard = timedStoryboardEntries(controller, storyboard_times);
    auto selection = timedSelectionDetails(controller, selection_times);
    slide_times.clear();
    storyboard_times.clear();
    selection_times.clear();
    for (int iteration = 0; iteration < repeats; ++iteration) {
      slides = timedSlides(controller, slide_times);
      storyboard = timedStoryboardEntries(controller, storyboard_times);
      selection = timedSelectionDetails(controller, selection_times);
    }
    verifyProjection(controller, slides, storyboard, selection, item_count, slide_count, selection_count);

    QJsonObject report{
        {QStringLiteral("items"), item_count},
        {QStringLiteral("slides"), slide_count},
        {QStringLiteral("selected_items"), selection_count},
        {QStringLiteral("repeats"), repeats},
        {QStringLiteral("contract_parity"), true},
        {QStringLiteral("slides_getter"), summarize(slide_times)},
        {QStringLiteral("storyboard_entries_getter"), summarize(storyboard_times)},
        {QStringLiteral("selection_details_getter"), summarize(selection_times)},
        {QStringLiteral("slides_sha256"), fingerprint(slides)},
        {QStringLiteral("storyboard_entries_sha256"), fingerprint(storyboard)},
        {QStringLiteral("selection_details_sha256"), fingerprint(selection)},
    };

    const auto json = QJsonDocument(report).toJson(QJsonDocument::Indented);
    if (argc >= 4) {
      QFile output(QString::fromLocal8Bit(argv[3]));
      if (!output.open(QIODevice::WriteOnly) || output.write(json) != json.size()) {
        throw std::runtime_error("Could not write JSON output file");
      }
    }
    std::cout << QJsonDocument(report).toJson(QJsonDocument::Compact).constData() << '\n';
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
