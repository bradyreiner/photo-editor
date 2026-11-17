#pragma once
#include <QColorSpace>
#include <QCryptographicHash>
#include <filesystem>

static bool gaussian_frame_sequence_regression() {
  const auto fail = [](const char* reason) { std::cerr << "PNG sequence: " << reason << '\n'; return false; };
  QTemporaryDir directory; const QDir root(directory.path());
  auto scene = std::make_shared<GaussianSceneData>(); scene->artifact_sha256 = QString(64,'a');
  GaussianPoint point; point.center_opacity = {0,0,6,0.4F}; point.covariance_a = {.3F,0,0,.3F}; point.covariance_b = {0,.3F,0,0}; point.sh[0] = 1;
  scene->points = {point};
  auto composition = std::make_shared<GaussianSceneComposition>(); composition->alpha_model = GaussianAlphaModel::Inria99;
  GaussianSceneLight source; source.id = "source"; source.name = "Source"; source.kind = 2; source.vector = {0,0,3}; source.rotation = {180,0,0};
  source.color = {1,0.2F,0.05F}; source.intensity = 3; source.size = {.4F,.2F}; source.camera_visible = true; composition->lights = {source};
  GaussianCamera a; a.image_size = {129,97}; a.fx = 120; a.fy = 110; a.cx = 64.5F; a.cy = 48.5F; a.composition = composition;
  a.photo.lens = {1,0.1F,0,0,0,16}; a.photo.lens.auto_focus = true; a.photo.lens.spherical_distortion = 0.2F;
  a.photo.post.bloom_amount = 0.6F; a.photo.post.bloom_radius = 32; a.photo.film.grain_amount = 0.3F; a.photo.tone = {1.2F,0.6F};
  auto b = a; b.position = {0,0,1}; GaussianCameraPath path; path.cameras = {a,b}; path.segment_seconds = 0.5; path.fps = 24;
  const auto destination = root.filePath(QString::fromUtf8("Master frames β")); auto progress = std::make_shared<GaussianFilmProgress>();
  const auto result = export_gaussian_film(scene,path,destination,257,{},progress,{},GaussianFilmFormat::PngSequence);
  const auto receipt = result.value("receipt").toMap(); const auto frames = receipt.value("frames").toList();
  const auto focuses = receipt.value("autofocus").toMap().value("samples").toList();
  if (!result.value("ok").toBool() || progress->completed.load() != 12 || receipt.value("schema") != "framehouse-gaussian-frame-sequence-v1" ||
      receipt.value("width").toInt() != 257 || receipt.value("height").toInt() != 193 || frames.size() != 12 || focuses.size() != 12 ||
      receipt.value("sceneObjects").toMap() != composition->description() || receipt.value("framePattern") != "frame-%06d.png" ||
      !receipt.value("encoding").toMap().value("lossless").toBool()) { std::cerr << QJsonDocument::fromVariant(result).toJson().constData(); return fail("export/receipt"); }
  QFile manifest(QDir(destination).filePath("camera-path.json"));
  if (!manifest.open(QIODevice::ReadOnly)) return fail("published receipt unavailable");
  const auto published = QJsonDocument::fromJson(manifest.readAll()).toVariant().toMap();
  if (published != QJsonDocument::fromVariant(receipt).toVariant().toMap() ||
      QDir(destination).entryList({"*.png"},QDir::Files).size() != 12) return fail("complete published folder");
  auto renderer = make_gaussian_scene_renderer();
  for (int index = 0; index < 12; ++index) {
    const auto entry = frames[index].toMap(); const auto filename = entry.value("file").toString();
    if (entry.value("frame").toInt() != index || filename != QStringLiteral("frame-%1.png").arg(index,6,10,QChar('0'))) return fail("frame ordering");
    auto camera = path.sample(static_cast<double>(index)/24).resized({257,193}); camera.photo.lens.auto_focus = false;
    camera.photo.lens.focus_distance = focuses[index].toMap().value("focusDistance").toFloat();
    const auto rendered = renderer->render(scene,camera,true); const QImage decoded(QDir(destination).filePath(filename));
    if (!rendered.error.isEmpty() || decoded.size() != QSize(257,193) || decoded.colorSpace() != QColorSpace(QColorSpace::SRgb)) return fail("PNG dimensions/profile");
    const auto expected = rendered.image.convertToFormat(QImage::Format_RGB888), actual = decoded.convertToFormat(QImage::Format_RGB888);
    QCryptographicHash pixels(QCryptographicHash::Sha256);
    for (int row = 0; row < actual.height(); ++row) {
      if (std::memcmp(actual.constScanLine(row),expected.constScanLine(row),static_cast<size_t>(actual.width())*3) != 0) return fail("decoded pixels differ from independent manual-focus replay");
      pixels.addData(QByteArrayView(reinterpret_cast<const char*>(actual.constScanLine(row)),actual.width()*3));
    }
    QFile image(QDir(destination).filePath(filename)); QCryptographicHash encoded(QCryptographicHash::Sha256);
    if (!image.open(QIODevice::ReadOnly) || !encoded.addData(&image) || entry.value("sha256") != QString::fromLatin1(encoded.result().toHex()) ||
        entry.value("rgbSha256") != QString::fromLatin1(pixels.result().toHex())) return fail("file/pixel integrity");
  }
  if (focuses.front().toMap().value("focusDistance").toDouble() != 3 || focuses.back().toMap().value("focusDistance").toDouble() >= 2.1) return fail("changing focus trace");
  const auto rejected = [&](const QString& target,int edge,GaussianFilmFormat format = GaussianFilmFormat::PngSequence) {
    return !export_gaussian_film(scene,path,target,edge,{},std::make_shared<GaussianFilmProgress>(),{},format).value("ok").toBool();
  };
  if (!rejected(destination,257) || !rejected("relative-output",257) || !rejected(root.filePath("too-large"),8193) ||
      !rejected(root.filePath(QStringLiteral("invalid")+QChar::Null+QStringLiteral("path")),257) ||
      !rejected(root.filePath("invalid-format"),257,static_cast<GaussianFilmFormat>(9))) return fail("invalid/overwrite preflight");
  const auto empty = root.filePath("existing-empty"); QDir().mkpath(empty);
  const auto dangling = root.filePath("dangling"); std::filesystem::create_symlink(root.filePath("missing").toStdString(),dangling.toStdString());
  if (!rejected(empty,257) || !QFileInfo(empty).isDir() || !rejected(dangling,257) || !QFileInfo(dangling).isSymLink()) return fail("directory/symlink no overwrite");
  const auto canceled_output = root.filePath("canceled"); auto canceled = std::make_shared<GaussianFilmProgress>(); canceled->canceled = true;
  if (!export_gaussian_film(scene,path,canceled_output,257,{},canceled,{},GaussianFilmFormat::PngSequence).value("canceled").toBool() || QFileInfo::exists(canceled_output)) return fail("preflight cancellation");
  auto long_path = path; long_path.segment_seconds = 10;
  auto active = std::make_shared<GaussianFilmProgress>();
  auto future = QtConcurrent::run([&] { return export_gaussian_film(scene,long_path,canceled_output,257,{},active,{},GaussianFilmFormat::PngSequence); });
  QElapsedTimer timer; timer.start();
  while (!future.isFinished() && timer.elapsed() < 10000) { if (active->completed.load() >= 1) active->canceled = true; QThread::msleep(1); }
  active->canceled = true; future.waitForFinished();
  if (!future.result().value("canceled").toBool() || active->completed.load() < 1 || active->completed.load() >= long_path.frameCount() ||
      QFileInfo::exists(canceled_output) || timer.elapsed() >= 5000) return fail("observed-frame cancellation");
  for (const bool folder : {false,true}) {
    const auto target = root.filePath(folder ? "late-folder" : "late-file"); auto state = std::make_shared<GaussianFilmProgress>(); auto collision_path = path; collision_path.segment_seconds = 1;
    auto pending = QtConcurrent::run([&] { return export_gaussian_film(scene,collision_path,target,257,{},state,{},GaussianFilmFormat::PngSequence); });
    timer.restart(); while (!pending.isFinished() && state->completed.load() == 0 && timer.elapsed() < 5000) QThread::msleep(1);
    bool occupied = false;
    if (!pending.isFinished() && state->completed.load() > 0) {
      if (folder) occupied = QDir().mkdir(target);
      else { QFile file(target); occupied = file.open(QIODevice::WriteOnly|QIODevice::NewOnly) && file.write("keep",4) == 4; }
    }
    while (!pending.isFinished() && timer.elapsed() < 15000) QThread::msleep(1);
    if (!pending.isFinished()) state->canceled = true;
    pending.waitForFinished();
    if (!occupied || pending.result().value("ok").toBool() || pending.result().value("canceled").toBool()) return fail("late collision was not rejected");
    if (folder) { if (!QFileInfo(target).isDir() || !QDir(target).entryList(QDir::AllEntries|QDir::Hidden|QDir::NoDotAndDotDot).isEmpty()) return fail("replaced an existing empty directory"); }
    else { QFile file(target); if (!file.open(QIODevice::ReadOnly) || file.readAll() != "keep") return fail("replaced an existing file"); }
  }
  if (!root.entryList({".framehouse-film-*"},QDir::Dirs|QDir::Hidden|QDir::NoDotAndDotDot).isEmpty()) return fail("staging directory leaked");
  std::cout << "PNG sequence: 12 exact RGB manual-focus replays, odd grid/profile, frozen lighting/effects, hashes, no encoder, cancellation and exclusive folder publication pass\n";
  return true;
}
