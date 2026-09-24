#include <QColor>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QQmlComponent>
#include <QQuickItem>
#include <QQuickView>
#include <QSGRendererInterface>
#include <QThread>
#include <QUrl>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

double srgb_to_linear(double value) {
  return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
}

double linear_to_srgb(double value) {
  value = std::max(0.0, value);
  return value <= 0.0031308 ? value * 12.92 : 1.055 * std::pow(value, 1.0 / 2.4) - 0.055;
}

using Matrix = std::array<std::array<double, 3>, 3>;

QColor expected_color(const QColor& source, const Matrix& matrix) {
  const std::array<double, 3> linear{
      srgb_to_linear(source.redF()),
      srgb_to_linear(source.greenF()),
      srgb_to_linear(source.blueF())};
  std::array<double, 3> transformed{};
  for (std::size_t row = 0; row < transformed.size(); ++row) {
    for (std::size_t column = 0; column < linear.size(); ++column) {
      transformed[row] += matrix[row][column] * linear[column];
    }
  }
  return QColor::fromRgbF(
      static_cast<float>(std::clamp(linear_to_srgb(transformed[0]), 0.0, 1.0)),
      static_cast<float>(std::clamp(linear_to_srgb(transformed[1]), 0.0, 1.0)),
      static_cast<float>(std::clamp(linear_to_srgb(transformed[2]), 0.0, 1.0)),
      source.alphaF());
}

bool colors_close(const QColor& actual, const QColor& expected, int tolerance = 5) {
  return std::abs(actual.red() - expected.red()) <= tolerance &&
         std::abs(actual.green() - expected.green()) <= tolerance &&
         std::abs(actual.blue() - expected.blue()) <= tolerance &&
         std::abs(actual.alpha() - expected.alpha()) <= tolerance;
}

QImage render_mode(QQuickView* view, QQuickItem* root, int mode) {
  root->setProperty("simulationMode", mode);
  view->requestUpdate();
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < 180) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    QThread::msleep(2);
  }
  return view->grabWindow();
}

