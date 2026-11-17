#pragma once
#include <QObject>
#include <QQuickItem>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace photo_editor::desktop {
class SceneLibraryController : public QObject {
  Q_OBJECT
  QML_NAMED_ELEMENT(SceneLibraryController)
  Q_PROPERTY(QVariantList scenes READ scenes NOTIFY changed)
  Q_PROPERTY(QString error READ error NOTIFY changed)
 public:
  explicit SceneLibraryController(QObject* parent = nullptr);
  QVariantList scenes() const { return scenes_; }
  QString error() const { return error_; }
  Q_INVOKABLE void refresh();
  Q_INVOKABLE QVariantMap saveScene(const QString& kind, const QString& project,
      const QString& name, const QString& view_id = {}, QQuickItem* viewport = nullptr);
 signals:
  void changed();
 private:
  QVariantMap fail(const QString& message);
  QString directory_, error_;
  QVariantList scenes_;
};
}  // namespace photo_editor::desktop
