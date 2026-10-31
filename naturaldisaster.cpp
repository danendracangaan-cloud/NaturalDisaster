// Natural Disaster Mod v3 - AML mod untuk GTA SA Android (armeabi-v7a)
// 7 bencana + HUD samping layar (jam, cuaca, suhu, angin, status bencana).
#include <mod/amlmod.h>
#include <mod/logger.h>
#include <mod/config.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

MYMODCFG(net.gua.naturaldisaster, Natural Disaster, 3.0, gua)

struct CVector { float x, y, z; };
struct CRGBA   { unsigned char r, g, b, a; };
struct CRect   { float left, bottom, right, top; };

typedef void    (*CamShake_t)(void*, float, float, float, float);
typedef CVector (*FindPlayerCoors_t)(int);
typedef void    (*AddExplosion_t)(void*, void*, int, CVector, unsigned int, unsigned char, float, unsigned char);
typedef void    (*ForceWeatherNow_t)(short);
typedef void    (*ReleaseWeather_t)();
typedef void    (*AddMessage_t)(unsigned short*, unsigned int, unsigned short, bool);
typedef void    (*FSetScale_t)(float, float);
typedef void    (*FSetColor_t)(CRGBA);
typedef void    (*FSetStyle_t)(short);
typedef void    (*FSetEdge_t)(short);
typedef void    (*FSetProp_t)(bool);
typedef void    (*FSetBack_t)(bool, bool);
typedef void    (*FPrint_t)(float, float, unsigned short*);
typedef void    (*FFlush_t)();
typedef void    (*DrawRect_t)(const CRect&, const CRGBA&);

static CamShake_t CamShake; static FindPlayerCoors_t FindPlayerCoors; static AddExplosion_t AddExplosion;
static ForceWeatherNow_t ForceWeatherNow; static ReleaseWeather_t ReleaseWeather; static AddMessage_t AddMessage;
static FSetScale_t FSetScale; static FSetColor_t FSetColor; static FSetStyle_t FSetStyle; static FSetEdge_t FSetEdge;
static FSetProp_t FSetProp; static FSetBack_t FSetBack; static FPrint_t FPrint; static FFlush_t FFlush; static DrawRect_t DrawRect;
static void *pTheCamera, *pRsGlobal;
static unsigned char *pHour, *pMin;
static short *pWeather;
static void* g_lib;

static void* Sym(const char* a, const char* b = nullptr)
{
    void* r = (void*)aml->GetSym(g_lib, a);
    if (!r && b) r = (void*)aml->GetSym(g_lib, b);
    return r;
}

enum DType { D_QUAKE, D_METEOR, D_TORNADO, D_STORM, D_SAND, D_ERUPT, D_HEAT, D_COUNT };
enum Phase { P_WARNING, P_ACTIVE, P_AFTER };
static const char* kKey[D_COUNT]  = { "earthquake", "meteor", "tornado", "storm", "sandstorm", "eruption", "heatwave" };
static const char* kName[D_COUNT] = { "EARTHQUAKE", "METEOR SHOWER", "TORNADO", "THUNDERSTORM", "SANDSTORM", "ERUPTION", "HEATWAVE" };
static const char* kWarn[D_COUNT] = { "WARNING: Ground tremors detected!", "WARNING: Fireballs in the sky!", "WARNING: Tornado spotted!",
                                      "WARNING: Severe storm approaching!", "WARNING: Sandstorm approaching!",
                                      "WARNING: The volcano is rumbling!", "WARNING: Extreme heat expected!" };
static const char* kStart[D_COUNT] = { "EARTHQUAKE! Take cover!", "METEOR SHOWER! Run!", "TORNADO! Get away!", "THUNDERSTORM!",
                                       "SANDSTORM!", "VOLCANIC ERUPTION!", "HEATWAVE!" };
static const char* kEnd[D_COUNT]   = { "The shaking has stopped.", "The meteor shower is over.", "The tornado has dissipated.",
                                       "The storm has passed.", "The sandstorm has passed.", "The eruption has calmed.",
                                       "The heatwave is over." };
static const char* kWx[21] = { "Extra Sunny", "Sunny", "Smog", "Smog", "Cloudy", "Sunny", "Extra Sunny", "Cloudy", "Thunderstorm",
                               "Foggy", "Sunny", "Heat Haze", "Cloudy", "Extra Sunny", "Sunny", "Cloudy", "Rain", "Hot Desert",
                               "Sunny Desert", "Sandstorm", "Underwater" };
static int SlotOf(int t) { return t <= D_TORNADO ? 0 : 1; }   // 0 = tanah, 1 = cuaca

