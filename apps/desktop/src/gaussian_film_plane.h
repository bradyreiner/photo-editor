#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace photo_editor::desktop {
// Finite spectral approximation to a stationary Gaussian density field, with
// covariance exp(-distance^2/(2*correlation_um^2)). This is an independently
// implemented statistical approximation, not discrete silver-grain simulation.
// Integrating each sinusoid over the actual pixel rectangle avoids introducing
// Monte Carlo sampling noise or changing the grain realization with resolution.
constexpr size_t film_grain_modes=128;
using FilmGrainSpectrum=std::array<std::array<float,4>,film_grain_modes>;
inline uint32_t film_plane_hash(uint32_t x) {
  x^=x>>16; x*=0x7feb352dU; x^=x>>15; x*=0x846ca68bU; return x^(x>>16);
}
inline double film_plane_sinc(double x) { return std::abs(x)<1e-8 ? 1-x*x/6 : std::sin(x)/x; }
inline FilmGrainSpectrum film_grain_spectrum(double correlation_um,double pixel_width_um,
    double pixel_height_um,uint32_t seed) {
  FilmGrainSpectrum result{};
  const double rotation=static_cast<double>(film_plane_hash(seed))/4294967296.0;
  for (size_t i=0;i<film_grain_modes;++i) {
    const double radius=std::sqrt(-2*std::log(1-(static_cast<double>(i)+0.5)/film_grain_modes))/correlation_um;
    const double angle=2*std::numbers::pi*std::fmod(rotation+static_cast<double>(i)*0.6180339887498948482,1.0);
    const double x=radius*std::cos(angle),y=radius*std::sin(angle);
    const double phase=2*std::numbers::pi*static_cast<double>(film_plane_hash(seed^film_plane_hash(static_cast<uint32_t>(i)+1)))/4294967296.0;
    const double amplitude=std::sqrt(2.0/film_grain_modes)*film_plane_sinc(x*pixel_width_um/2)*film_plane_sinc(y*pixel_height_um/2);
    result[i]={static_cast<float>(x),static_cast<float>(y),static_cast<float>(phase),static_cast<float>(amplitude)};
  }
  return result;
}
inline double film_grain_value(const FilmGrainSpectrum& spectrum,double x_um,double y_um) {
  double result=0;
  for (const auto& mode:spectrum) result+=mode[3]*std::sin(mode[0]*x_um+mode[1]*y_um+mode[2]);
  return result;
}
inline double film_grain_variance(const FilmGrainSpectrum& spectrum) {
  double result=0; for (const auto& mode:spectrum) result+=0.5*mode[3]*mode[3]; return result;
}
} // namespace photo_editor::desktop
