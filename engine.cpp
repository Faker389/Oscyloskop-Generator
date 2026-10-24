#include "engine.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static const double PI = 3.14159265358979323846;
static const double EDGE = 25e-9; // 0-100% edge; 10-90% is 20 ns (DG1022 square spec)

static double Clamp(double v, double a, double b) { return v < a ? a : (v > b ? b : v); }
static double Frac(double x) { double f = std::fmod(x, 1.0); return f < 0 ? f + 1.0 : f; }

static void NoteG(const char *s)
{
    std::snprintf(app.gmsg, sizeof(app.gmsg), "%s", s);
    app.gmsgT = 2.6;
}
static void NoteS(const char *s)
{
    std::snprintf(app.smsg, sizeof(app.smsg), "%s", s);
    app.smsgT = 2.6;
}

App app;

static int FmtPlaced(char *b, int n, double v, int digit, bool cursor, double scale, int scaleExp, const char *unit)
{
    double a = std::fabs(v);
    if (a < scale * 1e-9) a = scale;
    int e = (int)std::floor(std::log10(a) + 1e-9);
    int before = e - scaleExp + 1;
    if (before < 1) before = 1;
    if (before > 6) before = 6;
    int decimals = digit - (before - 1);
    if (decimals < 3) decimals = 3;
    if (decimals > 8) decimals = 8;
    std::snprintf(b, n, "%.*f %s", decimals, v / scale, unit);
    if (!cursor) return -1;
    int rel = e - digit - scaleExp;
    int idx = (rel >= 0) ? (before - 1 - rel) : (before - rel);
    if (v < 0) idx += 1;
    if (idx < 0 || idx >= (int)std::strlen(b) || b[idx] < '0' || b[idx] > '9') return -1;
    return idx;
}

static int FmtPick(char *b, int n, double v, int digit, bool cursor,
                   const double *scale, const int *expn, const char *const *unit, int count)
{
    double a = std::fabs(v);
    int i = count - 1;
    for (int k = 0; k < count; k++) if (a >= scale[k]) { i = k; break; }
    return FmtPlaced(b, n, v, digit, cursor, scale[i], expn[i], unit[i]);
}

void FmtFreq(char *b, int n, double hz)
{
    FmtFreqAt(b, n, hz, 0, false);
}

int FmtFreqAt(char *b, int n, double hz, int digit, bool cursor)
{
    static const double scale[] = { 1e6, 1e3, 1, 1e-3, 1e-6 };
    static const int expn[] = { 6, 3, 0, -3, -6 };
    static const char *unit[] = { "MHz", "kHz", "Hz", "mHz", "uHz" };
    if (digit < 0) digit = 0;
    if (digit > 8) digit = 8;
    return FmtPick(b, n, hz, digit, cursor, scale, expn, unit, 5);
}

int FmtTimeAt(char *b, int n, double sec, int digit, bool cursor)
{
    static const double scale[] = { 1, 1e-3, 1e-6, 1e-9 };
    static const int expn[] = { 0, -3, -6, -9 };
    static const char *unit[] = { "s", "ms", "us", "ns" };
    if (digit < 0) digit = 0;
    if (digit > 8) digit = 8;
    return FmtPick(b, n, sec, digit, cursor, scale, expn, unit, 4);
}
void FmtVpp(char *b, int n, double v)
{
    if (std::fabs(v) >= 1) std::snprintf(b, n, "%.1f Vpp", v);
    else std::snprintf(b, n, "%.2f mVpp", v * 1e3);
}
void FmtVolt(char *b, int n, double v)
{
    double a = std::fabs(v);
    if (a >= 1) std::snprintf(b, n, "%+.1f V", v);
    else if (a >= 1e-3) std::snprintf(b, n, "%+.2f mV", v * 1e3);
    else std::snprintf(b, n, "%+.1f uV", v * 1e6);
}
void FmtTime(char *b, int n, double s)
{
    double a = std::fabs(s);
    if (a >= 1) std::snprintf(b, n, "%.1f s", s);
    else if (a >= 1e-3) std::snprintf(b, n, "%.1f ms", s * 1e3);
    else if (a >= 1e-6) std::snprintf(b, n, "%.1f us", s * 1e6);
    else std::snprintf(b, n, "%.2f ns", s * 1e9);
}

const char *ArbName(int i)
{
    static const char *n[ARB_N] = {
        "Sinc", "NegRamp", "AttALT", "AmpALT", "StairDn", "StairUp", "StairUD", "CPulse",
        "PPulse", "NPulse", "Trapezia", "RndHalf", "AbsSine", "AbsHalf", "SineTra", "SineVer",
        "ExpRise", "ExpFall", "Tan", "Cot", "Sqrt", "X^2", "Gauss", "Haversin",
        "Lorentz", "Ln", "X^3", "Cardiac", "Quake", "Gamma", "Voice", "TwoTone",
        "Sinc2", "Gauss2", "DLorentz", "Haver2", "Laplace", "LogNorm", "Weibull", "Cauchy",
        "Bartlett", "Blackman", "Cheb", "Butter", "Bessel", "Dirich", "Fejer", "Kaiser"
    };
    if (i < 0) i = 0;
    return n[i % ARB_N];
}

static double Hash01(unsigned x)
{
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
    return (x & 65535) / 65535.0;
}

double ShapeOf(const GenCh &c, double phase)
{
    double p = Frac(phase);
    switch (c.wave) {
    case W_SINE: return std::sin(2 * PI * p);
    case W_SQUARE:
    case W_PULSE: {
        double e = std::min(0.2, EDGE * c.freq);
        double d = Clamp(c.duty / 100.0, e + 1e-6, 1.0 - e);
        double s;
        if (p < e) s = -1 + 2 * (p / e);
        else if (p < d) s = 1;
        else if (p < d + e) s = 1 - 2 * ((p - d) / e);
        else s = -1;
        if (p >= e && p < d) {
            double x = (p - e) / e;
            if (x < 8) s += 0.05 * std::exp(-x);
        }
        if (p >= d + e) {
            double x = (p - (d + e)) / e;
            if (x < 8) s -= 0.05 * std::exp(-x);
        }
        return s;
    }
    case W_RAMP: {
        double s = Clamp(c.symmetry / 100.0, 0.0, 1.0);
        if (s < 1e-4) return 1 - 2 * p;
        if (s > 0.999) return -1 + 2 * p;
        if (p < s) return -1 + 2 * (p / s);
        return 1 - 2 * ((p - s) / (1 - s));
    }
    case W_NOISE:
        return Hash01((unsigned)(p * 1e6) ^ 0x9e3779b9u) * 2 - 1;
    default: {
        int id = c.arb % ARB_N;
        double s;
        switch (id) {
        case 0: { double t = (p - 0.5) * 8 * PI; s = std::fabs(t) < 1e-4 ? 1 : std::sin(t) / t; break; }
        case 1: s = 1 - 2 * p; break;
        case 4: s = std::floor(p * 8) / 3.5 - 1; break;
        case 5: s = std::floor(p * 8) / 3.5 - 1; s = -s; break;
        case 10: s = (p < 0.2) ? -1 + p / 0.2 * 2 : (p < 0.8 ? 1 : 1 - (p - 0.8) / 0.2 * 2); break;
        case 12: s = std::fabs(std::sin(2 * PI * p)) * 2 - 1; break;
        case 16: s = -1 + 2 * (1 - std::exp(-p * 5)) / (1 - std::exp(-5.0)); break;
        case 17: s = 1 - 2 * (1 - std::exp(-p * 5)) / (1 - std::exp(-5.0)); break;
        case 22: s = std::exp(-0.5 * std::pow((p - 0.5) * 6, 2)) * 2 - 1; break;
        case 23: s = 0.5 * (1 - std::cos(2 * PI * p)) * 2 - 1; break;
        case 24: s = 1.0 / (1 + std::pow((p - 0.5) * 8, 2)) * 2 - 1; break;
        case 27: s = std::sin(2 * PI * p) * std::exp(-3 * p); break;
        case 31: s = 0.7 * std::sin(2 * PI * p) + 0.3 * std::sin(4 * PI * p); break;
        default:
            s = std::sin(2 * PI * p) + 0.35 * std::sin(2 * PI * (2 + id % 5) * p);
            s /= 1.35;
            break;
        }
        int bits = (&c == &app.gen.ch[1]) ? 511 : 8191;
        s = std::round(Clamp(s, -1.0, 1.0) * bits) / bits;
        return s;
    }
    }
}

static double FMax(const GenCh &c)
{
    switch (c.wave) {
    case W_SINE: return 20e6;
    case W_SQUARE: return 5e6;
    case W_RAMP: return 150e3;
    case W_PULSE: return 3e6;
    default: return 5e6;
    }
}
static double FMin(const GenCh &c) { return c.wave == W_PULSE ? 500e-6 : 1e-6; }

static void ClampDuty(GenCh &c)
{
    if (c.wave == W_SQUARE) {
        double lo = 20, hi = 80;
        if (c.freq > 4e6) lo = hi = 50;
        else if (c.freq > 3e6) { lo = 40; hi = 60; }
        c.duty = Clamp(c.duty, lo, hi);
    } else if (c.wave == W_PULSE) {
        double minD = 20e-9 * c.freq * 100.0;
        if (minD > 49) minD = 49;
        c.duty = Clamp(c.duty, minD, 100.0 - minD);
    }
    c.symmetry = Clamp(c.symmetry, 0.0, 100.0);
    c.phase = Frac(c.phase / 360.0) * 360.0;
}

static void ClampOut(GenCh &c)
{
    bool z = app.gen.highZ;
    bool ch2 = (&c == &app.gen.ch[1]);
    double amax = z ? (ch2 ? 6.0 : 20.0) : (ch2 ? 3.0 : 10.0);
    double amin = z ? 0.004 : 0.002;
    double vmax = z ? (ch2 ? 3.0 : 10.0) : (ch2 ? 1.5 : 5.0);
    c.freq = Clamp(c.freq, FMin(c), FMax(c));
    c.ampl = Clamp(c.ampl, amin, amax);
    double room = vmax - c.ampl * 0.5;
    if (room < 0) { c.ampl = 2 * vmax; room = 0; }
    c.offset = Clamp(c.offset, -room, room);
    ClampDuty(c);
}

static double ModShape(int kind, double phase)
{
    double p = Frac(phase);
    switch (kind) {
    case 1: return p < 0.5 ? 1 : -1;
    case 2: return -1 + 2 * p;
    case 3: return 1 - 2 * p;
    case 4: return p < 0.5 ? -1 + 4 * p : 3 - 4 * p;
    case 5: return Hash01((unsigned)(p * 10000)) * 2 - 1;
    default: return std::sin(2 * PI * p);
    }
}

static double CarrierCycles(const GenCh &c, double t)
{
    double dt = t - c.epoch;
    if (app.gen.mode == 2 && &c == &app.gen.ch[0]) {
        double T = app.gen.swTime;
        if (T < 1e-6) T = 1e-6;
        double u = std::fmod(dt, T); if (u < 0) u += T;
        double n = std::floor(dt / T);
        double f1 = app.gen.swStart, f2 = app.gen.swStop;
        if (!app.gen.swLog || f1 <= 0 || f2 <= 0) {
            double per = (f1 + f2) * 0.5 * T;
            return n * per + f1 * u + (f2 - f1) * u * u / (2 * T) + c.phase / 360.0;
        }
        double ratio = f2 / f1;
        double ln = std::log(ratio);
        double per = std::fabs(ln) < 1e-9 ? f1 * T : (f2 - f1) * T / ln;
        double cyc = std::fabs(ln) < 1e-9 ? f1 * u : f1 * T * (std::pow(ratio, u / T) - 1) / ln;
        return n * per + cyc + c.phase / 360.0;
    }
    if (app.gen.mode == 1 && &c == &app.gen.ch[0] && app.gen.modSource == 0 && app.gen.modType == 3) {
        double fm = std::max(2e-3, app.gen.modFreq);
        double Tm = 1.0 / fm;
        double n = std::floor(dt / Tm);
        double u = dt - n * Tm;
        double half = 0.5 * Tm;
        double base = n * (c.freq + app.gen.fskHop) * half;
        double cyc = u < half ? base + c.freq * u : base + c.freq * half + app.gen.fskHop * (u - half);
        return cyc + c.phase / 360.0;
    }
    double cyc = c.freq * dt + c.phase / 360.0;
    if (app.gen.mode == 1 && &c == &app.gen.ch[0] && app.gen.modSource == 0) {
        double fm = std::max(2e-3, app.gen.modFreq);
        if (app.gen.modType == 1)
            cyc += -(app.gen.fmDev / fm) * std::cos(2 * PI * fm * dt) / (2 * PI);
        else if (app.gen.modType == 2)
            cyc += (app.gen.pmDev / 360.0) * ModShape(app.gen.modShape, fm * dt);
    }
    return cyc;
}

