#include "gaussian_capture_preparation.h"
#include "qimage_color_conversion.h"
#include <QGuiApplication>
#include <QImageReader>
#include <QTemporaryDir>
#include <QDir>
#include <iostream>

using namespace photo_editor::desktop;
int main(int argc, char** argv) {
  QGuiApplication app(argc,argv);
  QTemporaryDir temporary;
  const QDir root(temporary.path());
  QImage source(256,128,QImage::Format_RGB888);
  for (int y = 0; y < source.height(); ++y) for (int x = 0; x < source.width(); ++x)
    source.setPixelColor(x,y,QColor(x,y*2,(x+y)%256));
  for (const auto& profile : {QColorSpace(QColorSpace::SRgb),QColorSpace(QColorSpace::DisplayP3)}) {
    source.setColorSpace(profile);
    for (const auto* extension : {"png","jpg","tif"}) {
      const auto stem = QStringLiteral("%1-%2").arg(profile == QColorSpace(QColorSpace::SRgb) ? "srgb" : "p3",QString::fromLatin1(extension));
      const auto input = root.filePath(stem+'.'+QString::fromLatin1(extension));
      if (!source.save(input,nullptr,100)) return 2;
      QImageReader reader(input); reader.setAutoTransform(true); auto reference = reader.read();
      if (!reference.colorSpace().isValid()) reference.setColorSpace(profile);
      reference = convert_tagged_qimage_color_space(reference,QColorSpace(QColorSpace::SRgb),QImage::Format_RGB888,
          QImageColorConversionPurpose::OutputPublication);
      const auto project = root.filePath(stem); if (!QDir().mkpath(QDir(project).filePath("images"))) return 2;
      const auto result = prepare_gaussian_capture({input},project,1600,std::make_shared<std::atomic_bool>(false));
      const QImage prepared(QDir(project).filePath("images/frame_000000.png"));
      if (!result.value("ok").toBool() || prepared.size() != reference.size() || prepared.colorSpace() != QColorSpace(QColorSpace::SRgb)) return 1;
      if (result.value("colorContract") != "capture-source-color-to-srgb-v2" ||
          result.value("frames").toList().front().toMap().value("colorInterpretation") != "display-referred-icc-to-srgb-v1") return 1;
      int maximum = 0; double absolute = 0;
      for (int y = 0; y < reference.height(); ++y) for (int x = 0; x < reference.width(); ++x) {
        const auto a = reference.pixelColor(x,y), b = prepared.pixelColor(x,y);
        for (const auto error : {std::abs(a.red()-b.red()),std::abs(a.green()-b.green()),std::abs(a.blue()-b.blue())}) {
          maximum = std::max(maximum,error); absolute += error;
        }
      }
      const auto mean = absolute/(reference.width()*reference.height()*3.0);
      std::cout << stem.toStdString() << " max=" << maximum << " mean=" << mean << '\n';
      if (maximum > 3 || mean > 0.35) return 1;
    }
  }
  return 0;
}
