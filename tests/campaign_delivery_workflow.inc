// Included by the controller test so delivery exercises the actual renderer,
// persistence and asynchronous export worker rather than a second implementation.
int run_campaign_delivery_workflow(const QDir& directory) {
  using namespace photo_editor::desktop;
  qputenv("PHOTO_EDITOR_DATABASE_URL", QByteArrayLiteral("postgres://example/test"));
  const auto geometry = campaign_delivery_geometry({240, 120}, {1080, 1920}, false);
  if (!geometry.valid || geometry.cropped || geometry.render_size != QSize(1080, 540) ||
      geometry.placement != QRectF(0, 690, 1080, 540) ||
      campaign_delivery_geometry({0, 0}, {1080, 1080}, false).valid ||
      campaign_delivery_geometry({16384, 64}, {1080, 1920}, true).valid)
    return fail("Fit geometry preserves proportions and excessive crop allocations fail closed");

  QImage source(240, 120, QImage::Format_ARGB32);
  source.setColorSpace(QColorSpace::SRgb);
  source.fill(Qt::red);
  {
    QPainter painter(&source);
    painter.fillRect(QRect(120, 0, 120, 120), Qt::blue);
  }
  const auto path = write_image(directory, QStringLiteral("campaign-source.png"), source);
  const auto destination = directory.filePath(QStringLiteral("delivery"));
  if (path.isEmpty() || !QDir().mkpath(destination)) return fail("Create the delivery fixture");
  CarouselDocumentController controller;
  controller.createCarouselWithSettings({{"carouselName", "Campaign"}, {"documentFormat", "custom"},
      {"customPixelWidth", 240}, {"customPixelHeight", 120}, {"slideCount", 1}});
  controller.assignPrimaryImage(path, QStringLiteral("Two color artwork"));
  controller.setExportDirectory(destination);
  controller.setExportFilenamePattern(QStringLiteral("campaign-{board_number}"));
  controller.setExportProofSheetEnabled(true);
  const auto original_plan = controller.exportOutputPreview();
  if (!controller.exportConfiguredCarousel()) return fail("Render the ordinary original as a color-managed reference");
  const QImage reference(original_plan.first().toMap().value("outputPath").toString());
  if (reference.isNull()) return fail("Read the ordinary exported reference");
  const auto left_color = reference.pixelColor(reference.width() / 4, reference.height() / 2);
  const auto right_color = reference.pixelColor(reference.width() * 3 / 4, reference.height() / 2);
  if (original_plan.size() != 3 || !controller.setExportDeliveryFormats({"landscape", "square", "story", "square"}) ||
      controller.exportDeliveryFormatIds() != QStringList{"square", "story", "landscape"})
    return fail("Original output stays the default and format selection has deterministic order");
  if (controller.setExportDeliveryFormats({}) || controller.setExportDeliveryFormats({"bogus"}) ||
      controller.setExportDeliveryFitMode(QStringLiteral("stretch")))
    return fail("Invalid output settings cannot create empty jobs or stretched deliveries");
  controller.undo();
  if (controller.exportDeliveryFormatIds() != QStringList{"original"}) return fail("One undo restores the previous delivery selection");
  controller.redo();
  if (controller.exportDeliveryFormatIds() != QStringList{"square", "story", "landscape"}) return fail("Redo restores every delivery selection");
  if (!controller.saveDocument()) return fail("Save the editable campaign with its delivery settings");
  const auto document_id = controller.savedDocumentId();
  const auto saved_plan = controller.exportOutputPreview();
  if (saved_plan.size() != 5 || !controller.startConfiguredExport()) return fail("Start one asynchronous multi-format export");
  // Worker framing must come from the captured plan, even when ordinary export
  // settings change while the original job is running.
  controller.setExportImageQuality(80);
  if (!wait_until([&] { return !controller.exportBusy(); }, 30000) ||
      controller.exportProgress().value("state").toString() != QStringLiteral("completed") ||
      controller.exportProgress().value("completedCount").toInt() != 3 ||
      controller.exportProgress().value("totalCount").toInt() != 3)
    return fail(qPrintable(QStringLiteral("Every delivery completes in the worker: ") + controller.status()));
  for (const auto& value : saved_plan) {
    const auto plan = value.toMap();
    const auto output_path = plan.value("outputPath").toString();
    if (plan.value("boardIndex", -1).toInt() < 0) {
      if (!QFileInfo::exists(output_path)) return fail("Delivery writes its proof and manifest alongside all sizes");
      continue;
    }
    const QImage output(output_path);
    if (output.size() != QSize(plan.value("width").toInt(), plan.value("height").toInt()) ||
        output.colorSpace() != QColorSpace(QColorSpace::SRgb) ||
        output.pixelColor(0, 0).alpha() != 0 ||
        output.pixelColor(output.width() / 4, output.height() / 2) != left_color ||
        output.pixelColor(output.width() * 3 / 4, output.height() / 2) != right_color)
      {
        std::cerr << plan.value("resolvedName").toString().toStdString()
                  << " size=" << output.width() << "x" << output.height()
                  << " alpha=" << output.pixelColor(0, 0).alpha()
                  << " profile=" << output.colorSpace().description().toStdString()
                  << " left=" << output.pixelColor(output.width() / 4, output.height() / 2).name().toStdString()
                  << " right=" << output.pixelColor(output.width() * 3 / 4, output.height() / 2).name().toStdString() << '\n';
        return fail("Each fitted file has exact dimensions, preserved artwork, transparent padding and an sRGB profile");
      }
  }
  QFile manifest(saved_plan.last().toMap().value("outputPath").toString());
  if (!manifest.open(QIODevice::ReadOnly)) return fail("Read delivery manifest");
  const auto outputs = QJsonDocument::fromJson(manifest.readAll()).object().value("outputs").toArray();
  if (outputs.size() != 3) return fail("Manifest lists every format independently");
  for (qsizetype index = 0; index < outputs.size(); ++index) {
    const auto entry = outputs[index].toObject();
    const auto plan = saved_plan[index].toMap();
    QFile delivered(plan.value("outputPath").toString());
    if (!delivered.open(QIODevice::ReadOnly) || entry.value("width").toInt() != plan.value("width").toInt() ||
        entry.value("height").toInt() != plan.value("height").toInt() ||
        entry.value("deliveryFormat").toString() != plan.value("deliveryFormat").toString() ||
        entry.value("sha256").toString().toLatin1() != QCryptographicHash::hash(delivered.readAll(), QCryptographicHash::Sha256).toHex())
      return fail("Manifest dimensions and hashes describe the actual delivered files");
  }
  CarouselDocumentController reopened;
  if (!reopened.loadDocument(document_id) ||
      reopened.exportDeliveryFormatIds() != QStringList{"square", "story", "landscape"} ||
      reopened.exportDeliveryFitMode() != QStringLiteral("fit"))
    return fail("Reopening retains the delivery recipe and original editable artwork");
  reopened.setExportDirectory(destination);
  reopened.setExportCollisionPolicy(QStringLiteral("rename"));
  auto renamed = reopened.exportOutputPreview();
  if (renamed.first().toMap().value("action").toString() != QStringLiteral("rename"))
    return fail("Multi-format exports retain collision-safe rename planning");
  reopened.setExportCollisionPolicy(QStringLiteral("skip"));
  if (!reopened.exportConfiguredCarousel()) return fail("Existing outputs can be skipped with their proof and manifest");
  reopened.setExportCollisionPolicy(QStringLiteral("rename"));
  if (!reopened.setExportDeliveryFormats({"square"}) || !reopened.setExportDeliveryFitMode(QStringLiteral("fill")))
    return fail("Select one filled square delivery");
  const auto filled_plan = reopened.exportOutputPreview();
  bool crop_warning = false;
  for (const auto& issue : reopened.exportPreflightIssues())
    crop_warning |= issue.toMap().value("code").toString() == QStringLiteral("delivery-center-crop");
  if (!crop_warning || !reopened.exportConfiguredCarousel()) return fail("Center crop is disclosed before export");
  const QImage filled(filled_plan.first().toMap().value("outputPath").toString());
  if (filled.size() != QSize(1080, 1080) || filled.pixelColor(0, 0).alpha() != 255 ||
      filled.pixelColor(100, 100) != left_color || filled.pixelColor(980, 980) != right_color ||
      QImage(path) != source)
    return fail("Fill covers the target without modifying the source file");
  if (!reopened.startConfiguredExport()) return fail("Start a cancellable delivery");
  reopened.cancelExport();
  if (!wait_until([&] { return !reopened.exportBusy(); }, 30000) ||
      reopened.exportProgress().value("state").toString() != QStringLiteral("cancelled"))
    return fail("Delivery cancellation reaches a stable stopped state");
  bool retry_started = false;
  if (!wait_until([&] {
        if (!retry_started) retry_started = reopened.retryFailedExport();
        return retry_started;
      }, 5000) ||
      !wait_until([&] { return !reopened.exportBusy(); }, 30000) ||
      reopened.exportProgress().value("state").toString() != QStringLiteral("completed"))
    return fail("A cancelled multi-format job can be retried successfully");
  const auto proof = qEnvironmentVariable("PHOTO_EDITOR_CAMPAIGN_PROOF_DIR");
  if (!proof.isEmpty()) {
    if (!QDir().mkpath(proof)) return fail("Create delivery artifact evidence");
    for (const auto& value : saved_plan) {
      const auto plan = value.toMap();
      const auto target = QDir(proof).filePath(plan.value("resolvedName").toString());
      QFile::remove(target);
      if (!QFile::copy(plan.value("outputPath").toString(), target)) return fail("Retain delivery proof artifacts");
    }
    const auto target = QDir(proof).filePath(QStringLiteral("filled-square.png"));
    QFile::remove(target);
    if (!QFile::copy(filled_plan.first().toMap().value("outputPath").toString(), target)) return fail("Retain crop proof");
  }
  reopened.createCarouselWithSettings({{"documentFormat", "custom"}, {"customPixelWidth", 16384},
      {"customPixelHeight", 64}, {"slideCount", 1}, {"primaryMediaPath", path}});
  reopened.setExportDirectory(destination);
  reopened.setExportDeliveryFormats({"story"});
  reopened.setExportDeliveryFitMode(QStringLiteral("fill"));
  bool limit_block = false;
  for (const auto& value : reopened.exportPreflightIssues()) {
    const auto issue = value.toMap();
    limit_block |= issue.value("code").toString() == QStringLiteral("delivery-render-limit") && issue.value("blocking").toBool();
  }
  if (!limit_block || reopened.startConfiguredExport()) return fail("Excessive aspect-ratio crops are blocked before a worker starts");
  reopened.createCarouselWithSettings({{"slideCount", 1}});
  if (reopened.exportDeliveryFormatIds() != QStringList{"original"} || reopened.exportDeliveryFitMode() != QStringLiteral("fit"))
    return fail("A new document starts with the original delivery only");
  std::cout << "Campaign delivery geometry, history, reopen, worker rendering, proof, hashes and collision checks passed\n";
  return EXIT_SUCCESS;
}