// Voltage the generator display claims, into the selected load.
static double Setpoint(const GenCh &c, double t)
{
    if (c.dc) return c.offset;
    double env = 1;
    double dt = t - c.epoch;
    if (app.gen.mode == 1 && &c == &app.gen.ch[0] && app.gen.modSource == 0 && app.gen.modType == 0) {
        double m = ModShape(app.gen.modShape, app.gen.modFreq * dt);
        env = 1 + (app.gen.amDepth / 100.0) * m;
    }
    double shape = 0;
    if (c.wave == W_NOISE) {
        unsigned n = (unsigned)std::floor(t * 1.0e7);
        shape = Hash01(n ^ (unsigned)(&c - app.gen.ch) * 17u) * 2 - 1;
    } else if (app.gen.mode == 3 && &c == &app.gen.ch[0]) {
        double T = std::max(app.gen.bPeriod, app.gen.bCycles / c.freq);
        double u = std::fmod(dt, T); if (u < 0) u += T;
        double on = app.gen.bCycles / c.freq;
        if (u >= on) return c.offset;
        shape = ShapeOf(c, u * c.freq + app.gen.bPhase / 360.0);
    } else {
        shape = ShapeOf(c, CarrierCycles(c, t));
    }
    double flat = 1;
    if (c.wave == W_SINE) {
        if (c.freq >= 5e6) flat = std::pow(10.0, -0.3 / 20.0);
        else if (c.freq >= 1e5) flat = std::pow(10.0, -0.15 / 20.0);
    }
    int bits = (&c == &app.gen.ch[1]) ? 511 : 8191;
    if (c.wave != W_ARB) shape = std::round(Clamp(shape, -1.2, 1.2) * bits) / (double)bits;
    return c.offset + (c.ampl * 0.5) * flat * env * shape;
}

static double OpenVolts(int ch, double t)
{
    const GenCh &c = app.gen.ch[ch];
    if (!c.output) return 0;
    if (ch == 1 && app.gen.counter) return 0;
    double v = Setpoint(c, t);
    if (!app.gen.highZ) v *= 2.0;
    return v;
}

static double MeanOpen(int ch)
{
    const GenCh &c = app.gen.ch[ch];
    if (!c.output || (ch == 1 && app.gen.counter)) return 0;
    if (c.dc) return app.gen.highZ ? c.offset : c.offset * 2;
    double f = std::max(c.freq, 1.0);
    double s = 0;
    const int N = 160;
    for (int i = 0; i < N; i++) s += OpenVolts(ch, c.epoch + i / (f * N));
    return s / N;
}

static int gEpoch = 0, gEpochDone = -1;
static double gMean[2];
static void Touch() { gEpoch++; }

double ScopeVolts(int ch, double t)
{
    if (gEpochDone != gEpoch) {
        gEpochDone = gEpoch;
        gMean[0] = MeanOpen(0);
        gMean[1] = MeanOpen(1);
    }
    const ScopeCh &s = app.sc.ch[ch];
    if (s.coupling == 2) return 0;
    double open = OpenVolts(ch, t);
    double mean = gMean[ch];
    double bw = s.bwLimit ? 20e6 : 60e6;
    double f = app.gen.ch[ch].freq;
    if (app.gen.ch[ch].wave == W_NOISE) f = 5e6;
    double g = 1.0 / std::sqrt(1.0 + (f / bw) * (f / bw));
    double v = mean + (open - mean) * g;
    if (app.sc.filtOn && app.sc.filtHi > 1.0) {
        double f0 = std::max(f, 1.0);
        double lp = 1.0 / std::sqrt(1.0 + (f0 / app.sc.filtHi) * (f0 / app.sc.filtHi));
        double hp = f0 / std::sqrt(f0 * f0 + app.sc.filtLo * app.sc.filtLo);
        double g2 = 1;
        if (app.sc.filtType == 0) g2 = lp;
        else if (app.sc.filtType == 1) g2 = hp;
        else if (app.sc.filtType == 2) g2 = lp * hp;
        else g2 = std::max(0.0, 1.0 - lp * hp);
        v = mean + (v - mean) * g2;
    }
    if (s.coupling == 1) v -= mean;
    if (s.invert) v = -v;
    return v * s.probe;
}

static double Ceil125(double v)
{
    if (v <= 0) return 1e-9;
    double e = std::floor(std::log10(v));
    double m = v / std::pow(10.0, e);
    double s = 1;
    if (m > 1) s = 2;
    if (m > 2) s = 5;
    if (m > 5) { s = 1; e += 1; }
    return s * std::pow(10.0, e);
}
static double Step125(double v, int dir)
{
    if (v <= 0) v = 1;
    double snapped = Ceil125(v * 0.999999);
    if (dir > 0) return Ceil125(snapped * 1.01);
    double down = snapped / 2.5;
    double c = Ceil125(down);
    if (c >= snapped) c = Ceil125(snapped * 0.4);
    return c;
}

static void DefaultCh(GenCh &c)
{
    c.wave = W_SINE; c.freq = 1000; c.ampl = 5; c.offset = 0; c.phase = 0;
    c.duty = 50; c.symmetry = 50; c.output = false; c.dc = false; c.arb = 0; c.epoch = 0;
}

void AppInit()
{
    std::memset(&app, 0, sizeof(app));
    DefaultCh(app.gen.ch[0]);
    DefaultCh(app.gen.ch[1]);
    app.gen.highZ = true;
    app.gen.beep = true;
    app.gen.digit = 3;
    app.gen.edit = PAR_FREQ;
    app.gen.page = GP_WAVE;
    app.gen.modFreq = 100;
    app.gen.amDepth = 100;
    app.gen.fmDev = 1000;
    app.gen.pmDev = 90;
    app.gen.fskHop = 10000;
    app.gen.swStart = 100;
    app.gen.swStop = 1000;
    app.gen.swTime = 1;
    app.gen.bCycles = 5;
    app.gen.bPeriod = 0.01;
    for (int i = 0; i < 2; i++) {
        app.sc.ch[i].on = true;
        app.sc.ch[i].vdiv = 1;
        app.sc.ch[i].probe = 1;
        app.sc.ch[i].coupling = 0;
    }
    app.sc.tdiv = 500e-6;
    app.sc.delayDiv = 50e-6;
    app.sc.trigMode = 0;
    app.sc.running = true;
    app.sc.menuOn = true;
    app.sc.page = SP_CH;
    app.sc.menuTime = 5;
    app.sc.menuIdle = 1e9;
    app.sc.trigSens = 0.3;
    app.sc.pulseWidth = 100e-6;
    app.sc.filtLo = 100;
    app.sc.filtHi = 1e3;
    app.sc.gridBright = 0.7f;
    app.sc.fftWin = 2;
    app.sc.averages = 16;
    app.sc.intensity = 0.9f;
    app.sc.mathVdiv = 1;
    app.sc.fftDb = 10;
    app.sc.refVdiv = 1;
    app.sc.curA = 1;
    app.sc.curB = -1;
    app.sc.measV = 0;
    app.sc.measT = 0;
    app.sc.beep = true;
    app.sc.holdoff = 100e-9;
    app.sc.status = 0;
}

static GenCh &Sel() { return app.gen.ch[app.gen.sel]; }

static double EditGet()
{
    GenCh &c = Sel();
    switch (app.gen.edit) {
    case PAR_FREQ: return c.freq;
    case PAR_PER: return 1.0 / c.freq;
    case PAR_AMPL: return c.ampl;
    case PAR_HI: return c.offset + c.ampl * 0.5;
    case PAR_OFF: return c.offset;
    case PAR_LO: return c.offset - c.ampl * 0.5;
    case PAR_PHASE: return c.phase;
    case PAR_DUTY: return c.duty;
    case PAR_SYMM: return c.symmetry;
    case PAR_MODF: return app.gen.modFreq;
    case PAR_MODD:
        if (app.gen.modType == 0) return app.gen.amDepth;
        if (app.gen.modType == 1) return app.gen.fmDev;
        if (app.gen.modType == 2) return app.gen.pmDev;
        return app.gen.fskHop;
    case PAR_SW1: return app.gen.swStart;
    case PAR_SW2: return app.gen.swStop;
    case PAR_SWT: return app.gen.swTime;
    case PAR_BN: return app.gen.bCycles;
    case PAR_BP: return app.gen.bPeriod;
    case PAR_BPH: return app.gen.bPhase;
    }
    return 0;
}

static void EditSet(double v)
{
    GenCh &c = Sel();
    switch (app.gen.edit) {
    case PAR_FREQ: c.freq = v; break;
    case PAR_PER: if (v > 0) c.freq = 1.0 / v; break;
    case PAR_AMPL: c.ampl = v; break;
    case PAR_HI: {
        double l = c.offset - c.ampl * 0.5;
        if (v < l) v = l;
        c.ampl = v - l; c.offset = (v + l) * 0.5;
        break;
    }
    case PAR_OFF: c.offset = v; break;
    case PAR_LO: {
        double h = c.offset + c.ampl * 0.5;
        if (v > h) v = h;
        c.ampl = h - v; c.offset = (h + v) * 0.5;
        break;
    }
    case PAR_PHASE: c.phase = v; break;
    case PAR_DUTY: c.duty = v; break;
    case PAR_SYMM: c.symmetry = v; break;
    case PAR_MODF: app.gen.modFreq = Clamp(v, 2e-3, app.gen.modType == 3 ? 50e3 : 20e3); return;
    case PAR_MODD:
        if (app.gen.modType == 0) app.gen.amDepth = Clamp(v, 0, 120);
        else if (app.gen.modType == 1) app.gen.fmDev = Clamp(v, 0, 5e6);
        else if (app.gen.modType == 2) app.gen.pmDev = Clamp(v, 0, 360);
        else app.gen.fskHop = v;
        if (app.gen.modType == 3) {
            app.gen.fskHop = Clamp(app.gen.fskHop, FMin(c), FMax(c));
        }
        return;
    case PAR_SW1: app.gen.swStart = Clamp(v, FMin(app.gen.ch[0]), FMax(app.gen.ch[0])); return;
    case PAR_SW2: app.gen.swStop = Clamp(v, FMin(app.gen.ch[0]), FMax(app.gen.ch[0])); return;
    case PAR_SWT: app.gen.swTime = Clamp(v, 1e-3, 500); return;
    case PAR_BN: app.gen.bCycles = Clamp(v, 1, 1000000); return;
    case PAR_BP: app.gen.bPeriod = Clamp(v, 1e-6, 500); return;
    case PAR_BPH: app.gen.bPhase = Frac(v / 360.0) * 360.0; return;
    }
    ClampOut(c);
    if (app.gen.mode == 3)
        app.gen.bPeriod = std::max(app.gen.bPeriod, app.gen.bCycles / app.gen.ch[0].freq);
}

static void SelectWave(int w)
{
    GenCh &c = Sel();
    c.wave = (Wf)w;
    c.dc = false;
    app.gen.page = GP_WAVE;
    app.gen.entry = false;
    app.gen.edit = (w == W_NOISE) ? PAR_AMPL : PAR_FREQ;
    ClampOut(c);
    NoteG(w == W_NOISE ? "Noise  5 MHz" : "Waveform");
}

static bool Ch1Only(const char *what)
{
    if (app.gen.sel != 0) { NoteG("CH1 only"); return false; }
    if (Sel().wave == W_NOISE || Sel().dc) { NoteG("Not for this wave"); return false; }
    (void)what;
    return true;
}

static void CycleMeas(bool timeList, int dir)
{
    if (timeList) {
        app.sc.measT += dir;
        if (app.sc.measT > 8) app.sc.measT = 0;
        if (app.sc.measT < 0) app.sc.measT = 8;
    } else {
        app.sc.measV += dir;
        if (app.sc.measV > 9) app.sc.measV = 0;
        if (app.sc.measV < 0) app.sc.measV = 9;
    }
}

static void SaveGen()
{
    int s = app.gen.slot;
    app.gen.mem[s][0] = app.gen.ch[0];
    app.gen.mem[s][1] = app.gen.ch[1];
    app.gen.savedHighZ[s] = app.gen.highZ;
    app.gen.memOk[s] = true;
    char b[64];
    std::snprintf(b, sizeof(b), "Setup %d saved", s + 1);
    NoteG(b);
}
static void RecallGen()
{
    int s = app.gen.slot;
    if (!app.gen.memOk[s]) { NoteG("Empty slot"); return; }
    app.gen.ch[0] = app.gen.mem[s][0];
    app.gen.ch[1] = app.gen.mem[s][1];
    app.gen.highZ = app.gen.savedHighZ[s];
    NoteG("Setup recalled");
}