// ---------------- config ----------------
static int cStartDelay, cInterval, cWarning, cDuration, cAftermath, cMinLvl, cMaxLvl, cCombo, cDiff, cPowerPct;
static int cMeteorMin, cMeteorMax, cApoc, cMsgs, cQuakeBlast, cLightning, cChaos;
static int cOn[D_COUNT];
static int hudOn, hudSide, hudX, hudY, hudScale, hudBg, hudAlpha, hudLog, hudNext, hudStyle, hudFlush;
static int cfgTest = -1, cfgTestLevel = 3;
static float dm = 1.0f;                                   // pengali kesulitan
static const float kDiff[4] = { 0.6f, 1.0f, 1.4f, 2.0f };
static float PW() { return cPowerPct / 100.0f * dm; }
static int CI(const char* k, int d, const char* s) { return cfg->Bind(k, d, s)->GetInt(); }

// ---------------- state ----------------
struct Event
{
    bool on; int type, phase, level, burst;
    double pStart, pEnd, tick, last;
    float mag, curMag, dist;
    CVector tpos, tvel; bool tnear;
    double impactAt; bool impactDone;
    double surgeAt; bool surged;
};
struct Thunder { double at; float power; };
static Event   ev[2];
static Thunder th[8];
static bool    ready, logicFromTimer;
static double  gt, realLast, frameDt, startAt, nextSpawn, lastStrikeAt = -999;
static int     lastType = -1, survived;
static float   chaos, windKmh = 6.f, heatBonus, lastStrikeDist;
static CVector P;
static char    logBuf[4][64];

// ---------------- helper ----------------
static float Rand01() { return rand() / (float)RAND_MAX; }
static float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static float Dist2D(const CVector& a, const CVector& b) { float dx = a.x - b.x, dy = a.y - b.y; return sqrtf(dx * dx + dy * dy); }
static CVector Ring(const CVector& c, float mn, float mx)
{
    float a = Rand01() * 6.2831853f, d = mn + Rand01() * (mx - mn);
    return CVector{ c.x + cosf(a) * d, c.y + sinf(a) * d, c.z };
}
static void Shake(float power, const CVector& p)
{
    power = Clamp(power, 0.f, 1.5f);
    if (power < 0.01f || !CamShake || !pTheCamera) return;
    CamShake(pTheCamera, power, p.x, p.y, p.z);
}
static void Boom(int type, const CVector& pos, float cs) { if (AddExplosion) AddExplosion(nullptr, nullptr, type, pos, 0, 1, cs, 1); }
static void Force(int id) { if (ForceWeatherNow) ForceWeatherNow((short)id); }
static void Msg(const char* s)
{
    if (!cMsgs || !AddMessage) return;
    static unsigned short buf[96];
    int i = 0;
    for (; s[i] && i < 95; ++i) buf[i] = (unsigned char)s[i];
    buf[i] = 0;
    AddMessage(buf, 4000, 0, false);
}
static void AddLog(const char* s)
{
    for (int i = 0; i < 3; ++i) memcpy(logBuf[i], logBuf[i + 1], 64);
    snprintf(logBuf[3], 64, "[%02d:%02d] %s", pHour ? *pHour : 0, pMin ? *pMin : 0, s);
}
static void QueueThunder(double at, float power)
{
    for (int i = 0; i < 8; ++i) if (th[i].at <= 0) { th[i].at = at; th[i].power = power; return; }
}
static int Rem(const Event& e) { int r = (int)(e.pEnd - gt + 0.99); return r < 0 ? 0 : r; }
static int RandLevel()
{
    int lo = cMinLvl < 1 ? 1 : cMinLvl, hi = cMaxLvl > 5 ? 5 : cMaxLvl;
    if (hi < lo) hi = lo;
    int lv = lo + rand() % (hi - lo + 1);
    if (cChaos && Rand01() < chaos / 150.f) lv++;
    return lv > 5 ? 5 : lv;
}

// ---------------- siklus bencana ----------------
static void StartEvent(int type, int level, double t)
{
    Event& e = ev[SlotOf(type)];
    memset(&e, 0, sizeof(e));
    e.on = true; e.type = type; e.level = level; e.phase = P_WARNING;
    e.pStart = t; e.pEnd = t + cWarning; e.tick = t; e.last = t; e.dist = 9999.f;
    e.mag = (type == D_QUAKE) ? 4.0f + level * 0.6f + Rand01() * 0.5f : 0.f;
    e.curMag = e.mag;
    if (cChaos) chaos = Clamp(chaos + 5.f, 0.f, 100.f);
    char b[64]; snprintf(b, sizeof b, "%s warning L%d", kName[type], level); AddLog(b);
    Msg(kWarn[type]);
    if (type == D_STORM) Force(7); else if (type == D_SAND) Force(18); else if (type == D_HEAT) Force(13);
    logger->Info("Mulai: %s level %d", kKey[type], level);
}

