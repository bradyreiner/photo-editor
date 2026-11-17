#include "histogram_scope_service.h"
#include "scope_plot_item.h"
#include <QGuiApplication>
#include <QQuickWindow>
#include <QTest>
#include <QDir>
#include <iostream>

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  using namespace photo_editor::desktop;
  QQuickWindow window;
  window.resize(512, 256);
  window.setColor(Qt::black);
  ScopePlotItem plot(window.contentItem());
  plot.setSize(QSizeF(512, 256));
  QVariantList density;
  for (int column = 0; column < 16; ++column)
    for (int level = 0; level < 16; ++level) density.append(level == column ? 1.0 : 0.0);
  const auto frame = QVariantMap{{QStringLiteral("columns"), 16}, {QStringLiteral("levels"), 16},
                               {QStringLiteral("red"), density}};
  plot.setFrame(frame);
  plot.setOptions({{QStringLiteral("mode"), QStringLiteral("Waveform")},
                   {QStringLiteral("luma"), false}, {QStringLiteral("trace"), false}});
  window.show();
  if (!QTest::qWaitForWindowExposed(&window)) return 77;
  QTest::qWait(100);
  const auto image = window.grabWindow();
  if (image.isNull()) return 77;
  const auto lit = image.pixelColor(16 * image.width() / 512, 248 * image.height() / 256);
  const auto dark = image.pixelColor(16 * image.width() / 512, 8 * image.height() / 256);
  if (lit.red() < 80 || lit.red() <= lit.blue() || dark.red() > 10) {
    std::cerr << "Native density cells must retain channel color, horizontal position and inverted level\n";
    return 1;
  }
  plot.setOptions({{QStringLiteral("mode"), QStringLiteral("Waveform")},
                   {QStringLiteral("rgb"), false}, {QStringLiteral("luma"), false}});
  QTest::qWait(50);
  if (window.grabWindow().pixelColor(16 * image.width() / 512, 248 * image.height() / 256).red() > 10) {
    std::cerr << "Disabling channels must remove the native density surface\n";
    return 1;
  }
  plot.setFrame({});
  plot.setReference(frame);
  plot.setOptions({{QStringLiteral("mode"), QStringLiteral("Waveform")},
                   {QStringLiteral("luma"), false}});
  QTest::qWait(50);
  const auto reference = window.grabWindow().pixelColor(16 * image.width() / 512, 248 * image.height() / 256);
  if (reference.red() < 20 || std::abs(reference.red() - reference.blue()) > 2) {
    std::cerr << "A held reference must remain visible and neutral when the live frame changes\n";
    return 1;
  }
  const auto output = qEnvironmentVariable("PHOTO_EDITOR_SCOPE_PLOT_CAPTURE");
  if (!output.isEmpty() && !image.save(output)) return 1;
  std::cout << "Native density pixels, disabled channels and held reference passed\n";
}