static void SaveScope()
{
    Scope::Snap &m = app.sc.mem[app.sc.slot];
    m.ok = true;
    m.ch[0] = app.sc.ch[0];
    m.ch[1] = app.sc.ch[1];
    m.tdiv = app.sc.tdiv; m.hpos = app.sc.hpos; m.trigLevel = app.sc.trigLevel;
    m.trigSrc = app.sc.trigSrc; m.trigSlope = app.sc.trigSlope; m.trigMode = app.sc.trigMode;
    m.trigCoup = app.sc.trigCoup; m.acq = app.sc.acq; m.averages = app.sc.averages;
    m.drawMode = app.sc.drawMode; m.grid = app.sc.grid;
    m.mathOn = app.sc.mathOn; m.mathInv = app.sc.mathInv; m.mathOp = app.sc.mathOp;
    m.mathVdiv = app.sc.mathVdiv; m.mathPos = app.sc.mathPos; m.fftDb = app.sc.fftDb;
    m.delayOn = app.sc.delayOn;
    char b[64];
    std::snprintf(b, sizeof(b), "Setup %d saved", app.sc.slot + 1);
    NoteS(b);
}
static void RecallScope()
{
    Scope::Snap &m = app.sc.mem[app.sc.slot];
    if (!m.ok) { NoteS("Empty slot"); return; }
    app.sc.ch[0] = m.ch[0]; app.sc.ch[1] = m.ch[1];
    app.sc.tdiv = m.tdiv; app.sc.hpos = m.hpos; app.sc.trigLevel = m.trigLevel;
    app.sc.trigSrc = m.trigSrc; app.sc.trigSlope = m.trigSlope; app.sc.trigMode = m.trigMode;
    app.sc.trigCoup = m.trigCoup; app.sc.acq = m.acq; app.sc.averages = m.averages;
    app.sc.drawMode = m.drawMode; app.sc.grid = m.grid;
    app.sc.mathOn = m.mathOn; app.sc.mathInv = m.mathInv; app.sc.mathOp = m.mathOp;
    app.sc.mathVdiv = m.mathVdiv; app.sc.mathPos = m.mathPos; app.sc.fftDb = m.fftDb;
    app.sc.delayOn = m.delayOn;
    NoteS("Setup recalled");
}

static void SaveRef()
{
    int src = app.sc.page == SP_REF ? app.sc.refSrc : app.sc.active;
    if (src > 1) src = 0;
    app.sc.refSrc = src;
    for (int i = 0; i < COLS; i++) app.sc.refV[i] = app.sc.col[src][i].mid;
    app.sc.refVdiv = app.sc.ch[src].vdiv;
    app.sc.refPos = app.sc.ch[src].pos;
    app.sc.refOn = true;
    app.sc.active = 3;
    app.sc.page = SP_REF;
    NoteS("REF saved");
}

static void AutoSet()
{
    int src = -1;
    for (int ch = 0; ch < 2; ch++)
        if (app.gen.ch[ch].output && !(ch == 1 && app.gen.counter)) { src = ch; break; }
    if (src < 0) { NoteS("No signal"); return; }
    for (int ch = 0; ch < 2; ch++) {
        bool live = app.gen.ch[ch].output && !(ch == 1 && app.gen.counter);
        app.sc.ch[ch].on = live;
        if (!live) continue;
        double mn = 1e9, mx = -1e9;
        double f = std::max(app.gen.ch[ch].freq, 10.0);
        for (int i = 0; i < 300; i++) {
            double v = ScopeVolts(ch, app.gen.ch[ch].epoch + i / (f * 300.0));
            if (v < mn) mn = v;
            if (v > mx) mx = v;
        }
        double vpp = std::max(1e-4, mx - mn);
        double vd = Ceil125(vpp / 6.0);
        vd = Clamp(vd, 2e-3, 10.0);
        app.sc.ch[ch].vdiv = vd;
        app.sc.ch[ch].pos = Clamp(-0.5 * (mx + mn) / vd, -6.0, 6.0);
        app.sc.ch[ch].coupling = 0;
    }
    double f = app.gen.ch[src].freq;
    if (app.gen.ch[src].wave != W_NOISE && f > 0)
        app.sc.tdiv = Clamp(Ceil125(0.25 / f), 5e-9, 50.0);
    app.sc.trigSrc = src;
    app.sc.trigLevel = 0.5 * (ScopeVolts(src, 0) + ScopeVolts(src, 0.5 / std::max(f, 1.0)));
    // 50 percent from a short scan
    {
        double mn = 1e9, mx = -1e9, ff = std::max(f, 10.0);
        for (int i = 0; i < 200; i++) {
            double v = ScopeVolts(src, i / (ff * 200.0));
            if (v < mn) mn = v; if (v > mx) mx = v;
        }
        app.sc.trigLevel = 0.5 * (mn + mx);
    }
    app.sc.trigMode = 0;
    app.sc.running = true;
    app.sc.hpos = 0;
    app.sc.active = src;
    NoteS("Auto");
}

static void SetLevel50()
{
    int src = app.sc.trigSrc;
    if (src > 1) { NoteS("EXT has no signal"); return; }
    double mn = 1e9, mx = -1e9;
    double f = std::max(app.gen.ch[src].freq, 10.0);
    for (int i = 0; i < 240; i++) {
        double v = ScopeVolts(src, i / (f * 240.0));
        if (v < mn) mn = v; if (v > mx) mx = v;
    }
    app.sc.trigLevel = 0.5 * (mn + mx);
    NoteS("Trigger 50%");
}

static bool TrigCouplePass(int src, double vRaw, double mean)
{
    double f = app.gen.ch[src].freq;
    if (app.sc.trigCoup == 2 && f > 80e3) return false;      // HF reject
    if (app.sc.trigCoup == 3 && f < 50e3) return false;      // LF reject
    (void)vRaw; (void)mean;
    return true;
}

static double TrigSample(int src, double t)
{
    if (src > 1) return 0;
    double v = ScopeVolts(src, t);
    if (app.sc.trigCoup == 1) {
        // AC trigger: remove the same mean the channel would
        // ScopeVolts already applied channel coupling. Add a dedicated AC by
        // subtracting mean of displayed signal over one period.
    }
    return v;
}

struct Meas {
    bool ok;
    double vmax, vmin, vpp, vavg, vrms, vamp, vtop, vbase, over, pre;
    double freq, per, rise, fall, pwid, nwid, pduty, nduty;
};

static double SampleSrc(int src, double t)
{
    double a = ScopeVolts(0, t);
    double b = ScopeVolts(1, t);
    if (src == 0) return a;
    if (src == 1) return b;
    double m = app.sc.mathOp == 0 ? a + b : app.sc.mathOp == 1 ? a - b : a * b;
    if (app.sc.mathInv) m = -m;
    return m;
}

// 10/90 times sit inside one coarse sample when the edge is 20 ns, so bisect.
static double RisingCross(int src, double tA, double tB, double level, double maxSpan)
{
    double lo = tA, hi = tB;
    double vA = SampleSrc(src, tA);
    double vB = SampleSrc(src, tB);
    if (vA > level) {
        double prev = tA, step = std::max(1e-9, (tB - tA) * 0.25), walked = 0;
        bool found = false;
        while (walked < maxSpan) {
            walked = std::min(maxSpan, walked + step);
            double t = tA - walked;
            if (SampleSrc(src, t) <= level) { lo = t; hi = prev; found = true; break; }
            prev = t;
            step *= 1.7;
        }
        if (!found) return tA;
    } else if (vB < level) {
        double prev = tB, step = std::max(1e-9, (tB - tA) * 0.25), walked = 0;
        bool found = false;
        while (walked < maxSpan) {
            walked = std::min(maxSpan, walked + step);
            double t = tB + walked;
            if (SampleSrc(src, t) >= level) { lo = prev; hi = t; found = true; break; }
            prev = t;
            step *= 1.7;
        }
        if (!found) return tB;
    }
    for (int n = 0; n < 48; n++) {
        double m = 0.5 * (lo + hi);
        if (SampleSrc(src, m) < level) lo = m; else hi = m;
    }
    return 0.5 * (lo + hi);
}

static double FallingCross(int src, double tA, double tB, double level, double maxSpan)
{
    double lo = tA, hi = tB;
    double vA = SampleSrc(src, tA);
    double vB = SampleSrc(src, tB);
    if (vA < level) {
        double prev = tA, step = std::max(1e-9, (tB - tA) * 0.25), walked = 0;
        bool found = false;
        while (walked < maxSpan) {
            walked = std::min(maxSpan, walked + step);
            double t = tA - walked;
            if (SampleSrc(src, t) >= level) { lo = t; hi = prev; found = true; break; }
            prev = t;
            step *= 1.7;
        }
        if (!found) return tA;
    } else if (vB > level) {
        double prev = tB, step = std::max(1e-9, (tB - tA) * 0.25), walked = 0;
        bool found = false;
        while (walked < maxSpan) {
            walked = std::min(maxSpan, walked + step);
            double t = tB + walked;
            if (SampleSrc(src, t) <= level) { lo = prev; hi = t; found = true; break; }
            prev = t;
            step *= 1.7;
        }
        if (!found) return tB;
    }
    for (int n = 0; n < 48; n++) {
        double m = 0.5 * (lo + hi);
        if (SampleSrc(src, m) > level) lo = m; else hi = m;
    }
    return 0.5 * (lo + hi);
}

static Meas Measure(int src)
{
    Touch();
    Meas m{};
    int gch = src < 2 ? src : 0;
    double f = std::max(app.gen.ch[gch].freq, 1.0);
    if (src == 2) f = std::max(app.gen.ch[app.sc.mathA].freq, 1.0);
    const int N = 800;
    double T = 2.0 / f;
    double dt = T / N;
    double sum = 0, sum2 = 0, vmin = 1e300, vmax = -1e300;
    static double buf[800];
    for (int i = 0; i < N; i++) {
        double v = SampleSrc(src, i * dt);
        buf[i] = v;
        sum += v; sum2 += v * v;
        if (v < vmin) vmin = v;
        if (v > vmax) vmax = v;
    }
    m.vavg = sum / N;
    m.vrms = std::sqrt(sum2 / N);
    m.vmin = vmin; m.vmax = vmax; m.vpp = vmax - vmin;
    int bins[256] = {};
    double span = std::max(1e-12, vmax - vmin);
    for (int i = 0; i < N; i++) {
        int b = (int)((buf[i] - vmin) / span * 255.999);
        if (b < 0) b = 0;
        if (b > 255) b = 255;
        bins[b]++;
    }
    int loB = 0, hiB = 128, loC = -1, hiC = -1;
    for (int b = 0; b < 128; b++) if (bins[b] > loC) { loC = bins[b]; loB = b; }
    for (int b = 128; b < 256; b++) if (bins[b] > hiC) { hiC = bins[b]; hiB = b; }
    m.vbase = vmin + (loB + 0.5) / 256.0 * span;
    m.vtop = vmin + (hiB + 0.5) / 256.0 * span;
    if (m.vtop < m.vbase) { double s = m.vtop; m.vtop = m.vbase; m.vbase = s; }
    m.vamp = m.vtop - m.vbase;
    if (m.vamp < 1e-12) m.vamp = m.vpp;
    m.over = (m.vmax - m.vtop) / m.vamp * 100.0;
    m.pre = (m.vbase - m.vmin) / m.vamp * 100.0;

    double mid = 0.5 * (m.vtop + m.vbase);
    int prevRise = -1, rises = 0, riseAt = -1, fallAt = -1;
    double first = 0, last = 0, psum = 0; int pcount = 0;
    for (int i = 1; i < N; i++) {
        if (buf[i - 1] < mid && buf[i] >= mid) {
            double frac = (mid - buf[i - 1]) / std::max(1e-15, buf[i] - buf[i - 1]);
            double tc = (i - 1 + frac) * dt;
            if (prevRise >= 0) {
                double width = 0;
                for (int k = prevRise; k < i; k++) if (buf[k] >= mid) width += dt;
                psum += width; pcount++;
            }
            if (rises == 0) { first = tc; riseAt = i; }
            last = tc; rises++;
            prevRise = i;
        } else if (fallAt < 0 && buf[i - 1] > mid && buf[i] <= mid) fallAt = i;
    }
    if (rises >= 2 && last > first) {
        m.freq = (rises - 1) / (last - first);
        m.per = 1.0 / m.freq;
        m.ok = true;
        if (pcount > 0) {
            m.pwid = psum / pcount;
            m.nwid = m.per - m.pwid;
            m.pduty = m.pwid * m.freq * 100.0;
            m.nduty = 100.0 - m.pduty;
        }
        double l10 = m.vbase + 0.1 * m.vamp;
        double l90 = m.vbase + 0.9 * m.vamp;
        double span = 0.5 / f;
        if (riseAt > 0) {
            double tA = (riseAt - 1) * dt, tB = riseAt * dt;
            double t10 = RisingCross(src, tA, tB, l10, span);
            double t90 = RisingCross(src, tA, tB, l90, span);
            if (t90 >= t10) m.rise = t90 - t10;
        }
        if (fallAt > 0) {
            double tA = (fallAt - 1) * dt, tB = fallAt * dt;
            double t90 = FallingCross(src, tA, tB, l90, span);
            double t10 = FallingCross(src, tA, tB, l10, span);
            if (t10 >= t90) m.fall = t10 - t90;
        }
    } else {
        m.freq = 0; m.per = 0; m.ok = std::fabs(m.vpp) > 1e-6;
    }
    if (src < 2 && !app.gen.ch[src].output) m.ok = false;
    return m;
}

static const char *MEAS_V[] = { "Vpp", "Vmax", "Vmin", "Vavg", "Vrms", "Vamp", "Vtop", "Vbase", "Over", "Pre" };
static const char *MEAS_T[] = { "Freq", "Period", "Rise", "Fall", "+Wid", "-Wid", "+Duty", "-Duty", "DlyR", "DlyF" };

const char *MeasNameV(int i) { return MEAS_V[(i < 0 ? 0 : i) % 10]; }
const char *MeasNameT(int i) { return MEAS_T[(i < 0 ? 0 : i) % 10]; }