static void EnterPhase(Event& e, int phase, double t)
{
    e.phase = phase; e.pStart = t; e.tick = t; e.last = t;
    float L = (float)e.level;
    if (phase == P_ACTIVE)
    {
        double dur = cDuration * (0.7 + 0.15 * L);
        e.pEnd = t + dur;
        Msg(kStart[e.type]);
        switch (e.type)
        {
        case D_STORM:  Force(8);  break;
        case D_SAND:   Force(19); break;
        case D_HEAT:   Force(17); break;
        case D_ERUPT:  Force(9); e.surgeAt = t + dur * 0.5; break;
        case D_METEOR: e.impactAt = t + dur * 0.45; break;
        case D_TORNADO:
        {
            float sd = 170.f + Rand01() * 60.f;
            e.tpos = Ring(P, sd, sd);
            CVector tg = Ring(P, 5.f, 40.f);
            double travel = dur * 0.8;
            e.tvel = CVector{ (float)((tg.x - e.tpos.x) / travel), (float)((tg.y - e.tpos.y) / travel), 0.f };
            break;
        }
        default: break;
        }
        char b[64]; snprintf(b, sizeof b, "%s started", kName[e.type]); AddLog(b);
    }
    else
    {
        e.pEnd = t + cAftermath;
        if (e.type == D_STORM) Force(16); else if (e.type == D_SAND) Force(17); else if (e.type == D_HEAT) Force(14);
    }
}

static void EndEvent(Event& e)
{
    Msg(kEnd[e.type]);
    char b[64]; snprintf(b, sizeof b, "%s ended", kName[e.type]); AddLog(b);
    if (SlotOf(e.type) == 1 && ReleaseWeather) ReleaseWeather();
    if (e.type == D_TORNADO && e.tnear && !ev[1].on && ReleaseWeather) ReleaseWeather();
    if (cChaos) chaos = Clamp(chaos - 3.f * e.level, 0.f, 100.f);
    survived++;
    e.on = false;
}

static void MainImpact(Event& e, const CVector& p)
{
    CVector c = Ring(p, 30.f, 70.f);
    Boom(2, c, 1.2f);
    Shake(PW() * 0.9f, p);
    for (int i = 0; i < 3 + e.level; ++i) Boom(0, Ring(c, 4.f, 18.f), 0.4f);
    Msg("DIRECT IMPACT!");
    AddLog("Major meteor impact");
    e.impactDone = true;
}

