#include "timeline_color_edit_controller.h"
#include "timeline_document_controller.h"
#include "editor_preview_controller.h"
#include "edit_recipe_state.h"
#include "timeline_video_grade_runtime.h"
#include "photo_editor/pipeline/photo_effect_registry.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QImage>
#include <QJsonDocument>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <functional>
#include <iostream>

using namespace photo_editor::desktop;
namespace {
bool wait_for(const std::function<bool()>& ready) {
  QElapsedTimer timer;
  timer.start();
  while (!ready() && timer.elapsed() < 20000) QTest::qWait(10);
  return ready();
}
int fail(const char* message) { std::cerr << message << '\n'; return 1; }
}

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  QTemporaryDir storage;
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", storage.filePath("app").toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", storage.filePath("cache").toUtf8());
  qputenv("PHOTO_EDITOR_DATABASE_URL", "postgresql://127.0.0.1:1/isolated_timeline_color_test");
  QImage source(64, 48, QImage::Format_RGBA8888);
  source.fill(QColor(90, 145, 205));
  source.setColorSpace(QColorSpace::SRgb);
  const auto path = storage.filePath("source.png");
  if (!source.save(path)) return fail("create the isolated clip source");
  const auto ffmpeg = QStandardPaths::findExecutable("ffmpeg");
  if (ffmpeg.isEmpty()) return fail("FFmpeg is required for the real Timeline video fixture");
  const auto video_path = storage.filePath("source.mp4");
  QProcess encode;
  encode.start(ffmpeg, {"-v", "error", "-nostdin", "-loop", "1", "-framerate", "12",
      "-i", path, "-t", "0.5", "-c:v", "libx264", "-pix_fmt", "yuv420p", video_path});
  if (!encode.waitForFinished(20000) || encode.exitCode() != 0) return fail("encode the isolated video fixture");
  TimelineDocumentController document;
  document.createTimelineWithSettings({{"timelineName", "Shared Edit controls"},
      {"width", 64}, {"height", 48}, {"frameRate", 12.0}});
  document.addClipFromGallery("source", video_path, "Source", 0.5, {});
  const auto source_clip_id = document.currentClip().value("clipId").toString();
  if (!document.addAdjustmentClip(0.5)) return fail("create the clip-owned grade target");
  const auto id = document.currentClip().value("clipId").toString();
  const auto initial_recipe = document.colorGradeRecipeJsonForClip(id);
  TimelineColorEditController bridge(&document);
  const auto catalog = bridge.availableEffects();
  if (catalog.size() != static_cast<qsizetype>(photo_editor::pipeline::photo_effect_registry.size()))
    return fail("Timeline discovery must expose every registered shared effect");
  for (const auto& item : catalog) {
    const auto type = item.toMap().value("type").toString();
    if (!photo_editor::pipeline::find_photo_effect(type.toStdString(), 1))
      return fail("Timeline discovery must not advertise an unimplemented effect");
  }
  // The library Add action starts with a closed/inactive Color inspector. Its
  // async frame preparation must retain the requested clip and commit once.
  if (!bridge.addEffect("duotone") || !wait_for([&] {
        return bridge.ready() && document.colorGradeRecipeJsonForClip(id) != initial_recipe;
      })) return fail("adding a catalog effect must activate and prepare the selected clip");
  const auto catalog_recipe = document.colorGradeRecipeJsonForClip(id);
  const auto catalog_state = parse_edit_recipe_json(catalog_recipe);
  if (catalog_state.layers.size() != 1 || qobject_cast<EditorPreviewController*>(bridge.editor())->photoLayerEffects(0).size() != 1)
    return fail("catalog Add must create its real editable effect layer");
  const auto catalog_frame = render_timeline_video_grade_frame(source, catalog_recipe, id,
      timeline_video_rec709_color_space());
  const auto ungraded_frame = render_timeline_video_grade_frame(source,
      initial_recipe.isEmpty() ? serialize_edit_recipe_json(EditRecipeState{}) : initial_recipe, id,
      timeline_video_rec709_color_space());
  if (!catalog_frame.ok() || !ungraded_frame.ok() || catalog_frame.image == ungraded_frame.image) {
    std::cerr << "catalog render: " << catalog_frame.error.toStdString()
              << "; baseline render: " << ungraded_frame.error.toStdString()
              << "; recipe: " << catalog_recipe.toStdString() << '\n';
    return fail("a discovered effect must visibly change the actual Timeline pixels");
  }
  bridge.undo();
  if (!wait_for([&] { return document.colorGradeRecipeJsonForClip(id) == initial_recipe; }))
    return fail("one Timeline Undo must remove both the added effect and its new layer");
  bridge.redo();
  if (!wait_for([&] { return document.colorGradeRecipeJsonForClip(id) == catalog_recipe; }))
    return fail("Timeline Redo must restore the exact catalog effect recipe");
  bridge.undo();
  if (!wait_for([&] { return document.colorGradeRecipeJsonForClip(id) == initial_recipe; }) ||
      bridge.addEffect("unknown_effect")) return fail("unknown catalog effects must be rejected without edits");
  bridge.setActive(false);
  document.selectClipsByIds({source_clip_id});
  const auto video_before_pending = document.colorGradeRecipeJsonForClip(source_clip_id);
  if (!bridge.addEffect("film_grain")) return fail("queue a catalog effect during frame preparation");
  document.selectClipsByIds({id});
  if (!wait_for([&] { return bridge.ready() && bridge.clipId() == id; }) ||
      document.colorGradeRecipeJsonForClip(source_clip_id) != video_before_pending ||
      document.colorGradeRecipeJsonForClip(id) != initial_recipe) {
    std::cerr << "ready=" << bridge.ready() << "; clip=" << bridge.clipId().toStdString()
              << "; expected=" << id.toStdString()
              << "; video unchanged=" << (document.colorGradeRecipeJsonForClip(source_clip_id) == video_before_pending)
              << "; adjustment unchanged=" << (document.colorGradeRecipeJsonForClip(id) == initial_recipe)
              << "; original=" << initial_recipe.toStdString()
              << "; current=" << document.colorGradeRecipeJsonForClip(id).toStdString() << '\n';
    return fail("selection changes must cancel pending catalog additions, never retarget them");
  }
  bridge.setActive(true);
  if (!wait_for([&] { return bridge.ready(); })) {
    std::cerr << bridge.error().toStdString() << '\n';
    return fail("open the real Edit controller for a timeline clip");
  }
  auto* editor = qobject_cast<EditorPreviewController*>(bridge.editor());
  if (!editor || !bridge.editable() || editor->catalogRecipePersistenceEnabled())
    return fail("the timeline editor must be editable and must not save Library recipes");
  QTest::qWait(200);
  if (document.colorGradeRecipeJsonForClip(id) != initial_recipe)
    return fail("opening the shared Color inspector must not author a grade or add Undo history");
  const auto recipe = [&] { return document.colorGradeRecipeJsonForClip(id); };
  const QList<QPair<const char*, double>> controls{
      {"exposure", 0.7}, {"brightness", 17}, {"levelsBlackPoint", 3},
      {"levelsGamma", 1.2}, {"levelsWhitePoint", 94}, {"clarity", 12}, {"dehaze", 7},
      {"luminanceNr", 15}, {"channelMixerRedFromGreen", 9}, {"vibrance", 18}};
  for (const auto& [name, value] : controls) {
    const auto before = recipe();
    if (!editor->setProperty(name, value) || !wait_for([&] { return recipe() != before; })) {
      std::cerr << name << ": " << bridge.error().toStdString() << '\n';
      return fail("an Edit control did not update the timeline's stored recipe");
    }
    if (document.hasColorGradePreview() || editor->recipeSavePending())
      return fail("committed controls must not leave a draft preview or Library save behind");
  }
  const auto before_drag = recipe();
  editor->beginCoalescedEdit();
  editor->setExposure(1.0);
  if (!wait_for([&] { return recipe() != before_drag; })) return fail("the Program grade should update while dragging");
  editor->setExposure(1.3);
  editor->setExposure(1.7);
  editor->endCoalescedEdit();
  if (!wait_for([&] { return parse_edit_recipe_json(recipe()).exposure == 1.7; }))
    return fail("the final drag value must be committed");
  bridge.undo();
  if (!wait_for([&] { return recipe() == before_drag; })) return fail("one Timeline Undo must revert the whole Edit gesture");
  bridge.redo();
  if (!wait_for([&] { return parse_edit_recipe_json(recipe()).exposure == 1.7; }))
    return fail("Timeline Redo must restore the Edit gesture");
  const auto before_comparison = recipe();
  if (!editor->setSectionComparison("light") || !wait_for([&] { return document.hasColorGradePreview(); }) ||
      recipe() != before_comparison || document.previewColorGradeRecipeJsonForClip(id) == before_comparison)
    return fail("Edit section comparisons must project into Program without rewriting the clip");
  editor->clearSectionComparison();
  if (!wait_for([&] { return !document.hasColorGradePreview(); }) || recipe() != before_comparison)
    return fail("releasing section comparison must restore the complete Program grade");

  const auto index = bridge.ensureEffectsLayer();
  if (index < 0) return fail("create a timeline-owned effects layer");
  int checked_effects = 0;
  for (const auto& effect : photo_editor::pipeline::photo_effect_registry) {
    const auto type = QString::fromUtf8(effect.type_id.data(), static_cast<qsizetype>(effect.type_id.size()));
    const auto before = recipe();
    if (!editor->previewPhotoLayerEffect(index, type)) return fail("start registered effect preview");
    if (type == "displacement_map") {
      QImage map(8, 8, QImage::Format_RGBA8888);
      map.fill(QColor(150, 110, 128));
      const auto map_path = storage.filePath("displacement.png");
      if (!map.save(map_path) || !editor->setPhotoLayerDisplacementMap(index,
            editor->photoLayerEffects(index).last().toMap().value("id").toString(), map_path))
        return fail("the Timeline shares Edit's embedded displacement-map import");
    }
    if (
        !wait_for([&] { return document.hasColorGradePreview(); })) {
      std::cerr << type.toStdString() << ": " << bridge.error().toStdString() << '\n';
      return fail("registered Edit effect must preview on the timeline");
    }
    if (recipe() != before || document.previewColorGradeRecipeJsonForClip(id) == before)
      return fail("effect previews must update Program without modifying the saved clip recipe");
    if (checked_effects == 0) {
      const auto draft_copy = storage.filePath("while-previewing.json");
      TimelineDocumentController draft_reopened;
      if (!document.saveTimelineCopyForAutomation(draft_copy).value("ok").toBool() ||
          !draft_reopened.loadTimeline(draft_copy) || draft_reopened.colorGradeRecipeJsonForClip(id) != before)
        return fail("saving during an effect preview must exclude the unapplied preview");
    }
    const auto rendered = render_timeline_video_grade_frame(source,
        document.previewColorGradeRecipeJsonForClip(id), id, timeline_video_rec709_color_space());
    if (!rendered.ok()) {
      std::cerr << type.toStdString() << ": " << rendered.error.toStdString() << '\n';
      return fail("every shared effect must execute through the actual Timeline render graph");
    }
    if (!editor->finishPhotoLayerEffectPreview(false) ||
        !wait_for([&] { return !document.hasColorGradePreview(); }) || recipe() != before)
      return fail("canceling an effect must preserve the original clip recipe exactly");
    ++checked_effects;
  }
  if (checked_effects != static_cast<int>(photo_editor::pipeline::photo_effect_registry.size()))
    return fail("shared effect registry coverage is incomplete");
  if (!editor->previewPhotoLayerEffect(index, "gaussian_blur") ||
      !editor->finishPhotoLayerEffectPreview(true) ||
      !wait_for([&] { return !document.hasColorGradePreview(); }))
    return fail("applying a preview must commit the effect to the clip");
  const auto applied = recipe();
  if (!applied.contains("gaussian_blur")) return fail("the applied effect must be persisted in the shared recipe");

  if (editor->presetNames().isEmpty()) return fail("Edit presets must also be available in Timeline");
  const auto preset = editor->presetNames().first();
  if (!editor->previewPreset(preset) || !wait_for([&] { return document.hasColorGradePreview(); }) || recipe() != applied)
    return fail("preset browsing must remain an unsaved Program preview");
  if (!editor->clearPresetPreview() || !wait_for([&] { return !document.hasColorGradePreview(); }) || recipe() != applied)
    return fail("canceling a preset must restore the saved clip exactly");
  if (!editor->applyPreset(preset) || !wait_for([&] { return recipe() != applied; }))
    return fail("applying the shared preset must commit it to Timeline");
  bridge.undo();
  if (!wait_for([&] { return recipe() == applied; })) return fail("Timeline Undo must revert preset Apply");

  editor->setWhiteBalanceSampling(true);
  bridge.sampleProgramAt(0.5, 0.5);
  if (!wait_for([&] { return !editor->whiteBalanceSampling(); }) || recipe() == applied)
    return fail("the shared white-balance eyedropper must sample the Program source and save the clip");
  const auto saved_recipe = recipe();
  const auto first_export = storage.filePath("shared-edit.mp4");
  if (!document.renderCurrentTimeline(first_export)) {
    std::cerr << document.status().toStdString() << '\n';
    return fail("the shared controls and spatial effect must export through Timeline");
  }
  const auto project_path = storage.filePath("shared-edit.json");
  if (!document.saveTimelineCopyForAutomation(project_path).value("ok").toBool())
    return fail("save a Timeline containing shared Edit controls and effects");
  bridge.setActive(false);
  if (document.hasColorGradePreview() || editor->recipeSavePending())
    return fail("closing Color must release all preview state without touching Library persistence");
  TimelineDocumentController reopened;
  if (!reopened.loadTimeline(project_path) || reopened.colorGradeRecipeJsonForClip(id) != saved_recipe)
    return fail("Timeline save and reopen must preserve the exact Edit recipe");
  const auto second_export = storage.filePath("reopened.mp4");
  if (!reopened.renderCurrentTimeline(second_export)) return fail("reopened shared effects must export");
  const auto decoded_frame = [&](const QString& file) {
    QProcess decode;
    decode.start(ffmpeg, {"-v", "error", "-nostdin", "-i", file, "-frames:v", "1",
        "-f", "rawvideo", "-pix_fmt", "rgb24", "pipe:1"});
    if (!decode.waitForFinished(20000) || decode.exitCode() != 0) return QByteArray{};
    return decode.readAllStandardOutput();
  };
  const auto first_frame = decoded_frame(first_export);
  if (first_frame.isEmpty() || first_frame != decoded_frame(second_export))
    return fail("shared Timeline grade delivery must be pixel identical after reopening");
  document.selectClipsByIds({source_clip_id});
  const auto video_id = document.currentClip().value("clipId").toString();
  bridge.setActive(true);
  if (!wait_for([&] { return bridge.ready() && bridge.clipId() == video_id; }))
    return fail("the same inspector must open directly on a selected video clip");
  editor->setExposure(0.4);
  if (!wait_for([&] { return parse_edit_recipe_json(document.colorGradeRecipeJsonForClip(video_id)).exposure == 0.4; }) ||
      document.colorGradeRecipeJsonForClip(id) != saved_recipe)
    return fail("selecting a video must isolate its grade from the adjustment clip");
  const auto locked_recipe = document.colorGradeRecipeJsonForClip(video_id);
  const auto row_index = document.currentClip().value("trackRowIndex").toInt();
  document.toggleTrackLocked(row_index);
  if (bridge.editable() || bridge.addEffect("duotone"))
    return fail("locked video tracks must disable shared controls and reject catalog Add");
  editor->setExposure(2.0);
  QTest::qWait(50);
  if (document.colorGradeRecipeJsonForClip(video_id) != locked_recipe)
    return fail("Edit controls must not mutate a locked Timeline track");
  bridge.setActive(false);
  std::cout << "PASS: shared Edit controls, coalesced Timeline Undo/Redo, " << checked_effects
            << " effects rendered, draft preview/cancel, presets, Program sampling, and pixel-identical save/reopen exports\n";
  return 0;
}
