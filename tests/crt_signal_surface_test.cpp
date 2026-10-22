#include <QDir>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickItemGrabResult>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTest>
#include <algorithm>
#include <iostream>

namespace {
int fail(const char* message) { std::cerr << message << '\n'; return 1; }
QImage capture(QQuickItem* item) {
  auto result = item->grabToImage();
  if (!result) return {};
  QElapsedTimer timer;
  timer.start();
  while (result->image().isNull() && timer.elapsed() < 3000) QTest::qWait(20);
  return result->image();
}
int changed_pixels(const QImage& left, const QImage& right, int tolerance = 2) {
  if (left.isNull() || right.isNull() || left.size() != right.size()) return -1;
  int result = 0;
  for (int y = 0; y < left.height(); ++y) {
    for (int x = 0; x < left.width(); ++x) {
      const auto a = left.pixelColor(x, y);
      const auto b = right.pixelColor(x, y);
      if (std::max({std::abs(a.red() - b.red()), std::abs(a.green() - b.green()),
                    std::abs(a.blue() - b.blue()), std::abs(a.alpha() - b.alpha())}) > tolerance)
        ++result;
    }
  }
  return result;
}
}

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  app.setApplicationName("Looksy CRT Preview");
  const auto verify = app.arguments().contains("--verify");
  QQmlApplicationEngine engine;
  engine.rootContext()->setContextProperty("demoMode", !verify);
  engine.load(QUrl::fromLocalFile(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/tests/crt_signal_preview.qml")));
  if (engine.rootObjects().isEmpty()) return fail("The live CRT preview must load production QML");
  auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
  if (!window) return fail("The CRT preview needs a window");
  if (!verify) return app.exec();
  QTest::qWait(250);
  if (window->rendererInterface()->graphicsApi() == QSGRendererInterface::Software) {
    std::cout << "CRT pixel verification requires a GPU renderer\n";
    return 77;
  }
  auto* surface = window->findChild<QQuickItem*>("crtPreviewSurface");
  auto* character = window->findChild<QQuickItem*>("crtWiringCharacter");
  auto* character_signal = character ? character->findChild<QObject*>("agentCharacterCrtSignal") : nullptr;
  if (!surface || !character_signal || character_signal->property("strength").toDouble() <= 0.5)
    return fail("The real character must enable its CRT renderer");
  surface->setProperty("active", false);
  surface->setProperty("strength", 0.0);
  QTest::qWait(50);
  const auto clean = capture(surface);
  surface->setProperty("strength", 1.0);
  QTest::qWait(50);
  const auto crt = capture(surface);
  const auto altered = changed_pixels(clean, crt);
  if (altered < 500) return fail("CRT strength must change actual GPU pixels");
  if (crt.pixelColor(1, 1).alpha() != 0)
    return fail("CRT bloom must leave distant transparent surroundings clear");
  surface->setProperty("active", true);
  QTest::qWait(100);
  const auto before = capture(surface);
  QTest::qWait(1100);
  const auto rolling = capture(surface);
  if (changed_pixels(before, rolling, 0) < 100)
    return fail("The render-thread beam must produce changing pixels without repainting the artwork");
  surface->setProperty("signalBreak", 1.0);
  QTest::qWait(30);
  const auto glitch = capture(surface);
  if (changed_pixels(rolling, glitch) < 100)
    return fail("A signal break must produce real displaced pixels");
  surface->setProperty("signalBreak", 0.0);
  surface->setProperty("reducedMotion", true);
  QTest::qWait(100);
  const auto frozen = capture(surface);
  QTest::qWait(250);
  if (surface->property("motionEnabled").toBool() || changed_pixels(frozen, capture(surface), 0) != 0)
    return fail("Reduced motion must retain a stable CRT image");
  surface->setProperty("signalBreak", 1.0);
  QTest::qWait(60);
  if (changed_pixels(frozen, capture(surface), 0) != 0)
    return fail("Reduced motion must suppress manual glitches too");
  surface->setProperty("visible", false);
  if (surface->property("motionEnabled").toBool()) return fail("Hidden CRT surfaces must stop their clock");
  // Exercise the production robot itself, not just a synthetic CRT test card.
  character->setWidth(320);
  character->setHeight(370);
  character->setX(300);
  character->setY(80);
  character->setProperty("mode", "idle");
  QTest::qWait(100);
  const auto robot_idle = capture(character);
  int opaque = 0;
  for (int y = 0; y < robot_idle.height(); ++y)
    for (int x = 0; x < robot_idle.width(); ++x)
      opaque += robot_idle.pixelColor(x, y).alpha() > 200 ? 1 : 0;
  if (opaque < 10000 || !character->property("artReady").toBool())
    return fail("The solid robot renderer must compile and draw actual geometry");
  character->setProperty("animated", true);
  QTest::qWait(100);
  const auto robot_before = capture(character);
  int delivered_frames = 0;
  const auto frame_connection = QObject::connect(window, &QQuickWindow::frameSwapped, &app,
      [&delivered_frames]() { ++delivered_frames; }, Qt::QueuedConnection);
  QElapsedTimer animation_timer;
  animation_timer.start();
  QTest::qWait(650);
  const auto animation_elapsed = animation_timer.elapsed();
  QObject::disconnect(frame_connection);
  const auto robot_after = capture(character);
  if (changed_pixels(robot_before, robot_after) < 100)
    return fail("The robot head and CRT face must animate on the GPU");
  character->setProperty("mode", "greeting");
  QTest::qWait(400);
  const auto robot_wave = capture(character);
  if (changed_pixels(robot_after, robot_wave) < 500)
    return fail("Greeting must articulate the arm, not only change a label");
  character->setProperty("mode", "success");
  QTest::qWait(400);
  const auto robot_happy = capture(character);
  character->setProperty("mode", "walk");
  QTest::qWait(100);
  const auto walk_before = capture(character);
  QTest::qWait(280);
  const auto robot_walk = capture(character);
  if (changed_pixels(walk_before, robot_walk) < 300)
    return fail("Walking must animate the articulated limbs");
  character->setProperty("reducedMotion", true);
  QTest::qWait(150);
  const auto robot_frozen = capture(character);
  QTest::qWait(250);
  if (character->property("motionEnabled").toBool() || changed_pixels(robot_frozen, capture(character), 0) != 0)
    return fail("Reduced motion must stop both the robot and the CRT surface");
  const auto output = qEnvironmentVariable("PHOTO_EDITOR_CRT_PROOF_DIR");
  if (!output.isEmpty()) {
    QDir().mkpath(output);
    robot_idle.save(QDir(output).filePath("robot-idle.png"));
    robot_wave.save(QDir(output).filePath("robot-wave.png"));
    robot_happy.save(QDir(output).filePath("robot-happy.png"));
    robot_walk.save(QDir(output).filePath("robot-walk.png"));
    robot_before.save(QDir(output).filePath("robot-before.png"));
    robot_after.save(QDir(output).filePath("robot-after.png"));
    clean.save(QDir(output).filePath("clean.png"));
    crt.save(QDir(output).filePath("crt-static.png"));
    before.save(QDir(output).filePath("live-before.png"));
    rolling.save(QDir(output).filePath("live-after.png"));
    glitch.save(QDir(output).filePath("glitch.png"));
  }
  std::cout << "Native robot preview: " << delivered_frames << " presented frames in "
            << animation_elapsed << " ms at 320x370 logical pixels\n";
  std::cout << "CRT GPU pixels passed: static treatment=" << altered
            << ", live beam, signal displacement, alpha, reduced motion, hidden state; solid robot, head motion, articulated wave, walking and reduced motion\n";
  return 0;
}
