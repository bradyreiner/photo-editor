#include <QFont>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QString>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>

int main(int argc, char** argv) {
  QGuiApplication application(argc, argv);
  if (argc != 2) {
    std::cerr << "usage: p21_footer_qt_metrics /path/to/SpaceMono-Regular.ttf\n";
    return 2;
  }

  const auto font_path = QString::fromUtf8(argv[1]);
  const auto font_id = QFontDatabase::addApplicationFont(font_path);
  const auto families = QFontDatabase::applicationFontFamilies(font_id);
  if (font_id < 0 || !families.contains(QStringLiteral("Space Mono"))) {
    std::cerr << "could not register the app's Space Mono font\n";
    return 1;
  }

  QFont font(QStringLiteral("Space Mono"));
  font.setPixelSize(17);
  font.setLetterSpacing(QFont::AbsoluteSpacing, 1.0);
  const QFontMetricsF metrics(font);
  const auto line_advance = metrics.lineSpacing();
  const auto bottom = 1080.0 - 22.0;
  const auto block_center = bottom - line_advance * 0.5;
  const auto title_position_y = block_center / 1080.0;
  const auto native_fixed_line_height = 17.0 * 1.2;
  const auto native_text_height = native_fixed_line_height +
      std::max(0.0, std::ceil(metrics.height()) - native_fixed_line_height);

  std::cout << std::setprecision(15)
            << "family=Space Mono\n"
            << "pixelSize=17\n"
            << "height=" << metrics.height() << '\n'
            << "ascent=" << metrics.ascent() << '\n'
            << "descent=" << metrics.descent() << '\n'
            << "lineSpacing=" << line_advance << '\n'
            << "nativeFixedLineHeight=" << native_fixed_line_height << '\n'
            << "nativeTextHeight=" << native_text_height << '\n'
            << "sourceBottomY=" << bottom << '\n'
            << "sourceBlockCenterY=" << block_center << '\n'
            << "titlePositionY=" << title_position_y << '\n';
  return 0;
}
