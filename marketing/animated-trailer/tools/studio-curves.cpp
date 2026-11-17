#include "photo_editor/pipeline/color_adjustments.h"
#include <fstream>
#include <set>
using namespace photo_editor::pipeline;
int main(int argc,char**argv){if(argc!=2)return 2;std::ofstream out(argv[1]);std::set<double> xs;for(int i=0;i<=64;i++)xs.insert(i/64.);xs.insert(.22);xs.insert(.55);out<<"export const curveData={x:[";bool first=true;for(auto x:xs){if(!first)out<<',';out<<x;first=false;}out<<"],series:[";for(int i=0;i<=8;i++){if(i)out<<',';out<<'[';ColorAdjustmentState s;double u=i/8.;s.master_curve_points={{0,0},{.22,.22-.06*u},{.55,.55+.05*u},{1,1}};first=true;for(auto x:xs){if(!first)out<<',';out<<evaluate_tone_curve(x,s);first=false;}out<<']';}out<<"]};\n";return out.good()?0:3;}