bool FormatMeasV(int src, int which, char *b, int n)
{
    Meas m = Measure(src);
    if (!m.ok && which != 3) { std::snprintf(b, n, "*****"); return false; }
    switch (which) {
    case 0: FmtVolt(b, n, m.vpp); break;
    case 1: FmtVolt(b, n, m.vmax); break;
    case 2: FmtVolt(b, n, m.vmin); break;
    case 3: FmtVolt(b, n, m.vavg); break;
    case 4: FmtVolt(b, n, m.vrms); break;
    case 5: FmtVolt(b, n, m.vamp); break;
    case 6: FmtVolt(b, n, m.vtop); break;
    case 7: FmtVolt(b, n, m.vbase); break;
    case 8: std::snprintf(b, n, "%.1f %%", m.over); break;
    default: std::snprintf(b, n, "%.1f %%", m.pre); break;
    }
    return true;
}
bool FormatMeasT(int src, int which, char *b, int n)
{
    if (which == 8 || which == 9) {
        if (!app.gen.ch[0].output || !app.gen.ch[1].output) { std::snprintf(b, n, "*****"); return false; }
        double f0 = app.gen.ch[0].freq, f1 = app.gen.ch[1].freq;
        if (std::fabs(f0 - f1) / f0 > 0.01) { std::snprintf(b, n, "*****"); return false; }
        double d = (app.gen.ch[1].phase - app.gen.ch[0].phase) / 360.0 / f0;
        if (which == 9) d = -d;
        FmtTime(b, n, d);
        return true;
    }
    Meas m = Measure(src);
    if (!m.ok || m.freq <= 0) { std::snprintf(b, n, "*****"); return false; }
    switch (which) {
    case 0: FmtFreq(b, n, m.freq); break;
    case 1: FmtTime(b, n, m.per); break;
    case 2: FmtTime(b, n, m.rise); break;
    case 3: FmtTime(b, n, m.fall); break;
    case 4: FmtTime(b, n, m.pwid); break;
    case 5: FmtTime(b, n, m.nwid); break;
    case 6: std::snprintf(b, n, "%.2f %%", m.pduty); break;
    default: std::snprintf(b, n, "%.2f %%", m.nduty); break;
    }
    return true;
}

void FormatAllMeas(int src, char lines[][24])
{
    Meas m = Measure(src);
    auto putv = [&](int i, double v, bool volt) {
        if (!m.ok && i != 3) std::snprintf(lines[i], 24, "*****");
        else if (volt) FmtVolt(lines[i], 24, v);
        else std::snprintf(lines[i], 24, "%.1f %%", v);
    };
    putv(0, m.vpp, true); putv(1, m.vmax, true); putv(2, m.vmin, true); putv(3, m.vavg, true);
    putv(4, m.vrms, true); putv(5, m.vamp, true); putv(6, m.vtop, true); putv(7, m.vbase, true);
    putv(8, m.over, false); putv(9, m.pre, false);
    for (int i = 10; i < 20; i++) std::snprintf(lines[i], 24, "*****");
    if (!m.ok || m.freq <= 0) return;
    FmtFreq(lines[10], 24, m.freq);
    FmtTime(lines[11], 24, m.per);
    FmtTime(lines[12], 24, m.rise);
    FmtTime(lines[13], 24, m.fall);
    FmtTime(lines[14], 24, m.pwid);
    FmtTime(lines[15], 24, m.nwid);
    std::snprintf(lines[16], 24, "%.2f %%", m.pduty);
    std::snprintf(lines[17], 24, "%.2f %%", m.nduty);
    char d[24];
    if (FormatMeasT(src, 8, d, 24)) std::snprintf(lines[18], 24, "%s", d);
    if (FormatMeasT(src, 9, d, 24)) std::snprintf(lines[19], 24, "%s", d);
}
// defining them here and adding to the header after compile if needed.
// The draw code calls FormatMeasV/T and MeasName. Add them to the header.

static void Acquire(double now)
{
    Touch();
    if (app.gen.alignReq) {
        app.gen.ch[0].epoch = now;
        app.gen.ch[1].epoch = now;
        app.gen.alignReq = false;
        NoteG("Phase aligned");
    }
    Scope &s = app.sc;
    double tdiv = s.delayOn ? s.delayDiv : s.tdiv;
    int src = s.trigSrc;
    bool got = false;
    double tTrig = now;
    if (src <= 1 && TrigCouplePass(src, 0, 0)) {
        double f = std::max(app.gen.ch[src].freq, 1.0);
        double span = Clamp(4.0 / f, 1e-6, 1.0);
        const int N = 1200;
        double dt = span / N;
        double prev = TrigSample(src, now - span);
        double last = now;
        double prevT = now - span;
        double vmin = prev, vmax = prev;
        for (int i = 1; i <= N; i++) {
            double t = now - span + i * dt;
            double v = TrigSample(src, t);
            if (v < vmin) vmin = v;
            if (v > vmax) vmax = v;
            bool rise = prev < s.trigLevel && v >= s.trigLevel;
            bool fall = prev > s.trigLevel && v <= s.trigLevel;
            bool cross = s.trigSlope == 0 ? rise : s.trigSlope == 1 ? fall : (rise || fall);
            if (cross) {
                double frac = (s.trigLevel - prev) / (v - prev + 1e-15);
                last = prevT + frac * (t - prevT);
                got = true;
            }
            prev = v; prevT = t;
        }
        if (got && (vmax - vmin) < s.trigSens * s.ch[src].vdiv) got = false;
        if (got && s.trigType == 1 && app.gen.ch[src].output) {
            double per = 1.0 / std::max(app.gen.ch[src].freq, 1.0);
            double pw = per * app.gen.ch[src].duty / 100.0;
            double w = s.pulseWhen < 3 ? pw : per - pw;
            int k = s.pulseWhen % 3;
            bool ok = k == 0 ? w < s.pulseWidth : k == 1 ? w > s.pulseWidth
                      : std::fabs(w - s.pulseWidth) <= 0.2 * s.pulseWidth + 1e-12;
            if (!ok) got = false;
        }
        if (got && s.trigType == 2) got = false;
        if (got) {
            if (last - s.winT0 < s.holdoff && s.have) got = false;
            else tTrig = last;
        }
    }
    if (s.timeBase == 1 || s.timeBase == 2) {
        got = true;
        tTrig = s.timeBase == 2 ? now - (5.0 - s.hpos) * tdiv : now;
    }
    if (s.force) { got = true; tTrig = now; s.force = false; }
    if (!s.running && s.have && !s.force) { s.status = 2; return; }
    if (!got) {
        if (s.trigMode == 1 || s.trigMode == 2) {
            s.status = 3;
            if (!s.have) {
                for (int c = 0; c < 2; c++)
                    for (int i = 0; i < COLS; i++) s.col[c][i] = {};
            }
            return;
        }
        tTrig = now;
        s.status = 0;
    } else {
        s.status = (s.trigMode == 0) ? 0 : 4;
        if (s.trigMode == 2) { s.running = false; s.status = 2; }
    }

    if (s.persist && s.have) {
        int g = s.ghostN % 6;
        for (int c = 0; c < 2; c++)
            for (int i = 0; i < COLS; i++) s.ghost[g][c][i] = s.col[c][i].mid;
        s.ghostN++;
    }

    double leftDiv = -5.0;
    for (int i = 0; i < COLS; i++) {
        double div0 = leftDiv + 10.0 * i / COLS;
        double div1 = leftDiv + 10.0 * (i + 1) / COLS;
        double tA = tTrig + (div0 - s.hpos) * tdiv;
        double tB = tTrig + (div1 - s.hpos) * tdiv;
        for (int c = 0; c < 2; c++) {
            if (s.acq == 2) {
                float lo = 1e30f, hi = -1e30f;
                const int K = 8;
                for (int k = 0; k < K; k++) {
                    float v = (float)ScopeVolts(c, tA + (tB - tA) * k / (K - 1.0));
                    if (v < lo) lo = v; if (v > hi) hi = v;
                }
                if (s.acq == 1) { /* unused */ }
                float mid = 0.5f * (lo + hi);
                if (s.acq == 1) {
                    float a = 1.0f / (float)s.averages;
                    mid = s.col[c][i].mid + a * (mid - s.col[c][i].mid);
                }
                s.col[c][i].lo = lo; s.col[c][i].hi = hi; s.col[c][i].mid = mid;
            } else {
                float v = (float)ScopeVolts(c, 0.5 * (tA + tB));
                if (s.acq == 1) {
                    float a = 1.0f / (float)s.averages;
                    v = s.col[c][i].mid + a * (v - s.col[c][i].mid);
                }
                s.col[c][i].lo = s.col[c][i].hi = s.col[c][i].mid = v;
            }
        }
        float a = s.col[s.mathA][i].mid;
        float b = s.col[s.mathB][i].mid;
        float m = s.mathOp == 0 ? a + b : s.mathOp == 1 ? a - b : a * b;
        if (s.mathInv) m = -m;
        s.math[i].lo = s.math[i].hi = s.math[i].mid = m;
    }
    s.have = true;
    s.winT0 = tTrig;
    s.winDt = tdiv;

    if (s.mathOn && s.mathOp == 3) {
        const int N = 256;
        double re[256], im[256];
        for (int i = 0; i < N; i++) {
            double div = -5.0 + 10.0 * i / N;
            double t = tTrig + (div - s.hpos) * tdiv;
            double nrm = 2 * PI * i / (N - 1);
            double w = 1;
            if (s.fftWin == 1) w = 0.5 * (1 - std::cos(nrm));
            else if (s.fftWin == 2) w = 0.54 - 0.46 * std::cos(nrm);
            else if (s.fftWin == 3) w = 0.42 - 0.5 * std::cos(nrm) + 0.08 * std::cos(2 * nrm);
            re[i] = SampleSrc(s.mathA, t) * w;
            im[i] = 0;
        }
        for (int i = 1, j = 0; i < N; i++) {
            int bit = N >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) { double tr = re[i]; re[i] = re[j]; re[j] = tr; tr = im[i]; im[i] = im[j]; im[j] = tr; }
        }
        for (int len = 2; len <= N; len <<= 1) {
            double ang = -2 * PI / len;
            double wr0 = std::cos(ang), wi0 = std::sin(ang);
            for (int i = 0; i < N; i += len) {
                double vr = 1, vi = 0;
                for (int k = 0; k < len / 2; k++) {
                    int j = i + k + len / 2;
                    double ur = re[i + k], ui = im[i + k];
                    double tr = vr * re[j] - vi * im[j];
                    double ti = vr * im[j] + vi * re[j];
                    re[i + k] = ur + tr; im[i + k] = ui + ti;
                    re[j] = ur - tr; im[j] = ui - ti;
                    double nvr = vr * wr0 - vi * wi0;
                    vi = vr * wi0 + vi * wr0; vr = nvr;
                }
            }
        }
        for (int i = 0; i < 128; i++) {
            double mag = std::sqrt(re[i] * re[i] + im[i] * im[i]) * 2.0 / N;
            s.fftMag[i] = s.fftVrms ? (float)mag : (float)(20.0 * std::log10(mag + 1e-12));
        }
        s.fftDf = 1.0 / (10.0 * tdiv);
        s.fftOk = true;
    }
}

void AppFrame()
{
    if (app.gmsgT > 0) app.gmsgT -= 1.0 / 60.0;
    if (app.smsgT > 0) app.smsgT -= 1.0 / 60.0;
    static double t0 = -1;
    // Time base is supplied by the caller through a function pointer? Use a
    // static clock incremented so self-test does not need raylib, while the
    // UI overwrites it.
    (void)t0;
}

void AppFrameAt(double now) { AppFrame(); Acquire(now); }

static void BeginEntry(char digit)
{
    if (!app.gen.entry) { app.gen.entry = true; app.gen.elen = 0; app.gen.ebuf[0] = 0; }
    if (app.gen.elen > 16) return;
    if (digit == '.' ) {
        for (int i = 0; i < app.gen.elen; i++) if (app.gen.ebuf[i] == '.') return;
    }
    app.gen.ebuf[app.gen.elen++] = digit;
    app.gen.ebuf[app.gen.elen] = 0;
}

static void ApplyEntry(double mult, int kind)
{
    if (!app.gen.entry || app.gen.elen == 0) { app.gen.entry = false; return; }
    double v = std::atof(app.gen.ebuf) * mult;
    if (kind == 1) { // Vrms -> Vpp
        GenCh &c = Sel();
        if (c.wave == W_SINE) v *= 2 * std::sqrt(2.0);
        else if (c.wave == W_RAMP) v *= 2 * std::sqrt(3.0);
        else v *= 2;
        app.gen.edit = app.gen.amplIsHi ? PAR_HI : PAR_AMPL;
    } else if (kind == 2) { // dBm into 50 ohm -> Vrms -> Vpp
        double vrms = std::sqrt(std::pow(10.0, v / 10.0) * 0.001 * 50.0);
        GenCh &c = Sel();
        v = vrms * ((c.wave == W_SINE) ? 2 * std::sqrt(2.0) : (c.wave == W_RAMP ? 2 * std::sqrt(3.0) : 2));
        app.gen.edit = PAR_AMPL;
    }
    EditSet(v);
    app.gen.entry = false;
    NoteG("Value set");
}

