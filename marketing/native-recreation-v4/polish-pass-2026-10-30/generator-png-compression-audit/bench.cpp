#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QTextStream>
#include <array>

static QByteArray rgbaHash(const QImage &source) {
  const QImage image = source.convertToFormat(QImage::Format_RGBA8888);
  QCryptographicHash hash(QCryptographicHash::Sha256);
  const qsizetype rowBytes = static_cast<qsizetype>(image.width()) * 4;
  for (int y = 0; y < image.height(); ++y) {
    hash.addData(reinterpret_cast<const char *>(image.constScanLine(y)), rowBytes);
  }
  return hash.result().toHex();
}

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  QTextStream out(stdout);
  if (argc != 3) {
    out << "usage: bench <source.png> <output-prefix>\n";
    return 2;
  }
  const QString input = QString::fromLocal8Bit(argv[1]);
  const QString prefix = QString::fromLocal8Bit(argv[2]);
  QImageReader reader(input, "PNG");
  QImage source = reader.read();
  if (source.isNull()) {
    out << "read_failed\t" << reader.errorString() << "\n";
    return 3;
  }
  source = source.convertToFormat(QImage::Format_RGBA8888);
  const QByteArray sourceHash = rgbaHash(source);
  out << "source\t" << input << "\t" << source.width() << "x" << source.height()
      << "\t" << sourceHash << "\n";
  for (const int level : std::array<int, 3>{11, 1, 0}) {
    const QString outputPath = prefix + QStringLiteral("-c%1.png").arg(level);
    QImageWriter writer(outputPath, "PNG");
    writer.setCompression(level);
    QElapsedTimer timer;
    timer.start();
    const bool wrote = writer.write(source);
    const qint64 encodeNs = timer.nsecsElapsed();
    if (!wrote) {
      out << "write_failed\t" << level << "\t" << writer.errorString() << "\n";
      return 4;
    }
    QImageReader verifyReader(outputPath, "PNG");
    QImage decoded = verifyReader.read();
    if (decoded.isNull()) {
      out << "decode_failed\t" << level << "\t" << verifyReader.errorString() << "\n";
      return 5;
    }
    const QByteArray decodedHash = rgbaHash(decoded);
    out << "variant\t" << level << "\t" << (encodeNs / 1000000.0)
        << "\t" << QFileInfo(outputPath).size() << "\t" << decodedHash
        << "\t" << (decodedHash == sourceHash ? "EXACT" : "MISMATCH") << "\n";
    if (decodedHash != sourceHash) {
      return 6;
    }
    QFile::remove(outputPath);
  }
  return 0;
}