static void TickEvent(Event& e, double t, const CVector& p)
{
    const float L = (float)e.level, pw = PW();
    float frac = (e.pEnd > e.pStart) ? (float)((t - e.pStart) / (e.pEnd - e.pStart)) : 1.f;
    frac = Clamp(frac, 0.f, 1.f);
    const float env = sinf(frac * 3.14159265f);
    e.tick = t + 1.0;

    switch (e.type)
    {
    case D_QUAKE:
    {
        float base = 0.10f + 0.17f * (e.mag - 4.0f);
        if (e.phase == P_WARNING) { Shake(pw * base * 0.35f, p); e.tick = t + 1.5 + Rand01() * 2.5; }
        else if (e.phase == P_ACTIVE)
        {
            float jolt = (Rand01() < 0.08f) ? 1.6f : 1.0f;                 // hentakan gelombang S
            Shake(pw * base * (0.35f + 0.65f * env) * jolt, p);
            if (cQuakeBlast && Rand01() < 0.010f * L * dm) Boom(4, Ring(p, 10.f, 45.f), 0.3f);
            e.tick = t + 0.2;
        }
        else
        {
            float am = Clamp(e.mag - 1.2f - Rand01(), 2.0f, 9.0f);          // gempa susulan
            e.curMag = am;
            Shake(pw * Clamp(0.10f + 0.17f * (am - 4.f), 0.05f, 1.f) * (1.f - frac * 0.6f), p);
            e.tick = t + 3.0 + Rand01() * 6.0;
        }
        break;
    }
    case D_METEOR:
    {
        if (e.phase == P_WARNING) { Shake(pw * 0.04f * L, p); e.tick = t + 2.5; }
        else if (e.phase == P_ACTIVE)
        {
            if (!e.impactDone && t >= e.impactAt) MainImpact(e, p);
            else if (!e.impactDone && e.impactAt - t < 6.0) Shake(pw * 0.25f * (1.f - (float)(e.impactAt - t) / 6.f), p);
            if (e.burst > 0) { Boom(0, Ring(p, (float)cMeteorMin, (float)cMeteorMax), 0.4f); e.burst--; e.tick = t + 0.12; }
            else
            {
                if (Rand01() < 0.12f) e.burst = 2 + rand() % 4;
                Boom(0, Ring(p, (float)cMeteorMin, (float)cMeteorMax), 0.4f);
                e.tick = e.burst > 0 ? t + 0.12 : t + Clamp((2.2f - 0.35f * L) / dm * (0.5f + Rand01()), 0.2f, 10.f);
            }
        }
        else { Boom(0, Ring(p, 40.f, 90.f), 0.2f); e.tick = t + 4.0 + Rand01() * 4.0; }
        break;
    }
    case D_TORNADO:
    {
        if (e.phase == P_WARNING) { Shake(pw * 0.05f * L, p); e.tick = t + 3.0; break; }
        double dt = t - e.last; if (dt > 0.5) dt = 0.5;
        e.last = t;
        float fade = (e.phase == P_AFTER) ? 1.f - frac : 1.f;
        e.tpos.x += e.tvel.x * (float)dt * fade;
        e.tpos.y += e.tvel.y * (float)dt * fade;
        float d = Dist2D(e.tpos, p), reach = 90.f + 25.f * L;
        e.dist = d;
        if (d < reach)
        {
            float k = 1.f - d / reach;
            Shake(pw * (0.05f + 0.15f * L * k * k) * fade, p);
            if (e.phase == P_ACTIVE && Rand01() < 0.5f * k * dm) Boom(0, Ring(e.tpos, 2.f, 14.f), 0.2f);   // puing
            if (e.phase == P_ACTIVE && d < 25.f && Rand01() < 0.12f * dm) Boom(0, Ring(p, 4.f, 10.f), 0.3f);
        }
        if (!ev[1].on)
        {
            bool nr = d < 120.f && e.phase != P_AFTER;
            if (nr != e.tnear) { e.tnear = nr; if (nr) Force(19); else if (ReleaseWeather) ReleaseWeather(); }
        }
        e.tick = t + 0.2;
        break;
    }
    case D_STORM:
    {
        if (e.phase == P_ACTIVE)
        {
            float r = Rand01(), dist = 40.f + 660.f * r * r;
            lastStrikeDist = dist; lastStrikeAt = t;
            QueueThunder(t + dist / 343.0, pw * (0.03f + 0.25f * (1.f - dist / 700.f)));   // petir tertunda sesuai jarak
            if (cLightning && e.level >= 2 && dist < 90.f) Boom(0, Ring(p, dist, dist), 0.3f);
            e.tick = t + Clamp((4.f + Rand01() * 7.f) / ((0.5f + 0.25f * L) * dm), 0.8f, 30.f);
        }
        else if (e.phase == P_WARNING) { QueueThunder(t + 1.0, pw * 0.03f); e.tick = t + 5.0; }
        else e.tick = t + 6.0;
        break;
    }
    case D_SAND:
    {
        float k = (e.phase == P_ACTIVE) ? (0.4f + 0.6f * env) : 0.15f;
        Shake(pw * (0.03f + 0.025f * L) * k, p);
        e.tick = t + 0.8 + Rand01() * 1.2;
        break;
    }
    case D_ERUPT:
    {
        if (e.phase == P_WARNING) { Shake(pw * 0.1f * L, p); e.tick = t + 2.0; }
        else if (e.phase == P_ACTIVE)
        {
            Shake(pw * (0.18f + 0.09f * L) * (0.4f + 0.6f * env), p);
            int n = 1 + rand() % (1 + e.level / 2);
            for (int i = 0; i < n; ++i) Boom(0, Ring(p, 15.f, 55.f), 0.3f);       // bom lava
            if (!e.surged && t >= e.surgeAt)
            {
                e.surged = true;
                Shake(pw * 1.1f, p);
                Boom(2, Ring(p, 30.f, 60.f), 1.0f);
                for (int i = 0; i < 3 + e.level; ++i) Boom(0, Ring(p, 20.f, 70.f), 0.5f);
                Msg("PYROCLASTIC SURGE!"); AddLog("Pyroclastic surge");
            }
            e.tick = t + 1.4 + Rand01() * 1.4;
        }
        else { Shake(pw * 0.1f * L * (1.f - frac), p); e.tick = t + 4.0 + Rand01() * 4.0; }
        break;
    }
    case D_HEAT:
    {
        if (e.phase == P_ACTIVE && Rand01() < 0.2f * dm * L / 5.f) Boom(4, Ring(p, 30.f, 110.f), 0.15f);   // mobil overheat
        e.tick = t + 3.0 + Rand01() * 4.0;
        break;
    }
    }
}

