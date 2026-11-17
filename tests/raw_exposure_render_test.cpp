#include "photo_render_utils.h"
#include "raw_image_decoder.h"
#include "edit_recipe_v13_persistence.h"
#include "render_plan_store.h"
#include "fixtures/synthetic_dng.h"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QTemporaryDir>

#include <cmath>
#include <array>
#include <cstdlib>
#include <iostream>

namespace {
using namespace photo_editor::desktop;

QImage render(const QString& path, double raw_ev, double light_ev, int edge,
              bool recovery, bool input_transform = true, bool reopen = false) {
  EditRecipeState state;
  state.raw_exposure = raw_ev;
  state.exposure = light_ev;
  state.raw_highlight_recovery = recovery;
  state.raw_input_transform_enabled = input_transform;
  state.raw_demosaic_quality = QStringLiteral("Full");
  QImage decoded;
  photo_editor::pipeline::DecodedImageMetadata metadata;
  if (!decode_source_image(path, &decoded, edge, state, &metadata)) return {};
  if (reopen) {
    DesktopRecipeV13Context context;
    context.recipe_id = QStringLiteral("raw-exposure-export-reopen");
    context.source_id = QStringLiteral("raw-exposure-render-source");
    context.decoder_provenance = QString::fromStdString(metadata.decoder_backend);
    context.raw_source = metadata.is_raw;
    const auto saved = serialize_desktop_recipe_as_canonical_v13(state, context);
    const auto parsed = photo_editor::pipeline::deserialize_edit_recipe_v13(
        saved.document.recipe_payload.toStdString());
    if (!saved.ok() || !parsed) return {};
    const auto restored = project_canonical_v13_to_desktop_state(*parsed);
    if (!restored.ok()) return {};
    state = *restored.state;
    if (!decode_source_image(path, &decoded, edge, state, &metadata)) return {};
  }
  const auto admission = RenderPlanStore::shared().admit(
      QStringLiteral("raw-exposure-render-source"), path, state, decoded.size(),
      photo_editor::pipeline::RenderPurpose::Export,
      photo_editor::pipeline::DisplayRenderingTarget::SrgbSimulation,
      std::nullopt, &metadata);
  if (!admission.admitted || !admission.plan) {
    std::cerr << "canonical RAW exposure export admission failed: "
              << admission.error.toStdString() << '\n';
    return {};
  }
  return render_output_image_with_backend(
      decoded, state, QColorSpace(QColorSpace::SRgb), QImage::Format_RGBA64,
      RenderBackendPreference::CpuOnly, &metadata, nullptr, {},
      photo_editor::pipeline::RemovalQuality::Final,
      RenderOutputPolicy::DisplayReferred, &*admission.plan);
}
}  // namespace

int main(int argc, char* argv[]) {
  QGuiApplication app(argc, argv);
  if (!raw_camera_linear_decoder_capability().available) return 77;
  QTemporaryDir temporary;
  auto path = temporary.filePath(QStringLiteral("exposure.dng"));
  if (argc > 1) {
    path = QString::fromLocal8Bit(argv[1]);
  } else {
    const auto payload = photo_editor::test_fixtures::synthetic_dng();
    QFile fixture(path);
    if (!fixture.open(QIODevice::WriteOnly) ||
        fixture.write(reinterpret_cast<const char*>(payload.data()),
                      static_cast<qint64>(payload.size())) !=
            static_cast<qint64>(payload.size())) return EXIT_FAILURE;
  }
  const auto output = argc > 2 ? QString::fromLocal8Bit(argv[2]) : QString{};
  if (!output.isEmpty() && !QDir().mkpath(output)) return EXIT_FAILURE;
  for (const auto recovery : {false, true}) {
    for (const auto edge : {512, 0}) {
      const auto neutral = render(path, 0.0, 0.0, edge, recovery);
      if (neutral.isNull()) return EXIT_FAILURE;
      for (const auto ev : {1.9, -2.0, 4.0}) {
        const auto raw = render(path, ev, 0.0, edge, recovery);
        const auto reference = render(path, 0.0, ev, edge, recovery);
        const auto combined = render(path, ev - 0.5, 0.5, edge, recovery);
        if (!output.isEmpty()) {
          const auto stem = QStringLiteral("edge-%1-recovery-%2-ev-%3")
                                .arg(edge).arg(recovery).arg(ev);
          if (!raw.save(QDir(output).filePath(stem + "-raw.png")) ||
              !reference.save(QDir(output).filePath(stem + "-reference.png")))
            return EXIT_FAILURE;
        }
        // The independent Light exposure route works on float scene color.
        // RAW exposure must preserve exactly that color/headroom, including
        // values that would saturate the decoder's integer camera channels.
        if (raw.isNull() || reference.isNull() || combined.isNull() ||
            raw != reference || combined != reference || raw == neutral) {
          std::cerr << "RAW exposure changed color or lost exposure intent: edge="
                    << edge << " recovery=" << recovery << " ev=" << ev << '\n';
          return EXIT_FAILURE;
        }
        std::cout << "raw_exposure_float_parity edge=" << edge
                  << " recovery=" << recovery << " ev=" << ev << std::endl;
      }
    }
  }
  // Exercise the decoder-characterized contract independently: it must not
  // bake EV into 16-bit samples before float Light EV cancels or persists it.
  for (const auto& exposure : {std::array{1.9, 0.5}, std::array{1.9, -1.9}}) {
    const auto combined = render(path, exposure[0], exposure[1], 512, false, false);
    const auto reference = render(path, 0.0, exposure[0] + exposure[1], 512, false, false);
    const auto reopened = render(path, exposure[0], exposure[1], 512, false, false, true);
    if (combined.isNull() || reference.isNull() || reopened.isNull() ||
        combined != reference || reopened != reference) {
      std::cerr << "decoder-characterized exposure changed pixels across canonical export/reopen\n";
      return EXIT_FAILURE;
    }
    std::cout << "raw_exposure_combined_export_parity input_transform=0 raw_ev=" << exposure[0]
              << " light_ev=" << exposure[1] << " reopened=1" << std::endl;
  }
  return EXIT_SUCCESS;
}
