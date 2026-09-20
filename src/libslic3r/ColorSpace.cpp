#include "ColorSpace.hpp"

#include <cmath>

namespace Slic3r {

namespace {

constexpr double k_pi = 3.14159265358979323846;

double pivot_rgb(double n)
{
    return (n > 0.04045 ? std::pow((n + 0.055) / 1.055, 2.4) : n / 12.92) * 100.0;
}

double pivot_xyz(double n)
{
    double i = std::cbrt(n);
    return n > 0.008856 ? i : 7.787 * n + 16.0 / 116.0;
}

void rgb_to_xyz(float R, float G, float B, float *X, float *Y, float *Z)
{
    R = static_cast<float>(pivot_rgb(R));
    G = static_cast<float>(pivot_rgb(G));
    B = static_cast<float>(pivot_rgb(B));
    *X = 0.412453f * R + 0.357580f * G + 0.180423f * B;
    *Y = 0.212671f * R + 0.715160f * G + 0.072169f * B;
    *Z = 0.019334f * R + 0.119193f * G + 0.950227f * B;
}

void xyz_to_lab(float X, float Y, float Z, float *L, float *a, float *b)
{
    const double REF_X = 95.047;
    const double REF_Y = 100.000;
    const double REF_Z = 108.883;
    const double x     = pivot_xyz(X / REF_X);
    const double y     = pivot_xyz(Y / REF_Y);
    const double z     = pivot_xyz(Z / REF_Z);
    *L = static_cast<float>(116.0 * y - 16.0);
    *a = static_cast<float>(500.0 * (x - y));
    *b = static_cast<float>(200.0 * (y - z));
}

} // namespace

CIELab rgb_to_lab(float r, float g, float b)
{
    float X = 0.f, Y = 0.f, Z = 0.f;
    float L = 0.f, a = 0.f, bb = 0.f;
    rgb_to_xyz(r, g, b, &X, &Y, &Z);
    xyz_to_lab(X, Y, Z, &L, &a, &bb);
    return {double(L), double(a), double(bb)};
}

CIELab rgb_u8_to_lab(unsigned r, unsigned g, unsigned b)
{
    return rgb_to_lab(float(r) / 255.f, float(g) / 255.f, float(b) / 255.f);
}

float delta_e00_f(float l1, float a1, float b1, float l2, float a2, float b2)
{
    auto rad2deg = [](float rad) {
        return static_cast<float>(360.0 * rad / (2.0 * k_pi));
    };
    auto deg2rad = [](float deg) {
        return static_cast<float>((2.0 * k_pi * deg) / 360.0);
    };

    float avgL = (l1 + l2) / 2.0f;
    float c1   = std::sqrt(std::pow(a1, 2) + std::pow(b1, 2));
    float c2   = std::sqrt(std::pow(a2, 2) + std::pow(b2, 2));
    float avgC = (c1 + c2) / 2.0f;
    float g    = (1.0f - std::sqrt(std::pow(avgC, 7) / (std::pow(avgC, 7) + std::pow(25.0f, 7)))) / 2.0f;

    float a1p = a1 * (1.0f + g);
    float a2p = a2 * (1.0f + g);

    float c1p = std::sqrt(std::pow(a1p, 2) + std::pow(b1, 2));
    float c2p = std::sqrt(std::pow(a2p, 2) + std::pow(b2, 2));

    float avgCp = (c1p + c2p) / 2.0f;

    float h1p = rad2deg(std::atan2(b1, a1p));
    if (h1p < 0.0f)
        h1p += 360.0f;

    float h2p = rad2deg(std::atan2(b2, a2p));
    if (h2p < 0.0f)
        h2p += 360.0f;

    float avghp = std::abs(h1p - h2p) > 180.0f ? (h1p + h2p + 360.0f) / 2.0f : (h1p + h2p) / 2.0f;

    float t = 1.0f - 0.17f * std::cos(deg2rad(avghp - 30.0f)) + 0.24f * std::cos(deg2rad(2.0f * avghp))
              + 0.32f * std::cos(deg2rad(3.0f * avghp + 6.0f)) - 0.2f * std::cos(deg2rad(4.0f * avghp - 63.0f));

    float deltahp = h2p - h1p;
    if (std::abs(deltahp) > 180.0f) {
        if (h2p <= h1p)
            deltahp += 360.0f;
        else
            deltahp -= 360.0f;
    }

    float deltalp = l2 - l1;
    float deltacp = c2p - c1p;

    deltahp = 2.0f * std::sqrt(c1p * c2p) * std::sin(deg2rad(deltahp) / 2.0f);

    float sl = 1.0f + ((0.015f * std::pow(avgL - 50.0f, 2)) / std::sqrt(20.0f + std::pow(avgL - 50.0f, 2)));
    float sc = 1.0f + 0.045f * avgCp;
    float sh = 1.0f + 0.015f * avgCp * t;

    float deltaro = 30.0f * std::exp(-(std::pow((avghp - 275.0f) / 25.0f, 2)));
    float rc      = 2.0f * std::sqrt(std::pow(avgCp, 7) / (std::pow(avgCp, 7) + std::pow(25.0f, 7)));
    float rt      = -rc * std::sin(2.0f * deg2rad(deltaro));

    float kl = 1;
    float kc = 1;
    float kh = 1;

    return std::sqrt(std::pow(deltalp / (kl * sl), 2) + std::pow(deltacp / (kc * sc), 2)
                     + std::pow(deltahp / (kh * sh), 2) + rt * (deltacp / (kc * sc)) * (deltahp / (kh * sh)));
}

double delta_e00(const CIELab &lhs, const CIELab &rhs)
{
    return double(delta_e00_f(float(lhs.L), float(lhs.a), float(lhs.b), float(rhs.L), float(rhs.a), float(rhs.b)));
}

} // namespace Slic3r