static void PressGenSoft(int i)
{
    Gen &g = app.gen;
    if (g.entry) {
        struct U { const char *n; double m; int k; };
        U u[6] = {};
        int nu = 0;
        auto add = [&](const char *n, double m, int k) { if (nu < 6) u[nu++] = { n, m, k }; };
        switch (g.edit) {
        case PAR_FREQ: case PAR_MODF: case PAR_SW1: case PAR_SW2: case PAR_MODD:
            if (g.edit == PAR_MODD && g.modType != 1 && g.modType != 3) break;
            add("uHz", 1e-6, 0); add("mHz", 1e-3, 0); add("Hz", 1, 0);
            add("kHz", 1e3, 0); add("MHz", 1e6, 0); add("Cancel", 0, -1); break;
        case PAR_PER: case PAR_SWT: case PAR_BP:
            add("ns", 1e-9, 0); add("us", 1e-6, 0); add("ms", 1e-3, 0); add("s", 1, 0); add("Cancel", 0, -1); break;
        case PAR_AMPL: case PAR_HI: case PAR_LO:
            add("mVpp", 1e-3, 0); add("Vpp", 1, 0); add("mVrms", 1e-3, 1); add("Vrms", 1, 1); add("dBm", 1, 2); add("Cancel", 0, -1); break;
        case PAR_OFF:
            add("mVdc", 1e-3, 0); add("Vdc", 1, 0); add("Cancel", 0, -1); break;
        case PAR_PHASE: case PAR_BPH:
            add("deg", 1, 0); add("Cancel", 0, -1); break;
        default:
            add("%", 1, 0); add("Cancel", 0, -1); break;
        }
        if (g.edit == PAR_PHASE || g.edit == PAR_BPH) { u[1].n = nullptr; }
        if (g.edit == PAR_MODD && g.modType == 2) { nu = 0; add("deg", 1, 0); add("Cancel", 0, -1); }
        if (g.edit == PAR_DUTY || g.edit == PAR_SYMM || g.edit == PAR_BN || (g.edit == PAR_MODD && g.modType == 0)) {
            nu = 0; add("%", 1, 0); add("Cancel", 0, -1);
            if (g.edit == PAR_BN) { nu = 0; add("x1", 1, 0); add("Cancel", 0, -1); }
        }
        if (i >= nu || !u[i].n) return;
        if (u[i].k < 0 || std::strcmp(u[i].n, "Cancel") == 0) { g.entry = false; return; }
        ApplyEntry(u[i].m, u[i].k);
        return;
    }
    if (g.page == GP_WAVE) {
        GenCh &c = Sel();
        if (c.wave == W_NOISE) {
            if (i == 0) g.edit = PAR_AMPL;
            else if (i == 1) g.edit = PAR_OFF;
            else NoteG("Noise BW 5 MHz");
            return;
        }
        if (i == 0) { g.editPeriod = !g.editPeriod; g.edit = g.editPeriod ? PAR_PER : PAR_FREQ; }
        else if (i == 1) { g.amplIsHi = !g.amplIsHi; g.edit = g.amplIsHi ? PAR_HI : PAR_AMPL; }
        else if (i == 2) { g.offIsLo = !g.offIsLo; g.edit = g.offIsLo ? PAR_LO : PAR_OFF; }
        else if (i == 3) g.edit = PAR_PHASE;
        else if (i == 4) {
            if (c.wave == W_SQUARE || c.wave == W_PULSE) g.edit = PAR_DUTY;
            else if (c.wave == W_RAMP) g.edit = PAR_SYMM;
            else if (c.wave == W_ARB) { c.arb = (c.arb + 1) % ARB_N; NoteG(ArbName(c.arb)); }
            else g.alignReq = true;
        } else if (c.wave != W_ARB) g.alignReq = true;
        return;
    }
    if (g.page == GP_MOD) {
        if (i == 0) g.modType = (g.modType + 1) % 4;
        else if (i == 1) { g.mode = (g.mode == 1) ? 0 : 1; NoteG(g.mode == 1 ? "Mod ON" : "Mod OFF"); }
        else if (i == 2) g.modShape = (g.modShape + 1) % 6;
        else if (i == 3) g.edit = PAR_MODF;
        else if (i == 4) g.edit = PAR_MODD;
        else { g.modSource = 1 - g.modSource; NoteG(g.modSource ? "Ext mod: no signal" : "Int mod"); }
        return;
    }
    if (g.page == GP_SWEEP) {
        if (i == 0) { g.mode = (g.mode == 2) ? 0 : 2; NoteG(g.mode == 2 ? "Sweep ON" : "Sweep OFF"); }
        else if (i == 1) g.edit = PAR_SW1;
        else if (i == 2) g.edit = PAR_SW2;
        else if (i == 3) g.edit = PAR_SWT;
        else if (i == 4) g.swLog = !g.swLog;
        else g.page = GP_WAVE;
        return;
    }
    if (g.page == GP_BURST) {
        if (i == 0) { g.mode = (g.mode == 3) ? 0 : 3; NoteG(g.mode == 3 ? "Burst ON" : "Burst OFF"); }
        else if (i == 1) g.edit = PAR_BN;
        else if (i == 2) g.edit = PAR_BP;
        else if (i == 3) g.edit = PAR_BPH;
        else if (i == 4) NoteG("N-cycle, internal trig");
        else g.page = GP_WAVE;
        return;
    }
    if (g.page == GP_STORE) {
        if (i == 0) g.slot = (g.slot + 1) % 10;
        else if (i == 1) SaveGen();
        else if (i == 2) RecallGen();
        else if (i == 5) g.page = GP_WAVE;
        return;
    }
    if (g.page == GP_UTIL) {
        if (i == 0) { g.sync = !g.sync; NoteG(g.sync ? "Sync ON" : "Sync OFF"); }
        else if (i == 1) {
            g.highZ = !g.highZ;
            ClampOut(g.ch[0]); ClampOut(g.ch[1]);
            NoteG(g.highZ ? "Load High Z" : "Load 50 ohm");
        } else if (i == 2) { Sel().dc = !Sel().dc; NoteG(Sel().dc ? "DC output" : "Wave output"); }
        else if (i == 3) { g.beep = !g.beep; NoteG(g.beep ? "Beep ON" : "Beep OFF"); }
        else if (i == 4) {
            g.counter = !g.counter;
            if (g.counter) g.ch[1].output = false;
            NoteG(g.counter ? "Counter uses CH2" : "Counter OFF");
        } else g.page = GP_WAVE;
        return;
    }
    if (g.page == GP_HELP) { g.page = GP_WAVE; return; }
}

static void TouchMenu()
{
    static const double hold[] = { 1, 2, 5, 10, 20, 1e9 };
    int i = app.sc.menuTime;
    if (i < 0 || i > 5) i = 5;
    app.sc.menuOn = true;
    app.sc.menuIdle = hold[i];
}

static void OpenScope(SPage p)
{
    app.sc.page = p;
    app.sc.sub = 0;
    app.sc.focus = 0;
    TouchMenu();
}

static void FactoryScope()
{
    for (int i = 0; i < 2; i++) {
        app.sc.ch[i].on = true;
        app.sc.ch[i].vdiv = 1;
        app.sc.ch[i].pos = 0;
        app.sc.ch[i].coupling = 0;
        app.sc.ch[i].probe = 1;
        app.sc.ch[i].bwLimit = false;
        app.sc.ch[i].invert = false;
        app.sc.ch[i].fine = false;
    }
    app.sc.tdiv = 500e-6;
    app.sc.hpos = 0;
    app.sc.delayOn = false;
    app.sc.timeBase = 0;
    app.sc.trigSrc = 0;
    app.sc.trigSlope = 0;
    app.sc.trigMode = 0;
    app.sc.trigCoup = 0;
    app.sc.trigType = 0;
    app.sc.trigLevel = 0;
    app.sc.trigSens = 0.3;
    app.sc.holdoff = 100e-9;
    app.sc.mathOn = false;
    app.sc.refOn = false;
    app.sc.acq = 0;
    app.sc.drawMode = 0;
    app.sc.grid = 0;
    app.sc.persist = 0;
    app.sc.measV = app.sc.measT = -1;
    app.sc.dispAll = false;
    app.sc.filtOn = false;
    OpenScope(SP_CH);
    app.sc.active = 0;
    NoteS("Factory setup");
}

static void PressScopeSoft(int i)
{
    Scope &s = app.sc;
    ScopeCh &ch = s.ch[s.active < 2 ? s.active : 0];
    TouchMenu();
    s.focus = i;
    switch (s.page) {
    case SP_CH:
        if (s.sub == 2) {
            if (i == 0) s.filtOn = !s.filtOn;
            else if (i == 1) s.filtType = (s.filtType + 1) % 4;
            else if (i == 4) s.sub = 0;
            break;
        }
        if (s.sub == 1) {
            if (i == 0) s.sub = 0;
            else if (i == 1) ch.fine = !ch.fine;
            else if (i == 2) ch.invert = !ch.invert;
            break;
        }
        if (i == 0) ch.coupling = (ch.coupling + 1) % 3;
        else if (i == 1) ch.bwLimit = !ch.bwLimit;
        else if (i == 2) ch.probe = (ch.probe == 1 ? 10 : ch.probe == 10 ? 100 : ch.probe == 100 ? 1000 : 1);
        else if (i == 3) s.sub = 2;
        else s.sub = 1;
        break;
    case SP_TRIG:
        if (s.sub == 1) {
            if (i == 0) s.trigCoup = (s.trigCoup + 1) % 4;
            else if (i == 3) { s.holdoff = 100e-9; NoteS("Holdoff 100 ns"); }
            else if (i == 4) s.sub = 0;
            break;
        }
        if (i == 0) { s.trigType = (s.trigType + 1) % 5; if (s.trigType == 2) NoteS("No video signal"); }
        else if (i == 1) s.trigSrc = (s.trigSrc + 1) % 4;
        else if (i == 2) {
            if (s.trigType == 1) s.pulseWhen = (s.pulseWhen + 1) % 6;
            else s.trigSlope = (s.trigSlope + 1) % 3;
        } else if (i == 3) {
            if (s.trigType != 1) s.trigMode = (s.trigMode + 1) % 3;
        } else s.sub = 1;
        break;
    case SP_MEAS:
        if (s.sub == 0) {
            if (i == 0) s.measSrc = (s.measSrc + 1) % 3;
            else if (i == 1) s.sub = 1;
            else if (i == 2) s.sub = 4;
            else if (i == 3) { s.measV = s.measT = -1; s.dispAll = false; NoteS("Measures cleared"); }
            else s.dispAll = !s.dispAll;
            break;
        }
        if (s.sub >= 1 && s.sub <= 3) {
            static const int vid[3][4] = { {1, 2, 0, 6}, {7, 5, 3, 4}, {8, 9, -1, -1} };
            if (i == 4) { s.sub = s.sub == 3 ? 0 : s.sub + 1; break; }
            if (vid[s.sub - 1][i] >= 0) s.measV = vid[s.sub - 1][i];
            break;
        }
        if (s.sub >= 4 && s.sub <= 6) {
            static const int tid[3][4] = { {1, 0, 2, 3}, {4, 5, 6, 7}, {8, 9, -1, -1} };
            if (i == 4) { s.sub = s.sub == 6 ? 0 : s.sub + 1; break; }
            if (tid[s.sub - 4][i] >= 0) s.measT = tid[s.sub - 4][i];
            break;
        }
        break;
    case SP_CUR:
        if (i == 0) s.curMode = (s.curMode + 1) % 4;
        else if (i == 1) s.curType = 1 - s.curType;
        else if (i == 2) s.curSrc = (s.curSrc + 1) % 2;
        else if (i == 3) s.curFocus = 0;
        else s.curFocus = 1;
        break;
    case SP_ACQ:
        if (i == 0) s.acq = (s.acq + 1) % 3;
        else if (i == 1) s.averages = s.averages >= 256 ? 2 : s.averages * 2;
        else if (i == 2) s.sampEqu = 1 - s.sampEqu;
        else if (i == 3) s.memShort = 1 - s.memShort;
        else NoteS(s.sampEqu ? "Equivalent 25 GSa/s" : "Real time 400 MSa/s");
        break;
    case SP_DISP:
        if (s.sub == 1) {
            if (i == 0) s.sub = 0;
            else if (i == 1) s.grid = (s.grid + 1) % 3;
            else if (i == 3) s.menuTime = (s.menuTime + 1) % 6;
            else if (i == 4) s.screenInv = !s.screenInv;
            break;
        }
        if (i == 0) s.drawMode = 1 - s.drawMode;
        else if (i == 1) { s.ghostN = 0; NoteS("Display cleared"); }
        else if (i == 2) s.persist = 1 - s.persist;
        else if (i == 4) s.sub = 1;
        break;
    case SP_STORE:
        if (s.sub == 1) {
            if (i == 0) s.slot = (s.slot + 1) % 10;
            else if (i == 1) { if (s.storeKind == 4) FactoryScope(); else RecallScope(); }
            else if (i == 2) { if (s.storeKind == 0) SaveRef(); else SaveScope(); }
            else if (i == 4) s.sub = 0;
            break;
        }
        if (i == 0) s.storeKind = (s.storeKind + 1) % 5;
        else if (i == 1) s.sub = 1;
        else if (i == 4 && s.storeKind == 4) FactoryScope();
        else NoteS("No USB disk");
        break;
    case SP_UTIL:
        if (s.sub == 1) {
            if (i == 0) s.sub = 0;
            else if (i == 1) { s.passFail = !s.passFail; NoteS(s.passFail ? "Pass/Fail on" : "Pass/Fail off"); }
            else if (i == 2) NoteS("Recorder needs a USB disk");
            else if (i == 3) NoteS("Self-cal OK");
            else { s.fastCal = !s.fastCal; NoteS(s.fastCal ? "Fast-Cal on" : "Fast-Cal off"); }
            break;
        }
        if (i == 0) NoteS("USB device");
        else if (i == 1) { s.beep = !s.beep; NoteS(s.beep ? "Sound ON" : "Sound OFF"); }
        else if (i == 2) s.counterOn = !s.counterOn;
        else if (i == 3) NoteS("English");
        else s.sub = 1;
        break;
    case SP_HORIZ:
        if (i == 0) { s.delayOn = !s.delayOn; NoteS(s.delayOn ? "Delayed scan" : "Main timebase"); }
        else if (i == 1) {
            s.timeBase = (s.timeBase + 1) % 3;
            if (s.timeBase == 2 && s.tdiv < 0.5) s.tdiv = 0.5;
            if (s.timeBase == 1) s.drawMode = 1;
            NoteS(s.timeBase == 0 ? "Y-T" : s.timeBase == 1 ? "X-Y" : "Roll");
        } else if (i == 2) { s.hpos = 0; NoteS("Trigger offset centered"); }
        break;
    case SP_MATH:
        if (i == 0) s.mathOp = (s.mathOp + 1) % 4;
        else if (s.mathOp == 3) {
            if (i == 1) s.mathA = 1 - s.mathA;
            else if (i == 2) s.fftWin = (s.fftWin + 1) % 4;
            else if (i == 3) s.fftFull = !s.fftFull;
            else s.fftVrms = !s.fftVrms;
        } else if (i == 1) s.mathA = 1 - s.mathA;
        else if (i == 2) s.mathB = 1 - s.mathB;
        else if (i == 3) s.mathInv = !s.mathInv;
        s.mathOn = true;
        break;
    case SP_REF:
        if (i == 0) s.refSrc = (s.refSrc + 1) % 3;
        else if (i == 1) {
            int src = s.refSrc > 1 ? 0 : s.refSrc;
            if (s.refSrc > 1) {
                for (int k = 0; k < COLS; k++) s.refV[k] = s.math[k].mid;
                s.refVdiv = s.mathVdiv; s.refPos = s.mathPos;
            } else SaveRef();
            if (s.refSrc > 1) { s.refOn = true; NoteS("REF saved"); }
            (void)src;
        } else if (i == 2) { s.refOn = false; NoteS("REF reset"); }
        break;
    default: break;
    }
}

