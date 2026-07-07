#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>

#include <QDir>
#include <QGuiApplication>
#include <QMetaObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QString>
#include <QVariant>
#include <QVariantMap>

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

bool near_value(const double actual, const double expected, const double tolerance = 1e-6) {
  return std::abs(actual - expected) <= tolerance;
}

bool expect_number(
    const QVariantMap& map,
    const QString& key,
    const double expected,
    const char* message) {
  const auto actual = map.value(key).toDouble();
  if (!near_value(actual, expected)) {
    std::cerr << message << " key=" << key.toStdString()
              << " actual=" << actual
              << " expected=" << expected << '\n';
    return false;
  }
  return true;
}

bool expect_bool(
    const QVariantMap& map,
    const QString& key,
    const bool expected,
    const char* message) {
  const auto actual = map.value(key).toBool();
  if (actual != expected) {
    std::cerr << message << " key=" << key.toStdString()
              << " actual=" << actual
              << " expected=" << expected << '\n';
    return false;
  }
  return true;
}

QVariantMap invoke_map(
    QObject* object,
    const char* method,
    const QVariantList& arguments) {
  QVariant return_value;
  bool invoked = false;
  if (arguments.size() == 2) {
    invoked = QMetaObject::invokeMethod(
        object,
        method,
        Q_RETURN_ARG(QVariant, return_value),
        Q_ARG(QVariant, arguments.at(0)),
        Q_ARG(QVariant, arguments.at(1)));
  } else if (arguments.size() == 4) {
    invoked = QMetaObject::invokeMethod(
        object,
        method,
        Q_RETURN_ARG(QVariant, return_value),
        Q_ARG(QVariant, arguments.at(0)),
        Q_ARG(QVariant, arguments.at(1)),
        Q_ARG(QVariant, arguments.at(2)),
        Q_ARG(QVariant, arguments.at(3)));
  }
  if (!invoked) {
    return {};
  }
  return return_value.toMap();
}

