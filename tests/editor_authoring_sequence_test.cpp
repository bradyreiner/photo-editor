#include "edit_adjustment_patch.h"
#include "editor_preview_controller.h"

#include <QDir>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTemporaryDir>

#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <cstdlib>
#include <iostream>
#include <random>
#include <stdexcept>
#include <vector>

using photo_editor::desktop::EditorPreviewController;

namespace {
QString recipe(const EditorPreviewController& editor) {
  const auto parsed = photo_editor::desktop::parse_adjustment_patch_json(
      editor.currentAdjustmentPatchJson());
  if (!parsed.ok()) throw std::runtime_error("live recipe cannot be decoded");
  return photo_editor::desktop::serialize_edit_recipe_json(*parsed.state);
}

void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

void check_structure(const EditorPreviewController& editor) {
  QSet<QString> layer_ids;
  for (const auto& value : editor.photoLayers()) {
    const auto id = value.toMap().value(QStringLiteral("id")).toString();
    require(!id.isEmpty() && !layer_ids.contains(id), "missing or duplicate layer identity");
    layer_ids.insert(id);
  }
  for (const auto& value : editor.photoLayers()) {
    const auto layer = value.toMap();
    auto ancestor = layer.value(QStringLiteral("parentId")).toString();
    QSet<QString> visited{layer.value(QStringLiteral("id")).toString()};
    while (!ancestor.isEmpty()) {
      require(layer_ids.contains(ancestor) && !visited.contains(ancestor), "orphan or cyclic layer parent");
      visited.insert(ancestor);
      for (const auto& parent : editor.photoLayers())
        if (parent.toMap().value(QStringLiteral("id")).toString() == ancestor) {
          require(parent.toMap().value(QStringLiteral("kind")).toString() == QStringLiteral("group"),
              "layer parent is not a group");
          ancestor = parent.toMap().value(QStringLiteral("parentId")).toString();
          break;
        }
    }
  }
  QSet<QString> component_ids;
  for (const auto& value : editor.maskStack()) {
    const auto component = value.toMap();
    const auto id = component.value(QStringLiteral("id")).toString();
    require(!id.isEmpty() && !component_ids.contains(id), "missing or duplicate mask identity");
    component_ids.insert(id);
  }
  require(editor.photoLayerCount() == editor.photoLayers().size(), "layer count disagrees with content");
  require(editor.maskCount() == editor.maskStack().size(), "mask count disagrees with content");
}

// Fixed arithmetic rather than std::uniform_int_distribution keeps a seed's
// journey identical across standard-library implementations and platforms.
int choose(std::mt19937& random, int size) {
  return static_cast<int>(random() % static_cast<std::uint32_t>(size));
}

void journey(std::uint32_t seed, int steps) {
  EditorPreviewController editor;
  editor.loadAsset(QStringLiteral("sequence-asset"), {}, {}, {}, {}, {}, {});
  std::mt19937 random(seed);
  std::vector<QString> trace;
  struct Coverage { int attempted = 0, applied = 0, rejected = 0, eligible = 0; };
  std::array<Coverage, 20> coverage{};
  // Guarantee useful mutations before random exploration; arbitrary short replay
  // requests still execute the same prefix. Step 4 deliberately probes a lock.
  constexpr std::array prefix{0, 1, 6, 7, 6, 7, 14, 19, 4, 3, 2, 9, 12, 13, 15, 16, 18, 17, 8, 5};
  int inverse_probes = 0;
  int locked_rejections = 0;
  for (int step = 0; step < steps; ++step) {
    const bool mandatory = step < static_cast<int>(prefix.size());
    int action = mandatory ? prefix[static_cast<std::size_t>(step)] : choose(random, 20);
    const int count = editor.photoLayerCount();
    const int masks = editor.maskCount();
    if ((count >= 32 || masks >= 64) &&
        (action <= 2 || action == 4 || action == 12 || action == 14 || action == 15 || action == 16)) action = 3;
    int index = count > 0 ? choose(random, count) : -1;
    if (mandatory) {
      if (step == 2 || step == 3 || step == 4 || step == 5 || step == 7 || step == 8) index = 0;
      if (step == 9) index = 1;
      if (step == 16) index = 2;
      if (step == 17) {
        for (int row = 0; row < count; ++row) {
          const auto group = editor.photoLayers().at(row).toMap();
          if (group.value("kind") != QStringLiteral("group")) continue;
          for (const auto& value : editor.photoLayers())
            if (value.toMap().value("parentId") == group.value("id")) index = row;
        }
      }
      if (step == 19) {
        for (int row = 1; row < count; ++row) {
          const auto candidate = editor.photoLayers().at(row).toMap();
          if (!candidate.value("locked").toBool() && candidate.value("parentId").toString().isEmpty()) {
            index = row;
            break;
          }
        }
      }
    }
    auto& stats = coverage[static_cast<std::size_t>(action)];
    ++stats.attempted;
    trace.push_back(QStringLiteral("%1: action=%2 layer=%3 layers=%4 masks=%5")
        .arg(step).arg(action).arg(index).arg(count).arg(masks));
    try {
      const auto before = recipe(editor);
      const auto layers = editor.photoLayers();
      const auto layer = index >= 0 ? layers.at(index).toMap() : QVariantMap{};
      const bool unlocked = index >= 0 && !layer.value("locked").toBool();
      bool expected_mutation = false;
      bool expected_rejection = false;
      std::optional<bool> accepted;
      switch (action) {
        case 0: expected_mutation = true; accepted = editor.addAdjustmentLayer(QStringLiteral("Adjust")); break;
        case 1: expected_mutation = true; accepted = editor.addFillLayer(QStringLiteral("#b28c42"), QStringLiteral("Fill")); break;
        case 2:
          expected_mutation = !editor.hasActiveMask() || editor.activeMaskEditDisabledReason().isEmpty();
          editor.addMask(choose(random, 2) == 0 ? QStringLiteral("linear") : QStringLiteral("radial"));
          break;
        case 3:
          expected_mutation = unlocked;
          expected_rejection = index >= 0 && !unlocked;
          if (index >= 0) accepted = editor.deletePhotoLayer(index);
          break;
        case 4:
          expected_mutation = unlocked;
          expected_rejection = index >= 0 && !unlocked;
          if (index >= 0) accepted = editor.duplicatePhotoLayerWithMasks(index);
          break;
        case 5: if (index >= 0) {
          const int offset = mandatory ? -1 : (choose(random, 2) == 0 ? -1 : 1);
          int position = 0, siblings = 0;
          for (const auto& value : layers) {
            const auto candidate = value.toMap();
            if (candidate.value("parentId").toString() != layer.value("parentId").toString()) continue;
            if (candidate.value("id") == layer.value("id")) position = siblings;
            ++siblings;
          }
          expected_mutation = unlocked && position + offset >= 0 && position + offset < siblings;
          expected_rejection = !unlocked;
          accepted = editor.movePhotoLayer(index, offset);
        } break;
        case 6: if (index >= 0) {
          const auto opacity = mandatory ? (step == 4 ? 0.25 : 0.75) : static_cast<double>(choose(random, 101)) / 100.0;
          expected_mutation = unlocked && std::abs(layer.value("opacity", 1.0).toDouble() - opacity) >= 0.0001;
          expected_rejection = !unlocked;
          accepted = editor.setPhotoLayerOpacity(index, opacity);
        } break;
        case 7: if (index >= 0) {
          const bool locked = mandatory ? step == 3 : choose(random, 2) != 0;
          expected_mutation = layer.value("locked").toBool() != locked;
          accepted = editor.setPhotoLayerLocked(index, locked);
        } break;
        case 8: {
          const auto exposure = mandatory ? 0.7 : static_cast<double>(choose(random, 41) - 20) / 10.0;
          expected_mutation = std::abs(editor.exposure() - exposure) > 0.0001;
          editor.setExposure(exposure);
        } break;
        case 9: if (editor.maskCount() > 0) {
          editor.setActiveMaskIndex(mandatory ? editor.activeMaskIndex() : choose(random, editor.maskCount()));
          const auto exposure = mandatory ? 0.3 : static_cast<double>(choose(random, 21) - 10) / 10.0;
          expected_mutation = editor.activeMaskEditDisabledReason().isEmpty() &&
              std::abs(editor.activeMaskExposure() - exposure) > 0.0001;
          expected_rejection = !editor.activeMaskEditDisabledReason().isEmpty();
          editor.setActiveMaskExposure(exposure);
        } break;
        case 10: editor.undo(); break;
        case 11: editor.redo(); break;
        case 12: if (editor.maskCount() > 0) {
          editor.setActiveMaskIndex(mandatory ? editor.activeMaskIndex() : choose(random, editor.maskCount()));
          const auto mask = editor.maskStack().at(editor.activeMaskIndex()).toMap();
          expected_mutation = !mask.value("maskSetId").toString().isEmpty() &&
              editor.activeMaskToolRole() == QStringLiteral("selection") && editor.activeMaskEditDisabledReason().isEmpty();
          accepted = editor.duplicateActiveMaskSet();
        } break;
        case 13: if (editor.maskCount() > 0) {
          editor.setActiveMaskIndex(mandatory ? editor.activeMaskIndex() : choose(random, editor.maskCount()));
          expected_mutation = !editor.activeMaskLocked() && editor.activeMaskEditDisabledReason().isEmpty();
          expected_rejection = !expected_mutation;
          editor.removeActiveMask();
        } break;
        case 14: expected_mutation = true;
          accepted = editor.addPointTextLayer(QStringLiteral("Line one\nLine two"), 8, 12, 320, 240); break;
        case 15: expected_mutation = true; accepted = editor.addLayerGroup(QStringLiteral("Group")); break;
        case 16: if (count > 1) {
          const int first = mandatory ? 1 : 0;
          const int second = mandatory ? 2 : index;
          if (second < 0 || second >= count || second == first) break;
          const auto first_layer = layers.at(first).toMap();
          const auto second_layer = layers.at(second).toMap();
          expected_mutation = second == first + 1 && !first_layer.value("locked").toBool() &&
              !second_layer.value("locked").toBool() &&
              first_layer.value("parentId").toString() == second_layer.value("parentId").toString();
          accepted = editor.groupPhotoLayers({first, second}, QStringLiteral("Group"));
        } break;
        case 17:
          expected_mutation = unlocked && layer.value("kind") == QStringLiteral("group");
          if (index >= 0) accepted = editor.ungroupPhotoLayer(index);
          break;
        case 18:
          if (unlocked && !layer.value("parentId").toString().isEmpty()) {
            for (const auto& value : layers) {
              const auto parent = value.toMap();
              if (parent.value("id") == layer.value("parentId"))
                expected_mutation = parent.value("kind") == QStringLiteral("group") && !parent.value("locked").toBool();
            }
          }
          if (index >= 0) accepted = editor.movePhotoLayerOutOfGroup(index);
          break;
        case 19: if (index >= 0 && layer.value("kind") == QStringLiteral("text")) {
          const auto text = QStringLiteral("Edited\nseed %1 step %2").arg(seed).arg(step);
          expected_mutation = unlocked && layer.value("text").toString() != text;
          expected_rejection = !unlocked;
          accepted = editor.setPhotoLayerTextById(layer.value("id").toString(), text);
        } break;
      }
      check_structure(editor);
      const auto current = recipe(editor);
      const bool changed = before != current;
      trace.back() += QStringLiteral(" eligible=%1 changed=%2 accepted=%3")
          .arg(expected_mutation).arg(changed).arg(accepted ? (*accepted ? "true" : "false") : "void/unavailable");
      if (expected_mutation) {
        ++stats.eligible;
        require(changed && (!accepted || *accepted), "eligible authoring operation did not apply");
      }
      if (expected_rejection) require(!changed && (!accepted || !*accepted), "blocked operation mutated its target");
      if (accepted) require(*accepted == changed, "operation result disagrees with actual recipe mutation");
      if (changed) ++stats.applied;
      else ++stats.rejected;
      if (action == 6 && expected_rejection) ++locked_rejections;
      if (changed && (action == 0 || action == 1 || action == 4 || action == 14 || action == 15 || action == 16))
        require(editor.photoLayerCount() > count, "successful layer creation did not add content");
      if (changed && action == 3) require(editor.photoLayerCount() < count, "successful layer deletion left its content");
      if (changed && (action == 2 || action == 12)) require(editor.maskCount() > masks, "successful mask creation did not add content");
      if (changed && action == 13) require(editor.maskCount() < masks, "successful mask deletion left its content");
      // Coalescing is allowed, but the inverse cannot alter the accepted state.
      if (editor.canUndo()) {
        editor.undo();
        require(editor.canRedo(), "Undo did not expose its inverse");
        editor.redo();
        require(recipe(editor) == current, "Undo/Redo changed the current recipe");
        ++inverse_probes;
      }
      if (step % 7 == 0 || step == steps - 1) {
        EditorPreviewController reopened;
        reopened.loadAsset(QStringLiteral("sequence-reopened"), {}, {}, {}, {}, {}, editor.currentAdjustmentPatchJson());
        check_structure(reopened);
        require(recipe(reopened) == current, "serialization/reload changed the recipe");
      }
      if (step == steps - 1 && steps >= static_cast<int>(prefix.size())) {
        for (int action = 0; action < 20; ++action)
          if (action != 10 && action != 11) require(coverage[static_cast<std::size_t>(action)].applied > 0,
              "required operation category never applied");
        require(locked_rejections > 0 && inverse_probes > 0, "lock rejection or inverse coverage missing");
      }
    } catch (const std::exception& error) {
      std::cerr << "authoring_sequence seed=" << seed << " steps=" << step + 1 << " failure=" << error.what() << '\n';
      for (const auto& row : trace) std::cerr << row.toStdString() << '\n';
      throw;
    }
  }
  QJsonArray operations;
  int applied = 0, rejected = 0;
  for (int action = 0; action < 20; ++action) {
    const auto& stats = coverage[static_cast<std::size_t>(action)];
    applied += stats.applied; rejected += stats.rejected;
    operations.push_back(QJsonObject{{"action", action}, {"attempted", stats.attempted}, {"applied", stats.applied},
        {"rejected", stats.rejected}, {"eligible", stats.eligible}});
  }
  const QJsonObject report{{"seed", static_cast<double>(seed)}, {"steps", steps}, {"attempted", steps},
      {"applied", applied}, {"rejected", rejected}, {"inverseProbes", inverse_probes},
      {"lockedRejections", locked_rejections}, {"operations", operations}};
  std::cout << "authoring_coverage " << QJsonDocument(report).toJson(QJsonDocument::Compact).constData() << '\n';
  std::cout << "authoring_sequence seed=" << seed << " steps=" << steps << " status=passed\n";
}

}  // namespace

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  QTemporaryDir runtime;
  if (!runtime.isValid()) return EXIT_FAILURE;
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", runtime.filePath(QStringLiteral("app-data")).toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", runtime.filePath(QStringLiteral("cache")).toUtf8());
  qputenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE", "1");
  // This is deterministic ownership/history exploration, not AI inference or
  // native-pointer qualification. No real user files or models are accessed.
  qputenv("PHOTO_EDITOR_AI_MODEL_DIR", runtime.filePath(QStringLiteral("no-models")).toUtf8());
  try {
    if (argc == 3) {
      bool seed_ok = false;
      bool steps_ok = false;
      const auto seed = QString::fromLocal8Bit(argv[1]).toUInt(&seed_ok);
      const auto steps = QString::fromLocal8Bit(argv[2]).toInt(&steps_ok);
      require(seed_ok && steps_ok && steps > 0 && steps <= 500, "usage: editor_authoring_sequence_test [seed steps(1..500)]");
      journey(seed, steps);
    } else {
      require(argc == 1, "usage: editor_authoring_sequence_test [seed steps(1..500)]");
      for (const auto seed : {7U, 29U, 113U, 701U}) journey(seed, 60);
    }
  } catch (const std::exception&) {
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
