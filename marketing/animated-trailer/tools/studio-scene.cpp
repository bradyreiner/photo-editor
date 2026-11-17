#include "gaussian_scene_renderer.h"
#include <QGuiApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <iostream>
#include <cmath>
using namespace photo_editor::desktop;
float ease(float t){t=std::clamp(t,0.f,1.f);return t*t*t*(t*(t*6-15)+10);}
int main(int argc,char**argv){QGuiApplication app(argc,argv);if(argc!=3)return 2;
 QFile f(argv[1]);if(!f.open(QIODevice::ReadOnly))return 3;auto hash=QString::fromLatin1(QCryptographicHash::hash(f.readAll(),QCryptographicHash::Sha256).toHex());f.close();
 auto loaded=load_gaussian_scene(QString::fromLocal8Bit(argv[1]),{},hash);if(!loaded.scene){std::cerr<<loaded.error.toStdString();return 4;}
 auto renderer=make_gaussian_scene_renderer();if(!renderer)return 5;QDir().mkpath(argv[2]);
 QVector3D low(1e6,1e6,1e6),high(-1e6,-1e6,-1e6);for(const auto&p:loaded.scene->points){for(int j=0;j<3;j++){low[j]=std::min(low[j],p.center_opacity[j]);high[j]=std::max(high[j],p.center_opacity[j]);}}
 auto center=(low+high)*.5f;float radius=(high-low).length()*.5f;QJsonArray cameras;
 for(int i=0;i<360;i++){float t=i/30.f,yaw=(-25+85*ease(t/12))*3.14159265f/180;
 GaussianCamera c;c.name=QString("Camera %1").arg(i);c.image_size={960,1080};c.fx=c.fy=1400;c.cx=480;c.cy=540;
 c.position=center+QVector3D(std::sin(yaw)*radius*2.7f,-radius*.05f,-std::cos(yaw)*radius*2.7f);c.forward=(center-c.position).normalized();c.right=QVector3D::crossProduct(c.forward,QVector3D(0,-1,0)).normalized();c.down=QVector3D::crossProduct(c.forward,c.right).normalized();
 float distance=(center-c.position).length();c.photo.lens.focus_distance=distance;
 if(t>=4&&t<9){float change=ease((t-4)/1.4f)*(1-ease((t-7)/1.4f));c.photo.lens.aperture_radius=.014f;c.photo.lens.focus_distance=distance-.38f*change;c.photo.lens.samples=32;}
 c.photo.exposure_ev=.35f*ease((t-9)/1.4f);
 auto result=renderer->render(loaded.scene,c,true);if(!result.error.isEmpty()||result.image.isNull()){std::cerr<<result.error.toStdString();return 6;}
 auto dest=QString::fromLocal8Bit(argv[2])+QString("/%1.png").arg(i,4,10,QChar('0'));if(!result.image.save(dest))return 7;cameras.append(QJsonObject{{"frame",i},{"yawDegrees",yaw*180/3.14159265},{"focusDistance",c.photo.lens.focus_distance},{"exposureEv",c.photo.exposure_ev}});if(i%30==0)std::cout<<i<<"/360"<<std::endl;}
 QFile receipt(QString::fromLocal8Bit(argv[2])+"/native-render.json");if(!receipt.open(QIODevice::WriteOnly))return 8;receipt.write(QJsonDocument(QJsonObject{{"sourceHash",hash},{"frames",360},{"fps",30},{"renderer","Looksy native GaussianSceneRenderer / Metal / explicit export readback"},{"scope","Photographic CC-BY scan. Native render assets, not native UI/performance proof."},{"cameras",cameras}}).toJson());return 0;}