static void TurnGen(int steps)
{
    if (steps == 0) return;
    if (app.gen.page == GP_STORE) {
        app.gen.slot = (app.gen.slot + steps) % 10;
        if (app.gen.slot < 0) app.gen.slot += 10;
        return;
    }
    if (app.gen.page == GP_WAVE && Sel().wave == W_ARB && app.gen.edit == PAR_PHASE && false) {}
    double cur = EditGet();
    double mag = std::fabs(cur);
    if (mag < 1e-15) mag = 1e-6;
    double e = std::floor(std::log10(mag));
    double step = std::pow(10.0, e - app.gen.digit);
    if (app.gen.edit == PAR_DUTY || app.gen.edit == PAR_SYMM || (app.gen.edit == PAR_MODD && app.gen.modType == 0))
        step = std::pow(10.0, -app.gen.digit);
    if (app.gen.edit == PAR_BN) step = std::max(1.0, std::pow(10.0, std::floor(std::log10(std::max(1.0, cur))) - app.gen.digit));
    EditSet(cur + steps * step);
}

static void TurnScope(int id, int steps)
{
    if (steps == 0) return;
    Scope &s = app.sc;
    if (id == S_MKNOB) {
        if (s.page == SP_CH && s.sub == 2 && s.focus == 2)
            s.filtHi = Clamp(Step125(s.filtHi, steps > 0 ? 1 : -1), 1, 20e6);
        else if (s.page == SP_CH && s.sub == 2 && s.focus == 3)
            s.filtLo = Clamp(Step125(std::max(s.filtLo, 1.0), steps > 0 ? 1 : -1), 1, 20e6);
        else if (s.page == SP_TRIG && s.sub == 1 && s.focus == 1)
            s.trigSens = Clamp(s.trigSens + steps * 0.1, 0.1, 1.0);
        else if (s.page == SP_TRIG && s.sub == 0 && s.trigType == 1 && s.focus == 3)
            s.pulseWidth = Clamp(Step125(std::max(s.pulseWidth, 20e-9), steps > 0 ? 1 : -1), 20e-9, 1.0);
        else if (s.page == SP_TRIG && s.sub == 1)
            s.holdoff = Clamp(s.holdoff * std::pow(10.0, steps * 0.1), 100e-9, 10.0);
        else if (s.page == SP_CUR && s.curMode) {
            double &c = s.curFocus ? s.curB : s.curA;
            c += steps * (s.curType == 0 ? 0.05 : 0.1);
        } else if (s.page == SP_ACQ && s.acq == 1) {
            while (steps > 0 && s.averages < 256) { s.averages *= 2; steps--; }
            while (steps < 0 && s.averages > 2) { s.averages /= 2; steps++; }
        } else if (s.page == SP_STORE && s.sub == 1) {
            s.slot = (s.slot + steps) % 10; if (s.slot < 0) s.slot += 10;
        } else if (s.page == SP_DISP && s.sub == 1 && s.focus == 2)
            s.gridBright = (float)Clamp(s.gridBright + steps * 0.05, 0.15, 1.0);
        else s.intensity = Clamp(s.intensity + steps * 0.05f, 0.15f, 1.0f);
        return;
    }
    if (id == S_VPOS) {
        if (s.active == 2) s.mathPos = Clamp(s.mathPos + steps * 0.1, -8, 8);
        else if (s.active == 3) s.refPos = Clamp(s.refPos + steps * 0.1, -8, 8);
        else s.ch[s.active].pos = Clamp(s.ch[s.active].pos + steps * (s.ch[s.active].fine ? 0.02 : 0.1), -8, 8);
    } else if (id == S_VSCALE) {
        if (s.active == 2) {
            if (s.mathOp == 3) s.fftDb = Clamp(Step125(s.fftDb, steps > 0 ? 1 : -1), 1, 20);
            else s.mathVdiv = Clamp(s.ch[0].fine ? s.mathVdiv * std::pow(1.05, steps) : Step125(s.mathVdiv, steps > 0 ? 1 : -1), 2e-3, 50);
        } else if (s.active == 3) s.refVdiv = Clamp(Step125(s.refVdiv, steps > 0 ? 1 : -1), 2e-3, 10);
        else {
            ScopeCh &ch = s.ch[s.active];
            ch.vdiv = Clamp(ch.fine ? ch.vdiv * std::pow(1.04, steps) : Step125(ch.vdiv, steps > 0 ? 1 : -1), 2e-3, 10.0);
        }
    } else if (id == S_HPOS) s.hpos = Clamp(s.hpos + steps * 0.1, -10, 10);
    else if (id == S_HSCALE) {
        double &td = s.delayOn ? s.delayDiv : s.tdiv;
        td = Clamp(Step125(td, steps > 0 ? 1 : -1), 5e-9, 50.0);
    } else if (id == S_TLEV) {
        int src = s.trigSrc > 1 ? 0 : s.trigSrc;
        double lim = s.ch[src].vdiv * 5.0;
        s.trigLevel = Clamp(s.trigLevel + steps * s.ch[src].vdiv * 0.05, -lim, lim);
    }
}

void AppPress(int id, float x, float y)
{
    (void)x;
    if (id >= S_SOFT && id < S_SOFT + 5) { PressScopeSoft(id - S_SOFT); return; }
    if (id >= G_SOFT && id < G_SOFT + 6) { PressGenSoft(id - G_SOFT); return; }
    if (id >= G_WAVE && id < G_WAVE + 6) { SelectWave(id - G_WAVE); return; }
    if (id >= G_KEY && id < G_KEY + 12) {
        static const char *k = "123+/-4560789.";
        // labels: 1 2 3 +/- 4 5 6 0 7 8 9 .
        static const char *lab[12] = { "1","2","3","+/-","4","5","6","0","7","8","9","." };
        const char *s = lab[id - G_KEY];
        if (std::strcmp(s, "+/-") == 0) {
            if (app.gen.entry) {
                if (app.gen.ebuf[0] == '-') { std::memmove(app.gen.ebuf, app.gen.ebuf + 1, app.gen.elen); app.gen.elen--; }
                else { std::memmove(app.gen.ebuf + 1, app.gen.ebuf, app.gen.elen + 1); app.gen.ebuf[0] = '-'; app.gen.elen++; }
            } else EditSet(-EditGet());
        } else BeginEntry(s[0]);
        (void)k;
        return;
    }
    switch (id) {
    case S_MENU_TOG:
        app.sc.menuOn = !app.sc.menuOn;
        if (app.sc.menuOn && app.sc.page == SP_NONE) OpenScope(SP_CH);
        else if (app.sc.menuOn) TouchMenu();
        break;
    case S_MEASURE: OpenScope(SP_MEAS); break;
    case S_ACQUIRE: OpenScope(SP_ACQ); break;
    case S_STORAGE: OpenScope(SP_STORE); break;
    case S_CURSOR: OpenScope(SP_CUR); if (app.sc.curMode == 0) app.sc.curMode = 1; break;
    case S_DISPLAY: OpenScope(SP_DISP); break;
    case S_UTILITY: OpenScope(SP_UTIL); break;
    case S_AUTO: AutoSet(); break;
    case S_RUN: app.sc.running = !app.sc.running; app.sc.status = app.sc.running ? 1 : 2; break;
    case S_CH1:
        if (app.sc.page == SP_CH && app.sc.sub != 2 && app.sc.active == 0 && app.sc.ch[0].on && app.sc.menuOn) {
            app.sc.ch[0].on = false; app.sc.page = SP_NONE;
        } else { app.sc.ch[0].on = true; app.sc.active = 0; OpenScope(SP_CH); }
        break;
    case S_CH2:
        if (app.sc.page == SP_CH && app.sc.sub != 2 && app.sc.active == 1 && app.sc.ch[1].on && app.sc.menuOn) {
            app.sc.ch[1].on = false; app.sc.page = SP_NONE;
        } else { app.sc.ch[1].on = true; app.sc.active = 1; OpenScope(SP_CH); }
        break;
    case S_MATH: app.sc.mathOn = true; app.sc.active = 2; OpenScope(SP_MATH); break;
    case S_REF: app.sc.active = 3; OpenScope(SP_REF); break;
    case S_OFF:
        if (app.sc.active == 0) app.sc.ch[0].on = false;
        else if (app.sc.active == 1) app.sc.ch[1].on = false;
        else if (app.sc.active == 2) app.sc.mathOn = false;
        else app.sc.refOn = false;
        app.sc.page = SP_NONE;
        break;
    case S_HMENU: OpenScope(SP_HORIZ); break;
    case S_TMENU: OpenScope(SP_TRIG); break;
    case S_T50: SetLevel50(); break;
    case S_TFORCE: app.sc.force = true; if (!app.sc.running) app.sc.running = true; break;
    case G_MOD:
        if (!Ch1Only("Mod")) break;
        app.gen.page = GP_MOD; app.gen.entry = false; break;
    case G_SWEEP:
        if (!Ch1Only("Sweep")) break;
        app.gen.page = GP_SWEEP; app.gen.entry = false; break;
    case G_BURST:
        if (!Ch1Only("Burst")) break;
        app.gen.page = GP_BURST; app.gen.entry = false; break;
    case G_STORE: app.gen.page = GP_STORE; app.gen.entry = false; break;
    case G_UTIL: app.gen.page = GP_UTIL; app.gen.entry = false; break;
    case G_HELP: app.gen.page = GP_HELP; app.gen.entry = false; break;
    case G_LOCAL: app.gen.page = GP_WAVE; app.gen.entry = false; NoteG("Local"); break;
    case G_LEFT: app.gen.digit = std::max(0, app.gen.digit - 1); break;
    case G_RIGHT: app.gen.digit = std::min(6, app.gen.digit + 1); break;
    case G_VIEW: app.gen.view = (app.gen.view + 1) % 3; break;
    case G_OUT1: app.gen.ch[0].output = !app.gen.ch[0].output; NoteG(app.gen.ch[0].output ? "CH1 output ON" : "CH1 output OFF"); break;
    case G_OUT2:
        if (app.gen.counter) { NoteG("CH2 used by counter"); break; }
        app.gen.ch[1].output = !app.gen.ch[1].output;
        NoteG(app.gen.ch[1].output ? "CH2 output ON" : "CH2 output OFF");
        break;
    case G_CHSEL: app.gen.sel = (x < y) ? app.gen.sel : app.gen.sel; break; // replaced below
    default: break;
    }
}

