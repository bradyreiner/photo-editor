#include "gaussian_spacetime.h"
#include <iostream>
#include <limits>
#include <random>

using namespace photo_editor::desktop;
int main() {
  std::mt19937 generator(74019);
  std::uniform_real_distribution<double> sample(-0.7, 0.7);
  int comparisons = 0;
  double maximum_error = 0;
  for (int trial = 0; trial < 100; ++trial) {
    std::array<double,16> factor{}, covariance{};
    for (size_t r=0;r<4;++r) for (size_t c=0;c<=r;++c) factor[r*4+c]=r==c ? 0.5+std::abs(sample(generator)) : sample(generator);
    for (size_t r=0;r<4;++r) for (size_t c=0;c<4;++c)
      for (size_t k=0;k<4;++k) covariance[r*4+c]+=factor[r*4+k]*factor[c*4+k];
    const std::array<double,4> mean{sample(generator),sample(generator),3+sample(generator),sample(generator)};
    const auto compiled=compile_gaussian_spacetime(mean,covariance,0.8);
    if (!compiled) { std::cerr << "Valid joint covariance rejected"; return 1; }
    // Independent oracle: invert the entire 4D covariance by Gauss-Jordan;
    // compare its joint quadratic form with the compiled factorized density.
    double augmented[4][8]{};
    for (size_t r=0;r<4;++r) for (size_t c=0;c<8;++c) augmented[r][c]=c<4 ? covariance[r*4+c] : double(c-4==r);
    for (size_t p=0;p<4;++p) {
      const auto pivot=augmented[p][p];
      for (auto& v:augmented[p]) v/=pivot;
      for (size_t r=0;r<4;++r) if(r!=p) {
        const auto scale=augmented[r][p]; for(size_t c=0;c<8;++c) augmented[r][c]-=scale*augmented[p][c];
      }
    }
    const auto& a=compiled->spatial.covariance_a; const auto& b=compiled->spatial.covariance_b;
    const double det=double(a[0])*(double(a[3])*b[1]-double(b[0])*b[0])-
        double(a[1])*(double(a[1])*b[1]-double(b[0])*a[2])+double(a[2])*(double(a[1])*b[0]-double(a[3])*a[2]);
    const double inverse[3][3]={{(double(a[3])*b[1]-double(b[0])*b[0])/det,(double(a[2])*b[0]-double(a[1])*b[1])/det,(double(a[1])*b[0]-double(a[2])*a[3])/det},
        {(double(a[2])*b[0]-double(a[1])*b[1])/det,(double(a[0])*b[1]-double(a[2])*a[2])/det,(double(a[1])*a[2]-double(a[0])*b[0])/det},
        {(double(a[1])*b[0]-double(a[2])*a[3])/det,(double(a[1])*a[2]-double(a[0])*b[0])/det,(double(a[0])*a[3]-double(a[1])*a[1])/det}};
    for(int probe=0;probe<40;++probe) {
      std::array<double,4> offset{sample(generator),sample(generator),sample(generator),sample(generator)};
      double joint=0;
      for(size_t r=0;r<4;++r) for(size_t c=0;c<4;++c) joint+=offset[r]*augmented[r][c+4]*offset[c];
      const auto seconds=static_cast<float>(mean[3]+offset[3]);
      const auto center=gaussian_spacetime_center(compiled->spatial,compiled->temporal,seconds);
      const double standardized=(seconds-compiled->temporal.velocity_time[3])*compiled->temporal.normalization[0];
      double factored=standardized*standardized;
      for(size_t r=0;r<3;++r) for(size_t c=0;c<3;++c)
        factored+=(mean[r]+offset[r]-center[r])*inverse[r][c]*(mean[c]+offset[c]-center[c]);
      maximum_error=std::max(maximum_error,std::abs(std::exp(-joint/2)-std::exp(-factored/2)));
      ++comparisons;
    }
  }
  std::array<double,16> identity{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
  int rejected=0;
  for(int mode=0;mode<7;++mode) {
    auto bad=identity;
    if(mode==0) bad[15]=0;
    if(mode==1) bad[15]=-1;
    if(mode==2) bad[3]=2;
    if(mode==3) bad[3]=bad[12]=2;
    if(mode==4) bad[0]=std::numeric_limits<double>::quiet_NaN();
    if(mode==5) bad[0]=1e-100;
    if(mode==6) bad[0]=std::numeric_limits<double>::infinity();
    if(compile_gaussian_spacetime({0,0,3,0},bad,0.8)) return 1;
    ++rejected;
  }
  if(compile_gaussian_spacetime({0,0,3,0},identity,2) ||
      compile_gaussian_spacetime({0,0,3,std::numeric_limits<double>::quiet_NaN()},identity,0.8)) return 1;
  const std::array<double,16> thin{1+1e-10,1,0,0, 1,1+1e-10,0,0, 0,0,1,0.3, 0,0,0.3,1};
  const auto flat=compile_gaussian_spacetime({0,0,3,0},thin,0.8);
  if(!flat || valid_gaussian_conditional(flat->spatial) || !valid_gaussian_temporal_factor(flat->temporal)) return 1;
  std::cout << "{\"jointDensityComparisons\":" << comparisons << ",\"maximumAbsoluteDensityError\":" << maximum_error
            << ",\"invalidInputsRejected\":" << rejected+2 << ",\"thinCovarianceFactorPreserved\":true}\n";
  return maximum_error < 2e-6 ? 0 : 1;
}
