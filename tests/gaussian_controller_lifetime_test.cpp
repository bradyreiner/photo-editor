#include "gaussian_scene_controller.h"
#include "gaussian_scene_process.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QThread>
#include <QtConcurrent>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>
#ifdef Q_OS_UNIX
#include <csignal>
#include <unistd.h>
#endif
using photo_editor::desktop::GaussianSceneController;
namespace {
bool write(const QString& path,const QByteArray& bytes) {
  QDir().mkpath(QFileInfo(path).absolutePath()); QFile f(path);
  return f.open(QIODevice::WriteOnly) && f.write(bytes)==bytes.size();
}
bool wait_for(const std::function<bool()>& predicate,int timeout=5000) {
  QElapsedTimer t;t.start();
  while(!predicate() && t.elapsed()<timeout) { QCoreApplication::processEvents(); QThread::msleep(2); }
  return predicate();
}
struct Gate {
  std::mutex mutex; std::condition_variable condition;
  bool released=false; std::atomic_bool entered=false,done=false,saw_cancel=false;
  void release() { std::lock_guard lock(mutex);released=true;condition.notify_all(); }
};
}
int main(int argc,char** argv) {
  QCoreApplication app(argc,argv);
#ifdef Q_OS_UNIX
  if(argc>1) {
    if(app.arguments().contains("--completed-backend")) {
      std::cout << std::string(100000, 'x') << "\nEval iter 200: PSNR 22.94, ssim 0.713\n" << std::flush;
      return 0;
    }
    std::signal(SIGTERM,SIG_IGN);
    if(!write("backend.pid",QByteArray::number(getpid()))) return 1;
    for(int i=0;i<2000;++i) { std::cout << "Refine iter " << i << ", 123 splats\n" << std::flush; QThread::msleep(10); }
    return 0;
  }
#endif
  QTemporaryDir root;
  const auto source=root.filePath("source");
  for(int i=0;i<8;++i) if(!write(QDir(source).filePath(QString::number(i)+".jpg"),"original")) return 1;
  qputenv("PHOTO_EDITOR_COLMAP_BINARY",QCoreApplication::applicationFilePath().toUtf8());
  qputenv("PHOTO_EDITOR_BRUSH_BINARY",QCoreApplication::applicationFilePath().toUtf8());
  auto gate=std::make_shared<Gate>();
  auto controller=std::make_unique<GaussianSceneController>();
  controller->setCapturePreparer([gate](const QStringList&,const QString&,int,const std::shared_ptr<std::atomic_bool>& canceled) {
    gate->entered=true;
    { std::unique_lock lock(gate->mutex);gate->condition.wait(lock,[&]{return gate->released;}); }
    gate->saw_cancel=canceled->load();gate->done=true;
    return QVariantMap{{"ok",false},{"error","held preparation canceled"}};
  });
  const auto preparation_project=root.filePath("held-preparation");
  if(!controller->startCapture(source,preparation_project,200,512).value("ok").toBool() ||
      !wait_for([&]{return gate->entered.load();})) return 1;
  // Safety release makes the old blocking destructor fail without hanging CI.
  std::thread safety([gate]{std::this_thread::sleep_for(std::chrono::milliseconds(500));gate->release();});
  QElapsedTimer elapsed;elapsed.start();controller.reset();const auto preparation_us=elapsed.nsecsElapsed()/1000;
  gate->release();safety.join();
  bool ok=preparation_us<100000 && wait_for([&]{return gate->done.load();}) && gate->saw_cancel;
  std::cout << "Preparation teardown_us=" << preparation_us << " canceled=" << gate->saw_cancel << '\n';
  GaussianSceneController reopened;
  if(!reopened.openProject(preparation_project).value("ok").toBool() || reopened.snapshot().value("state")!="interrupted") return 1;
#ifdef Q_OS_UNIX
  for(bool destroy : {true,false}) {
    controller=std::make_unique<GaussianSceneController>();
    controller->setCapturePreparer([](const QStringList&,const QString&,int,const std::shared_ptr<std::atomic_bool>&) {
      return QVariantMap{{"ok",true},{"frameCount",8}};
    });
    const auto project=root.filePath(destroy?"held-process":"cancel-process");
    if(!controller->startCapture(source,project,200,512).value("ok").toBool() ||
        !wait_for([&]{return QFileInfo::exists(QDir(project).filePath("backend.pid"));})) return 1;
    QFile pid_file(QDir(project).filePath("backend.pid"));if(!pid_file.open(QIODevice::ReadOnly)) return 1;
    const auto pid=static_cast<pid_t>(pid_file.readAll().toLongLong());
    if(pid<=0 || kill(pid,0)!=0) return 1;
    if(!wait_for([&]{return controller->snapshot().value("splatCount").toInt()==123;})) return 1;
    elapsed.restart();
    if(destroy) controller.reset(); else controller->cancel();
    const auto teardown_us=elapsed.nsecsElapsed()/1000;
    if(!destroy && (controller->startCapture(source,root.filePath("too-soon"),200,512).value("ok").toBool() ||
        controller->openProject(preparation_project).value("ok").toBool())) return 1;
    int ticks=0;
    const bool stopped=wait_for([&]{++ticks;return kill(pid,0)!=0;});
    if(!destroy && (!wait_for([&]{return !controller->snapshot().value("busy").toBool();}) ||
        controller->snapshot().value("state")!="canceled")) return 1;
    std::cout << (destroy?"Process destruction":"Process cancel") << " return_us=" << teardown_us
              << " child_reaped=" << stopped << " event_ticks=" << ticks << '\n';
    ok=ok && teardown_us<100000 && stopped;
    if(!destroy) {
      controller->setCapturePreparer([](const QStringList&,const QString&,int,const std::shared_ptr<std::atomic_bool>&) {
        return QVariantMap{{"ok",false},{"error","fresh operation"}};
      });
      if(!controller->startCapture(source,root.filePath("fresh-operation"),200,512).value("ok").toBool() ||
          !wait_for([&]{return !controller->snapshot().value("busy").toBool();}) ||
          controller->snapshot().value("state")!="failed" || controller->snapshot().value("error")!="fresh operation") return 1;
    }
    if(destroy && (!reopened.openProject(project).value("ok").toBool() || reopened.snapshot().value("state")!="interrupted")) return 1;
  }
  // Exercise worker completion and failure independently of queued GUI signals.
  // These use the same worker as every COLMAP and training stage.
  for(int mode=0;mode<3;++mode) {
    auto canceled=std::make_shared<std::atomic_bool>(mode==0);
    auto log=std::make_shared<photo_editor::desktop::GaussianProcessLog>();
    const auto program=mode==1?root.filePath("missing-backend"):QCoreApplication::applicationFilePath();
    const auto directory=root.path();
    auto future=QtConcurrent::run([program,directory,canceled,log] {
      return photo_editor::desktop::run_gaussian_process(program,{"--completed-backend"},directory,
          QProcessEnvironment::systemEnvironment(),canceled,log);
    });
    if(!wait_for([&]{return future.isFinished();})) return 1;
    const auto result=future.result();
    if(mode==0 && (result.value("exitStatus").toInt()!=int(QProcess::CrashExit) || !log->tail.isEmpty())) return 1;
    if(mode==1 && !result.value("failedToStart").toBool()) return 1;
    if(mode==2 && (result.value("failedToStart").toBool() || result.value("exitCode").toInt()!=0 ||
        result.value("exitStatus").toInt()!=int(QProcess::NormalExit) || log->tail.size()!=32768 ||
        !log->tail.endsWith("Eval iter 200: PSNR 22.94, ssim 0.713\n"))) return 1;
  }
  std::cout << "Restart isolation, pre-launch cancellation, failed start and bounded final output pass\n";
#endif
  QFile original(QDir(source).filePath("0.jpg"));
  if(!original.open(QIODevice::ReadOnly) || original.readAll()!="original") return 1;
  return ok?0:1;
}