void AppPressCh(int id, float localX)
{
    if (id == G_CHSEL) {
        app.gen.sel = localX < 31 ? 0 : 1;
        NoteG(app.gen.sel ? "Edit CH2" : "Edit CH1");
        return;
    }
    AppPress(id, localX, 0);
}

void AppTurn(int id, int steps)
{
    if (id == G_KNOB || id == G_LEFT || id == G_RIGHT) {
        if (id == G_LEFT || id == G_RIGHT) AppPress(id, 0, 0);
        else TurnGen(steps);
        return;
    }
    TurnScope(id, steps);
}

void AppKnobClick(int id)
{
    if (id == S_VPOS) {
        if (app.sc.active == 2) app.sc.mathPos = 0;
        else if (app.sc.active == 3) app.sc.refPos = 0;
        else app.sc.ch[app.sc.active].pos = 0;
    } else if (id == S_HPOS) app.sc.hpos = 0;
    else if (id == S_TLEV) app.sc.trigLevel = 0;
    else if (id == S_VSCALE) {
        if (app.sc.active < 2) app.sc.ch[app.sc.active].fine = !app.sc.ch[app.sc.active].fine;
    } else if (id == S_HSCALE) {
        app.sc.delayOn = !app.sc.delayOn;
        NoteS(app.sc.delayOn ? "Delayed scan" : "Main timebase");
    } else if (id == G_KNOB) app.gen.alignReq = true;
}

void AppKey(int key)
{
    // raylib key codes passed through. Digits and punctuation only.
    if (key >= '0' && key <= '9') BeginEntry((char)key);
    else if (key == '.') BeginEntry('.');
    else if (key == '-' ) AppPress(G_KEY + 3, 0, 0);
    else if (key == 259) { // backspace
        if (app.gen.entry && app.gen.elen > 0) app.gen.ebuf[--app.gen.elen] = 0;
    } else if (key == 256) app.gen.entry = false;
}

void AppAlign(double now)
{
    app.gen.ch[0].epoch = now;
    app.gen.ch[1].epoch = now;
    NoteG("Phase aligned");
}

// The sine-menu Align key must call AppAlign. Patch PressGenSoft by re-handling
// from AppPressCh path: see AppSoftFix.

void AppSoft(int i, double now)
{
    if (app.gen.page == GP_WAVE && !app.gen.entry && i == 4 && Sel().wave == W_SINE) {
        AppAlign(now);
        return;
    }
    if (app.gen.page == GP_WAVE && !app.gen.entry && i == 5 && Sel().wave != W_ARB) {
        AppAlign(now);
        return;
    }
    PressGenSoft(i);
}

void AppFrameAt(double now);

// Provide a single entry used by main.
void AppTick(double now)
{
    if (app.gmsgT > 0) app.gmsgT -= 1.0 / 60.0;
    if (app.smsgT > 0) app.smsgT -= 1.0 / 60.0;
    if (app.sc.menuOn && app.sc.menuTime < 5) {
        app.sc.menuIdle -= 1.0 / 60.0;
        if (app.sc.menuIdle <= 0) app.sc.menuOn = false;
    }
    Acquire(now);
}

static void Put(char *a, char *b, const char *x, const char *y)
{
    std::snprintf(a, 28, "%s", x ? x : "");
    std::snprintf(b, 28, "%s", y ? y : "");
}

void GenMenu(int i, char *a, char *b, bool *hot)
{
    a[0] = b[0] = 0;
    *hot = false;
    Gen &g = app.gen;
    if (g.entry) {
        const char *labs[6] = { "", "", "", "", "", "" };
        int n = 0;
        auto set = [&](const char *s) { if (n < 6) labs[n++] = s; };
        switch (g.edit) {
        case PAR_FREQ: case PAR_MODF: case PAR_SW1: case PAR_SW2:
            set("uHz"); set("mHz"); set("Hz"); set("kHz"); set("MHz"); set("Cancel"); break;
        case PAR_PER: case PAR_SWT: case PAR_BP:
            set("ns"); set("us"); set("ms"); set("s"); set("Cancel"); break;
        case PAR_AMPL: case PAR_HI: case PAR_LO:
            set("mVpp"); set("Vpp"); set("mVrms"); set("Vrms"); set("dBm"); set("Cancel"); break;
        case PAR_OFF: set("mVdc"); set("Vdc"); set("Cancel"); break;
        case PAR_PHASE: case PAR_BPH: set("deg"); set("Cancel"); break;
        case PAR_MODD:
            if (g.modType == 0) { set("%"); set("Cancel"); }
            else if (g.modType == 2) { set("deg"); set("Cancel"); }
            else { set("Hz"); set("kHz"); set("MHz"); set("Cancel"); }
            break;
        case PAR_BN: set("x1"); set("Cancel"); break;
        default: set("%"); set("Cancel"); break;
        }
        if (i < 6 && labs[i][0]) Put(a, b, labs[i], "");
        return;
    }
    GenCh &c = g.ch[g.sel];
    const char *wave = c.wave == W_SINE ? "Sine" : c.wave == W_SQUARE ? "Square" : c.wave == W_RAMP ? "Ramp"
        : c.wave == W_PULSE ? "Pulse" : c.wave == W_NOISE ? "Noise" : ArbName(c.arb);
    if (g.page == GP_WAVE) {
        if (c.wave == W_NOISE) {
            if (i == 0) Put(a, b, "Ampl", "");
            else if (i == 1) Put(a, b, "Offset", "");
            else if (i == 2) Put(a, b, "BW", "5 MHz");
            *hot = (i == 0 && g.edit == PAR_AMPL) || (i == 1 && (g.edit == PAR_OFF || g.edit == PAR_LO));
            return;
        }
        if (i == 0) Put(a, b, g.editPeriod ? "Period" : "Freq", "");
        else if (i == 1) Put(a, b, g.amplIsHi ? "HiLevel" : "Ampl", "");
        else if (i == 2) Put(a, b, g.offIsLo ? "LoLevel" : "Offset", "");
        else if (i == 3) Put(a, b, "Phase", "");
        else if (i == 4) {
            if (c.wave == W_SQUARE || c.wave == W_PULSE) Put(a, b, "Duty", "Cyc");
            else if (c.wave == W_RAMP) Put(a, b, "Symm", "");
            else if (c.wave == W_ARB) Put(a, b, "Arb", wave);
            else Put(a, b, "Align", "Phase");
        } else Put(a, b, wave, "");
        *hot = (i == 0 && (g.edit == PAR_FREQ || g.edit == PAR_PER)) ||
               (i == 1 && (g.edit == PAR_AMPL || g.edit == PAR_HI)) ||
               (i == 2 && (g.edit == PAR_OFF || g.edit == PAR_LO)) ||
               (i == 3 && g.edit == PAR_PHASE) ||
               (i == 4 && (g.edit == PAR_DUTY || g.edit == PAR_SYMM));
        return;
    }
    if (g.page == GP_MOD) {
        const char *tp = g.modType == 0 ? "AM" : g.modType == 1 ? "FM" : g.modType == 2 ? "PM" : "FSK";
        const char *sh = g.modShape == 0 ? "Sine" : g.modShape == 1 ? "Square" : g.modShape == 2 ? "UpRamp"
            : g.modShape == 3 ? "DnRamp" : g.modShape == 4 ? "Tri" : "Noise";
        if (i == 0) Put(a, b, "Type", tp);
        else if (i == 1) Put(a, b, "State", g.mode == 1 ? "ON" : "OFF");
        else if (i == 2) Put(a, b, "Shape", sh);
        else if (i == 3) Put(a, b, "ModFreq", "");
        else if (i == 4) Put(a, b, g.modType == 0 ? "Depth" : g.modType == 1 ? "Dev" : g.modType == 2 ? "PDev" : "Hop", "");
        else Put(a, b, "Source", g.modSource ? "Ext" : "Int");
        *hot = (i == 3 && g.edit == PAR_MODF) || (i == 4 && g.edit == PAR_MODD);
        return;
    }
    if (g.page == GP_SWEEP) {
        if (i == 0) Put(a, b, "Sweep", g.mode == 2 ? "ON" : "OFF");
        else if (i == 1) Put(a, b, "Start", "");
        else if (i == 2) Put(a, b, "Stop", "");
        else if (i == 3) Put(a, b, "Time", "");
        else if (i == 4) Put(a, b, g.swLog ? "Log" : "Linear", "");
        else Put(a, b, "Back", "");
        *hot = (i == 1 && g.edit == PAR_SW1) || (i == 2 && g.edit == PAR_SW2) || (i == 3 && g.edit == PAR_SWT);
        return;
    }
    if (g.page == GP_BURST) {
        if (i == 0) Put(a, b, "Burst", g.mode == 3 ? "ON" : "OFF");
        else if (i == 1) Put(a, b, "Cycles", "");
        else if (i == 2) Put(a, b, "Period", "");
        else if (i == 3) Put(a, b, "Phase", "");
        else if (i == 4) Put(a, b, "Ncycle", "");
        else Put(a, b, "Back", "");
        *hot = (i == 1 && g.edit == PAR_BN) || (i == 2 && g.edit == PAR_BP) || (i == 3 && g.edit == PAR_BPH);
        return;
    }
    if (g.page == GP_STORE) {
        char sl[16]; std::snprintf(sl, sizeof(sl), "Slot %d", g.slot + 1);
        if (i == 0) Put(a, b, sl, "");
        else if (i == 1) Put(a, b, "Save", "");
        else if (i == 2) Put(a, b, "Recall", "");
        else if (i == 5) Put(a, b, "Back", "");
        return;
    }
    if (g.page == GP_UTIL) {
        if (i == 0) Put(a, b, "Sync", g.sync ? "ON" : "OFF");
        else if (i == 1) Put(a, b, "Load", g.highZ ? "HighZ" : "50ohm");
        else if (i == 2) Put(a, b, "DC", c.dc ? "ON" : "OFF");
        else if (i == 3) Put(a, b, "Beep", g.beep ? "ON" : "OFF");
        else if (i == 4) Put(a, b, "Count", g.counter ? "ON" : "OFF");
        else Put(a, b, "Back", "");
        return;
    }
    if (i == 5) Put(a, b, "Back", "");
    else if (i == 0) Put(a, b, "Help", "");
}

