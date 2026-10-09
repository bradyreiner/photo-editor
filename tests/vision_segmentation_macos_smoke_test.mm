#include <cstdlib>
#include <iostream>

#include <QCoreApplication>
#include <QColor>
#include <QImage>
#include <QString>

#include "vision_segmentation.h"
#include "object_pick_policy.h"
#include "segmentation_result_schema.h"

namespace {

int fail(const QString& message) {
  std::cerr << message.toStdString() << '\n';
  return EXIT_FAILURE;
}

bool valid_result(const photo_editor::desktop::SegmentationResult& result) {
  return result.success && result.width > 0 && result.height > 0 &&
         result.mask_data.size() == result.width * result.height;
}

}  // namespace

int main(int argc, char** argv) {
  @autoreleasepool {
    QCoreApplication app(argc, argv);
    if (argc >= 5 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--smart-select")) {
      const QImage source(QString::fromLocal8Bit(argv[2]));
      if (source.isNull()) {
        return fail(QStringLiteral("Failed to load the Smart Select source image"));
      }
      QVector<photo_editor::desktop::SmartSelectPrompt> prompts;
      for (auto index = 4; index < argc; ++index) {
        const auto fields = QString::fromLocal8Bit(argv[index]).split(',');
        if (fields.size() != 3) {
          return fail(QStringLiteral("Prompts must use normalized x,y,+ or x,y,- values"));
        }
        bool x_ok = false;
        bool y_ok = false;
        const auto x = fields.at(0).toDouble(&x_ok);
        const auto y = fields.at(1).toDouble(&y_ok);
        if (!x_ok || !y_ok || (fields.at(2) != QStringLiteral("+") &&
                               fields.at(2) != QStringLiteral("-"))) {
          return fail(QStringLiteral("Smart Select prompt is malformed"));
        }
        prompts.append(photo_editor::desktop::SmartSelectPrompt{
            .x = x, .y = y, .negative = fields.at(2) == QStringLiteral("-")});
      }
      const auto result = photo_editor::desktop::generate_smart_select_mask(source, prompts);
      if (!valid_result(result)) {
        return fail(QStringLiteral("Smart Select returned no usable mask: %1")
                        .arg(result.error_message));
      }
      QImage mask(result.width, result.height, QImage::Format_Grayscale8);
      auto nonzero = qint64{0};
      for (auto y = 0; y < result.height; ++y) {
        auto* row = mask.scanLine(y);
        for (auto x = 0; x < result.width; ++x) {
          const auto alpha = static_cast<unsigned char>(
              result.mask_data.at(y * result.width + x));
          row[x] = alpha;
          nonzero += alpha > 0 ? 1 : 0;
        }
      }
      const auto output_path = QString::fromLocal8Bit(argv[3]);
      if (!mask.save(output_path)) {
        return fail(QStringLiteral("Failed to save the Smart Select matte to %1")
                        .arg(output_path));
      }
      std::cout << "mask=" << output_path.toStdString()
                << " dimensions=" << result.width << 'x' << result.height
                << " selectedPixels=" << nonzero;
      for (const auto& prompt : prompts) {
        const auto value = photo_editor::desktop::segmentation_mask_value_at_normalized(
            result.mask_data, result.width, result.height, prompt.x, prompt.y);
        std::cout << " prompt=" << (prompt.negative ? '-' : '+') << ':' << value;
      }
      const auto accepted = photo_editor::desktop::smart_select_result_honors_prompts(
          result, prompts);
      std::cout << " acceptedByPromptPolicy=" << accepted << '\n';
      return accepted ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    if (!photo_editor::desktop::vision_segmentation_available()) {
      return fail(QStringLiteral("Apple Vision segmentation should be available in the macOS build"));
    }

    QImage screenshot(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/site/assets/app-preview.png"));
    if (screenshot.isNull()) {
      return fail(QStringLiteral("Failed to load the representative desktop photo fixture"));
    }

    // Crop away application chrome so Vision sees the actual landscape photo.
    const auto photo = argc == 2 ? QImage(QString::fromLocal8Bit(argv[1])).scaled(1024, 1024, Qt::KeepAspectRatio, Qt::SmoothTransformation) : screenshot.copy(
        qRound(screenshot.width() * 0.058),
        qRound(screenshot.height() * 0.015),
        qRound(screenshot.width() * 0.696),
        qRound(screenshot.height() * 0.857))
                           .scaled(
                               1024,
                               1024,
                               Qt::KeepAspectRatio,
                               Qt::SmoothTransformation);
    if (photo.isNull()) {
      return fail(QStringLiteral("Failed to crop the representative photo"));
    }

    const auto sky = photo_editor::desktop::generate_semantic_mask(
        photo,
        QStringLiteral("sky"));
    if (!valid_result(sky)) {
      return fail(QStringLiteral("The shipped sky semantic runtime returned an invalid matte: %1")
                      .arg(sky.error_message));
    }

    const auto subject = photo_editor::desktop::generate_semantic_mask(
        photo,
        QStringLiteral("subject"));
    if (subject.success && !valid_result(subject)) {
      return fail(QStringLiteral("Apple Vision reported success with an invalid foreground matte"));
    }
    if (!subject.success && subject.error_message.trimmed().isEmpty()) {
      return fail(QStringLiteral("Apple Vision failed without a diagnostic"));
    }

    const auto background = photo_editor::desktop::generate_semantic_mask(
        photo,
        QStringLiteral("background"));
    if (argc == 2) {
      std::cout << "subjectSuccess=" << subject.success << " backgroundSuccess=" << background.success
                << " backgroundError=" << background.error_message.toStdString();
      if (valid_result(background)) {
        const auto center = (background.height / 2) * background.width + background.width / 2;
        std::cout << " backgroundCenterAlpha=" << static_cast<int>(static_cast<unsigned char>(background.mask_data.at(center)));
      }
      std::cout << '\n';
      return valid_result(subject) && valid_result(background) ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    if (background.success && !valid_result(background)) {
      return fail(QStringLiteral("Apple Vision reported success with an invalid background matte"));
    }
    if (!background.success && background.error_message.trimmed().isEmpty()) {
      return fail(QStringLiteral("Apple Vision background selection failed without a diagnostic"));
    }
    if (subject.success && background.success &&
        (background.width != subject.width || background.height != subject.height)) {
      return fail(QStringLiteral("Foreground and background mattes disagree on dimensions"));
    }

    QImage guided_fixture(256, 192, QImage::Format_ARGB32);
    guided_fixture.fill(QColor(32, 62, 92));
    for (int y = 60; y < 132; ++y) {
      for (int x = 96; x < 160; ++x) {
        guided_fixture.setPixelColor(x, y, QColor(216, 128, 76));
      }
    }
    const auto guided = photo_editor::desktop::generate_smart_select_mask(
        guided_fixture,
        QVector<photo_editor::desktop::SmartSelectPrompt>{
            {.x = 0.5, .y = 0.5, .negative = false},
            {.x = 0.4125, .y = 0.5, .negative = false},
            {.x = 0.5875, .y = 0.5, .negative = false},
            {.x = 0.5, .y = 0.3775, .negative = false},
            {.x = 0.5, .y = 0.6225, .negative = false},
            {.x = 0.1625, .y = 0.5, .negative = true},
            {.x = 0.8375, .y = 0.5, .negative = true},
        });
    if (!valid_result(guided)) {
      return fail(QStringLiteral("The evidence-guided fallback returned an invalid matte: %1")
                      .arg(guided.error_message));
    }
    return EXIT_SUCCESS;
  }
}
