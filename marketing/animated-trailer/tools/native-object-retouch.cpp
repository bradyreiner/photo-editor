#include "ai_model_runtime.h"
#include <QCoreApplication>
#include <QColorSpace>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <iostream>

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  if (argc != 4) return 2;
  qunsetenv("PHOTO_EDITOR_TEST_FAKE_AI_MODELS");
  QImage source(QString::fromLocal8Bit(argv[1]));
  auto mask = QImage(QString::fromLocal8Bit(argv[2])).convertToFormat(QImage::Format_Grayscale8);
  if (source.isNull() || mask.isNull() || source.size() != mask.size()) return 3;
  source = source.convertToFormat(QImage::Format_ARGB32);
  source.setColorSpace(QColorSpace(QColorSpace::SRgb));
  photo_editor::desktop::AiInpaintRequest request;
  request.source_image = source;
  request.mask_width = source.width(); request.mask_height = source.height();
  request.mask_data.resize(source.width()*source.height());
  for (int y=0;y<source.height();++y)
    std::memcpy(request.mask_data.data()+y*source.width(),mask.constScanLine(y),static_cast<size_t>(source.width()));
  request.retouch_mode = QStringLiteral("remove");
  request.quality_mode = QStringLiteral("final");
  request.prompt = QStringLiteral("smooth"); request.seed = 17;
  if (qEnvironmentVariableIsSet("LOOKSY_DEMO_PATCH")) {
    request.retouch_mode = QStringLiteral("patch"); request.patch_source_set=true;
    request.patch_source_x=650.0/1200;request.patch_source_y=763.0/1200;request.patch_source_width=63.0/1200;request.patch_source_height=159.0/1800;
  }
  QElapsedTimer timer; timer.start();
  const auto result=photo_editor::desktop::run_ai_inpaint(request);
  if (!result.success) { std::cerr << result.error_message.toStdString(); return 4; }
  auto output=source.copy();
  for (int y=0;y<source.height();++y) for (int x=0;x<source.width();++x) {
    if (static_cast<unsigned char>(request.mask_data.at(y*source.width()+x))==0) continue;
    const int px=x-result.patch_x,py=y-result.patch_y;
    if (px>=0 && py>=0 && px<result.patch_image.width() && py<result.patch_image.height())
      output.setPixel(x,y,result.patch_image.pixel(px,py));
  }
  if (!output.save(QString::fromLocal8Bit(argv[3]))) return 5;
  const QJsonObject receipt{{"method",result.model_name},{"provider",QStringLiteral("Local CPU")},
    {"fallback",result.fallback},{"milliseconds",timer.elapsed()},
    {"scope","Actual production run_ai_inpaint C++ path, built without ONNX to use Local CPU Fill; not a Qt UI recording."},
    {"outside_mask","Original source pixels retained exactly in lossless PNG"}};
  std::cout << QJsonDocument(receipt).toJson().toStdString();
  return 0;
}
