#include "ColorSpaceConvert.hpp"
#include "libslic3r/ColorSpace.hpp"

#include <algorithm>
#include <boost/algorithm/string.hpp>
#include <wx/colordlg.h>
#include <cmath>

const static float param_13 = 1.0f / 3.0f;
const static float param_16116 = 16.0f / 116.0f;
const static float Xn = 0.950456f;
const static float Yn = 1.0f;
const static float Zn = 1.088754f;

std::tuple<int, int, int> rgb_to_yuv(float r, float g, float b)
{
    int y = (int)(0.2126 * r + 0.7152 * g + 0.0722 * b);
    int u = (int)(-0.09991 * r - 0.33609 * g + 0.436 * b);
    int v = (int)(0.615 * r - 0.55861 * g - 0.05639 * b);
    return std::make_tuple(y, u, v);
}

double PivotRGB(double n)
{
    return (n > 0.04045 ? std::pow((n + 0.055) / 1.055, 2.4) : n / 12.92) * 100.0;
}

double PivotXYZ(double n)
{
    double i = std::cbrt(n);
    return n > 0.008856 ? i : 7.787 * n + 16.0 / 116.0;
}

void XYZ2RGB(float X, float Y, float Z, float* R, float* G, float* B)
{
    float RR, GG, BB;
    RR = 3.240479f * X - 1.537150f * Y - 0.498535f * Z;
    GG = -0.969256f * X + 1.875992f * Y + 0.041556f * Z;
    BB = 0.055648f * X - 0.204043f * Y + 1.057311f * Z;

    *R = (float)std::clamp(RR, 0.f, 255.f);
    *G = (float)std::clamp(GG, 0.f, 255.f);
    *B = (float)std::clamp(BB, 0.f, 255.f);
}

void Lab2XYZ(float L, float a, float b, float* X, float* Y, float* Z)
{
    float fX, fY, fZ;

    fY = (L + 16.0f) / 116.0f;
    if (fY > 0.206893f)
        *Y = fY * fY * fY;
    else
        *Y = (fY - param_16116) / 7.787f;

    fX = a / 500.0f + fY;
    if (fX > 0.206893f)
        *X = fX * fX * fX;
    else
        *X = (fX - param_16116) / 7.787f;

    fZ = fY - b / 200.0f;
    if (fZ > 0.206893f)
        *Z = fZ * fZ * fZ;
    else
        *Z = (fZ - param_16116) / 7.787f;

    (*X) *= Xn;
    (*Y) *= Yn;
    (*Z) *= Zn;
}

void Lab2RGB(float L, float a, float b, float* R, float* G, float* B)
{
    float X = 0.0f, Y = 0.0f, Z = 0.0f;
    Lab2XYZ(L, a, b, &X, &Y, &Z);
    XYZ2RGB(X, Y, Z, R, G, B);
}

void RGB2XYZ(float R, float G, float B, float* X, float* Y, float* Z)
{
    R = PivotRGB(R);
    G = PivotRGB(G);
    B = PivotRGB(B);

    *X = 0.412453f * R + 0.357580f * G + 0.180423f * B;
    *Y = 0.212671f * R + 0.715160f * G + 0.072169f * B;
    *Z = 0.019334f * R + 0.119193f * G + 0.950227f * B;
}

void XYZ2Lab(float X, float Y, float Z, float* L, float* a, float* b)
{
    double REF_X = 95.047;
    double REF_Y = 100.000;
    double REF_Z = 108.883;

    double x = PivotXYZ(X / REF_X);
    double y = PivotXYZ(Y / REF_Y);
    double z = PivotXYZ(Z / REF_Z);

    *L = 116.0 * y - 16.0;
    *a = 500.0 * (x - y);
    *b = 200.0 * (y - z);
}

void RGB2Lab(float R, float G, float B, float* L, float* a, float* b)
{
    const Slic3r::CIELab lab = Slic3r::rgb_to_lab(R, G, B);
    *L = float(lab.L);
    *a = float(lab.a);
    *b = float(lab.b);
}

// The input r, g, b values should be in range [0, 1]. The output h is in range [0, 360], s is in range [0, 1] and v is in range [0, 1].
void RGB2HSV(float r, float g, float b, float* h, float* s, float* v)
{
    float Cmax = std::max(std::max(r, g), b);
    float Cmin = std::min(std::min(r, g), b);
    float delta = Cmax - Cmin;

    if (std::abs(delta) < 0.001) {
        *h = 0.f;
    }
    else if (Cmax == r) {
        *h = 60.f * fmod((g - b) / delta, 6.f);
    }
    else if (Cmax == g) {
        *h = 60.f * ((b - r) / delta + 2);
    }
    else {
        *h = 60.f * ((r - g) / delta + 4);
    }

    if (std::abs(Cmax) < 0.001) {
        *s = 0.f;
    }
    else {
        *s = delta / Cmax;
    }

    *v = Cmax;
}

float DeltaE00(float l1, float a1, float b1, float l2, float a2, float b2)
{
    return Slic3r::delta_e00_f(l1, a1, b1, l2, a2, b2);
}

float DeltaE94(float l1, float a1, float b1, float l2, float a2, float b2)
{
    float k_L = 1;
    float k_C = 1;
    float k_H = 1;
    float K1 = 0.045;
    float K2 = 0.015;

    float delta_l = l1 - l2;
    float C1 = std::sqrt(a1 * a1 + b1 * b1);
    float C2 = std::sqrt(a2 * a2 + b2 * b2);
    float delta_c = C1 - C2;
    float delta_a = a1 - a2;
    float delta_b = b1 - b2;
    float delta_h = std::sqrt(delta_a * delta_a + delta_b * delta_b - delta_c * delta_c);
    float SL = 1.0;
    float SC = 1 + K1 * C1;
    float SH = 1 + K2 * C1;

    float delta_e94 = std::sqrt(std::pow(delta_l / (k_L * SL), 2) + std::pow(delta_c / (k_C * SC), 2) + std::pow(delta_h / (k_H / k_C * SH), 2));
    return delta_e94;
}

float DeltaE76(float l1, float a1, float b1, float l2, float a2, float b2)
{
    return std::sqrt(std::pow((l1 - l2), 2) + std::pow((a1 - a2), 2) + std::pow((b1 - b2), 2));
}

std::string color_to_string(const wxColour &color)
{
    std::string str = std::to_string(color.Red()) + "," + std::to_string(color.Green()) + "," + std::to_string(color.Blue()) + "," + std::to_string(color.Alpha());
    return str;
}

wxColour string_to_wxColor(const std::string &str)
{
    wxColour                 color;
    std::vector<std::string> result;
    boost::split(result, str, boost::is_any_of(","));
    if (result.size() == 4) {
        color = wxColour(std::stoi(result[0]), std::stoi(result[1]), std::stoi(result[2]), std::stoi(result[3]));
    }
    return color;
};