static void TrySpawn(double t)
{
    if (cApoc > 0 && !ev[0].on && !ev[1].on && Rand01() * 100.f < cApoc)
    {
        int g[3], gn = 0, w[4], wn = 0;
        for (int i = 0; i < D_COUNT; ++i) if (cOn[i]) { if (SlotOf(i) == 0) g[gn++] = i; else w[wn++] = i; }
        if (gn && wn)
        {
            StartEvent(g[rand() % gn], 5, t);
            StartEvent(w[rand() % wn], 5, t);
            Msg("APOCALYPSE!"); AddLog("APOCALYPSE EVENT");
            nextSpawn = t + cInterval * 1.5;
            return;
        }
    }
    int pool[D_COUNT], n = 0;
    for (int pass = 0; pass < 2 && n == 0; ++pass)
        for (int i = 0; i < D_COUNT; ++i)
            if (cOn[i] && !ev[SlotOf(i)].on && (pass == 1 || i != lastType)) pool[n++] = i;
    if (n == 0) { nextSpawn = t + 5.0; return; }

    int type = pool[rand() % n];
    lastType = type;
    StartEvent(type, RandLevel(), t);

    if (Rand01() * 100.f < cCombo)
    {
        int p2[D_COUNT], m = 0;
        for (int i = 0; i < D_COUNT; ++i) if (cOn[i] && SlotOf(i) != SlotOf(type) && !ev[SlotOf(i)].on) p2[m++] = i;
        if (m) StartEvent(p2[rand() % m], RandLevel(), t);
    }
    double iv = cInterval * (0.75 + Rand01() * 0.5) * (1.0 - 0.4 * chaos / 100.0) / (0.6 + 0.4 * dm);
    nextSpawn = t + (iv < 20.0 ? 20.0 : iv);
}

static void Tick()
{
    timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    double r = ts.tv_sec + ts.tv_nsec / 1e9, d = r - realLast;
    realLast = r;
    if (d < 0) d = 0;
    if (d > 0.25) d = 0.25;           // game pause tidak ikut dihitung
    frameDt = d; gt += d;
}

static void OnFrame()
{
    Tick();
    if (!ready)
    {
        if (startAt == 0) startAt = gt + cStartDelay;
        if (gt < startAt) return;
        ready = true; nextSpawn = gt;
    }
    if (!FindPlayerCoors) return;
    CVector p = FindPlayerCoors(0);
    if (p.x == 0.0f && p.y == 0.0f && p.z == 0.0f) return;     // pemain belum ada
    P = p;

    static bool testDone = false;
    if (!testDone) { testDone = true; if (cfgTest >= 0) { StartEvent(cfgTest, cfgTestLevel, gt); nextSpawn = gt + cInterval; } }

    if (cChaos) chaos = Clamp(chaos + 0.15f * (float)frameDt, 0.f, 100.f);
    for (int i = 0; i < 8; ++i) if (th[i].at > 0 && gt >= th[i].at) { Shake(th[i].power, p); th[i].at = 0; }

    float heatT = 0.f, windT = 6.f;
    for (int i = 0; i < 2; ++i)
    {
        Event& e = ev[i];
        if (!e.on) continue;
        if (gt >= e.pEnd)
        {
            if (e.phase == P_WARNING)     EnterPhase(e, P_ACTIVE, gt);
            else if (e.phase == P_ACTIVE) EnterPhase(e, P_AFTER, gt);
            else { EndEvent(e); continue; }
        }
        if (gt >= e.tick) TickEvent(e, gt, p);

        float f = e.phase == P_ACTIVE ? 1.f : (e.phase == P_AFTER ? 0.4f : 0.3f), L = (float)e.level, w = 0.f;
        switch (e.type)
        {
        case D_STORM:   w = (35.f + 14.f * L) * f; break;
        case D_SAND:    w = (45.f + 16.f * L) * f; break;
        case D_ERUPT:   w = (25.f + 5.f * L) * f; break;
        case D_TORNADO: w = (e.tnear ? 90.f + 30.f * L : 30.f + 5.f * L) * f; break;
        case D_HEAT:    heatT = (e.phase == P_ACTIVE) ? 10.f : 4.f; break;
        default: break;
        }
        if (w > windT) windT = w;
    }
    float k = Clamp((float)frameDt * 0.5f, 0.f, 1.f);
    windKmh += (windT - windKmh) * k;
    heatBonus += (heatT - heatBonus) * k;

    if (gt >= nextSpawn) TrySpawn(gt);
}

