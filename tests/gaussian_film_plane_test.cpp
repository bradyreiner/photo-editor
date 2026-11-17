#include "gaussian_film_plane.h"
#include <iostream>
using namespace photo_editor::desktop;
int main() {
  const auto fail=[](const char* message) { std::cerr<<message<<'\n'; return 1; };
  // Same physical pixel evaluated as one rectangle or four subpixels. The
  // independent integral identity catches resolution-dependent realizations.
  double max_error=0;
  for (const uint32_t seed:{0U,1U,4294967295U}) {
    const auto coarse=film_grain_spectrum(12,36,24,seed);
    const auto fine=film_grain_spectrum(12,18,12,seed);
    for (int y=-10;y<=10;++y) for (int x=-10;x<=10;++x) {
      double average=0;
      for (const double dy:{-6.0,6.0}) for (const double dx:{-9.0,9.0})
        average+=film_grain_value(fine,x*36+dx,y*24+dy)/4;
      const double error=std::abs(average-film_grain_value(coarse,x*36,y*24));
      max_error=std::max(max_error,error);
      if (error>2e-7) return fail("Physical pixel integration differs across resolutions");
    }
    const auto point=film_grain_spectrum(12,0,0,seed);
    if (std::abs(film_grain_variance(point)-1)>1e-7 || film_grain_variance(coarse)>=film_grain_variance(fine) ||
        film_grain_variance(fine)>=film_grain_variance(point)) return fail("Unresolved grain variance must decrease");
  }
  // Stationary phase ensemble: unit point variance and the target correlation
  // at one correlation length. Frequency quadrature is an approximation.
  double mean=0,variance=0,covariance=0;
  for (uint32_t seed=0;seed<4096;++seed) {
    const auto spectrum=film_grain_spectrum(12,0,0,seed);
    const double a=film_grain_value(spectrum,0,0),b=film_grain_value(spectrum,12,0);
    mean+=a/4096; variance+=a*a/4096; covariance+=a*b/4096;
  }
  if (std::abs(mean)>0.05 || std::abs(variance-1)>0.06 || std::abs(covariance-std::exp(-0.5))>0.06)
    return fail("Grain ensemble mean, variance or spatial covariance differs from Gaussian field");
  std::cout<<"Film plane: rectangle integral error="<<max_error<<", ensemble mean="<<mean
      <<", variance="<<variance<<", covariance="<<covariance<<'\n';
  return 0;
}
