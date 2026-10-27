#include "falsecolor_core.h"

#include <array>
#include <cmath>

namespace fc {

const int kPalette255[kNumBands][3] = {
    {74, 44, 85},    //  0 purple
    {61, 98, 137},   //  1 blue
    {82, 130, 160},  //  2 steel blue
    {107, 160, 174}, //  3 teal
    {128, 163, 168}, //  4 grey-blue
    {142, 154, 132}, //  5 greyish olive
    {146, 185, 106}, //  6 green
    {160, 171, 159}, //  7 greyish green
    {211, 149, 144}, //  8 dark pink
    {218, 179, 175}, //  9 light pink
    {222, 228, 212}, // 10 greenish white
    {241, 242, 154}, // 11 pale yellow
    {254, 249, 122}, // 12 yellow
    {235, 179, 89},  // 13 orange
    {223, 88, 63},   // 14 red-orange
};

#ifdef FC_GENERATOR
const float kPaletteF[kNumBands][3] = {};  // the generator derives the header from the ints
#else
const float kPaletteF[kNumBands][3] = FC_PALETTE_INIT;
#endif

const char* const kSourceNames[FC_NUM_SOURCES] = {
    "Rec.709", "Rec.2020", "Apple Gamma 2.0", "ACEScct (AP1)",
    "DVR WG/Rec709", "DVR WG/Intermediate", "Cineon Film Log",
};

const char* const kBandLabels[kNumBands] = {
    "PRETO SEM DETALHE",        //  0
    "SOMBRA PROFUNDA",          //  1
    "SOMBRA ESCURA",            //  2
    "SOMBRA COM TEXTURA",       //  3
    "SOMBRA ABERTA",            //  4
    "MEIO-TOM ESCURO",          //  5
    "PELE ESCURA - CINZA 18%",  //  6  mid-tone zone
    "PELE MÉDIA",               //  7  mid-tone zone
    "PELE CLARA",               //  8  mid-tone zone
    "LUZ ALTA",                 //  9
    "BRANCO COM TEXTURA",       // 10
    "BRANCO",                   // 11
    "BRANCO BRILHANTE",         // 12
    "QUASE ESTOURADO",          // 13
    "ESTOURADO (CLIP)",         // 14
};

int bandIreLow(int k) { return (int)std::lround(k * 109.0 / 15.0); }
int bandIreHigh(int k) { return (int)std::lround((k + 1) * 109.0 / 15.0); }

namespace {
struct M3 { double a[9]; };
M3 mul(const M3& x, const M3& y) {
    M3 r{};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            for (int k = 0; k < 3; ++k) r.a[i * 3 + j] += x.a[i * 3 + k] * y.a[k * 3 + j];
    return r;
}
M3 inv(const M3& m) {
    const double* a = m.a;
    double det = a[0] * (a[4] * a[8] - a[5] * a[7]) - a[1] * (a[3] * a[8] - a[5] * a[6]) + a[2] * (a[3] * a[7] - a[4] * a[6]);
    M3 r{{(a[4] * a[8] - a[5] * a[7]) / det, (a[2] * a[7] - a[1] * a[8]) / det, (a[1] * a[5] - a[2] * a[4]) / det,
          (a[5] * a[6] - a[3] * a[8]) / det, (a[0] * a[8] - a[2] * a[6]) / det, (a[2] * a[3] - a[0] * a[5]) / det,
          (a[3] * a[7] - a[4] * a[6]) / det, (a[1] * a[6] - a[0] * a[7]) / det, (a[0] * a[4] - a[1] * a[3]) / det}};
    return r;
}
struct Prim { double rx, ry, gx, gy, bx, by, wx, wy; };
// RGB -> XYZ from chromaticities (SMPTE RP 177).
M3 rgbToXyz(const Prim& p) {
    auto col = [](double x, double y) { return std::array<double, 3>{x / y, 1.0, (1 - x - y) / y}; };
    auto r = col(p.rx, p.ry), g = col(p.gx, p.gy), b = col(p.bx, p.by), w = col(p.wx, p.wy);
    M3 P{{r[0], g[0], b[0], r[1], g[1], b[1], r[2], g[2], b[2]}};
    M3 Pi = inv(P);
    double s[3];
    for (int i = 0; i < 3; ++i) s[i] = Pi.a[i * 3] * w[0] + Pi.a[i * 3 + 1] * w[1] + Pi.a[i * 3 + 2] * w[2];
    M3 S{{s[0], 0, 0, 0, s[1], 0, 0, 0, s[2]}};
    return mul(P, S);
}
// Bradford chromatic adaptation XYZ(src white) -> XYZ(dst white).
M3 bradford(double sx, double sy, double dx, double dy) {
    const M3 B{{0.8951, 0.2664, -0.1614, -0.7502, 1.7135, 0.0367, 0.0389, -0.0685, 1.0296}};
    auto lms = [&](double x, double y) {
        double X = x / y, Y = 1.0, Z = (1 - x - y) / y;
        return std::array<double, 3>{B.a[0] * X + B.a[1] * Y + B.a[2] * Z, B.a[3] * X + B.a[4] * Y + B.a[5] * Z,
                                     B.a[6] * X + B.a[7] * Y + B.a[8] * Z};
    };
    auto s = lms(sx, sy), d = lms(dx, dy);
    M3 D{{d[0] / s[0], 0, 0, 0, d[1] / s[1], 0, 0, 0, d[2] / s[2]}};
    return mul(inv(B), mul(D, B));
}

const Prim kRec709{0.640, 0.330, 0.300, 0.600, 0.150, 0.060, 0.3127, 0.3290};
const Prim kRec2020{0.708, 0.292, 0.170, 0.797, 0.131, 0.046, 0.3127, 0.3290};
const Prim kAP1{0.713, 0.293, 0.165, 0.830, 0.128, 0.044, 0.32168, 0.33767};
const Prim kDWG{0.8000, 0.3130, 0.1682, 0.9877, 0.0790, -0.1155, 0.3127, 0.3290};
}  // namespace

FCMat3 sourceToRec709(int source, bool* identity) {
    const Prim* p = nullptr;
    switch (source) {
        case FC_SRC_REC2020: p = &kRec2020; break;
        case FC_SRC_ACESCCT: p = &kAP1; break;
        case FC_SRC_DWG_REC709:
        case FC_SRC_DWG_DI: p = &kDWG; break;
        default: break;  // Rec.709, Apple Gamma 2.0 and Cineon use Rec.709 primaries
    }
    FCMat3 out{};
    if (!p) {
        out.m[0] = out.m[4] = out.m[8] = 1.0f;
        if (identity) *identity = true;
        return out;
    }
    M3 toXyz = rgbToXyz(*p);
    if (p->wx != kRec709.wx || p->wy != kRec709.wy) toXyz = mul(bradford(p->wx, p->wy, kRec709.wx, kRec709.wy), toXyz);
    M3 m = mul(inv(rgbToXyz(kRec709)), toXyz);
    for (int i = 0; i < 9; ++i) out.m[i] = (float)m.a[i];
    if (identity) *identity = false;
    return out;
}

int bandFromY(float y) { return fc_band(y); }

float measureY(int source, float r, float g, float b) {
    bool ident = true;
    FCMat3 m = sourceToRec709(source, &ident);
    return fc_measure(source, m, ident ? 1 : 0, r, g, b);
}

void falseColorPixel(const float in[4], int source, float mix, float out[4]) {
    bool ident = true;
    FCMat3 m = sourceToRec709(source, &ident);
    FCPixel p{in[0], in[1], in[2], in[3]};
    FCPixel o = fc_shade(p, source, m, ident ? 1 : 0, mix);
    out[0] = o.r; out[1] = o.g; out[2] = o.b; out[3] = o.a;
}

}  // namespace fc