// ---------------- HUD ----------------
struct HLine { char t[80]; CRGBA c; };
static HLine hl[24];
static int   hn;
static void AddLine(CRGBA c, const char* fmt, ...)
{
    if (hn >= 24) return;
    va_list a; va_start(a, fmt);
    vsnprintf(hl[hn].t, sizeof hl[hn].t, fmt, a);
    va_end(a);
    hl[hn].c = c; hn++;
}
static const char* DayPart(int h)
{
    if (h >= 5 && h < 7) return "DAWN";
    if (h >= 7 && h < 18) return "DAY";
    if (h >= 18 && h < 20) return "DUSK";
    return "NIGHT";
}
static int Temperature()
{
    float h = pHour ? (float)*pHour + (pMin ? *pMin / 60.f : 0.f) : 12.f;
    float t = 25.f + 8.f * sinf((h - 9.f) * 0.2617994f);
    int w = pWeather ? *pWeather : 0;
    if (w == 8 || w == 16) t -= 6; else if (w == 9) t -= 3;
    else if (w == 4 || w == 7 || w == 12 || w == 15) t -= 2;
    else if (w == 17 || w == 11) t += 7; else if (w == 19) t += 3;
    return (int)(t + heatBonus + 0.5f);
}

static void DrawHud()
{
    if (!hudOn || !FPrint || !FSetScale || !FSetColor) return;
    static const CRGBA WHITE{255, 255, 255, 255}, YEL{255, 215, 60, 255}, RED{255, 80, 80, 255}, ORG{255, 150, 40, 255},
                       GRN{120, 235, 120, 255}, CYN{120, 220, 255, 255}, GRY{175, 175, 175, 255};
    hn = 0;
    int h = pHour ? *pHour : -1, m = pMin ? *pMin : 0, w = pWeather ? *pWeather : -1;
    if (h >= 0) AddLine(YEL, "TIME %02d:%02d  %s", h, m, DayPart(h)); else AddLine(YEL, "TIME --:--");
    AddLine(CYN, "WEATHER: %s", (w >= 0 && w <= 20) ? kWx[w] : "Unknown");
    AddLine(WHITE, "TEMP %dC   WIND %d km/h", Temperature(), (int)windKmh);

    int score = 0;
    for (int i = 0; i < 2; ++i) if (ev[i].on) score += ev[i].level * (ev[i].phase == P_ACTIVE ? 2 : 1);
    const char* tn = "CALM"; CRGBA tc = GRN;
    if (score >= 12) { tn = "EXTREME"; tc = RED; } else if (score >= 8) { tn = "SEVERE"; tc = RED; }
    else if (score >= 4) { tn = "HIGH"; tc = ORG; } else if (score > 0) { tn = "ELEVATED"; tc = YEL; }
    AddLine(tc, "THREAT: %s   CHAOS %d%%", tn, (int)chaos);

    bool any = false;
    for (int i = 0; i < 2; ++i)
    {
        const Event& e = ev[i];
        if (!e.on) continue;
        any = true;
        const char* ph = e.phase == P_WARNING ? "WARNING" : (e.phase == P_ACTIVE ? "ACTIVE" : "AFTERMATH");
        CRGBA c = e.phase == P_WARNING ? ORG : (e.phase == P_ACTIVE ? RED : YEL);
        if (e.type == D_QUAKE) AddLine(c, "%s M%.1f  %s %ds", kName[e.type], e.mag, ph, Rem(e));
        else AddLine(c, "%s L%d  %s %ds", kName[e.type], e.level, ph, Rem(e));
        switch (e.type)
        {
        case D_QUAKE:   if (e.phase == P_AFTER) AddLine(WHITE, " AFTERSHOCK M%.1f", e.curMag); break;
        case D_METEOR:  if (e.phase == P_ACTIVE) { if (!e.impactDone) AddLine(WHITE, " MAIN IMPACT IN %ds", (int)(e.impactAt - gt + 0.99)); else AddLine(WHITE, " MAIN IMPACT DONE"); } break;
        case D_TORNADO: if (e.phase != P_WARNING) AddLine(WHITE, " EF%d  DISTANCE %dm", e.level - 1, (int)e.dist); break;
        case D_STORM:   if (lastStrikeAt > 0 && gt - lastStrikeAt < 60.0) AddLine(WHITE, " LAST STRIKE %dm", (int)lastStrikeDist); break;
        case D_ERUPT:   AddLine(WHITE, " VEI %d", 2 + e.level); break;
        default: break;
        }
    }
    if (!any) AddLine(GRN, "No active disaster");

    if (hudNext)
    {
        if (!ready) { int s = (int)((startAt > 0 ? startAt : gt + cStartDelay) - gt); AddLine(GRY, "STARTS IN %d:%02d", s / 60, s % 60); }
        else if (ev[0].on && ev[1].on) AddLine(GRY, "NEXT: after current events");
        else { int s = (int)(nextSpawn - gt); if (s < 0) s = 0; AddLine(GRY, "NEXT DISASTER %d:%02d", s / 60, s % 60); }
    }
    AddLine(GRY, "SURVIVED: %d", survived);
    if (hudLog) for (int i = 1; i < 4; ++i) if (logBuf[i][0]) AddLine(GRY, "%s", logBuf[i]);

    float W = 1280.f, H = 720.f;
    if (pRsGlobal)
    {
        int mw = *(int*)((uintptr_t)pRsGlobal + 0x0C), mh = *(int*)((uintptr_t)pRsGlobal + 0x10);
        if (mw > 200 && mh > 200 && mw < 8000 && mh < 8000) { W = (float)mw; H = (float)mh; }
    }
    float sx = W / 640.f, sy = H / 448.f, S = hudScale / 100.f;
    float lineH = 12.f * S * sy, panelW = 215.f * S * sx;
    float x0 = hudSide ? W - panelW - hudX * sx : hudX * sx, y0 = hudY * sy;

    if (hudBg && DrawRect)
    {
        CRect r{ x0 - 5.f * sx, y0 + hn * lineH + 4.f * sy, x0 + panelW, y0 - 4.f * sy };
        CRGBA bg{ 0, 0, 0, (unsigned char)hudAlpha };
        DrawRect(r, bg);
    }
    if (FSetProp)  FSetProp(true);
    if (FSetBack)  FSetBack(false, false);
    if (FSetStyle) FSetStyle((short)hudStyle);
    if (FSetEdge)  FSetEdge(1);
    FSetScale(0.38f * S * sx, 0.85f * S * sy);
    static unsigned short buf[96];
    for (int i = 0; i < hn; ++i)
    {
        FSetColor(hl[i].c);
        int k = 0;
        for (; hl[i].t[k] && k < 95; ++k) buf[k] = (unsigned char)hl[i].t[k];
        buf[k] = 0;
        FPrint(x0, y0 + i * lineH, buf);
    }
    if (hudFlush && FFlush) FFlush();
}

