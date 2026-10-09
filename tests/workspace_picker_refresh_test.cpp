#include "catalog_asset_model.h"
#include <QGuiApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJSValue>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QThread>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#if !defined(_WIN32)
#include <sys/resource.h>
#endif
using photo_editor::desktop::CatalogAssetItem;
using photo_editor::desktop::CatalogAssetModel;

class PickerCatalog final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString projectFilter MEMBER project_filter NOTIFY filtersChanged)
  Q_PROPERTY(bool loading MEMBER loading NOTIFY loadingChanged)
 public:
  CatalogAssetModel model;
  QString project_filter = QStringLiteral("all");
  bool loading = false;
  bool reverse_rows = false;
  int calls = 0;
  qsizetype rows = 0;
  double projection_ms = 0;
  QVariantList projected(const QString& project) const {
    auto result = model.timelinePickerAssets(project);
    if (reverse_rows) std::reverse(result.begin(),result.end());
    return result;
  }
  Q_INVOKABLE QVariantList timelinePickerAssets(const QString& project) {
    QElapsedTimer timer; timer.start();
    auto result = projected(project);
    projection_ms += static_cast<double>(timer.nsecsElapsed()) / 1e6;
    ++calls; rows += result.size();
    return result;
  }
  Q_INVOKABLE QVariantList mediaPickerAssets(const QString& project,const QStringList& media_types = {}) {
    QElapsedTimer timer;timer.start();
    auto result=model.mediaPickerAssets(project,media_types);
    if(reverse_rows) std::reverse(result.begin(),result.end());
    projection_ms+=static_cast<double>(timer.nsecsElapsed())/1e6;
    ++calls;rows+=result.size();return result;
  }
  void resetCounts() { calls=0; rows=0; projection_ms=0; }
  void burst() {
    emit assetCountChanged(); emit totalAssetCountChanged();
    emit projectStatsRevisionChanged(); emit loadingChanged(); emit mediaDurationsChanged();
  }
 signals:
  void assetCountChanged();
  void totalAssetCountChanged();
  void projectStatsRevisionChanged();
  void loadingChanged();
  void mediaDurationsChanged();
  void filtersChanged();
  void dataChanged();
  void modelReset();
};

