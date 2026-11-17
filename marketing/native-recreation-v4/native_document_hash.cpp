// Read-only diagnostic: use the app's Qt JSON canonicalization for backups.
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <iostream>

int main(int argc, char** argv) {
  if (argc != 2 && argc != 3) return 2;
  const QFileInfo info(QString::fromLocal8Bit(argv[1]));
  if (!info.isFile() || info.isSymLink() || info.size() > 256LL * 1024 * 1024)
    return 2;
  QFile file(info.absoluteFilePath());
  if (!file.open(QIODevice::ReadOnly)) return 2;
  const auto bytes = file.readAll();
  QJsonParseError error;
  const auto document = QJsonDocument::fromJson(bytes, &error);
  if (error.error != QJsonParseError::NoError || !document.isObject()) return 2;
  auto object = document.object();
  const auto timeline_id = object.value(QStringLiteral("timelineId")).toString();
  if (timeline_id.isEmpty()) return 2;
  object.remove(QStringLiteral("updatedAt"));
  const auto sha = [](const QByteArray& value) {
    return QString::fromLatin1(QCryptographicHash::hash(
        value, QCryptographicHash::Sha256).toHex());
  };
  QJsonObject result{
      {QStringLiteral("timelineId"), timeline_id},
      {QStringLiteral("rawSha256"), sha(bytes)},
      {QStringLiteral("documentHash"), sha(QJsonDocument(object).toJson(QJsonDocument::Compact))},
      {QStringLiteral("fileBytes"), bytes.size()},
      {QStringLiteral("path"), info.canonicalFilePath()}};
  if (argc == 3) {
    const auto clip_id = QString::fromLocal8Bit(argv[2]);
    auto found = 0;
    const auto strip = [&](QJsonArray clips) {
      for (qsizetype index = 0; index < clips.size(); ++index) {
        auto clip = clips[index].toObject();
        if (clip.value(QStringLiteral("clipId")).toString() != clip_id) continue;
        ++found;
        clip.remove(QStringLiteral("compoundStatus"));
        clip.remove(QStringLiteral("compoundError"));
        clips.replace(index, clip);
      }
      return clips;
    };
    auto tracks = object.value(QStringLiteral("tracks")).toArray();
    for (qsizetype index = 0; index < tracks.size(); ++index) {
      auto track = tracks[index].toObject();
      track.insert(QStringLiteral("clips"), strip(track.value(QStringLiteral("clips")).toArray()));
      tracks.replace(index, track);
    }
    object.insert(QStringLiteral("tracks"), tracks);
    object.insert(QStringLiteral("clips"), strip(object.value(QStringLiteral("clips")).toArray()));
    if (found == 0) return 2;
    result.insert(QStringLiteral("compoundClipCopies"), found);
    result.insert(QStringLiteral("compoundProjectionSha256"),
                  sha(QJsonDocument(object).toJson(QJsonDocument::Compact)));
  }
  std::cout << QJsonDocument(result).toJson(QJsonDocument::Compact).constData() << '\n';
}
