#include <algorithm>
#include <cstdlib>
#include <iostream>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJSEngine>
#include <QJsonDocument>
#include <QSaveFile>

class ReportBoundary final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QVariantMap report READ report CONSTANT)
 public:
  explicit ReportBoundary(QVariantMap report) : report_(std::move(report)) {}
  QVariantMap report() const { return report_; }
 private:
  QVariantMap report_;
};

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  const auto args = app.arguments();
  if (args.size() != 3) {
    std::cerr << "usage: agent_report_boundary_benchmark REPORT RECEIPT\n";
    return EXIT_FAILURE;
  }
  QFile source(args[1]);
  if (!source.open(QIODevice::ReadOnly)) return EXIT_FAILURE;
  QJsonParseError error;
  auto document = QJsonDocument::fromJson(source.readAll(), &error);
  if (error.error != QJsonParseError::NoError || !document.isObject()) return EXIT_FAILURE;
  auto report = document.toVariant().toMap();
  const auto bytes = document.toJson(QJsonDocument::Compact).size();
  document = {};
  ReportBoundary boundary(report);
  QJSEngine engine;
  QJSEngine::setObjectOwnership(&boundary, QJSEngine::CppOwnership);
  engine.globalObject().setProperty(QStringLiteral("bridge"), engine.newQObject(&boundary));
  // The same property boundary as QML: ordinary field reads, no artificially
  // eager stringify of the full report. Repeated reads model independent UI bindings.
  auto inspect = engine.evaluate(QStringLiteral(R"JS(
    (function() {
      const report = bridge.report;
      const validation = report.validationReport || {};
      const diff = bridge.report.recipeDiff || {};
      const assets = bridge.report.assetReports || [];
      const calls = bridge.report.toolCalls || [];
      const warnings = bridge.report.warnings || [];
      return [report.ok, report.scope, report.mode, report.runId,
        validation.ok, validation.needsUserReview,
        Object.keys(diff).length, assets.length, calls.length, warnings.length,
        report.afterProofPath || ""].join("|");
    })
  )JS"));
  if (!inspect.isCallable()) return EXIT_FAILURE;
  QVariantList samples;
  QList<qint64> timings;
  QString visible_signature;
  for (int i = 0; i < 21; ++i) {
    engine.collectGarbage();
    QElapsedTimer timer;
    timer.start();
    const auto value = inspect.call();
    const auto ns = timer.nsecsElapsed();
    if (value.isError()) return EXIT_FAILURE;
    if (i == 0) visible_signature = value.toString();
    if (value.toString() != visible_signature) return EXIT_FAILURE;
    if (i > 0) { timings.append(ns); samples.append(ns); }
  }
  std::sort(timings.begin(), timings.end());
  const QVariantMap receipt{
      {QStringLiteral("contract"), QStringLiteral("agent-report-qml-boundary-v1")},
      {QStringLiteral("jsonBytes"), bytes},
      {QStringLiteral("medianNs"), timings[timings.size() / 2]},
      {QStringLiteral("samplesNs"), samples},
      {QStringLiteral("visibleSignature"), visible_signature},
      {QStringLiteral("mode"), QStringLiteral("full")}};
  QSaveFile output(args[2]);
  if (!output.open(QIODevice::WriteOnly)) return EXIT_FAILURE;
  const auto encoded = QJsonDocument::fromVariant(receipt).toJson(QJsonDocument::Indented);
  if (output.write(encoded) != encoded.size() || !output.commit()) return EXIT_FAILURE;
  std::cout << "jsonBytes=" << bytes << " medianNs=" << timings[timings.size() / 2] << '\n';
  return EXIT_SUCCESS;
}

#include "agent_report_boundary_benchmark.moc"