static void (*CTimer_Update_o)();
static void CTimer_Update_h() { CTimer_Update_o(); OnFrame(); }
static void (*CHud_Draw_o)();
static void CHud_Draw_h() { CHud_Draw_o(); if (!logicFromTimer) OnFrame(); DrawHud(); }

extern "C" void OnModLoad()
{
    logger->SetTag("NaturalDisaster");
    cStartDelay = CI("StartDelay", 90, "Main");   cInterval = CI("Interval", 180, "Main");
    cWarning    = CI("WarningTime", 12, "Main");  cDuration = CI("Duration", 45, "Main");
    cAftermath  = CI("AftermathTime", 25, "Main");
    cMinLvl = CI("MinLevel", 1, "Main");          cMaxLvl = CI("MaxLevel", 5, "Main");
    cCombo  = CI("ComboChancePercent", 25, "Main"); cApoc = CI("ApocalypseChancePercent", 3, "Main");
    cDiff   = CI("Difficulty", 1, "Main");        // 0 mudah, 1 normal, 2 sulit, 3 gila
    cPowerPct = CI("ShakePowerPercent", 100, "Main");
    cMeteorMin = CI("MeteorMinDist", 15, "Main"); cMeteorMax = CI("MeteorMaxDist", 60, "Main");
    cMsgs = CI("ScreenMessages", 1, "Main");      cQuakeBlast = CI("QuakeExplosions", 1, "Main");
    cLightning = CI("LightningStrikes", 1, "Main"); cChaos = CI("ChaosSystem", 1, "Main");
    cOn[D_QUAKE] = CI("Earthquake", 1, "Disasters");  cOn[D_METEOR] = CI("MeteorShower", 1, "Disasters");
    cOn[D_TORNADO] = CI("Tornado", 1, "Disasters");   cOn[D_STORM] = CI("Thunderstorm", 1, "Disasters");
    cOn[D_SAND] = CI("Sandstorm", 1, "Disasters");    cOn[D_ERUPT] = CI("Eruption", 1, "Disasters");
    cOn[D_HEAT] = CI("Heatwave", 1, "Disasters");
    hudOn = CI("Enabled", 1, "HUD");    hudSide = CI("Side", 0, "HUD");     // 0 kiri, 1 kanan
    hudX = CI("X", 8, "HUD");           hudY = CI("Y", 70, "HUD");
    hudScale = CI("ScalePercent", 130, "HUD"); hudBg = CI("Background", 1, "HUD"); hudAlpha = CI("BackgroundAlpha", 170, "HUD");
    hudLog = CI("EventLog", 1, "HUD");  hudNext = CI("ShowNext", 1, "HUD");
    hudStyle = CI("FontStyle", 1, "HUD"); hudFlush = CI("FlushFonts", 1, "HUD");
    const char* test = cfg->Bind("TestDisaster", "", "Debug")->GetString();   // earthquake/meteor/tornado/storm/sandstorm/eruption/heatwave
    cfgTestLevel = CI("TestLevel", 3, "Debug");
    for (int i = 0; i < D_COUNT; ++i) if (test && strcmp(test, kKey[i]) == 0) cfgTest = i;
    cfg->Save();

    if (cDiff < 0) cDiff = 0;
    if (cDiff > 3) cDiff = 3;
    dm = kDiff[cDiff];
    srand((unsigned)time(nullptr));

    g_lib = aml->GetLibHandle("libGTASA.so");
    if (!g_lib) { logger->Error("libGTASA.so tidak ketemu"); return; }

    pTheCamera      = Sym("TheCamera");
    pRsGlobal       = Sym("RsGlobal");
    pHour           = (unsigned char*)Sym("_ZN6CClock18ms_nGameClockHoursE");
    pMin            = (unsigned char*)Sym("_ZN6CClock20ms_nGameClockMinutesE");
    pWeather        = (short*)Sym("_ZN8CWeather14NewWeatherTypeE");
    CamShake        = (CamShake_t)Sym("_ZN7CCamera8CamShakeEffff");
    FindPlayerCoors = (FindPlayerCoors_t)Sym("_Z15FindPlayerCoorsi");
    AddExplosion    = (AddExplosion_t)Sym("_ZN10CExplosion12AddExplosionEP7CEntityS1_14eExplosionType7CVectorjhfh");
    ForceWeatherNow = (ForceWeatherNow_t)Sym("_ZN8CWeather15ForceWeatherNowEs");
    ReleaseWeather  = (ReleaseWeather_t)Sym("_ZN8CWeather14ReleaseWeatherEv");
    AddMessage      = (AddMessage_t)Sym("_ZN9CMessages15AddMessageJumpQEPtjtb");
    FSetScale       = (FSetScale_t)Sym("_ZN5CFont8SetScaleEff");
    FSetColor       = (FSetColor_t)Sym("_ZN5CFont8SetColorE5CRGBA");
    FSetStyle       = (FSetStyle_t)Sym("_ZN5CFont12SetFontStyleEs");
    FSetEdge        = (FSetEdge_t)Sym("_ZN5CFont7SetEdgeEs");
    FSetProp        = (FSetProp_t)Sym("_ZN5CFont15SetProportionalEb");
    FSetBack        = (FSetBack_t)Sym("_ZN5CFont13SetBackgroundEbb");
    FPrint          = (FPrint_t)Sym("_ZN5CFont11PrintStringEffPKt", "_ZN5CFont11PrintStringEffPt");
    FFlush          = (FFlush_t)Sym("_ZN5CFont16RenderFontBufferEv");
    DrawRect        = (DrawRect_t)Sym("_ZN9CSprite2d8DrawRectERK5CRectRK5CRGBA");

    logger->Info("Logika: cam=%p shake=%p coors=%p expl=%p weather=%p msg=%p", pTheCamera, (void*)CamShake,
                 (void*)FindPlayerCoors, (void*)AddExplosion, (void*)ForceWeatherNow, (void*)AddMessage);
    logger->Info("HUD: hour=%p min=%p wx=%p rs=%p print=%p scale=%p color=%p rect=%p", (void*)pHour, (void*)pMin,
                 (void*)pWeather, pRsGlobal, (void*)FPrint, (void*)FSetScale, (void*)FSetColor, (void*)DrawRect);
    if (!FindPlayerCoors) logger->Error("FindPlayerCoors tidak ketemu: bencana nonaktif (HUD tetap jalan)");

    void* tm  = Sym("_ZN6CTimer6UpdateEv");
    void* hud = Sym("_ZN4CHud4DrawEv", "_Z13Render2dStuffv");
    if (tm)  { aml->Hook(tm, (void*)CTimer_Update_h, (void**)&CTimer_Update_o); logicFromTimer = true; }
    else logger->Error("CTimer::Update tidak ketemu, logika jalan dari hook HUD");
    if (hud) aml->Hook(hud, (void*)CHud_Draw_h, (void**)&CHud_Draw_o);
    else logger->Error("Hook HUD tidak ketemu: HUD tidak tampil");
}