void ScopeMenu(int i, char *a, char *b, bool *hot)
{
    a[0] = b[0] = 0; *hot = false;
    Scope &s = app.sc;
    int ai = s.active < 2 ? s.active : 0;
    ScopeCh &ch = s.ch[ai];
    const char *src4 = s.trigSrc == 0 ? "CH1" : s.trigSrc == 1 ? "CH2" : s.trigSrc == 2 ? "EXT" : "EXT/5";
    const char *slope = s.trigSlope == 0 ? "Rise" : s.trigSlope == 1 ? "Fall" : "Both";
    const char *sweep = s.trigMode == 0 ? "Auto" : s.trigMode == 1 ? "Normal" : "Single";
    const char *mode = s.trigType == 0 ? "Edge" : s.trigType == 1 ? "Pulse" : s.trigType == 2 ? "Video"
        : s.trigType == 3 ? "Slope" : "Alt";
    switch (s.page) {
    case SP_CH:
        if (s.sub == 2) {
            const char *ft = s.filtType == 0 ? "LPF" : s.filtType == 1 ? "HPF" : s.filtType == 2 ? "BPF" : "BRF";
            char hz[24];
            if (i == 0) Put(a, b, "Filter", s.filtOn ? "On" : "Off");
            else if (i == 1) Put(a, b, "Type", ft);
            else if (i == 2) { FmtFreq(hz, 24, s.filtHi); Put(a, b, "Upper", hz); }
            else if (i == 3) { FmtFreq(hz, 24, s.filtLo); Put(a, b, "Lower", hz); }
            else Put(a, b, "Back", "");
            *hot = i == s.focus;
            break;
        }
        if (s.sub == 1) {
            if (i == 0) Put(a, b, "2/2", "");
            else if (i == 1) Put(a, b, "Volts/Div", ch.fine ? "Fine" : "Coarse");
            else if (i == 2) Put(a, b, "Invert", ch.invert ? "ON" : "OFF");
            break;
        }
        if (i == 0) Put(a, b, "Coupling", ch.coupling == 0 ? "DC" : ch.coupling == 1 ? "AC" : "GND");
        else if (i == 1) Put(a, b, "BW Limit", ch.bwLimit ? "ON" : "OFF");
        else if (i == 2) { char p[16]; std::snprintf(p, 16, "%gX", ch.probe); Put(a, b, "Probe", p); }
        else if (i == 3) Put(a, b, "Digital", "Filter");
        else Put(a, b, "1/2", "");
        break;
    case SP_TRIG:
        if (s.sub == 1) {
            char hs[24];
            if (i == 0) Put(a, b, "Coupling", s.trigCoup == 0 ? "DC" : s.trigCoup == 1 ? "AC" : s.trigCoup == 2 ? "HF Rej" : "LF Rej");
            else if (i == 1) { char n[16]; std::snprintf(n, 16, "%.1fdiv", s.trigSens); Put(a, b, "Sens", n); }
            else if (i == 2) { FmtTime(hs, 24, s.holdoff); Put(a, b, "Holdoff", hs); }
            else if (i == 3) Put(a, b, "Ho Reset", "100ns");
            else Put(a, b, "Back", "");
            *hot = (i == 1 || i == 2) && i == s.focus;
            break;
        }
        if (i == 0) Put(a, b, "Mode", mode);
        else if (i == 1) Put(a, b, "Source", src4);
        else if (i == 2 && s.trigType == 1) {
            static const char *w[] = { "+less", "+more", "+equal", "-less", "-more", "-equal" };
            Put(a, b, "When", w[s.pulseWhen % 6]);
        } else if (i == 2) Put(a, b, s.trigType == 2 ? "Polarity" : "Slope", s.trigType == 2 ? (s.trigSlope ? "-" : "+") : slope);
        else if (i == 3 && s.trigType == 1) { char n[24]; FmtTime(n, 24, s.pulseWidth); Put(a, b, "Width", n); }
        else if (i == 3) Put(a, b, "Sweep", sweep);
        else Put(a, b, "Set Up", "");
        *hot = i == 3 && s.trigType == 1;
        break;
    case SP_MEAS: {
        const char *ms = s.measSrc == 0 ? "CH1" : s.measSrc == 1 ? "CH2" : "MATH";
        if (s.sub == 1) {
            if (i == 0) Put(a, b, "Vmax", ""); else if (i == 1) Put(a, b, "Vmin", "");
            else if (i == 2) Put(a, b, "Vpp", ""); else if (i == 3) Put(a, b, "Vtop", "");
            else Put(a, b, "1/3", "");
        } else if (s.sub == 2) {
            if (i == 0) Put(a, b, "Vbase", ""); else if (i == 1) Put(a, b, "Vamp", "");
            else if (i == 2) Put(a, b, "Vavg", ""); else if (i == 3) Put(a, b, "Vrms", "");
            else Put(a, b, "2/3", "");
        } else if (s.sub == 3) {
            if (i == 0) Put(a, b, "Overshoot", ""); else if (i == 1) Put(a, b, "Preshoot", "");
            else if (i == 4) Put(a, b, "3/3", "");
        } else if (s.sub == 4) {
            if (i == 0) Put(a, b, "Period", ""); else if (i == 1) Put(a, b, "Freq", "");
            else if (i == 2) Put(a, b, "Rise", ""); else if (i == 3) Put(a, b, "Fall", "");
            else Put(a, b, "1/3", "");
        } else if (s.sub == 5) {
            if (i == 0) Put(a, b, "+Width", ""); else if (i == 1) Put(a, b, "-Width", "");
            else if (i == 2) Put(a, b, "+Duty", ""); else if (i == 3) Put(a, b, "-Duty", "");
            else Put(a, b, "2/3", "");
        } else if (s.sub == 6) {
            if (i == 0) Put(a, b, "Delay", "1-2 rise"); else if (i == 1) Put(a, b, "Delay", "1-2 fall");
            else if (i == 4) Put(a, b, "3/3", "");
        } else {
            if (i == 0) Put(a, b, "Source", ms);
            else if (i == 1) Put(a, b, "Voltage", "");
            else if (i == 2) Put(a, b, "Time", "");
            else if (i == 3) Put(a, b, "Clear", "");
            else Put(a, b, "Disp All", s.dispAll ? "ON" : "OFF");
        }
        break;
    }
    case SP_CUR:
        if (i == 0) Put(a, b, "Mode", s.curMode == 0 ? "Off" : s.curMode == 1 ? "Manual" : s.curMode == 2 ? "Track" : "Auto");
        else if (i == 1) Put(a, b, "Type", s.curType ? "X" : "Y");
        else if (i == 2) Put(a, b, "Source", s.curSrc ? "CH2" : "CH1");
        else if (i == 3) Put(a, b, "Cursor", "A");
        else Put(a, b, "Cursor", "B");
        *hot = (i == 3 && s.curFocus == 0) || (i == 4 && s.curFocus == 1);
        break;
    case SP_ACQ:
        if (i == 0) Put(a, b, "Acquire", s.acq == 0 ? "Normal" : s.acq == 1 ? "Average" : "Peak");
        else if (i == 1) { char n[16]; std::snprintf(n, 16, "%d", s.averages); Put(a, b, "Averages", n); }
        else if (i == 2) Put(a, b, "Sampling", s.sampEqu ? "Equ-Time" : "RealTime");
        else if (i == 3) Put(a, b, "MemDepth", s.memShort ? "Normal" : "Long");
        else Put(a, b, "Sa Rate", s.sampEqu ? "25 GSa" : "400 MSa");
        break;
    case SP_DISP:
        if (s.sub == 1) {
            static const char *mt[] = { "1s", "2s", "5s", "10s", "20s", "Infinite" };
            if (i == 0) Put(a, b, "2/2", "");
            else if (i == 1) Put(a, b, "Grid", s.grid == 0 ? "Full" : s.grid == 1 ? "Half" : "None");
            else if (i == 2) { char n[16]; std::snprintf(n, 16, "%d%%", (int)(s.gridBright * 100)); Put(a, b, "Bright", n); }
            else if (i == 3) Put(a, b, "Menu", mt[s.menuTime < 6 ? s.menuTime : 5]);
            else Put(a, b, "Screen", s.screenInv ? "Invert" : "Normal");
            break;
        }
        if (i == 0) Put(a, b, "Type", s.drawMode ? "Dots" : "Vectors");
        else if (i == 1) Put(a, b, "Clear", "");
        else if (i == 2) Put(a, b, "Persist", s.persist ? "Infinite" : "OFF");
        else if (i == 3) { char n[16]; std::snprintf(n, 16, "%d%%", (int)(s.intensity * 100)); Put(a, b, "Intens", n); }
        else Put(a, b, "1/2", "");
        break;
    case SP_STORE: {
        const char *sk = s.storeKind == 0 ? "Wave" : s.storeKind == 1 ? "Setups" : s.storeKind == 2 ? "Bitmap"
            : s.storeKind == 3 ? "CSV" : "Factory";
        if (s.sub == 1) {
            char sl[16]; std::snprintf(sl, 16, "Int_%02d", s.slot);
            if (i == 0) Put(a, b, "Internal", sl);
            else if (i == 1) Put(a, b, "Load", "");
            else if (i == 2) Put(a, b, "Save", "");
            else if (i == 4) Put(a, b, "Back", "");
            break;
        }
        if (i == 0) Put(a, b, "Storage", sk);
        else if (i == 1) Put(a, b, "Internal", "");
        else if (i == 2) Put(a, b, "External", "");
        else if (i == 3) Put(a, b, "Disk", "Mana.");
        else if (s.storeKind == 4) Put(a, b, "Load", "");
        break;
    }
    case SP_UTIL:
        if (s.sub == 1) {
            if (i == 0) Put(a, b, "2/2", "");
            else if (i == 1) Put(a, b, "Pass/Fail", s.passFail ? "ON" : "OFF");
            else if (i == 2) Put(a, b, "Record", "");
            else if (i == 3) Put(a, b, "Self-Cal", "");
            else Put(a, b, "Fast-Cal", s.fastCal ? "ON" : "OFF");
            break;
        }
        if (i == 0) Put(a, b, "I/O", "USB");
        else if (i == 1) Put(a, b, "Sound", s.beep ? "ON" : "OFF");
        else if (i == 2) Put(a, b, "Counter", s.counterOn ? "ON" : "OFF");
        else if (i == 3) Put(a, b, "Language", "English");
        else Put(a, b, "1/2", "");
        break;
    case SP_HORIZ:
        if (i == 0) Put(a, b, "Delayed", s.delayOn ? "ON" : "OFF");
        else if (i == 1) Put(a, b, "Time Base", s.timeBase == 0 ? "Y-T" : s.timeBase == 1 ? "X-Y" : "Roll");
        else if (i == 2) Put(a, b, "Trig-Off", "Reset");
        break;
    case SP_MATH:
        if (s.mathOp == 3) {
            const char *win = s.fftWin == 0 ? "Rect" : s.fftWin == 1 ? "Hanning" : s.fftWin == 2 ? "Hamming" : "Blackman";
            if (i == 0) Put(a, b, "Operate", "FFT");
            else if (i == 1) Put(a, b, "Source", s.mathA ? "CH2" : "CH1");
            else if (i == 2) Put(a, b, "Window", win);
            else if (i == 3) Put(a, b, "Display", s.fftFull ? "Full" : "Split");
            else Put(a, b, "Scale", s.fftVrms ? "Vrms" : "dBVrms");
            break;
        }
        if (i == 0) Put(a, b, "Operate", s.mathOp == 0 ? "A+B" : s.mathOp == 1 ? "A-B" : "AxB");
        else if (i == 1) Put(a, b, "Source A", s.mathA ? "CH2" : "CH1");
        else if (i == 2) Put(a, b, "Source B", s.mathB ? "CH2" : "CH1");
        else if (i == 3) Put(a, b, "Invert", s.mathInv ? "ON" : "OFF");
        break;
    case SP_REF:
        if (i == 0) Put(a, b, "Source", s.refSrc == 0 ? "CH1" : s.refSrc == 1 ? "CH2" : "MATH");
        else if (i == 1) Put(a, b, "Save", "");
        else if (i == 2) Put(a, b, "Reset", "");
        else if (i == 3) Put(a, b, "Location", "Internal");
        break;
    default:
        break;
    }
}

int AppTest(const char *path)
{
    AppInit();
    FILE *f = std::fopen(path, "w");
    if (!f) return 1;
    int fail = 0;
    auto check = [&](const char *name, bool ok, double got, double exp) {
        std::fprintf(f, "%s %s  got %.6g expected %.6g\n", ok ? "OK" : "FAIL", name, got, exp);
        if (!ok) fail++;
    };
    app.gen.ch[0].output = true;
    Meas m = Measure(0);
    check("sine Vpp", std::fabs(m.vpp - 5) < 0.08, m.vpp, 5);
    check("sine freq", std::fabs(m.freq - 1000) < 5, m.freq, 1000);
    double vrms = 5.0 / (2 * std::sqrt(2.0));
    check("sine Vrms", std::fabs(m.vrms - vrms) < 0.05, m.vrms, vrms);
    double sineRise = 2.0 * std::asin(0.8) / (2.0 * PI * 1000.0);
    check("sine rise", std::fabs(m.rise - sineRise) < 8e-6, m.rise, sineRise);
    app.gen.highZ = false;
    ClampOut(app.gen.ch[0]);
    // 5 Vpp into 50 ohm is within CH1's 10 Vpp limit, scope sees 10 Vpp.
    m = Measure(0);
    check("50ohm Vpp", std::fabs(m.vpp - 10) < 0.15, m.vpp, 10);
    app.gen.highZ = true;
    ClampOut(app.gen.ch[0]);
    app.gen.ch[0].output = false;
    m = Measure(0);
    check("output off", std::fabs(m.vpp) < 1e-6, m.vpp, 0);
    app.gen.ch[0].output = true;
    app.gen.ch[0].wave = W_SQUARE;
    ClampOut(app.gen.ch[0]);
    m = Measure(0);
    check("square rise", m.rise > 10e-9 && m.rise < 40e-9, m.rise, 20e-9);
    app.gen.ch[1].ampl = 10;
    ClampOut(app.gen.ch[1]);
    check("CH2 clamp", std::fabs(app.gen.ch[1].ampl - 6) < 1e-6, app.gen.ch[1].ampl, 6);
    app.gen.ch[0].wave = W_SQUARE;
    app.gen.ch[0].freq = 9e6;
    ClampOut(app.gen.ch[0]);
    check("square fmax", std::fabs(app.gen.ch[0].freq - 5e6) < 1, app.gen.ch[0].freq, 5e6);
    app.gen.ch[0].wave = W_SINE;
    app.gen.ch[0].freq = 1000;
    app.gen.ch[0].ampl = 5;
    app.gen.ch[0].offset = 1;
    ClampOut(app.gen.ch[0]);
    app.sc.ch[0].coupling = 1;
    m = Measure(0);
    check("AC average", std::fabs(m.vavg) < 0.05, m.vavg, 0);
    app.sc.ch[0].coupling = 0;
    app.sc.ch[0].probe = 10;
    m = Measure(0);
    check("probe 10x", std::fabs(m.vpp - 50) < 0.8, m.vpp, 50);
    char nb[48];
    int mk = FmtFreqAt(nb, 48, 1000, 0, true);
    check("freq name", std::strcmp(nb, "1.000 kHz") == 0 && mk == 0, mk, 0);
    mk = FmtFreqAt(nb, 48, 1000, 3, true);
    check("freq digit", std::strcmp(nb, "1.000 kHz") == 0 && mk == 4, mk, 4);
    mk = FmtTimeAt(nb, 48, 0.001, 0, true);
    check("period name", std::strcmp(nb, "1.000 ms") == 0 && mk == 0, mk, 0);
    std::fprintf(f, fail ? "FAILED %d\n" : "ALL PASSED\n", fail);
    std::fclose(f);
    AppInit();
    return fail;
}
