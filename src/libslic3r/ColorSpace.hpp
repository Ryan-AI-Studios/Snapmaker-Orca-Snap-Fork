#ifndef slic3r_ColorSpace_hpp_
#define slic3r_ColorSpace_hpp_

namespace Slic3r {

// CIELab sample. Same layout as the GUI matcher type.
struct CIELab {
    double L = 0.0;
    double a = 0.0;
    double b = 0.0;
};

// sRGB channels in [0, 1] → CIELab (D65, same path as GUI ColorSpaceConvert::RGB2Lab).
CIELab rgb_to_lab(float r, float g, float b);

// 8-bit sRGB → CIELab.
CIELab rgb_u8_to_lab(unsigned r, unsigned g, unsigned b);

// CIEDE2000 (same path as GUI ColorSpaceConvert::DeltaE00).
double delta_e00(const CIELab &lhs, const CIELab &rhs);
float  delta_e00_f(float l1, float a1, float b1, float l2, float a2, float b2);

} // namespace Slic3r

#endif