QColor sample_swatch(const QImage& image, int index) {
  const auto x = ((index * 2 + 1) * image.width()) / 6;
  return image.pixelColor(x, image.height() / 2);
}

}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", QByteArray("offscreen"));
  QGuiApplication app(argc, argv);

  QFile vertex_shader(QStringLiteral(
      ":/photo-editor/shaders/carousel_color_vision.vert.qsb"));
  QFile fragment_shader(QStringLiteral(
      ":/photo-editor/shaders/carousel_color_vision.frag.qsb"));
  if (!vertex_shader.open(QIODevice::ReadOnly) || vertex_shader.size() < 100 ||
      !fragment_shader.open(QIODevice::ReadOnly) || fragment_shader.size() < 100) {
    return fail("compiled color-vision shader resources should be packaged for runtime use");
  }

  QQuickView view;
  view.setColor(Qt::black);
  view.setResizeMode(QQuickView::SizeRootObjectToView);
  QQmlComponent component(view.engine());
  component.setData(R"QML(
import QtQuick

Item {
    id: root
    width: 96
    height: 32
    property int simulationMode: 1

    Item {
        anchors.fill: parent
        layer.enabled: true
        layer.smooth: false
        layer.effect: ShaderEffect {
            property var source
            property int simulationMode: root.simulationMode
            vertexShader: "qrc:/photo-editor/shaders/carousel_color_vision.vert.qsb"
            fragmentShader: "qrc:/photo-editor/shaders/carousel_color_vision.frag.qsb"
        }

        Rectangle { x: 0; width: 32; height: 32; color: "#ff0000" }
        Rectangle { x: 32; width: 32; height: 32; color: "#00ff00" }
        Rectangle { x: 64; width: 32; height: 32; color: "#0000ff" }
    }
}
)QML",
                    QUrl(QStringLiteral("qrc:/carousel-color-vision-effect-test.qml")));
  if (component.isError()) {
    std::cerr << component.errorString().toStdString();
    return fail("color-vision effect QML should compile");
  }
  auto* root_object = component.create();
  auto* root_item = qobject_cast<QQuickItem*>(root_object);
  if (root_item == nullptr) {
    delete root_object;
    return fail("color-vision effect test should create a QQuickItem");
  }
  view.setContent(QUrl(QStringLiteral("qrc:/carousel-color-vision-effect-test.qml")),
                  &component,
                  root_item);
  view.resize(96, 32);
  view.show();

  const std::array<QColor, 3> source_colors{
      QColor(QStringLiteral("#ff0000")),
      QColor(QStringLiteral("#00ff00")),
      QColor(QStringLiteral("#0000ff"))};
  const std::array<Matrix, 4> matrices{
      Matrix{{{{0.56667, 0.43333, 0.0}},
              {{0.55833, 0.44167, 0.0}},
              {{0.0, 0.24167, 0.75833}}}},
      Matrix{{{{0.625, 0.375, 0.0}},
              {{0.7, 0.3, 0.0}},
              {{0.0, 0.3, 0.7}}}},
      Matrix{{{{0.95, 0.05, 0.0}},
              {{0.0, 0.43333, 0.56667}},
              {{0.0, 0.475, 0.525}}}},
      Matrix{{{{0.2126, 0.7152, 0.0722}},
              {{0.2126, 0.7152, 0.0722}},
              {{0.2126, 0.7152, 0.0722}}}}};

  for (std::size_t mode_index = 0; mode_index < matrices.size(); ++mode_index) {
    bool changes_primary = false;
    for (const auto& source : source_colors) {
      const auto expected = expected_color(source, matrices[mode_index]);
      changes_primary = changes_primary || !colors_close(expected, source, 1);
      if (mode_index == matrices.size() - 1 &&
          (std::abs(expected.red() - expected.green()) > 1 ||
           std::abs(expected.green() - expected.blue()) > 1)) {
        return fail("CPU reference should prove neutral grayscale output");
      }
    }
    if (!changes_primary) {
      return fail("every inspection matrix should visibly transform at least one primary color");
    }
  }

  QElapsedTimer scene_timer;
  scene_timer.start();
  while (scene_timer.elapsed() < 80) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    QThread::msleep(2);
  }
  if (view.rendererInterface()->graphicsApi() == QSGRendererInterface::Software) {
    std::cout << "GPU ShaderEffect rendering unavailable; compiled QSB resources and the CPU matrix reference passed.\n";
    return EXIT_SUCCESS;
  }

  for (std::size_t mode_index = 0; mode_index < matrices.size(); ++mode_index) {
    const auto image = render_mode(&view, root_item, static_cast<int>(mode_index + 1));
    if (image.isNull() || image.width() < 3 || image.height() < 1) {
      return fail("color-vision shader should render an offscreen image");
    }
    for (std::size_t swatch = 0; swatch < source_colors.size(); ++swatch) {
      const auto actual = sample_swatch(image, static_cast<int>(swatch));
      const auto expected = expected_color(source_colors[swatch], matrices[mode_index]);
      if (!colors_close(actual, expected)) {
        std::cerr << "mode=" << (mode_index + 1) << " swatch=" << swatch
                  << " actual=" << actual.name(QColor::HexArgb).toStdString()
                  << " expected=" << expected.name(QColor::HexArgb).toStdString() << '\n';
        return fail("color-vision shader pixels should match the declared linear-RGB matrix");
      }
    }
  }

  const auto grayscale = render_mode(&view, root_item, 4);
  for (int swatch = 0; swatch < 3; ++swatch) {
    const auto color = sample_swatch(grayscale, swatch);
    if (std::abs(color.red() - color.green()) > 1 ||
        std::abs(color.green() - color.blue()) > 1) {
      return fail("grayscale inspection should produce neutral output for every primary swatch");
    }
  }

  return EXIT_SUCCESS;
}