void set_identity_view(QObject* mapper) {
  mapper->setProperty("contentX", 50.0);
  mapper->setProperty("contentY", 20.0);
  mapper->setProperty("contentWidth", 400.0);
  mapper->setProperty("contentHeight", 200.0);
  mapper->setProperty("cropX", 0.0);
  mapper->setProperty("cropY", 0.0);
  mapper->setProperty("cropWidth", 1.0);
  mapper->setProperty("cropHeight", 1.0);
  mapper->setProperty("rotationDegrees", 0.0);
  mapper->setProperty("devicePixelRatio", 1.0);
}

}  // namespace

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);

  QQmlEngine engine;
  const auto qml_path = QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
                            .filePath(QStringLiteral(
                                "apps/desktop/qml/components/CanvasCoordinateMapper.qml"));
  QQmlComponent component(&engine, QUrl::fromLocalFile(qml_path));
  if (component.isError()) {
    for (const auto& error : component.errors()) {
      std::cerr << error.toString().toStdString() << '\n';
    }
    return fail("CanvasCoordinateMapper.qml should load without QML errors");
  }

  std::unique_ptr<QObject> mapper(component.create());
  if (!mapper) {
    for (const auto& error : component.errors()) {
      std::cerr << error.toString().toStdString() << '\n';
    }
    return fail("CanvasCoordinateMapper.qml should create a mapper object");
  }

  const auto contract =
      mapper->property("coordinateTransformContract").toString();
  if (!contract.contains(QStringLiteral("view-to-image:true")) ||
      !contract.contains(QStringLiteral("image-to-view:true")) ||
      !contract.contains(QStringLiteral("crop:true")) ||
      !contract.contains(QStringLiteral("rotation:true")) ||
      !contract.contains(QStringLiteral("device-pixel-ratio:true"))) {
    return fail("coordinate mapper should expose its transform contract");
  }

  set_identity_view(mapper.get());
  auto view_point = invoke_map(
      mapper.get(),
      "imageToViewPoint",
      {0.25, 0.5});
  if (!expect_bool(view_point, QStringLiteral("valid"), true, "identity image-to-view validity") ||
      !expect_number(view_point, QStringLiteral("x"), 150.0, "identity image-to-view x") ||
      !expect_number(view_point, QStringLiteral("y"), 120.0, "identity image-to-view y")) {
    return fail("identity image-to-view transform should be exact");
  }
  auto image_point = invoke_map(
      mapper.get(),
      "viewToImagePoint",
      {150.0, 120.0});
  if (!expect_bool(image_point, QStringLiteral("valid"), true, "identity view-to-image validity") ||
      !expect_number(image_point, QStringLiteral("x"), 0.25, "identity view-to-image x") ||
      !expect_number(image_point, QStringLiteral("y"), 0.5, "identity view-to-image y")) {
    return fail("identity view-to-image transform should round trip exactly");
  }

  mapper->setProperty("contentX", -100.0);
  mapper->setProperty("contentY", 30.0);
  mapper->setProperty("contentWidth", 800.0);
  mapper->setProperty("contentHeight", 400.0);
  view_point = invoke_map(mapper.get(), "imageToViewPoint", {0.5, 0.5});
  if (!expect_number(view_point, QStringLiteral("x"), 300.0, "zoom/pan image-to-view x") ||
      !expect_number(view_point, QStringLiteral("y"), 230.0, "zoom/pan image-to-view y")) {
    return fail("zoom and pan should be part of the exact coordinate transform");
  }

  set_identity_view(mapper.get());
  mapper->setProperty("cropX", 0.2);
  mapper->setProperty("cropY", 0.1);
  mapper->setProperty("cropWidth", 0.5);
  mapper->setProperty("cropHeight", 0.4);
  view_point = invoke_map(mapper.get(), "imageToViewPoint", {0.45, 0.3});
  image_point = invoke_map(mapper.get(), "viewToImagePoint", {250.0, 120.0});
  if (!expect_number(view_point, QStringLiteral("x"), 250.0, "crop image-to-view x") ||
      !expect_number(view_point, QStringLiteral("y"), 120.0, "crop image-to-view y") ||
      !expect_number(image_point, QStringLiteral("x"), 0.45, "crop view-to-image x") ||
      !expect_number(image_point, QStringLiteral("y"), 0.3, "crop view-to-image y")) {
    return fail("crop windows should round trip through image/view coordinates");
  }

  set_identity_view(mapper.get());
  mapper->setProperty("rotationDegrees", 90.0);
  view_point = invoke_map(mapper.get(), "imageToViewPoint", {0.25, 0.5});
  image_point = invoke_map(mapper.get(), "viewToImagePoint", {250.0, 70.0});
  if (!expect_number(view_point, QStringLiteral("x"), 250.0, "rotation image-to-view x") ||
      !expect_number(view_point, QStringLiteral("y"), 70.0, "rotation image-to-view y") ||
      !expect_number(image_point, QStringLiteral("x"), 0.25, "rotation view-to-image x") ||
      !expect_number(image_point, QStringLiteral("y"), 0.5, "rotation view-to-image y")) {
    return fail("rotation should round trip through image/view coordinates");
  }

  set_identity_view(mapper.get());
  auto view_rect = invoke_map(mapper.get(), "imageToViewRect", {0.2, 0.25, 0.3, 0.5});
  auto image_rect = invoke_map(mapper.get(), "viewToImageRect", {130.0, 70.0, 120.0, 100.0});
  if (!expect_bool(view_rect, QStringLiteral("valid"), true, "rect image-to-view validity") ||
      !expect_number(view_rect, QStringLiteral("x"), 130.0, "rect image-to-view x") ||
      !expect_number(view_rect, QStringLiteral("y"), 70.0, "rect image-to-view y") ||
      !expect_number(view_rect, QStringLiteral("width"), 120.0, "rect image-to-view width") ||
      !expect_number(view_rect, QStringLiteral("height"), 100.0, "rect image-to-view height") ||
      !expect_number(image_rect, QStringLiteral("x"), 0.2, "rect view-to-image x") ||
      !expect_number(image_rect, QStringLiteral("y"), 0.25, "rect view-to-image y") ||
      !expect_number(image_rect, QStringLiteral("width"), 0.3, "rect view-to-image width") ||
      !expect_number(image_rect, QStringLiteral("height"), 0.5, "rect view-to-image height")) {
    return fail("rect transforms should round trip exactly");
  }

  mapper->setProperty("devicePixelRatio", 2.0);
  const auto device_rect = invoke_map(
      mapper.get(),
      "devicePixelRectForViewRect",
      {10.25, 5.5, 20.25, 10.25});
  if (!expect_number(device_rect, QStringLiteral("x"), 21.0, "dpr rect x") ||
      !expect_number(device_rect, QStringLiteral("y"), 11.0, "dpr rect y") ||
      !expect_number(device_rect, QStringLiteral("width"), 41.0, "dpr rect width") ||
      !expect_number(device_rect, QStringLiteral("height"), 21.0, "dpr rect height") ||
      !expect_number(device_rect, QStringLiteral("devicePixelRatio"), 2.0, "dpr rect ratio")) {
    return fail("device-pixel-ratio view rect conversion should be exact");
  }

  return EXIT_SUCCESS;
}
