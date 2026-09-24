#include "generated_mask_quality.h"

#include <cstdlib>
#include <iostream>

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

QVariantMap mask_with_alpha(
    const QString& kind,
    int width,
    int height,
    const QByteArray& alpha,
    const QByteArray& confidence = {}) {
  QVariantMap mask{
      {QStringLiteral("kind"), kind},
      {QStringLiteral("segmentationData"), alpha},
      {QStringLiteral("segmentationWidth"), width},
      {QStringLiteral("segmentationHeight"), height},
  };
  if (!confidence.isEmpty()) {
    mask.insert(QStringLiteral("confidenceData"), confidence);
    mask.insert(QStringLiteral("confidenceWidth"), width);
    mask.insert(QStringLiteral("confidenceHeight"), height);
  }
  return mask;
}

}  // namespace

int main() {
  using namespace photo_editor::desktop;
  if (!generated_mask_quality_report_for_mask(
           QVariantMap{{QStringLiteral("kind"), QStringLiteral("linear")}},
           false)
           .isEmpty()) {
    return fail("non-generated masks should not manufacture a quality report");
  }
  const auto empty = generated_mask_quality_report_for_mask(
      QVariantMap{{QStringLiteral("kind"), QStringLiteral("subject")}}, false);
  if (empty.value(QStringLiteral("score")).toInt() != 0 ||
      !empty.value(QStringLiteral("flags")).toStringList().contains(
          QStringLiteral("empty"))) {
    return fail("missing generated alpha should report empty at score zero");
  }

  QByteArray centered(100, char(0));
  centered[44] = char(0xff);
  centered[45] = char(0xff);
  centered[54] = char(0xff);
  centered[55] = char(0xff);
  const auto good = generated_mask_quality_report_for_mask(
      mask_with_alpha(QStringLiteral("subject"), 10, 10, centered), false);
  if (good.value(QStringLiteral("score")).toInt() != 100 ||
      good.value(QStringLiteral("label")).toString() != QStringLiteral("Good") ||
      good.value(QStringLiteral("selectedPixels")).toInt() != 4) {
    return fail("bounded centered alpha should receive an exact good report");
  }

  QByteArray edge(100, char(0));
  edge[0] = char(0xff);
  edge[1] = char(0xff);
  const auto clipped = generated_mask_quality_report_for_mask(
      mask_with_alpha(QStringLiteral("subject"), 10, 10, edge), false);
  const auto expected_edge = generated_mask_quality_report_for_mask(
      mask_with_alpha(QStringLiteral("sky"), 10, 10, edge), false);
  if (clipped.value(QStringLiteral("score")).toInt() != 85 ||
      !clipped.value(QStringLiteral("flags")).toStringList().contains(
          QStringLiteral("clipped")) ||
      expected_edge.value(QStringLiteral("flags"))
          .toStringList()
          .contains(QStringLiteral("clipped"))) {
    return fail("edge contact should penalize subjects but remain expected for sky");
  }

  QByteArray confidence(100, char(0));
  confidence[44] = char(64);
  confidence[45] = char(64);
  confidence[54] = char(64);
  confidence[55] = char(64);
  const auto uncertain = generated_mask_quality_report_for_mask(
      mask_with_alpha(
          QStringLiteral("subject"), 10, 10, centered, confidence),
      true);
  const auto uncertain_flags =
      uncertain.value(QStringLiteral("flags")).toStringList();
  if (uncertain.value(QStringLiteral("score")).toInt() != 45 ||
      uncertain.value(QStringLiteral("label")).toString() !=
          QStringLiteral("Problem") ||
      !uncertain_flags.contains(QStringLiteral("low-confidence")) ||
      !uncertain_flags.contains(QStringLiteral("stale"))) {
    return fail("low confidence and stale provenance should combine deterministically");
  }

  QByteArray huge(100, char(0xff));
  huge[0] = char(0);
  const auto huge_report = generated_mask_quality_report_for_mask(
      mask_with_alpha(QStringLiteral("background"), 10, 10, huge), false);
  QByteArray tiny(10000, char(0));
  tiny[5050] = char(0xff);
  const auto tiny_report = generated_mask_quality_report_for_mask(
      mask_with_alpha(QStringLiteral("subject"), 100, 100, tiny), false);
  if (!huge_report.value(QStringLiteral("flags")).toStringList().contains(
          QStringLiteral("huge")) ||
      huge_report.value(QStringLiteral("score")).toInt() != 70 ||
      !tiny_report.value(QStringLiteral("flags")).toStringList().contains(
          QStringLiteral("tiny")) ||
      tiny_report.value(QStringLiteral("score")).toInt() != 55) {
    return fail("huge and tiny coverage thresholds should produce exact bounded penalties");
  }
  return EXIT_SUCCESS;
}