namespace {
void require(bool passed,const char* message) { if (!passed) throw std::runtime_error(message); }
const QStringList picker_fields={"assetId","cameraLabel","thumbnailSource","mediaType","durationText",
    "durationSeconds","filePath","timelineDocumentPath","online"};
QVariantList pickerFields(const QVariantList& rows) {
  QVariantList result;
  for(const auto& row:rows) {
    const auto full=row.toMap();QVariantMap picker;
    for(const auto& key:picker_fields) {
      require(full.contains(key),"Picker row is missing a required field");
      picker.insert(key,full.value(key));
    }
    result.push_back(picker);
  }
  return result;
}
QString read(const QString& path) {
  QFile file(path); require(file.open(QIODevice::ReadOnly),"Cannot read source fixture");
  return QString::fromUtf8(file.readAll());
}
QString block(const QString& source, qsizetype start) {
  require(start >= 0,"Required QML source block is absent");
  auto cursor=source.indexOf('{',start); require(cursor>=0,"Source block has no opening brace");
  int depth=1; ++cursor;
  while (depth && cursor<source.size()) {
    if(source.at(cursor)=='{') ++depth;
    if(source.at(cursor)=='}') --depth;
    ++cursor;
  }
  require(depth==0,"QML block is not balanced");
  return source.mid(start,cursor-start);
}
QString function(const QString& source,const QString& name,bool required=true) {
  auto pos=source.indexOf(QStringLiteral("function ")+name+QLatin1Char('('));
  if(pos<0 && !required) return {};
  return block(source,pos);
}
QString qmlFixture(const QString& source,bool carousel) {
  QString functions;
  for (const auto& name : {"preferredMediaPickerProject","hasProjectScopedMediaPicker","refreshMediaPickerAssets","setMediaPickerScope","openMediaPicker"})
    functions+=function(source,QLatin1String(name))+QLatin1Char('\n');
  functions+=function(source,QStringLiteral("requestMediaPickerRefresh"),false);
  const auto connection_target=source.indexOf("target: catalogAssets");
  const auto connections=block(source,source.lastIndexOf("Connections {",connection_target));
  QString properties;
  QRegularExpression props(QStringLiteral("^    (?:readonly )?property [^\\n]*mediaPicker(?:Assets|ProjectName|Scope|Intent|AssetsDirty|PreferredProject)[ :][^\\n]*"),QRegularExpression::MultilineOption);
  auto matches=props.globalMatch(source);
  while(matches.hasNext()) properties+=matches.next().captured()+QLatin1Char('\n');
  QString timer;
  const auto timer_id=source.indexOf("id: mediaPickerRefreshTimer");
  if(timer_id>=0) timer=block(source,source.lastIndexOf("Timer {",timer_id));
  QString preferred_handler;
  QRegularExpression handler(QStringLiteral("^    onMediaPickerPreferredProjectChanged:[^\\n]*"),QRegularExpression::MultilineOption);
  auto match=handler.match(source); if(match.hasMatch()) preferred_handler=match.captured();
  QString visibility;
  QRegularExpression visible(QStringLiteral("^    onVisibleChanged: \\{"),QRegularExpression::MultilineOption);
  match=visible.match(source); if(match.hasMatch()) visibility=block(source,match.capturedStart());
  QString popup_visibility;
  const auto popup_start=source.lastIndexOf("GalleryPickerDialog {");
  const auto popup=block(source,popup_start);
  auto popup_visible=popup.indexOf("onVisibleChanged:");
  if(popup_visible>=0) popup_visibility=block(popup,popup_visible);
  QString startup;
  if(carousel) {
    const auto completed=block(source,source.indexOf("Component.onCompleted: {"));
    if(completed.contains("refreshMediaPickerAssets()")) startup="Component.onCompleted: refreshMediaPickerAssets()";
  }
  return QStringLiteral(R"QML(
import QtQuick
import QtQuick.Controls.Basic
import "file:%1/apps/desktop/qml/components"
ApplicationWindow {
  width: 900; height: 680; visible: true
  Theme { id: pickerTheme }
  QtObject { id: timelineDocument; objectName: "pickerDocument"; property string projectBinding: "" }
  Item { objectName: "workspaceHost"; anchors.fill: parent
  Item {
    id: root; objectName: "pickerWorkspace"; anchors.fill: parent
    %2
    function scheduleBackgroundWorkIntentPublish() {}
    function closeTransientSurfaces() { mediaPickerDialog.close() }
    %3
    %4
    %5
    %6
    %7
    %8
    GalleryPickerDialog {
      id: mediaPickerDialog; theme: pickerTheme; assets: root.mediaPickerAssets
      showProjectScopeToggle: root.hasProjectScopedMediaPicker(); scope: root.mediaPickerScope
      onProjectScopeRequested: root.setMediaPickerScope("project")
      onAllMediaScopeRequested: root.setMediaPickerScope("all")
      %9
    }
  }
  }
}
)QML").arg(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR),properties,functions,connections,timer,preferred_handler,visibility,startup,popup_visibility);
}
void settle(int ms=40) {
  QElapsedTimer timer;timer.start();
  while(timer.elapsed()<ms) { QCoreApplication::processEvents();QThread::msleep(1); }
}
QVariantList assets(QObject* root) {
  const auto value=root->property("mediaPickerAssets");
  return pickerFields(value.metaType()==QMetaType::fromType<QJSValue>() ? value.value<QJSValue>().toVariant().toList() : value.toList());
}
QVariantList expected(PickerCatalog& catalog,QObject* root,bool carousel) {
  auto rows=pickerFields(catalog.projected(root->property("mediaPickerProjectName").toString()));
  if(!carousel) return rows;
  QVariantList result;
  for(const auto& value:rows) {
    const auto kind=value.toMap().value("mediaType").toString();
    if(kind=="photo" || (kind=="video" && root->property("mediaPickerIntent")!="brand-logo")) result.push_back(value);
  }
  return result;
}
void open(QObject* root,bool carousel,const QString& intent=QStringLiteral("slot")) {
  const bool invoked=carousel ? QMetaObject::invokeMethod(root,"openMediaPicker",Q_ARG(QVariant,QVariant(intent))) : QMetaObject::invokeMethod(root,"openMediaPicker");
  require(invoked,"Picker open handler not callable");
}
double p95(QList<double> values) {
  std::sort(values.begin(),values.end());
  return values.at(static_cast<qsizetype>(std::ceil(static_cast<double>(values.size())*0.95))-1);
}
QJsonObject run(const QString& workspace,int count,const QString& directory,bool verify) {
  const bool carousel=workspace=="Carousel";
  const auto source=read(directory+QLatin1Char('/')+workspace+"View.qml");
  PickerCatalog catalog;
  QVector<CatalogAssetItem> records;
  for(int index=0;index<count;++index) {
    CatalogAssetItem item;
    item.asset_id=QStringLiteral("asset-%1").arg(index,6,10,QLatin1Char('0'));
    item.camera_label=QStringLiteral("Fixture %1").arg(index);
    item.file_path=QStringLiteral("/isolated-picker-fixture/%1").arg(item.asset_id);
    item.media_type=index%4==1 ? "video" : index%4==2 ? "audio" : index%4==3 ? "timeline" : "photo";
    item.duration_text="0:12";item.duration_seconds=12;
    item.project_names={index%2==0 ? QStringLiteral("Project A") : QStringLiteral("Project B")};
    item.project_summary=item.project_names.first();
    item.online=false;
    records.push_back(item);
  }
  catalog.model.replaceAssetsForTesting(records);
  if(verify) {
    for(const auto& file:{"GalleryPickerDialog.qml","GalleryAssetCard.qml"}) {
      const auto code=read(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/apps/desktop/qml/components/")+QLatin1String(file));
      QRegularExpression access(QStringLiteral("\\b(?:selectedAsset|asset)\\.([A-Za-z][A-Za-z0-9_]*)"));
      auto references=access.globalMatch(code);
      while(references.hasNext()) require(picker_fields.contains(references.next().captured(1)),"Dialog reads an untested picker field");
    }
    for(const auto& project:{QString{},QStringLiteral("Project A"),QStringLiteral("Project B"),QStringLiteral("unassigned"),QStringLiteral("missing")}) {
      const auto full=catalog.model.timelinePickerAssets(project);
      for(const auto& row:full) require(!row.toMap().contains("recipeJson"),"Full picker now provides recipeJson; update selected recipe retrieval before using slim rows");
      for(const auto& types:QList<QStringList>{{},{"photo"},{"photo","video"},{"video"},{"audio"},{"unknown"}}) {
        QVariantList oracle;
        for(const auto& row:full) if(types.isEmpty() || types.contains(row.toMap().value("mediaType").toString())) oracle.push_back(row);
        const auto compact=catalog.model.mediaPickerAssets(project,types);
        require(compact==pickerFields(oracle),"Lightweight picker changed fields, ordering, project or media filtering");
      }
    }
  }
  QQmlEngine engine;
  engine.rootContext()->setContextProperty("catalogAssets",&catalog);
  QQmlComponent component(&engine);
  component.setData(qmlFixture(source,carousel).toUtf8(),QUrl::fromLocalFile(directory+"/picker-fixture.qml"));
  std::unique_ptr<QObject> object(component.create());
  if(!object) throw std::runtime_error(component.errorString().toStdString());
  auto* root=object->findChild<QObject*>("pickerWorkspace");
  auto* popup=object->findChild<QObject*>("galleryPickerDialog");
  require(root && popup,"Actual gallery popup or workspace fixture missing");
  settle();
  const auto startup_calls=catalog.calls;
  root->setProperty("visible",false);
  QList<double> hidden_ms,open_ms,visible_projection_ms,visible_settled_ms;
  int hidden_calls=0,visible_calls=0;
  qsizetype hidden_rows=0;
  for(int iteration=0;iteration<5;++iteration) {
    catalog.resetCounts();QElapsedTimer timer;timer.start();catalog.burst();
    hidden_ms.push_back(static_cast<double>(timer.nsecsElapsed())/1e6);
    settle();hidden_calls+=catalog.calls;hidden_rows+=catalog.rows;
  }
  root->setProperty("visible",true);
  for(int iteration=0;iteration<5;++iteration) {
    catalog.resetCounts();QElapsedTimer timer;timer.start();open(root,carousel);
    open_ms.push_back(static_cast<double>(timer.nsecsElapsed())/1e6);settle();
    require(assets(root)==expected(catalog,root,carousel),"Opening the actual popup returned stale/incorrect rows");
    require(catalog.calls==1,"Opening should build exactly one current projection");
    catalog.resetCounts();timer.restart();catalog.burst();settle();
    visible_settled_ms.push_back(static_cast<double>(timer.nsecsElapsed())/1e6);
    visible_projection_ms.push_back(catalog.projection_ms);visible_calls+=catalog.calls;
    require(assets(root)==expected(catalog,root,carousel),"Visible refresh changed projected contents");
    require(QMetaObject::invokeMethod(popup,"close"),"Popup close handler missing");settle();
  }
  QJsonObject report{{"workspace",workspace},{"assets",count},{"startup_projection_calls",startup_calls},
      {"hidden_projection_calls",hidden_calls},{"hidden_mapped_rows",static_cast<qint64>(hidden_rows)},
      {"hidden_signal_burst_p95_ms",p95(hidden_ms)},{"open_callback_p95_ms",p95(open_ms)},
      {"visible_projection_calls",visible_calls},{"visible_projection_work_p95_ms",p95(visible_projection_ms)},
      {"visible_burst_settled_p95_ms",p95(visible_settled_ms)},
      {"exact_picker_fields",true},
      {"projection_sha256",QString::fromLatin1(QCryptographicHash::hash(QJsonDocument::fromVariant(assets(root)).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex())}};
#if defined(__APPLE__)
  rusage usage{};
  require(getrusage(RUSAGE_SELF,&usage)==0,"Cannot measure peak resident memory");
  report.insert("peak_rss_mib",static_cast<double>(usage.ru_maxrss)/(1024.0*1024.0));
#endif
  if(verify) {
    require(startup_calls==0 && hidden_calls==0,"Inactive picker performed catalog projection work");
    require(visible_calls==5,"Visible catalog signal bursts did not coalesce to one projection");
    open(root,carousel);settle();
    require(QMetaObject::invokeMethod(popup,"selectAssetIndex",Q_ARG(QVariant,QVariant(2))),"Cannot select an asset");
    const auto selected=popup->property("selectedAssetId").toString();require(!selected.isEmpty(),"Selection fixture is empty");
    catalog.reverse_rows=true;emit catalog.modelReset();settle();
    require(popup->property("selectedAssetId")==selected && assets(root)==expected(catalog,root,carousel),
            "A model reset changed selection identity or kept stale rows");
    for(auto& record:records) if(record.asset_id==selected) { record.camera_label="Updated selected media";record.duration_seconds=23;record.duration_text="0:23"; }
    catalog.model.replaceAssetsForTesting(records);emit catalog.dataChanged();settle();
    require(popup->property("selectedAssetId")==selected && popup->property("selectedDisplayLabel")=="Updated selected media"
            && popup->property("selectedDurationSeconds").toDouble()==23,"Selected asset metadata stayed stale");
    records.erase(std::remove_if(records.begin(),records.end(),[&](const auto& record){return record.asset_id==selected;}),records.end());
    catalog.model.replaceAssetsForTesting(records);emit catalog.modelReset();settle();
    require(!popup->property("hasSelection").toBool(),"Removed asset remained selected");
    catalog.resetCounts();catalog.burst();QMetaObject::invokeMethod(popup,"close");settle();
    require(catalog.calls==0,"Closing a picker executed its queued refresh");
    open(root,carousel);settle();
    auto* host=object->findChild<QObject*>("workspaceHost");require(host,"Workspace parent missing");
    catalog.resetCounts();catalog.burst();host->setProperty("visible",false);settle();
    require(catalog.calls==0 && !popup->property("visible").toBool(),"Hidden workspace executed a queued refresh or kept its picker open");
    catalog.project_filter="Project A";emit catalog.filtersChanged();settle();
    require(catalog.calls==0,"Hidden project change performed a projection");
    host->setProperty("visible",true);open(root,carousel);settle();
    require(root->property("mediaPickerProjectName")=="Project A" && assets(root)==expected(catalog,root,carousel),
            "Reopening did not apply the current project scope");
    catalog.project_filter="Project B";emit catalog.filtersChanged();settle();
    require(root->property("mediaPickerProjectName")=="Project B" && assets(root)==expected(catalog,root,carousel),
            "Open picker did not track project changes");
    if(!carousel) {
      auto* document=object->findChild<QObject*>("pickerDocument");require(document,"Timeline document missing");
      document->setProperty("projectBinding","Project A");settle();
      require(root->property("mediaPickerProjectName")=="Project A" && assets(root)==expected(catalog,root,false),"Timeline binding did not override catalog project");
      document->setProperty("projectBinding","");settle();
      require(root->property("mediaPickerProjectName")=="Project B","Clearing Timeline binding did not restore catalog project");
    }
    if(carousel) { QMetaObject::invokeMethod(popup,"close");settle();catalog.project_filter="all";emit catalog.filtersChanged();open(root,true,"brand-logo");settle();
      require(!assets(root).isEmpty(),"Logo filtering fixture is empty");
      for(const auto& row:assets(root)) require(row.toMap().value("mediaType")=="photo","Logo picker included non-photo media"); }
    QMetaObject::invokeMethod(popup,"close");settle();
    catalog.resetCounts();emit catalog.dataChanged();emit catalog.loadingChanged();settle();
    require(catalog.calls==0,"Closed picker rebuilt data while workspace stayed visible");
    report.insert("lifecycle_contract_passed",true);
  }
  object.reset();settle();
  return report;
}
}
int main(int argc,char** argv) {
  qputenv("QT_QPA_PLATFORM","offscreen");qputenv("QT_QUICK_BACKEND","software");
  QTemporaryDir data; if(!data.isValid()) return 2;
  qputenv("PHOTO_EDITOR_APP_DATA_DIR",data.path().toUtf8());qputenv("PHOTO_EDITOR_APP_DATA_ROOT",data.path().toUtf8());
  QGuiApplication app(argc,argv);
  const bool verify=argc==2 && QString::fromLocal8Bit(argv[1])=="--verify";
  if(!verify && argc!=4 && argc!=5) return 2;
  const QString default_source=QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/apps/desktop/qml/workspaces");
  try {
    if(verify) {run("Carousel",1000,default_source,true);run("Timeline",1000,default_source,true);std::cout<<"workspace_picker_refresh_contract_passed\n";return 0;}
    const auto report=run(QString::fromLocal8Bit(argv[1]),QString::fromLocal8Bit(argv[2]).toInt(),argc==5 ? QString::fromLocal8Bit(argv[4]):default_source,false);
    QFile output(QString::fromLocal8Bit(argv[3]));require(output.open(QIODevice::WriteOnly),"Cannot write report");
    output.write(QJsonDocument(report).toJson());std::cout<<QJsonDocument(report).toJson(QJsonDocument::Compact).constData()<<'\n';
  } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
  return 0;
}
#include "workspace_picker_refresh_test.moc"
