// Independent native-resolution diagnostics, never resample or register away
// reconstruction errors. The caller must establish the training/evaluation split.
namespace {
struct ErrorRegion { QRect rect; double mse = 0; };
std::vector<ErrorRegion> error_regions(const QImage& reference, const QImage& candidate) {
  std::vector<ErrorRegion> regions;
  constexpr int edge = 256;
  for (int y = 0; y < reference.height(); y += edge) {
    for (int x = 0; x < reference.width(); x += edge) {
      const QRect rect(x, y, std::min(edge, reference.width() - x), std::min(edge, reference.height() - y));
      double squared = 0;
      for (int row = rect.top(); row <= rect.bottom(); ++row) {
        for (int col = rect.left(); col <= rect.right(); ++col) {
          const auto a = reference.pixelColor(col, row), b = candidate.pixelColor(col, row);
          for (const int delta : {a.red()-b.red(), a.green()-b.green(), a.blue()-b.blue()}) squared += delta * delta;
        }
      }
      regions.push_back({rect, squared / (rect.width() * rect.height() * 3.0 * 255 * 255)});
    }
  }
  std::stable_sort(regions.begin(), regions.end(), [](const auto& a, const auto& b) { return a.mse > b.mse; });
  return regions;
}
QString file_digest(const QString& path) {
  QFile file(path);
  QCryptographicHash hash(QCryptographicHash::Sha256);
  if (!file.open(QIODevice::ReadOnly) || !hash.addData(&file)) return {};
  return QString::fromLatin1(hash.result().toHex());
}
bool save_new_image(const QImage& image, const QString& path) {
  QFile file(path);
  return file.open(QIODevice::WriteOnly | QIODevice::NewOnly) && image.save(&file, "PNG");
}
QVariantMap reconstruction_report(const QString& references, const QString& candidates, const QString& output) {
  const auto fail = [](const QString& error) { return QVariantMap{{"ok", false}, {"error", error}}; };
  const QDir source(references), rendered(candidates), destination(output);
  if (!source.exists() || !rendered.exists() || !QFileInfo(output).isAbsolute() || QFileInfo::exists(output))
    return fail("Input directories must exist and output must be a new absolute directory.");
  const auto names = rendered.entryList({"*.png"}, QDir::Files, QDir::Name);
  if (names.isEmpty() || names.size() > 256) return fail("Expected 1 to 256 named PNG candidates.");
  for (const auto& name : names) if (!QFileInfo::exists(source.filePath(name))) return fail("Missing reference: " + name);
  if (!QDir().mkpath(output)) return fail("Cannot create diagnostic directory.");
  QVariantList frames;
  double mse_sum = 0, ssim_sum = 0, psnr_sum = 0;
  bool finite_psnr = true;
  for (const auto& name : names) {
    const auto reference_path = source.filePath(name), candidate_path = rendered.filePath(name);
    const auto reference = load_quality_image(reference_path), candidate = load_quality_image(candidate_path);
    auto frame = compare(reference, candidate);
    if (!frame.value("ok").toBool()) return fail(name + ": " + frame.value("error").toString());
    frame.insert("name", name);
    frame.insert("reference", QFileInfo(reference_path).absoluteFilePath());
    frame.insert("candidate", QFileInfo(candidate_path).absoluteFilePath());
    const auto reference_hash = file_digest(reference_path), candidate_hash = file_digest(candidate_path);
    if (reference_hash.isEmpty() || candidate_hash.isEmpty()) return fail("Cannot hash inputs.");
    frame.insert("referenceSha256", reference_hash); frame.insert("candidateSha256", candidate_hash);
    QVariantList crops;
    const auto regions = error_regions(reference, candidate);
    for (size_t index = 0; index < std::min(size_t{3}, regions.size()); ++index) {
      const auto& region = regions[index];
      const auto a = reference.copy(region.rect).convertToFormat(QImage::Format_RGB32);
      const auto b = candidate.copy(region.rect).convertToFormat(QImage::Format_RGB32);
      QImage triptych(a.width() * 3, a.height(), QImage::Format_RGB32);
      triptych.setColorSpace(reference.colorSpace());
      { QPainter painter(&triptych); painter.drawImage(0, 0, a); painter.drawImage(a.width(), 0, b); }
      for (int y = 0; y < a.height(); ++y) for (int x = 0; x < a.width(); ++x) {
        const auto av = a.pixelColor(x, y), bv = b.pixelColor(x, y);
        triptych.setPixelColor(x + 2 * a.width(), y, QColor(std::min(255, 3 * std::abs(av.red()-bv.red())),
            std::min(255, 3 * std::abs(av.green()-bv.green())), std::min(255, 3 * std::abs(av.blue()-bv.blue()))));
      }
      const auto crop_name = QString("%1-worst-%2.png").arg(QFileInfo(name).completeBaseName()).arg(index + 1);
      if (!save_new_image(triptych, destination.filePath(crop_name))) return fail("Cannot save crop.");
      crops.push_back(QVariantMap{{"x", region.rect.x()}, {"y", region.rect.y()}, {"width", region.rect.width()},
          {"height", region.rect.height()}, {"mse", region.mse}, {"image", crop_name}});
    }
    frame.insert("worstRegions", crops);
    mse_sum += frame.value("mse").toDouble(); ssim_sum += frame.value("ssim").toDouble();
    finite_psnr = finite_psnr && !frame.value("identical").toBool();
    psnr_sum += frame.value("psnrDb").toDouble();
    frames.push_back(frame);
  }
  const double count = static_cast<double>(frames.size());
  QVariantMap report{{"ok", true}, {"schema", "framehouse-reconstruction-diagnostics-v1"},
      {"scope", "named-candidate-versus-source; split provenance must be verified separately"},
      {"colorContract", "stored-display-RGB; no color transform, alignment or resampling"},
      {"cropLayout", "reference | candidate | absolute RGB error amplified 3x; native pixels"},
      {"qualityCertified", false}, {"frameCount", frames.size()}, {"meanFrameMse", mse_sum / count},
      {"meanFrameSsim", ssim_sum / count}, {"meanFramePsnrDb", finite_psnr ? QVariant(psnr_sum/count) : QVariant()},
      {"frames", frames}};
  QFile receipt(destination.filePath("report.json"));
  const auto bytes = QJsonDocument::fromVariant(report).toJson(QJsonDocument::Indented);
  if (!receipt.open(QIODevice::WriteOnly | QIODevice::NewOnly) || receipt.write(bytes) != bytes.size())
    return fail("Cannot save report; partial diagnostic directory is not a completed evaluation.");
  return report;
}
bool reconstruction_report_test() {
  QTemporaryDir root;
  if (!root.isValid() || !QDir().mkpath(root.filePath("reference")) || !QDir().mkpath(root.filePath("candidate"))) return false;
  QImage oversized(maximum_quality_edge + 1, 1, QImage::Format_RGB32); oversized.fill(Qt::black);
  if (!save_new_image(oversized, root.filePath("oversized.png")) ||
      !load_quality_image(root.filePath("oversized.png")).isNull() ||
      !load_quality_image(root.filePath("not-an-image.png")).isNull() ||
      compare(oversized, oversized).value("ok").toBool()) return false;
  QImage a(512, 256, QImage::Format_RGB32); a.fill(Qt::black);
  auto b = a.copy(); b.setPixelColor(300, 12, Qt::white);
  const auto regions = error_regions(a, b);
  if (regions.size() != 2 || regions.front().rect.x() != 256 || regions.back().mse != 0) return false;
  if (!save_new_image(a, root.filePath("reference/frame.png")) || !save_new_image(b, root.filePath("candidate/frame.png"))) return false;
  const auto result = reconstruction_report(root.filePath("reference"), root.filePath("candidate"), root.filePath("proof"));
  if (!result.value("ok").toBool() || result.value("qualityCertified").toBool() || result.value("frameCount").toInt() != 1) return false;
  const QImage crop(root.filePath("proof/frame-worst-1.png"));
  if (crop.size() != QSize(768, 256) || crop.pixelColor(44, 12) != QColor(Qt::black) ||
      crop.pixelColor(300, 12) != QColor(Qt::white) || crop.pixelColor(556, 12) != QColor(Qt::white)) return false;
  const auto frame = result.value("frames").toList().front().toMap();
  if (frame.value("referenceSha256").toString().size() != 64 || frame.value("candidateSha256").toString().size() != 64 ||
      frame.value("referenceSha256") == frame.value("candidateSha256")) return false;
  const auto identity = reconstruction_report(root.filePath("reference"), root.filePath("reference"), root.filePath("identity"));
  if (!identity.value("ok").toBool() || identity.value("meanFrameMse").toDouble() != 0 ||
      !identity.value("meanFramePsnrDb").isNull() || identity.value("meanFrameSsim").toDouble() != 1) return false;
  if (reconstruction_report(root.filePath("reference"), root.filePath("candidate"), root.filePath("proof")).value("ok").toBool()) return false;
  if (!QDir().mkpath(root.filePath("missing")) ||
      reconstruction_report(root.filePath("missing"), root.filePath("candidate"), root.filePath("bad")).value("ok").toBool()) return false;
  if (QFileInfo::exists(root.filePath("bad"))) return false;
  if (!QDir().mkpath(root.filePath("wrong-size")) ||
      !save_new_image(a.copy(0, 0, 10, 10), root.filePath("wrong-size/frame.png"))) return false;
  const auto wrong = reconstruction_report(root.filePath("reference"), root.filePath("wrong-size"), root.filePath("wrong-proof"));
  return !wrong.value("ok").toBool() && !QFileInfo::exists(root.filePath("wrong-proof/report.json"));
}
}
