#pragma once

// Rigol DG1022 driving a Rigol DS1062C.
// The scope input is 1 MOhm. Each generator output is wired to the matching
// scope channel. A 50 ohm load setting doubles the open-circuit voltage the
// scope actually sees. Limits come from the DG1022 datasheet.

enum Id : int {
    ID_NONE = 0,
    S_MENU_TOG, S_SOFT, S_MKNOB = S_SOFT + 5,
    S_MEASURE, S_ACQUIRE, S_STORAGE, S_CURSOR, S_DISPLAY, S_UTILITY,
    S_AUTO, S_RUN, S_CH1, S_CH2, S_MATH, S_REF, S_OFF,
    S_VPOS, S_VSCALE, S_HMENU, S_HPOS, S_HSCALE,
    S_TMENU, S_TLEV, S_T50, S_TFORCE,
    G_SOFT, G_WAVE = G_SOFT + 6,
    G_MOD = G_WAVE + 6, G_UTIL, G_SWEEP, G_STORE, G_BURST, G_HELP, G_LOCAL,
    G_KEY, G_KNOB = G_KEY + 12, G_LEFT, G_RIGHT, G_VIEW, G_OUT1, G_OUT2, G_CHSEL
};

enum Wf { W_SINE, W_SQUARE, W_RAMP, W_PULSE, W_NOISE, W_ARB };
enum GPage { GP_WAVE, GP_MOD, GP_SWEEP, GP_BURST, GP_STORE, GP_UTIL, GP_HELP };
enum Param {
    PAR_FREQ, PAR_PER, PAR_AMPL, PAR_HI, PAR_OFF, PAR_LO, PAR_PHASE,
    PAR_DUTY, PAR_SYMM, PAR_MODF, PAR_MODD, PAR_SW1, PAR_SW2, PAR_SWT, PAR_BN, PAR_BP, PAR_BPH
};
enum SPage {
    SP_NONE, SP_CH, SP_MATH, SP_REF, SP_HORIZ, SP_TRIG,
    SP_MEAS, SP_CUR, SP_ACQ, SP_DISP, SP_STORE, SP_UTIL
};

static const int COLS = 500;
static const int ARB_N = 48;

struct SampleCol { float lo, hi, mid; };

struct GenCh {
    Wf wave;
    double freq, ampl, offset, phase, duty, symmetry;
    bool output;
    bool dc;
    int arb;
    double epoch;
};

struct Gen {
    GenCh ch[2];
    int sel;
    bool highZ;
    bool beep, sync, counter;
    int digit;
    Param edit;
    bool amplIsHi, offIsLo, editPeriod;
    GPage page;
    int view;
    int slot;
    bool entry;
    char ebuf[24];
    int elen;
    int mode;          // 0 off, 1 mod, 2 sweep, 3 burst
    int modType;       // 0 AM 1 FM 2 PM 3 FSK
    int modShape;      // 0 sine 1 square 2 up 3 down 4 tri 5 noise
    int modSource;     // 0 internal 1 external (no cable -> no modulation)
    double modFreq, amDepth, fmDev, pmDev, fskHop;
    double swStart, swStop, swTime;
    bool swLog;
    double bCycles, bPeriod, bPhase;
    bool alignReq;
    GenCh mem[10][2];
    bool memOk[10];
    bool savedHighZ[10];
};

struct ScopeCh {
    bool on;
    double vdiv, pos;
    int coupling;      // 0 DC 1 AC 2 GND
    double probe;
    bool bwLimit, invert, fine;
};

struct Scope {
    ScopeCh ch[2];
    double tdiv, hpos, delayDiv;
    bool delayOn;
    int trigSrc, trigSlope, trigMode, trigCoup;
    double trigLevel, holdoff;
    bool running, force;
    bool menuOn;
    SPage page;
    int sub, focus;
    int timeBase;          // 0 Y-T, 1 X-Y, 2 Roll
    int trigType;          // 0 edge, 1 pulse, 2 video, 3 slope, 4 alternate
    double trigSens;
    int pulseWhen;
    double pulseWidth;
    bool filtOn;
    int filtType;          // 0 LPF 1 HPF 2 BPF 3 BRF
    double filtLo, filtHi;
    int fftWin;            // 0 rect 1 hanning 2 hamming 3 blackman
    bool fftFull, fftVrms;
    bool dispAll, counterOn, screenInv, fastCal, passFail;
    int menuTime;          // 0..4 timed, 5 infinite
    double menuIdle;
    int storeKind;         // 0 wave 1 setup 2 bitmap 3 csv 4 factory
    int sampEqu, memShort;
    float gridBright;
    int active;        // 0 CH1 1 CH2 2 MATH 3 REF
    int acq, averages; // acq 0 normal 1 average 2 peak
    int drawMode, grid, persist;
    float intensity;
    bool mathOn, mathInv;
    int mathOp, mathA, mathB;
    double mathVdiv, mathPos, fftDb;
    bool refOn;
    int refSrc;
    double refVdiv, refPos;
    float refV[COLS];
    int curMode, curType, curFocus, curSrc;
    double curA, curB;
    int measV, measT, measSrc;
    bool beep;
    int slot;
    int status;
    bool have;
    SampleCol col[2][COLS];
    SampleCol math[COLS];
    float ghost[6][2][COLS];
    int ghostN;
    double winT0, winDt;
    float fftMag[128];
    double fftDf;
    bool fftOk;
    struct Snap {
        bool ok;
        ScopeCh ch[2];
        double tdiv, hpos, trigLevel, mathVdiv, mathPos, fftDb;
        int trigSrc, trigSlope, trigMode, trigCoup, acq, averages, drawMode, grid;
        bool mathOn, mathInv, delayOn;
        int mathOp;
    } mem[10];
};

struct App {
    Gen gen;
    Scope sc;
    char gmsg[120];
    char smsg[120];
    double gmsgT, smsgT;
    bool audioReady;
};

extern App app;

void AppInit();
void AppPress(int id, float x, float y);
void AppPressCh(int id, float localX);
void AppTurn(int id, int steps);
void AppKnobClick(int id);
void AppKey(int key);
void AppTick(double now);
int AppTest(const char *path);
void GenMenu(int i, char *a, char *b, bool *hot);
void ScopeMenu(int i, char *a, char *b, bool *hot);
const char *MeasNameV(int i);
const char *MeasNameT(int i);
bool FormatMeasV(int src, int which, char *b, int n);
bool FormatMeasT(int src, int which, char *b, int n);
void FormatAllMeas(int src, char lines[][24]);

double ScopeVolts(int ch, double t);
double ShapeOf(const GenCh &c, double phase);
const char *ArbName(int i);

void FmtFreq(char *b, int n, double hz);
int FmtFreqAt(char *b, int n, double hz, int digit, bool cursor);
int FmtTimeAt(char *b, int n, double sec, int digit, bool cursor);
void FmtVpp(char *b, int n, double v);
void FmtVolt(char *b, int n, double v);
void FmtTime(char *b, int n, double s);
