// =====================================================================
//  Optimizalis - interfaz gráfica (Win32 + GDI+, dibujada a mano)
// =====================================================================
#include "engine.hpp"
#include <algorithm>
namespace Gdiplus { using std::min; using std::max; }
#include <objidl.h>
#include <gdiplus.h>
#include <windowsx.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <thread>
#include <map>
#include <cmath>
#include <cstdio>

using namespace Gdiplus;

#define WM_SCAN_DONE   (WM_APP + 1)
#define WM_SEARCH_DONE (WM_APP + 2)
#define WM_DEL_DONE    (WM_APP + 3)

// ===================================================================
//  Estado global de la interfaz
// ===================================================================
static HWND g_hwnd = nullptr;
static HINSTANCE g_inst = nullptr;
static float S = 1.f;
static inline float px(float v) { return v * S; }
static int W = 1320, H = 840;
static const float SIDEBAR = 248.f;

enum Page { PG_HOME, PG_SCAN, PG_RESULTS, PG_DETAIL, PG_SEARCH, PG_PROTECT, PG_HISTORY, PG_SETTINGS };
enum Modal { MD_NONE, MD_CONFIRM, MD_DELETING, MD_DELRESULT, MD_INSTALLED, MD_INFO };
enum Action {
    A_NONE = 0, A_BLOCK, A_NAV, A_SCAN_QUICK, A_SCAN_FULL, A_SAFE_CLEAN, A_CANCEL_WORK, A_OPEN_CAT, A_BACK,
    A_ROW, A_ROW_OPEN, A_SEL_ALL, A_SEL_NONE, A_SORT, A_DELETE, A_MODAL_CANCEL, A_MODAL_CONFIRM, A_DEL_STOP,
    A_RESULT_OK, A_OPEN_LOG, A_OPEN_BACKUPS, A_OPEN_FOLDER, A_SEARCH_GO, A_SEARCH_LAST, A_UNINSTALL,
    A_SEARCH_ANYWAY, A_SET_DEC, A_SET_INC, A_ACCENT, A_TOGGLE_ANIM, A_ELEVATE, A_RESCAN, A_SCROLLTHUMB,
    A_VERIFY_AGAIN, A_FOCUS_EDIT, A_INFO_OK, A_HOME_CARD, A_MSCROLL, A_MROW, A_MROW_OPEN
};
enum { KEY_PAGE = 900000, KEY_MODAL, KEY_GAUGE, KEY_RECOV, KEY_FREED, KEY_TOAST, KEY_SAFE, KEY_SCANBAR };

static int g_page = PG_HOME;
static int g_modal = MD_NONE;
static bool g_busy = false;
static int g_busyKind = 0;  // 0 análisis, 1 búsqueda, 2 eliminación
static std::thread g_worker;
static std::mutex g_resMx;
static std::vector<Category> g_cats, g_resCats;
static Category g_search, g_resSearch;
static DelResult g_resDel, g_delRes;
static bool g_resTrunc = false;
static bool g_scanned = false, g_searchDone = false, g_lastQuick = true, g_safeAfterScan = false;
static wstring g_scanWhen, g_searchTerm;
static ULONGLONG g_workStart = 0;

static Category* g_detail = nullptr;
static bool g_detailIsSearch = false;
static std::vector<int> g_view;
static uint64_t g_viewMax = 1;
static int g_sortMode = 0;
static wstring g_filter;
static int g_lastClick = -1;

static float g_scroll = 0, g_scrollTarget = 0, g_scrollMax = 0, g_scrollRatio = 1;
static bool g_dragThumb = false;
static float g_dragY0 = 0, g_dragScroll0 = 0;

static std::vector<Item*> g_pendingDel;
static bool g_confirmStrong = false, g_delFromSearch = false;
static std::vector<Uninst> g_installed;
static wstring g_infoTitle, g_infoText;

static wstring g_toast;
static ARGB g_toastColor = 0;
static ULONGLONG g_toastAt = 0;

static std::vector<DriveInfo> g_drives;
static uint64_t g_histFreed = 0, g_histCleanups = 0, g_histItems = 0;
static std::vector<LogEntry> g_log;

static HWND g_edSearch = nullptr, g_edFilter = nullptr, g_edConfirm = nullptr;
static HFONT g_edFont = nullptr;
static HBRUSH g_inputBrush = nullptr;
static RectF g_rcEdSearch, g_rcEdFilter, g_rcEdConfirm;
static bool g_visEdSearch = false, g_visEdFilter = false, g_visEdConfirm = false;

static HDC g_memDC = nullptr;
static HBITMAP g_memBmp = nullptr, g_memOld = nullptr;
static Bitmap* g_bgBmp = nullptr;
static CachedBitmap* g_bgCached = nullptr;
static bool g_timerOn = false;
static ULONGLONG g_lastFrame = 0;

// ===================================================================
//  Colores
// ===================================================================
static ARGB ACC1 = 0xFF2DD4BF, ACC2 = 0xFF6366F1;
static const ARGB C_TEXT = 0xFFE9EDF5, C_DIM = 0xFF959DB2, C_FAINT = 0xFF5F6779, C_DANGER = 0xFFF43F5E,
                  C_WARN = 0xFFF59E0B, C_OK = 0xFF22C55E, C_INPUT = 0xFF121722, C_DARK = 0xFF0A0D13;
struct AccentDef { const wchar_t* name; ARGB a, b; };
static const AccentDef ACCENTS[6] = {
    {L"Turquesa", 0xFF2DD4BF, 0xFF6366F1}, {L"Violeta", 0xFFA78BFA, 0xFFEC4899}, {L"Azul", 0xFF60A5FA, 0xFF22D3EE},
    {L"Rosa", 0xFFF472B6, 0xFFF59E0B}, {L"Ámbar", 0xFFFBBF24, 0xFFF43F5E}, {L"Verde", 0xFF4ADE80, 0xFF2DD4BF}};

static Color C(ARGB c) { return Color(c); }
static Color CA(ARGB c, float a) {
    int v = (int)a; if (v < 0) v = 0; if (v > 255) v = 255;
    return Color((BYTE)v, (BYTE)((c >> 16) & 255), (BYTE)((c >> 8) & 255), (BYTE)(c & 255));
}
static ARGB mix(ARGB a, ARGB b, float t) {
    auto ch = [&](int s) { return (ARGB)((((a >> s) & 255) * (1 - t)) + (((b >> s) & 255) * t)); };
    return 0xFF000000 | (ch(16) << 16) | (ch(8) << 8) | ch(0);
}

// ===================================================================
//  Animación
// ===================================================================
static std::map<long long, float> g_anim;
static float g_dt = 0.016f;
static bool g_needAnim = false;
static long long K(int id, int data) { return ((long long)id << 32) | (unsigned)data; }
static float anim(long long key, float target, float speed = 14.f) {
    auto it = g_anim.find(key);
    if (it == g_anim.end()) { g_anim[key] = target; return target; }
    float& v = it->second;
    if (!g_set.animations) { v = target; return v; }
    float k = 1.f - expf(-speed * g_dt);
    v += (target - v) * k;
    if (fabsf(target - v) <= 0.002f * std::max(1.f, fabsf(target))) v = target;
    else g_needAnim = true;
    return v;
}
static void animSet(long long key, float v) { g_anim[key] = v; }
static float ease(float t) { t = std::clamp(t, 0.f, 1.f); return 1.f - (1.f - t) * (1.f - t) * (1.f - t); }

// ===================================================================
//  Zonas interactivas
// ===================================================================
struct Hot { RectF r; int id, data; bool hand; };
static std::vector<Hot> g_hots;
static int g_hovId = 0, g_hovData = 0, g_prsId = 0, g_prsData = 0;
static bool g_hovHand = false;
static POINT g_mouse{-1, -1};
static void addHot(const RectF& r, int id, int data = 0, bool hand = true) { g_hots.push_back({r, id, data, hand}); }
static bool hov(int id, int data = 0) { return g_hovId == id && g_hovData == data; }
static bool prs(int id, int data = 0) { return g_prsId == id && g_prsData == data && hov(id, data); }
static void invalidate() { if (g_hwnd) InvalidateRect(g_hwnd, nullptr, FALSE); }
static void hitTest() {
    int id = 0, d = 0; bool hand = false;
    for (int i = (int)g_hots.size() - 1; i >= 0; --i)
        if (g_hots[i].r.Contains((REAL)g_mouse.x, (REAL)g_mouse.y)) { id = g_hots[i].id; d = g_hots[i].data; hand = g_hots[i].hand; break; }
    g_hovHand = hand;
    if (id != g_hovId || d != g_hovData) { g_hovId = id; g_hovData = d; invalidate(); }
}

// ===================================================================
//  Primitivas de dibujo
// ===================================================================
static void roundPath(GraphicsPath& p, const RectF& r, float rad) {
    float d = std::min(rad * 2, std::min(r.Width, r.Height));
    if (d <= 0.5f) { p.AddRectangle(r); return; }
    p.AddArc(r.X, r.Y, d, d, 180, 90);
    p.AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
    p.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
    p.AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
    p.CloseFigure();
}
static void fillRound(Graphics& g, const RectF& r, float rad, const Brush& b) {
    if (r.Width <= 0 || r.Height <= 0) return;
    GraphicsPath p; roundPath(p, r, rad); g.FillPath(&b, &p);
}
static void strokeRound(Graphics& g, const RectF& r, float rad, const Pen& pen) {
    if (r.Width <= 0 || r.Height <= 0) return;
    GraphicsPath p; roundPath(p, r, rad); g.DrawPath(&pen, &p);
}
static void fillRoundC(Graphics& g, const RectF& r, float rad, const Color& c) { SolidBrush b(c); fillRound(g, r, rad, b); }

enum { F_REG, F_SEMI, F_LIGHT, F_BOLD };
static std::map<int, Font*> g_fonts;
static Font* font(float size, int fam) {
    int key = fam * 100000 + (int)(size * 10);
    auto it = g_fonts.find(key);
    if (it != g_fonts.end()) return it->second;
    const wchar_t* name = fam == F_SEMI ? L"Segoe UI Semibold" : fam == F_LIGHT ? L"Segoe UI Light" : L"Segoe UI";
    int style = fam == F_BOLD ? FontStyleBold : FontStyleRegular;
    Font* f = new Font(name, px(size), style, UnitPixel);
    if (!f->IsAvailable()) { delete f; f = new Font(L"Segoe UI", px(size), style, UnitPixel); }
    g_fonts[key] = f;
    return f;
}
static void clearFonts() { for (auto& f : g_fonts) delete f.second; g_fonts.clear(); }

enum { AL_L, AL_C, AL_R };
enum { TR_NONE, TR_CHAR, TR_PATH };
static void makeFormat(StringFormat& sf, int align, int valign, int trim, bool wrap) {
    sf.SetAlignment(align == AL_L ? StringAlignmentNear : align == AL_C ? StringAlignmentCenter : StringAlignmentFar);
    sf.SetLineAlignment(valign == 0 ? StringAlignmentNear : valign == 1 ? StringAlignmentCenter : StringAlignmentFar);
    sf.SetTrimming(trim == TR_NONE ? StringTrimmingNone : trim == TR_PATH ? StringTrimmingEllipsisPath : StringTrimmingEllipsisCharacter);
    sf.SetFormatFlags(wrap ? StringFormatFlagsLineLimit : (StringFormatFlagsNoWrap | StringFormatFlagsLineLimit));
}
static void textB(Graphics& g, const wstring& s, const RectF& r, float size, const Brush& b, int fam = F_REG,
                  int align = AL_L, int valign = 1, int trim = TR_CHAR, bool wrap = false) {
    if (s.empty() || r.Width <= 1) return;
    StringFormat sf;
    makeFormat(sf, align, valign, trim, wrap);
    g.DrawString(s.c_str(), (INT)s.size(), font(size, fam), r, &sf, &b);
}
static void text(Graphics& g, const wstring& s, const RectF& r, float size, const Color& c, int fam = F_REG,
                 int align = AL_L, int valign = 1, int trim = TR_CHAR, bool wrap = false) {
    SolidBrush b(c);
    textB(g, s, r, size, b, fam, align, valign, trim, wrap);
}
static float textW(Graphics& g, const wstring& s, float size, int fam) {
    if (s.empty()) return 0;
    StringFormat sf;
    sf.SetFormatFlags(StringFormatFlagsNoWrap);
    RectF out;
    g.MeasureString(s.c_str(), (INT)s.size(), font(size, fam), PointF(0, 0), &sf, &out);
    return out.Width;
}
static void textGrad(Graphics& g, const wstring& s, float x, float y, float h, float size, int fam, ARGB a, ARGB b, int align = AL_L, float boxW = 0) {
    float tw = textW(g, s, size, fam) + px(4);
    float sx = x;
    if (align == AL_R) sx = x + boxW - tw;
    else if (align == AL_C) sx = x + (boxW - tw) / 2;
    RectF r(sx, y, tw, h);
    LinearGradientBrush lb(r, C(a), C(b), LinearGradientModeHorizontal);
    textB(g, s, r, size, lb, fam, AL_L, 1, TR_NONE);
}

// --------------------------------------------------------------- iconos
static void icon(Graphics& g, int ic, float cx, float cy, float s, ARGB col, float alpha = 255.f) {
    float u = s / 24.f;
    auto X = [&](float x) { return cx + (x - 12.f) * u; };
    auto Y = [&](float y) { return cy + (y - 12.f) * u; };
    Pen p(CA(col, alpha * ((col >> 24) & 255) / 255.f), std::max(1.2f, 1.9f * u));
    p.SetStartCap(LineCapRound); p.SetEndCap(LineCapRound); p.SetLineJoin(LineJoinRound);
    SolidBrush fill(CA(col, alpha * ((col >> 24) & 255) / 255.f));
    auto L = [&](float a, float b, float c, float d) { g.DrawLine(&p, X(a), Y(b), X(c), Y(d)); };
    auto poly = [&](std::initializer_list<float> v, bool close) {
        std::vector<PointF> pts;
        for (auto it = v.begin(); it != v.end(); it += 2) pts.push_back(PointF(X(*it), Y(*(it + 1))));
        if (close) g.DrawPolygon(&p, pts.data(), (INT)pts.size());
        else g.DrawLines(&p, pts.data(), (INT)pts.size());
    };
    auto circ = [&](float x, float y, float r) { g.DrawEllipse(&p, X(x - r), Y(y - r), 2 * r * u, 2 * r * u); };
    auto dot = [&](float x, float y, float r) { g.FillEllipse(&fill, X(x - r), Y(y - r), 2 * r * u, 2 * r * u); };
    auto rrect = [&](float x, float y, float w, float h, float r) { GraphicsPath gp; roundPath(gp, RectF(X(x), Y(y), w * u, h * u), r * u); g.DrawPath(&p, &gp); };
    auto arc = [&](float x, float y, float r, float a0, float sw) { g.DrawArc(&p, X(x - r), Y(y - r), 2 * r * u, 2 * r * u, a0, sw); };
    switch (ic) {
    case IC_HOME: poly({3, 10.5f, 12, 3, 21, 10.5f}, false); poly({5.5f, 9, 5.5f, 21, 18.5f, 21, 18.5f, 9}, false); poly({10, 21, 10, 15, 14, 15, 14, 21}, false); break;
    case IC_RADAR: circ(12, 12, 9.5f); circ(12, 12, 5); L(12, 12, 18.5f, 5.5f); dot(12, 12, 1.6f); break;
    case IC_GRID: rrect(3, 3, 7.5f, 7.5f, 2); rrect(13.5f, 3, 7.5f, 7.5f, 2); rrect(3, 13.5f, 7.5f, 7.5f, 2); rrect(13.5f, 13.5f, 7.5f, 7.5f, 2); break;
    case IC_GHOST: {
        GraphicsPath gp;
        gp.AddArc(X(5), Y(3), 14 * u, 14 * u, 180, 180);
        PointF pts[] = {PointF(X(19), Y(10)), PointF(X(19), Y(20.5f)), PointF(X(16.6f), Y(18.5f)), PointF(X(14.2f), Y(20.5f)),
                        PointF(X(12), Y(18.5f)), PointF(X(9.8f), Y(20.5f)), PointF(X(7.4f), Y(18.5f)), PointF(X(5), Y(20.5f)), PointF(X(5), Y(10))};
        gp.AddLines(pts, 9); gp.CloseFigure(); g.DrawPath(&p, &gp);
        dot(9.5f, 10.5f, 1.4f); dot(14.5f, 10.5f, 1.4f);
        break;
    }
    case IC_SHIELD: {
        GraphicsPath gp;
        gp.AddLine(X(12), Y(2.5f), X(20), Y(5.5f)); gp.AddLine(X(20), Y(5.5f), X(20), Y(11.5f));
        gp.AddBezier(X(20), Y(11.5f), X(20), Y(16.5f), X(16.5f), Y(19.8f), X(12), Y(21.8f));
        gp.AddBezier(X(12), Y(21.8f), X(7.5f), Y(19.8f), X(4), Y(16.5f), X(4), Y(11.5f));
        gp.AddLine(X(4), Y(11.5f), X(4), Y(5.5f)); gp.CloseFigure(); g.DrawPath(&p, &gp);
        poly({8.5f, 12, 11, 14.5f, 15.5f, 9.5f}, false);
        break;
    }
    case IC_SETTINGS:
        circ(12, 12, 3.2f); circ(12, 12, 7.2f);
        for (int i = 0; i < 8; ++i) { float a = i * 3.14159f / 4; L(12 + cosf(a) * 7.2f, 12 + sinf(a) * 7.2f, 12 + cosf(a) * 10, 12 + sinf(a) * 10); }
        break;
    case IC_HISTORY: circ(12, 12, 9.5f); poly({12, 6.5f, 12, 12, 15.8f, 14.2f}, false); break;
    case IC_TEMP: L(6, 3, 18, 3); L(6, 21, 18, 21); poly({7.5f, 3, 7.5f, 7, 12, 12, 7.5f, 17, 7.5f, 21}, false); poly({16.5f, 3, 16.5f, 7, 12, 12, 16.5f, 17, 16.5f, 21}, false); break;
    case IC_WINTEMP: rrect(3, 4, 18, 16, 2.5f); L(3, 8.5f, 21, 8.5f); poly({12, 11, 12, 14.5f, 14.5f, 16}, false); break;
    case IC_GLOBE: circ(12, 12, 9.5f); g.DrawEllipse(&p, X(8), Y(2.5f), 8 * u, 19 * u); L(2.5f, 12, 21.5f, 12); break;
    case IC_APPS: circ(7, 7, 3.4f); circ(17, 7, 3.4f); circ(7, 17, 3.4f); circ(17, 17, 3.4f); break;
    case IC_CODE: poly({8, 6, 2.5f, 12, 8, 18}, false); poly({16, 6, 21.5f, 12, 16, 18}, false); L(14, 4, 10, 20); break;
    case IC_UPDATE: arc(12, 12, 8.5f, -40, 290); poly({20.5f, 3.5f, 19.4f, 7.2f, 15.6f, 6.2f}, false); break;
    case IC_BUG: circ(12, 12, 9.5f); L(12, 7, 12, 13); dot(12, 16.6f, 1.3f); break;
    case IC_IMAGE: rrect(3, 4, 18, 16, 2.5f); circ(8.5f, 9.5f, 1.8f); poly({3, 18, 9, 12.5f, 14, 17, 17, 14, 21, 18}, false); break;
    case IC_DOC: poly({6, 2.5f, 14, 2.5f, 19, 7.5f, 19, 21.5f, 6, 21.5f}, true); poly({14, 2.5f, 14, 7.5f, 19, 7.5f}, false); L(9, 12, 16, 12); L(9, 16, 16, 16); break;
    case IC_FILE: poly({6, 2.5f, 14, 2.5f, 19, 7.5f, 19, 21.5f, 6, 21.5f}, true); poly({14, 2.5f, 14, 7.5f, 19, 7.5f}, false); break;
    case IC_WINDOW: rrect(3, 4, 18, 16, 2.5f); L(3, 9, 21, 9); dot(6.2f, 6.5f, 0.9f); dot(8.8f, 6.5f, 0.9f); break;
    case IC_TRASH: L(3.5f, 6, 20.5f, 6); poly({5.5f, 6, 6.5f, 21, 17.5f, 21, 18.5f, 6}, false); poly({9, 6, 9, 3, 15, 3, 15, 6}, false); L(10, 10, 10, 17); L(14, 10, 14, 17); break;
    case IC_FOLDER: poly({3, 6, 3, 19.5f, 21, 19.5f, 21, 8.5f, 11.5f, 8.5f, 9.5f, 5, 3, 5}, true); break;
    case IC_PROJECT: poly({3, 6, 3, 19.5f, 21, 19.5f, 21, 8.5f, 11.5f, 8.5f, 9.5f, 5, 3, 5}, true); poly({10, 11.5f, 8, 14, 10, 16.5f}, false); poly({14, 11.5f, 16, 14, 14, 16.5f}, false); break;
    case IC_BIG: poly({12, 3, 21, 8, 12, 13, 3, 8}, true); poly({3, 12.5f, 12, 17.5f, 21, 12.5f}, false); poly({3, 16.5f, 12, 21.5f, 21, 16.5f}, false); break;
    case IC_DUP: rrect(8.5f, 8.5f, 12.5f, 12.5f, 2); poly({15.5f, 8.5f, 15.5f, 3, 3, 3, 3, 15.5f, 8.5f, 15.5f}, false); break;
    case IC_DOWNLOAD: L(12, 3, 12, 15); poly({7, 10, 12, 15, 17, 10}, false); poly({4, 16.5f, 4, 21, 20, 21, 20, 16.5f}, false); break;
    case IC_LOCK: rrect(5, 11, 14, 10, 2.5f); arc(12, 8.5f, 4, 180, 180); L(8, 8.5f, 8, 11); L(16, 8.5f, 16, 11); break;
    case IC_CHECK: poly({5, 12.5f, 10, 17.5f, 19.5f, 7}, false); break;
    case IC_REGISTRY: poly({12, 2.5f, 21, 7, 21, 17, 12, 21.5f, 3, 17, 3, 7}, true); poly({3, 7, 12, 12, 21, 7}, false); L(12, 12, 12, 21.5f); break;
    case IC_EXTERNAL: poly({14, 3, 21, 3, 21, 10}, false); L(21, 3, 11, 13); poly({18, 14, 18, 21, 3, 21, 3, 6, 10, 6}, false); break;
    case IC_BACK: poly({15, 5, 8, 12, 15, 19}, false); break;
    case IC_WARNING: poly({12, 3, 22, 20.5f, 2, 20.5f}, true); L(12, 9.5f, 12, 14); dot(12, 17.3f, 1.2f); break;
    case IC_BOLT: poly({13, 2, 4, 13.5f, 11, 13.5f, 10, 22, 20, 9.5f, 13, 9.5f}, true); break;
    case IC_SPARKLE: {
        PointF pts[8];
        for (int i = 0; i < 8; ++i) { float a = -3.14159f / 2 + i * 3.14159f / 4; float r = (i % 2 == 0) ? 9.5f : 2.6f; pts[i] = PointF(X(11 + cosf(a) * r), Y(13 + sinf(a) * r)); }
        g.DrawPolygon(&p, pts, 8);
        L(19.5f, 2.5f, 19.5f, 6.5f); L(17.5f, 4.5f, 21.5f, 4.5f);
        break;
    }
    case IC_SEARCH: circ(10.5f, 10.5f, 6.8f); L(15.5f, 15.5f, 21, 21); break;
    case IC_X: L(6, 6, 18, 18); L(18, 6, 6, 18); break;
    case IC_SORT: L(4, 7, 20, 7); L(4, 12, 15, 12); L(4, 17, 10, 17); break;
    case IC_DRIVE: rrect(2.5f, 7, 19, 10, 2.5f); dot(17, 12, 1.2f); L(6, 12, 11, 12); break;
    case IC_INFO: circ(12, 12, 9.5f); L(12, 11, 12, 16.5f); dot(12, 7.6f, 1.2f); break;
    case IC_PLUS: L(12, 5, 12, 19); L(5, 12, 19, 12); break;
    case IC_MINUS: L(5, 12, 19, 12); break;
    default: break;
    }
}

static void star(Graphics& g, float cx, float cy, float r, const Color& c) {
    PointF pts[8];
    for (int i = 0; i < 8; ++i) { float a = -3.14159f / 2 + i * 3.14159f / 4; float rr = (i % 2 == 0) ? r : r * 0.3f; pts[i] = PointF(cx + cosf(a) * rr, cy + sinf(a) * rr); }
    SolidBrush b(c); g.FillPolygon(&b, pts, 8);
}
static void logo(Graphics& g, float x, float y, float s) {
    RectF r(x, y, s, s);
    {   // brillo
        GraphicsPath gp; RectF gr = r; gr.Inflate(s * 0.35f, s * 0.35f); gp.AddEllipse(gr);
        PathGradientBrush pb(&gp); pb.SetCenterColor(CA(ACC1, 70)); Color sur = CA(ACC1, 0); INT n = 1; pb.SetSurroundColors(&sur, &n);
        g.FillPath(&pb, &gp);
    }
    LinearGradientBrush lb(r, C(ACC1), C(ACC2), 45.f);
    fillRound(g, r, s * 0.28f, lb);
    LinearGradientBrush sheen(RectF(x, y, s, s * 0.55f), Color(70, 255, 255, 255), Color(0, 255, 255, 255), LinearGradientModeVertical);
    fillRound(g, RectF(x, y, s, s * 0.55f), s * 0.28f, sheen);
    Pen p(Color(240, 255, 255, 255), s * 0.09f);
    p.SetStartCap(LineCapRound); p.SetEndCap(LineCapRound);
    float m = s * 0.22f;
    g.DrawArc(&p, RectF(x + m, y + m, s - 2 * m, s - 2 * m), 135, 270);
    star(g, x + s * 0.5f, y + s * 0.5f, s * 0.17f, Color(255, 255, 255, 255));
}

// --------------------------------------------------------------- componentes
static void shadow(Graphics& g, const RectF& r, float rad, float k = 1.f) {
    for (int i = 1; i <= 4; ++i) {
        RectF s = r; s.Inflate(px(i * 1.6f), px(i * 1.6f)); s.Offset(0, px(i * 1.6f));
        fillRoundC(g, s, rad + px(i * 1.6f), Color((BYTE)(22 * k / i), 0, 0, 0));
    }
}
static void card(Graphics& g, const RectF& r, float rad, float hover = 0.f, bool solid = false) {
    shadow(g, r, rad, solid ? 2.f : 1.f);
    fillRoundC(g, r, rad, solid ? Color(250, 17, 21, 31) : Color(205, 14, 18, 27));
    LinearGradientBrush lb(PointF(r.X, r.Y - 1), PointF(r.X, r.Y + r.Height + 1),
                           Color((BYTE)(22 + hover * 12), 255, 255, 255), Color((BYTE)(7 + hover * 6), 255, 255, 255));
    fillRound(g, r, rad, lb);
    Pen pen(Color((BYTE)(24 + hover * 36), 255, 255, 255), px(1));
    strokeRound(g, r, rad, pen);
}
static void glowCircle(Graphics& g, float cx, float cy, float r, ARGB c, float a) {
    GraphicsPath gp; gp.AddEllipse(cx - r, cy - r, 2 * r, 2 * r);
    PathGradientBrush pb(&gp); pb.SetCenterColor(CA(c, a)); Color sur = CA(c, 0); INT n = 1; pb.SetSurroundColors(&sur, &n);
    g.FillPath(&pb, &gp);
}
static void iconBadge(Graphics& g, int ic, float cx, float cy, float r, ARGB c) {
    fillRoundC(g, RectF(cx - r, cy - r, 2 * r, 2 * r), r * 0.62f, CA(c, 38));
    Pen pen(CA(c, 60), px(1)); strokeRound(g, RectF(cx - r, cy - r, 2 * r, 2 * r), r * 0.62f, pen);
    icon(g, ic, cx, cy, r * 1.05f, c);
}

enum { B_PRIMARY, B_SECONDARY, B_DANGER, B_GHOST, B_SUCCESS };
static void button(Graphics& g, const RectF& r, const wstring& label, int id, int data, int style, int ic = IC_NONE,
                   bool enabled = true, float fs = 13.5f) {
    float h = enabled ? anim(K(id, data), hov(id, data) ? 1.f : 0.f, 18.f) : 0.f;
    RectF rr = r;
    if (enabled && prs(id, data)) rr.Offset(0, px(1));
    float rad = std::min(px(11), rr.Height / 2);
    float al = enabled ? 255.f : 95.f;
    ARGB tc = C_TEXT;
    switch (style) {
    case B_PRIMARY: case B_DANGER: case B_SUCCESS: {
        ARGB a = style == B_PRIMARY ? ACC1 : style == B_DANGER ? 0xFFFB7185 : 0xFF4ADE80;
        ARGB b = style == B_PRIMARY ? ACC2 : style == B_DANGER ? 0xFFE11D48 : 0xFF14B8A6;
        if (h > 0.01f) { RectF gr = rr; gr.Inflate(px(5) * h, px(5) * h); fillRoundC(g, gr, rad + px(5) * h, CA(a, 45 * h)); }
        LinearGradientBrush lb(rr, CA(a, al), CA(b, al), LinearGradientModeHorizontal);
        fillRound(g, rr, rad, lb);
        LinearGradientBrush sh(RectF(rr.X, rr.Y - 1, rr.Width, rr.Height / 2 + 2), Color((BYTE)(40 + 30 * h), 255, 255, 255), Color(0, 255, 255, 255), LinearGradientModeVertical);
        fillRound(g, RectF(rr.X, rr.Y, rr.Width, rr.Height / 2), rad, sh);
        tc = style == B_DANGER ? 0xFFFFFFFF : C_DARK;
        break;
    }
    case B_SECONDARY: {
        fillRoundC(g, rr, rad, Color((BYTE)((16 + 14 * h) * al / 255), 255, 255, 255));
        Pen pen(Color((BYTE)((34 + 40 * h) * al / 255), 255, 255, 255), px(1));
        strokeRound(g, rr, rad, pen);
        break;
    }
    case B_GHOST:
        if (h > 0.01f) fillRoundC(g, rr, rad, Color((BYTE)(18 * h), 255, 255, 255));
        tc = mix(C_DIM, C_TEXT, h);
        break;
    }
    float tw = textW(g, label, fs, F_SEMI);
    float isz = px(16), gap = ic != IC_NONE && !label.empty() ? px(8) : 0;
    float total = tw + (ic != IC_NONE ? isz + gap : 0);
    float x = rr.X + (rr.Width - total) / 2;
    if (ic != IC_NONE) { icon(g, ic, x + isz / 2, rr.Y + rr.Height / 2, isz, tc, al); x += isz + gap; }
    text(g, label, RectF(x - px(1), rr.Y, tw + px(4), rr.Height), fs, CA(tc, al), F_SEMI, AL_L, 1, TR_NONE);
    if (enabled) addHot(r, id, data);
}
static float chip(Graphics& g, float x, float y, const wstring& s, ARGB c, float fs = 11.5f, int ic = IC_NONE, bool rightAlign = false) {
    float tw = textW(g, s, fs, F_SEMI);
    float h = px(24), w = tw + px(20) + (ic != IC_NONE ? px(18) : 0);
    if (rightAlign) x -= w;
    RectF r(x, y, w, h);
    fillRoundC(g, r, h / 2, CA(c, 32));
    Pen p(CA(c, 70), px(1)); strokeRound(g, r, h / 2, p);
    float tx = x + px(10);
    if (ic != IC_NONE) { icon(g, ic, tx + px(6), y + h / 2, px(13), c); tx += px(18); }
    text(g, s, RectF(tx, y, tw + px(4), h), fs, C(c), F_SEMI, AL_L, 1, TR_NONE);
    return w;
}
static void checkbox(Graphics& g, float x, float y, float s, bool on, float hover, bool disabled, int key) {
    RectF r(x, y, s, s);
    float t = anim(K(77000, key), on ? 1.f : 0.f, 20.f);
    if (disabled) { fillRoundC(g, r, px(6), Color(10, 255, 255, 255)); icon(g, IC_LOCK, x + s / 2, y + s / 2, s * 0.7f, C_DANGER, 200); return; }
    fillRoundC(g, r, px(6), Color((BYTE)(14 + 18 * hover), 255, 255, 255));
    Pen p(Color((BYTE)(70 + 60 * hover), 255, 255, 255), px(1.4f)); strokeRound(g, r, px(6), p);
    if (t > 0.01f) {
        LinearGradientBrush lb(r, CA(ACC1, 255 * t), CA(ACC2, 255 * t), 45.f);
        fillRound(g, r, px(6), lb);
        icon(g, IC_CHECK, x + s / 2, y + s / 2, s * 0.78f, C_DARK, 255 * t);
    }
}
static void ring(Graphics& g, float cx, float cy, float r, float thick, float frac, ARGB c1, ARGB c2) {
    RectF rr(cx - r, cy - r, 2 * r, 2 * r);
    Pen tp(Color(22, 255, 255, 255), thick); g.DrawEllipse(&tp, rr);
    frac = std::clamp(frac, 0.f, 1.f);
    if (frac > 0.002f) {
        LinearGradientBrush lb(rr, C(c1), C(c2), 45.f);
        Pen p(&lb, thick); p.SetStartCap(LineCapRound); p.SetEndCap(LineCapRound);
        g.DrawArc(&p, rr, -90.f, 360.f * frac);
    }
}
static void spinner(Graphics& g, float cx, float cy, float r, float thick, ARGB c) {
    float a = (float)fmod(GetTickCount64() * 0.36, 360.0);
    Pen tp(Color(20, 255, 255, 255), thick); g.DrawEllipse(&tp, cx - r, cy - r, 2 * r, 2 * r);
    Pen p(C(c), thick); p.SetStartCap(LineCapRound); p.SetEndCap(LineCapRound);
    g.DrawArc(&p, cx - r, cy - r, 2 * r, 2 * r, a, 100);
    g_needAnim = true;
}
static void progressBar(Graphics& g, const RectF& r, float frac, ARGB a, ARGB b) {
    fillRoundC(g, r, r.Height / 2, Color(18, 255, 255, 255));
    frac = std::clamp(frac, 0.f, 1.f);
    if (frac > 0) {
        RectF f(r.X, r.Y, std::max(r.Height, r.Width * frac), r.Height);
        LinearGradientBrush lb(r, C(a), C(b), LinearGradientModeHorizontal);
        fillRound(g, f, r.Height / 2, lb);
    }
}
static void toggleSwitch(Graphics& g, const RectF& r, bool on, int id, int data) {
    float t = anim(K(id, data + 5000), on ? 1.f : 0.f, 16.f);
    if (t > 0.01f) { LinearGradientBrush lb(r, CA(ACC1, 255 * t), CA(ACC2, 255 * t), LinearGradientModeHorizontal); fillRound(g, r, r.Height / 2, lb); }
    fillRoundC(g, r, r.Height / 2, Color((BYTE)(30 * (1 - t)), 255, 255, 255));
    Pen p(Color(50, 255, 255, 255), px(1)); strokeRound(g, r, r.Height / 2, p);
    float d = r.Height - px(6);
    float x = r.X + px(3) + (r.Width - d - px(6)) * t;
    fillRoundC(g, RectF(x, r.Y + px(3), d, d), d / 2, Color(255, 255, 255, 255));
    addHot(r, id, data);
}
static void inputBox(Graphics& g, const RectF& r, HWND ed, int focusData) {
    bool f = GetFocus() == ed;
    float t = anim(K(A_FOCUS_EDIT, focusData + 100), f ? 1.f : 0.f, 16.f);
    if (t > 0.01f) { RectF gr = r; gr.Inflate(px(3), px(3)); fillRoundC(g, gr, px(14), CA(ACC1, 40 * t)); }
    fillRoundC(g, r, px(12), C(C_INPUT));
    Pen p(t > 0.01f ? CA(mix(0xFFFFFFFF, ACC1, t), 60 + 160 * t) : Color(40, 255, 255, 255), px(1.2f));
    strokeRound(g, r, px(12), p);
    addHot(r, A_FOCUS_EDIT, focusData, false);
}
static void levelTag(int lv, wstring& s, ARGB& c) {
    if (lv == LV_SAFE) { s = L"Seguro"; c = C_OK; }
    else if (lv == LV_REVIEW) { s = L"Revisar"; c = C_WARN; }
    else { s = L"Personal"; c = C_DANGER; }
}
static wstring fmtTime(ULONGLONG ms) {
    unsigned s = (unsigned)(ms / 1000);
    wchar_t b[32]; swprintf(b, 32, L"%02u:%02u", s / 60, s % 60);
    return b;
}
static void toast(const wstring& s, ARGB c = 0) { g_toast = s; g_toastColor = c ? c : ACC1; g_toastAt = GetTickCount64(); animSet(KEY_TOAST, 0); invalidate(); }

// ===================================================================
//  Lógica de navegación y trabajos
// ===================================================================
static void refreshStats() {
    g_drives = drives();
    historyStats(g_histFreed, g_histCleanups, g_histItems);
}
static void navigate(int page) {
    if (page == g_page) return;
    g_page = page;
    animSet(KEY_PAGE, 0);
    g_scroll = g_scrollTarget = 0;
    SetFocus(g_hwnd);
    if (page == PG_HOME) { refreshStats(); animSet(KEY_GAUGE, 0); animSet(KEY_RECOV, 0); }
    if (page == PG_HISTORY) { g_log = readLog(3000); historyStats(g_histFreed, g_histCleanups, g_histItems); }
    if (page == PG_RESULTS) animSet(KEY_RECOV, 0);
    if (page == PG_SEARCH) SetFocus(g_edSearch);
    invalidate();
}
static wstring itemName(const Item& it) {
    if (it.kind == Kind::RecycleBin) return L"Papelera de reciclaje";
    if (it.kind == Kind::RegValue) return it.valueName.empty() ? L"(predeterminado)" : it.valueName;
    return nameOf(it.path);
}
static wstring itemParent(const Item& it) {
    if (it.kind == Kind::RecycleBin) return it.path;
    return parentOf(it.path);
}
static void rebuildView() {
    g_view.clear();
    g_viewMax = 1;
    if (!g_detail) return;
    auto& items = g_detail->items;
    for (int i = 0; i < (int)items.size(); ++i) {
        if (!g_filter.empty()) {
            if (lower(items[i].path).find(g_filter) == wstring::npos && lower(items[i].note).find(g_filter) == wstring::npos) continue;
        }
        g_view.push_back(i);
        if (items[i].blocked.empty()) g_viewMax = std::max<uint64_t>(g_viewMax, items[i].size);
    }
    if (g_sortMode == 1) {
        std::vector<wstring> keys(items.size());
        for (int i : g_view) keys[i] = lower(itemName(items[i]));
        std::stable_sort(g_view.begin(), g_view.end(), [&](int a, int b) { return keys[a] < keys[b]; });
    } else {
        std::stable_sort(g_view.begin(), g_view.end(), [&](int a, int b) {
            bool ab = !items[a].blocked.empty(), bb = !items[b].blocked.empty();
            if (ab != bb) return bb;
            return items[a].size > items[b].size;
        });
    }
    g_lastClick = -1;
}
static void openDetail(Category* c, bool fromSearch) {
    g_detail = c;
    g_detailIsSearch = fromSearch;
    g_sortMode = 0;
    g_filter.clear();
    SetWindowTextW(g_edFilter, L"");
    rebuildView();
    g_page = -1;
    navigate(PG_DETAIL);
}
static void joinWorker() { if (g_worker.joinable()) g_worker.join(); }

static void startScan(bool quick) {
    if (g_busy) { navigate(PG_SCAN); return; }
    joinWorker();
    g_busy = true; g_busyKind = 0; g_lastQuick = quick;
    g_work.stop = false; g_work.count = 0;
    { std::lock_guard<std::mutex> l(g_work.m); g_work.steps.clear(); g_work.current.clear(); }
    g_workStart = GetTickCount64();
    navigate(PG_SCAN);
    g_worker = std::thread([quick] {
        auto r = scanAll(quick);
        { std::lock_guard<std::mutex> l(g_resMx); g_resCats = std::move(r); }
        PostMessageW(g_hwnd, WM_SCAN_DONE, 0, 0);
    });
}
static void runSearch(const wstring& term) {
    if (g_busy) return;
    joinWorker();
    g_busy = true; g_busyKind = 1;
    g_searchTerm = trim(term);
    g_work.stop = false; g_work.count = 0;
    { std::lock_guard<std::mutex> l(g_work.m); g_work.steps.clear(); g_work.current.clear(); }
    g_workStart = GetTickCount64();
    navigate(PG_SCAN);
    wstring t = g_searchTerm;
    g_worker = std::thread([t] {
        bool trunc = false;
        Category c = searchProgram(t, trunc);
        { std::lock_guard<std::mutex> l(g_resMx); g_resSearch = std::move(c); g_resTrunc = trunc; }
        PostMessageW(g_hwnd, WM_SEARCH_DONE, 0, 0);
    });
}
static wstring editText(HWND e) {
    int n = GetWindowTextLengthW(e);
    wstring s(n + 1, L'\0');
    GetWindowTextW(e, &s[0], n + 1);
    s.resize(n);
    return s;
}
static void showInfo(const wstring& t, const wstring& s) { g_infoTitle = t; g_infoText = s; g_modal = MD_INFO; animSet(KEY_MODAL, 0); invalidate(); }
static void searchGo(bool skipInstalled) {
    wstring term = trim(editText(g_edSearch));
    if (term.empty()) { SetFocus(g_edSearch); toast(L"Escribe el nombre del programa que quieres borrar", C_WARN); return; }
    wstring why;
    if (!validTerm(term, why)) { showInfo(L"Nombre no válido", why); return; }
    g_searchTerm = term;
    if (!skipInstalled) {
        g_installed = installedMatching(term);
        if (!g_installed.empty()) { g_modal = MD_INSTALLED; animSet(KEY_MODAL, 0); invalidate(); return; }
    }
    runSearch(term);
}
// Confirmación: lo que se va a borrar, agrupado por su origen (categoría) y con el motivo
struct CGroup { wstring name, why; int icon = IC_NONE; uint32_t color = 0; int level = 0; std::vector<Item*> items; uint64_t size = 0, keptFiles = 0, keptBytes = 0; };
struct CRow { bool header; int group; Item* it; float y, h; };  // y/h en unidades lógicas
static std::vector<CGroup> g_cgroups;
static std::vector<CRow> g_crows;
static float g_crowsH = 0;
static float g_mScroll = 0, g_mScrollTarget = 0, g_mScrollMax = 0, g_mScrollRatio = 1;
static bool g_dragModal = false;

static void askDelete(const std::vector<std::pair<Category*, std::vector<Item*>>>& src, bool fromSearch) {
    g_cgroups.clear(); g_crows.clear(); g_pendingDel.clear();
    bool strong = false;
    for (auto& s : src) {
        if (s.second.empty()) continue;
        CGroup gr;
        gr.name = s.first->name; gr.why = s.first->desc; gr.icon = s.first->icon; gr.color = s.first->color; gr.level = s.first->level;
        for (Item* i : s.second) {
            gr.items.push_back(i); gr.size += i->size; gr.keptFiles += i->keptFiles; gr.keptBytes += i->keptBytes;
            g_pendingDel.push_back(i);
        }
        std::stable_sort(gr.items.begin(), gr.items.end(), [](Item* a, Item* b) { return a->size > b->size; });
        if (gr.level != LV_SAFE) strong = true;
        g_cgroups.push_back(std::move(gr));
    }
    if (g_pendingDel.empty()) { toast(L"No hay nada seleccionado", C_WARN); return; }
    std::stable_sort(g_cgroups.begin(), g_cgroups.end(), [](const CGroup& a, const CGroup& b) { return a.size > b.size; });
    float y = 0;
    for (size_t gi = 0; gi < g_cgroups.size(); ++gi) {
        float hh = (gi ? 12.f : 0.f) + (g_cgroups[gi].keptFiles ? 96.f : 76.f);
        g_crows.push_back({true, (int)gi, nullptr, y, hh}); y += hh;
        for (Item* it : g_cgroups[gi].items) { g_crows.push_back({false, (int)gi, it, y, 64.f}); y += 64.f; }
    }
    g_crowsH = y + 8.f;
    g_mScroll = g_mScrollTarget = 0;
    g_confirmStrong = strong;
    g_delFromSearch = fromSearch;
    SetWindowTextW(g_edConfirm, L"");
    g_modal = MD_CONFIRM;
    animSet(KEY_MODAL, 0);
    SetFocus(g_edConfirm);
    invalidate();
}
static void safeCleanAsk() {
    if (!g_scanned) { g_safeAfterScan = true; startScan(true); return; }
    std::vector<std::pair<Category*, std::vector<Item*>>> src;
    for (auto& c : g_cats) {
        if (c.level != LV_SAFE) continue;
        std::vector<Item*> v;
        for (auto& i : c.items) if (i.blocked.empty()) v.push_back(&i);
        if (!v.empty()) src.push_back({&c, v});
    }
    if (src.empty()) { toast(L"No quedan temporales ni cachés que limpiar", C_OK); return; }
    askDelete(src, false);
}
static void startDelete() {
    if (g_busy || g_pendingDel.empty()) return;
    joinWorker();
    g_busy = true; g_busyKind = 2;
    g_work.stop = false;
    g_work.delIndex = 0; g_work.delTotal = g_pendingDel.size();
    g_workStart = GetTickCount64();
    g_modal = MD_DELETING; animSet(KEY_MODAL, 0);
    SetFocus(g_hwnd);
    g_worker = std::thread([] {
        DelResult r = runDelete(g_pendingDel);
        { std::lock_guard<std::mutex> l(g_resMx); g_resDel = std::move(r); }
        PostMessageW(g_hwnd, WM_DEL_DONE, 0, 0);
    });
    invalidate();
}
static uint64_t safeTotal() { uint64_t t = 0; for (auto& c : g_cats) if (c.level == LV_SAFE) t += c.total(); return t; }
static uint64_t allTotal() { uint64_t t = 0; for (auto& c : g_cats) t += c.total(); return t; }

// ===================================================================
//  Fondo
// ===================================================================
static void freeBg() { delete g_bgCached; g_bgCached = nullptr; delete g_bgBmp; g_bgBmp = nullptr; }
static void buildBg() {
    freeBg();
    g_bgBmp = new Bitmap(W, H, PixelFormat32bppPARGB);
    Graphics g(g_bgBmp);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.Clear(Color(255, 9, 12, 18));
    float m = (float)std::max(W, H);
    glowCircle(g, W * 0.86f, -H * 0.08f, m * 0.55f, ACC1, 48);
    glowCircle(g, W * 0.28f, H * 1.08f, m * 0.55f, ACC2, 42);
    glowCircle(g, W * 0.58f, H * 0.42f, m * 0.32f, 0xFF8B5CF6, 14);
    float sw = px(SIDEBAR);
    SolidBrush sb(Color(165, 8, 10, 16)); g.FillRectangle(&sb, 0.f, 0.f, sw, (REAL)H);
    Pen ln(Color(20, 255, 255, 255), px(1)); g.DrawLine(&ln, sw - 0.5f, 0.f, sw - 0.5f, (REAL)H);
    SolidBrush dot(Color(9, 255, 255, 255));
    float step = px(26);
    for (float y = px(14); y < H; y += step)
        for (float x = sw + px(14); x < W; x += step) g.FillEllipse(&dot, x, y, px(1.7f), px(1.7f));
}

// ===================================================================
//  Barra lateral
// ===================================================================
static void drawSidebar(Graphics& g) {
    float sw = px(SIDEBAR);
    logo(g, px(24), px(26), px(42));
    text(g, L"Optimizalis", RectF(px(78), px(26), sw - px(84), px(26)), 19.5f, C(C_TEXT), F_SEMI);
    text(g, L"Optimizador de espacio", RectF(px(78), px(51), sw - px(84), px(18)), 11.5f, C(C_DIM));

    struct NavItem { int page; int ic; wstring label; };
    bool working = g_busy && g_busyKind != 2;
    std::vector<NavItem> nav = {
        {PG_HOME, IC_HOME, L"Inicio"},
        {working ? PG_SCAN : PG_RESULTS, working ? IC_RADAR : IC_GRID, working ? (g_busyKind == 1 ? L"Buscando…" : L"Analizando…") : L"Resultados"},
        {PG_SEARCH, IC_GHOST, L"Restos de programas"},
        {PG_PROTECT, IC_SHIELD, L"Protección"},
        {PG_HISTORY, IC_HISTORY, L"Historial"},
        {PG_SETTINGS, IC_SETTINGS, L"Ajustes"}};
    text(g, L"MENÚ", RectF(px(28), px(100), px(100), px(16)), 10.5f, C(C_FAINT), F_SEMI);
    float y = px(122);
    for (size_t i = 0; i < nav.size(); ++i) {
        auto& n = nav[i];
        bool active = g_page == n.page || (g_page == PG_DETAIL && ((g_detailIsSearch && n.page == PG_SEARCH) || (!g_detailIsSearch && n.page == PG_RESULTS))) ||
                      (g_page == PG_SCAN && n.page == PG_SCAN);
        RectF r(px(14), y, sw - px(28), px(44));
        float h = anim(K(A_NAV, (int)i + 100), hov(A_NAV, n.page) ? 1.f : 0.f, 18.f);
        float a = anim(K(A_NAV, (int)i + 200), active ? 1.f : 0.f, 14.f);
        if (a > 0.01f) {
            LinearGradientBrush lb(r, CA(ACC1, 46 * a), CA(ACC2, 14 * a), LinearGradientModeHorizontal);
            fillRound(g, r, px(12), lb);
            Pen pen(CA(ACC1, 60 * a), px(1)); strokeRound(g, r, px(12), pen);
            fillRoundC(g, RectF(r.X + px(1), r.Y + px(12), px(3.5f), r.Height - px(24)), px(2), CA(ACC1, 255 * a));
        }
        if (h > 0.01f) fillRoundC(g, r, px(12), Color((BYTE)(14 * h), 255, 255, 255));
        ARGB ic = mix(C_DIM, ACC1, a);
        if (n.page == PG_SCAN) spinner(g, r.X + px(25), r.Y + r.Height / 2, px(8), px(2.2f), ACC1);
        else icon(g, n.ic, r.X + px(25), r.Y + r.Height / 2, px(19), mix(ic, C_TEXT, h * (1 - a)));
        text(g, n.label, RectF(r.X + px(48), r.Y, r.Width - px(54), r.Height), 13.5f, C(mix(C_DIM, C_TEXT, std::max(a, h))), a > 0.5f ? F_SEMI : F_REG);
        if (n.page == PG_RESULTS && g_scanned) {
            wstring s = fmtSize(allTotal());
            float tw = textW(g, s, 10.5f, F_SEMI);
            RectF cr(r.X + r.Width - tw - px(22), r.Y + px(12), tw + px(14), px(20));
            fillRoundC(g, cr, px(10), CA(ACC1, 30));
            text(g, s, cr, 10.5f, C(ACC1), F_SEMI, AL_C, 1, TR_NONE);
        }
        addHot(r, A_NAV, n.page);
        y += px(48);
    }

    // Unidades
    float by = H - px(96) - (float)std::min<size_t>(g_drives.size(), 3) * px(46);
    text(g, L"UNIDADES", RectF(px(28), by - px(24), px(120), px(16)), 10.5f, C(C_FAINT), F_SEMI);
    for (size_t i = 0; i < g_drives.size() && i < 3; ++i) {
        auto& d = g_drives[i];
        float used = 1.f - (float)d.freeB / (float)d.total;
        ARGB c = used > 0.9f ? C_DANGER : used > 0.75f ? C_WARN : ACC1;
        icon(g, IC_DRIVE, px(36), by + px(10), px(17), C_DIM);
        text(g, d.root.substr(0, 2), RectF(px(52), by, px(40), px(20)), 12.5f, C(C_TEXT), F_SEMI);
        text(g, fmtSize(d.freeB) + L" libres", RectF(px(84), by, sw - px(112), px(20)), 11.5f, C(C_DIM), F_REG, AL_R);
        float f = anim(K(88000, (int)i), used, 6.f);
        progressBar(g, RectF(px(28), by + px(25), sw - px(56), px(5)), f, c, mix(c, ACC2, 0.5f));
        by += px(46);
    }
    // Permisos
    RectF ar(px(14), H - px(80), sw - px(28), px(60));
    fillRoundC(g, ar, px(14), Color(14, 255, 255, 255));
    Pen pen(Color(22, 255, 255, 255), px(1)); strokeRound(g, ar, px(14), pen);
    ARGB ac = g_admin ? C_OK : C_WARN;
    iconBadge(g, g_admin ? IC_SHIELD : IC_BOLT, ar.X + px(28), ar.Y + ar.Height / 2, px(15), ac);
    text(g, g_admin ? L"Administrador" : L"Modo usuario", RectF(ar.X + px(52), ar.Y + px(10), ar.Width - px(60), px(20)), 13.f, C(C_TEXT), F_SEMI);
    if (g_admin) text(g, L"Acceso completo", RectF(ar.X + px(52), ar.Y + px(30), ar.Width - px(60), px(18)), 11.5f, C(C_DIM));
    else {
        bool h = hov(A_ELEVATE);
        text(g, L"Pulsa para elevar permisos", RectF(ar.X + px(52), ar.Y + px(30), ar.Width - px(60), px(18)), 11.5f, C(h ? ACC1 : C_DIM));
        addHot(ar, A_ELEVATE);
    }
}

// ===================================================================
//  Páginas
// ===================================================================
static RectF innerRect(const RectF& c) { return RectF(c.X + px(40), px(116), c.Width - px(80), H - px(116) - px(30)); }
static void header(Graphics& g, const RectF& c, const wstring& title, const wstring& sub, float rightReserve = 0) {
    float x = c.X + px(40), w = c.Width - px(80) - rightReserve;
    text(g, title, RectF(x, px(30), w, px(42)), 28.f, C(C_TEXT), F_SEMI);
    text(g, sub, RectF(x, px(72), w, px(22)), 13.5f, C(C_DIM));
}
static void scrollbar(Graphics& g, const RectF& view, float contentH) {
    g_scrollMax = std::max(0.f, contentH - view.Height);
    if (g_scrollMax <= 0) return;
    float th = std::max(px(40), view.Height * view.Height / contentH);
    float ty = view.Y + (view.Height - th) * (g_scroll / g_scrollMax);
    RectF tr(view.X + view.Width - px(7), ty, px(5), th);
    bool h = hov(A_SCROLLTHUMB) || g_dragThumb;
    fillRoundC(g, tr, px(3), Color(h ? 110 : 55, 255, 255, 255));
    RectF hit(tr.X - px(6), tr.Y, px(16), tr.Height);
    addHot(hit, A_SCROLLTHUMB, 0, false);
    g_scrollRatio = g_scrollMax / std::max(1.f, view.Height - th);
}

// --------------------------------------------------------------- Inicio
static void pageHome(Graphics& g, const RectF& c) {
    SYSTEMTIME st; GetLocalTime(&st);
    wstring hi = st.wHour < 14 ? L"Buenos días" : st.wHour < 21 ? L"Buenas tardes" : L"Buenas noches";
    header(g, c, hi, L"Mantén tu equipo ligero: analiza, revisa y libera espacio sin tocar lo esencial.");
    RectF in = innerRect(c);
    float x = in.X, y = in.Y, w = in.Width;

    // Tarjeta principal
    RectF hero(x, y, w, px(262));
    card(g, hero, px(24));
    {
        GraphicsState s = g.Save();
        GraphicsPath clip; roundPath(clip, hero, px(24)); g.SetClip(&clip, CombineModeIntersect);
        glowCircle(g, hero.X + hero.Width - px(120), hero.Y + px(40), px(320), ACC2, 40);
        glowCircle(g, hero.X + px(140), hero.Y + hero.Height / 2, px(200), ACC1, 26);
        g.Restore(s);
    }
    DriveInfo sys;
    for (auto& d : g_drives) if (lower(d.root.substr(0, 2)) == lower(P.sysdrive.substr(0, 2))) sys = d;
    if (!sys.total && !g_drives.empty()) sys = g_drives[0];
    float used = sys.total ? 1.f - (float)sys.freeB / (float)sys.total : 0.f;
    float gu = anim(KEY_GAUGE, used, 3.5f);
    float gcx = hero.X + px(140), gcy = hero.Y + hero.Height / 2, gr = px(86);
    ring(g, gcx, gcy, gr, px(16), gu, ACC1, ACC2);
    wchar_t pct[16]; swprintf(pct, 16, L"%d%%", (int)(gu * 100 + 0.5f));
    text(g, pct, RectF(gcx - gr, gcy - px(30), 2 * gr, px(46)), 38.f, C(C_TEXT), F_LIGHT, AL_C);
    text(g, L"en uso", RectF(gcx - gr, gcy + px(16), 2 * gr, px(20)), 12.5f, C(C_DIM), F_REG, AL_C);

    float ix = hero.X + px(268);
    float rx = hero.X + hero.Width - px(330);
    float iw = std::max(px(120), rx - ix - px(20));
    text(g, L"DISCO DEL SISTEMA", RectF(ix, hero.Y + px(56), iw, px(18)), 11.f, C(C_DIM), F_SEMI);
    text(g, L"Unidad " + (sys.root.empty() ? L"C:" : sys.root.substr(0, 2)), RectF(ix, hero.Y + px(76), iw, px(34)), 24.f, C(C_TEXT), F_SEMI);
    text(g, fmtSize(sys.freeB) + L" libres de " + fmtSize(sys.total), RectF(ix, hero.Y + px(112), iw, px(22)), 14.f, C(C_DIM));
    float freePct = sys.total ? (float)sys.freeB / sys.total : 1.f;
    if (freePct < 0.10f) chip(g, ix, hero.Y + px(148), L"Espacio crítico", C_DANGER, 11.5f, IC_WARNING);
    else if (freePct < 0.20f) chip(g, ix, hero.Y + px(148), L"Poco espacio libre", C_WARN, 11.5f, IC_WARNING);
    else chip(g, ix, hero.Y + px(148), L"Buen estado", C_OK, 11.5f, IC_CHECK);

    // Recuperable
    float rw = px(300);
    text(g, L"ESPACIO RECUPERABLE", RectF(rx, hero.Y + px(40), rw, px(18)), 11.f, C(C_DIM), F_SEMI);
    if (g_scanned) {
        float v = anim(KEY_RECOV, (float)allTotal(), 5.f);
        textGrad(g, fmtSize((uint64_t)v), rx, hero.Y + px(60), px(62), 46.f, F_LIGHT, ACC1, ACC2);
        text(g, L"Último análisis: " + g_scanWhen, RectF(rx, hero.Y + px(124), rw, px(20)), 12.f, C(C_DIM));
    } else {
        text(g, L"—", RectF(rx, hero.Y + px(60), rw, px(62)), 46.f, C(C_FAINT), F_LIGHT);
        text(g, L"Aún no has analizado el equipo", RectF(rx, hero.Y + px(124), rw, px(20)), 12.f, C(C_DIM));
    }
    button(g, RectF(rx, hero.Y + px(170), px(160), px(44)), L"Análisis completo", A_SCAN_FULL, 0, B_PRIMARY, IC_RADAR, true, 13.5f);
    button(g, RectF(rx + px(170), hero.Y + px(170), px(120), px(44)), L"Rápido", A_SCAN_QUICK, 0, B_SECONDARY, IC_BOLT);

    // Tarjetas de acción
    float y2 = hero.Y + hero.Height + px(22);
    float gap = px(18), cw = (w - 2 * gap) / 3, ch = px(166);
    struct AC { int ic; ARGB col; wstring title, desc, btn; int action; };
    wstring safeBtn = g_scanned ? L"Limpiar " + fmtSize(safeTotal()) : L"Limpiar ahora";
    AC acs[3] = {
        {IC_SPARKLE, C_OK, L"Limpieza segura", L"Temporales, cachés y papelera en un clic, sin tocar tus archivos.", safeBtn, A_SAFE_CLEAN},
        {IC_GHOST, 0xFFA78BFA, L"Restos de programas", L"Busca en discos y registro todo rastro de un programa y bórralo.", L"Buscar restos", A_HOME_CARD},
        {IC_SHIELD, ACC1, L"Protección activa", L"Windows, el arranque y tus carpetas principales están bloqueados.", L"Ver qué se protege", A_HOME_CARD}};
    for (int i = 0; i < 3; ++i) {
        RectF r(x + i * (cw + gap), y2, cw, ch);
        bool hc = acs[i].action == A_SAFE_CLEAN ? hov(A_SAFE_CLEAN) : hov(A_HOME_CARD, i);
        float h = anim(K(A_HOME_CARD, i + 50), hc ? 1.f : 0.f, 14.f);
        r.Offset(0, -px(3) * h);
        card(g, r, px(20), h);
        iconBadge(g, acs[i].ic, r.X + px(38), r.Y + px(40), px(19), acs[i].col);
        text(g, acs[i].title, RectF(r.X + px(70), r.Y + px(26), r.Width - px(84), px(28)), 16.f, C(C_TEXT), F_SEMI);
        text(g, acs[i].desc, RectF(r.X + px(22), r.Y + px(66), r.Width - px(44), px(42)), 12.5f, C(C_DIM), F_REG, AL_L, 0, TR_CHAR, true);
        RectF br(r.X + px(22), r.Y + r.Height - px(52), r.Width - px(44), px(36));
        if (acs[i].action == A_SAFE_CLEAN) button(g, br, acs[i].btn, A_SAFE_CLEAN, 0, B_SUCCESS, IC_SPARKLE, !g_busy);
        else button(g, br, acs[i].btn, A_HOME_CARD, i, B_SECONDARY, IC_NONE);
    }

    // Estadísticas
    float y3 = y2 + ch + px(22);
    if (y3 + px(90) <= in.Y + in.Height + px(10)) {
        struct St { int ic; wstring v, l; ARGB c; };
        St sts[3] = {{IC_SPARKLE, fmtSize(g_histFreed), L"liberados con Optimizalis", ACC1},
                     {IC_CHECK, fmtNum(g_histCleanups), L"limpiezas realizadas", C_OK},
                     {IC_TRASH, fmtNum(g_histItems), L"elementos eliminados", 0xFFF472B6}};
        for (int i = 0; i < 3; ++i) {
            RectF r(x + i * (cw + gap), y3, cw, px(84));
            card(g, r, px(18));
            iconBadge(g, sts[i].ic, r.X + px(36), r.Y + r.Height / 2, px(17), sts[i].c);
            text(g, sts[i].v, RectF(r.X + px(66), r.Y + px(14), r.Width - px(80), px(32)), 22.f, C(C_TEXT), F_SEMI);
            text(g, sts[i].l, RectF(r.X + px(66), r.Y + px(46), r.Width - px(80), px(20)), 12.f, C(C_DIM));
        }
    }
}

// --------------------------------------------------------------- Análisis en curso
static void pageScan(Graphics& g, const RectF& c) {
    std::vector<StepInfo> steps; wstring cur;
    { std::lock_guard<std::mutex> l(g_work.m); steps = g_work.steps; cur = g_work.current; }
    uint64_t cnt = g_work.count;
    bool running = g_busy && g_busyKind != 2;
    wstring title = g_busyKind == 1 ? L"Buscando restos de \"" + g_searchTerm + L"\"" : (g_lastQuick ? L"Análisis rápido" : L"Análisis completo");
    header(g, c, title, running ? L"Puedes seguir usando el equipo. Cancela cuando quieras: lo encontrado se conserva."
                                : L"Terminado.");
    RectF in = innerRect(c);
    float lw = std::max(px(380), in.Width * 0.47f);
    RectF L(in.X, in.Y, lw, in.Height);
    card(g, L, px(24));
    float r = std::min(px(118), (L.Height - px(230)) / 2);
    float cx = L.X + L.Width / 2, cy = L.Y + px(44) + r + px(14);
    glowCircle(g, cx, cy, r * 1.9f, ACC1, 36);
    Pen t1(Color(16, 255, 255, 255), px(1));
    g.DrawEllipse(&t1, cx - r * 0.62f, cy - r * 0.62f, r * 1.24f, r * 1.24f);
    g.DrawEllipse(&t1, cx - r * 0.34f, cy - r * 0.34f, r * 0.68f, r * 0.68f);
    Pen tr(Color(20, 255, 255, 255), px(14));
    g.DrawEllipse(&tr, cx - r, cy - r, 2 * r, 2 * r);
    if (running) {
        float a = (float)fmod(GetTickCount64() * 0.22, 360.0);
        for (int i = 59; i >= 0; --i) {  // estela continua: segmentos solapados del más tenue al más intenso
            float t = 1.f - i / 60.f;
            Pen p(CA(mix(ACC1, ACC2, i / 60.f), 255 * t * t * t), px(14));
            g.DrawArc(&p, cx - r, cy - r, 2 * r, 2 * r, a - i * 2.5f - 3.f, 6.f);
        }
        {
            Pen head(C(ACC1), px(14)); head.SetStartCap(LineCapRound); head.SetEndCap(LineCapRound);
            g.DrawArc(&head, cx - r, cy - r, 2 * r, 2 * r, a - 2.f, 2.f);
        }
        // barrido del radar
        GraphicsPath sw;
        sw.AddPie(cx - r * 0.9f, cy - r * 0.9f, r * 1.8f, r * 1.8f, a - 50, 50);
        PathGradientBrush pb(&sw);
        pb.SetCenterColor(CA(ACC1, 10)); Color sur = CA(ACC1, 34); INT n = 1; pb.SetSurroundColors(&sur, &n);
        g.FillPath(&pb, &sw);
        g_needAnim = true;
    } else ring(g, cx, cy, r, px(14), 1.f, ACC1, ACC2);
    text(g, fmtNum(cnt), RectF(cx - r, cy - px(28), 2 * r, px(44)), 32.f, C(C_TEXT), F_LIGHT, AL_C);
    text(g, L"elementos analizados", RectF(cx - r, cy + px(16), 2 * r, px(20)), 12.f, C(C_DIM), F_REG, AL_C);
    wstring el = fmtTime(GetTickCount64() - g_workStart);
    float ew = textW(g, el, 12.f, F_SEMI) + px(40);
    RectF er(cx - ew / 2, cy + r + px(22), ew, px(26));
    fillRoundC(g, er, px(13), Color(16, 255, 255, 255));
    icon(g, IC_HISTORY, er.X + px(15), er.Y + px(13), px(13), C_DIM);
    text(g, el, RectF(er.X + px(26), er.Y, ew - px(28), er.Height), 12.f, C(C_TEXT), F_SEMI);
    {
        wstring stepName; size_t doneN = 0;
        for (auto& s : steps) { if (s.status == 1) stepName = s.name; if (s.status == 2) ++doneN; }
        float pct = steps.empty() ? 0.f : (float)doneN / steps.size();
        float sy = er.Y + er.Height + px(22);
        if (sy + px(64) < L.Y + L.Height - px(126)) {
            text(g, running ? (stepName.empty() ? L"Preparando…" : stepName) : L"Completado", RectF(L.X + px(30), sy, L.Width - px(60), px(26)), 16.f, C(C_TEXT), F_SEMI, AL_C);
            wchar_t pb[32]; swprintf(pb, 32, L"%d%% completado", (int)(pct * 100 + 0.5f));
            text(g, pb, RectF(L.X + px(30), sy + px(28), L.Width - px(60), px(20)), 12.f, C(C_DIM), F_REG, AL_C);
        }
    }

    RectF pr(L.X + px(24), L.Y + L.Height - px(118), L.Width - px(48), px(40));
    fillRoundC(g, pr, px(12), Color(14, 255, 255, 255));
    icon(g, IC_FOLDER, pr.X + px(20), pr.Y + pr.Height / 2, px(16), C_DIM);
    text(g, cur.empty() ? L"Preparando…" : cur, RectF(pr.X + px(38), pr.Y, pr.Width - px(50), pr.Height), 12.f, C(C_DIM), F_REG, AL_L, 1, TR_PATH);
    bool stopping = g_work.stop;
    button(g, RectF(cx - px(90), L.Y + L.Height - px(64), px(180), px(42)), stopping ? L"Cancelando…" : L"Cancelar",
           A_CANCEL_WORK, 0, B_SECONDARY, IC_X, running && !stopping);

    // Pasos
    RectF R(L.X + L.Width + px(20), in.Y, in.Width - L.Width - px(20), in.Height);
    card(g, R, px(24));
    size_t done = 0; for (auto& s : steps) if (s.status == 2) ++done;
    text(g, L"Progreso", RectF(R.X + px(26), R.Y + px(22), R.Width - px(52), px(26)), 16.f, C(C_TEXT), F_SEMI);
    wstring cnts = std::to_wstring(done) + L" de " + std::to_wstring(steps.size());
    text(g, cnts, RectF(R.X + px(26), R.Y + px(22), R.Width - px(52), px(26)), 12.5f, C(C_DIM), F_REG, AL_R);
    float pf = steps.empty() ? 0.f : (float)done / steps.size();
    progressBar(g, RectF(R.X + px(26), R.Y + px(58), R.Width - px(52), px(6)), anim(KEY_SCANBAR, pf, 6.f), ACC1, ACC2);
    float ry = R.Y + px(80);
    float rh = std::min(px(40), (R.Height - px(96)) / std::max<size_t>(1, steps.size()));
    for (auto& s : steps) {
        RectF row(R.X + px(16), ry, R.Width - px(32), rh);
        float icx = row.X + px(20), icy = row.Y + rh / 2;
        if (s.status == 1) {
            fillRoundC(g, row, px(10), CA(ACC1, 22));
            spinner(g, icx, icy, px(8), px(2.2f), ACC1);
        } else if (s.status == 2) {
            fillRoundC(g, RectF(icx - px(10), icy - px(10), px(20), px(20)), px(10), CA(C_OK, 40));
            icon(g, IC_CHECK, icx, icy, px(13), C_OK);
        } else {
            Pen p(Color(60, 255, 255, 255), px(1.5f)); g.DrawEllipse(&p, icx - px(7), icy - px(7), px(14), px(14));
        }
        text(g, s.name, RectF(row.X + px(40), row.Y, row.Width - px(170), rh), 13.f, C(s.status == 0 ? C_DIM : C_TEXT), s.status == 1 ? F_SEMI : F_REG);
        wstring right = s.status == 2 ? (s.size ? fmtSize(s.size) : (s.count ? fmtNum(s.count) + L" elem." : L"nada")) : s.status == 1 ? L"analizando…" : L"";
        text(g, right, RectF(row.X + row.Width - px(150), row.Y, px(140), rh), 12.5f, C(s.status == 1 ? ACC1 : (s.size ? C_TEXT : C_FAINT)), s.size ? F_SEMI : F_REG, AL_R);
        ry += rh;
    }
}

// --------------------------------------------------------------- Resultados
static void pageResults(Graphics& g, const RectF& c) {
    if (!g_scanned) {
        header(g, c, L"Resultados", L"Analiza el equipo para ver cuánto espacio puedes recuperar.");
        RectF in = innerRect(c);
        RectF r(in.X + (in.Width - px(520)) / 2, in.Y + px(40), px(520), px(320));
        card(g, r, px(24));
        glowCircle(g, r.X + r.Width / 2, r.Y + px(90), px(110), ACC1, 40);
        iconBadge(g, IC_RADAR, r.X + r.Width / 2, r.Y + px(86), px(36), ACC1);
        text(g, L"Todavía no hay resultados", RectF(r.X, r.Y + px(140), r.Width, px(30)), 20.f, C(C_TEXT), F_SEMI, AL_C);
        text(g, L"El análisis rápido tarda unos segundos; el completo recorre todos tus discos.", RectF(r.X + px(40), r.Y + px(176), r.Width - px(80), px(40)), 13.f, C(C_DIM), F_REG, AL_C, 0, TR_CHAR, true);
        button(g, RectF(r.X + r.Width / 2 - px(175), r.Y + px(240), px(170), px(44)), L"Análisis completo", A_SCAN_FULL, 0, B_PRIMARY, IC_RADAR);
        button(g, RectF(r.X + r.Width / 2 + px(5), r.Y + px(240), px(170), px(44)), L"Análisis rápido", A_SCAN_QUICK, 0, B_SECONDARY, IC_BOLT);
        return;
    }
    header(g, c, L"Resultados", L"Análisis " + wstring(g_lastQuick ? L"rápido" : L"completo") + L" · " + g_scanWhen, px(200));
    button(g, RectF(c.X + c.Width - px(40) - px(190), px(40), px(190), px(42)), L"Repetir análisis", A_RESCAN, 0, B_SECONDARY, IC_UPDATE, !g_busy);
    RectF in = innerRect(c);

    // Resumen
    RectF sr(in.X, in.Y, in.Width, px(158));
    card(g, sr, px(22));
    uint64_t tot = allTotal(), safe = 0, rev = 0, pers = 0;
    for (auto& cat : g_cats) { if (cat.level == LV_SAFE) safe += cat.total(); else if (cat.level == LV_REVIEW) rev += cat.total(); else pers += cat.total(); }
    text(g, L"ESPACIO RECUPERABLE", RectF(sr.X + px(28), sr.Y + px(22), px(300), px(18)), 11.f, C(C_DIM), F_SEMI);
    float v = anim(KEY_RECOV, (float)tot, 5.f);
    textGrad(g, fmtSize((uint64_t)v), sr.X + px(26), sr.Y + px(40), px(56), 40.f, F_LIGHT, ACC1, ACC2);
    float bw = sr.Width - px(28) - px(310);
    RectF bar(sr.X + px(28), sr.Y + px(106), bw, px(12));
    {
        GraphicsState s = g.Save();
        GraphicsPath clip; roundPath(clip, bar, px(6)); g.SetClip(&clip, CombineModeIntersect);
        SolidBrush bg(Color(18, 255, 255, 255)); g.FillRectangle(&bg, bar);
        float bx = bar.X;
        for (auto& cat : g_cats) {
            uint64_t t = cat.total();
            if (!t || !tot) continue;
            float ww = std::max(px(3), bar.Width * (float)t / tot);
            SolidBrush b(C(cat.color)); g.FillRectangle(&b, bx, bar.Y, ww - px(1.5f), bar.Height);
            bx += ww;
        }
        g.Restore(s);
    }
    float lx = sr.X + px(28);
    struct Lg { ARGB c; wstring s; };
    Lg lg[3] = {{C_OK, L"Seguro " + fmtSize(safe)}, {C_WARN, L"Revisar " + fmtSize(rev)}, {C_DANGER, L"Personal " + fmtSize(pers)}};
    for (auto& l : lg) {
        fillRoundC(g, RectF(lx, sr.Y + px(130), px(9), px(9)), px(4.5f), C(l.c));
        float tw = textW(g, l.s, 12.f, F_REG);
        text(g, l.s, RectF(lx + px(15), sr.Y + px(124), tw + px(6), px(20)), 12.f, C(C_DIM));
        lx += tw + px(36);
    }
    float bx2 = sr.X + sr.Width - px(286);
    text(g, L"Temporales y cachés, sin revisar nada", RectF(bx2, sr.Y + px(30), px(260), px(18)), 12.f, C(C_DIM), F_REG, AL_C);
    button(g, RectF(bx2, sr.Y + px(56), px(260), px(48)), L"Limpieza segura · " + fmtSize(safe), A_SAFE_CLEAN, 0, B_SUCCESS, IC_SPARKLE, !g_busy && safe > 0, 14.f);
    text(g, L"Pulsa una categoría para revisar y elegir", RectF(bx2, sr.Y + px(114), px(260), px(18)), 11.5f, C(C_FAINT), F_REG, AL_C);

    // Rejilla de categorías
    RectF view(in.X - px(10), sr.Y + sr.Height + px(14), in.Width + px(20), in.Y + in.Height - (sr.Y + sr.Height + px(14)) + px(30));
    float gap = px(16);
    int cols = std::max(2, (int)((in.Width + gap) / (px(268) + gap)));
    float cw = (in.Width - gap * (cols - 1)) / cols, ch = px(142);
    int rows = ((int)g_cats.size() + cols - 1) / cols;
    float contentH = rows * (ch + gap) + px(24);
    uint64_t mx = 1; for (auto& cat : g_cats) mx = std::max(mx, cat.total());
    GraphicsState st = g.Save();
    g.SetClip(view, CombineModeIntersect);
    for (size_t i = 0; i < g_cats.size(); ++i) {
        auto& cat = g_cats[i];
        int col = (int)i % cols, row = (int)i / cols;
        RectF r(in.X + col * (cw + gap), view.Y + px(10) + row * (ch + gap) - g_scroll, cw, ch);
        if (r.Y + r.Height < view.Y || r.Y > view.Y + view.Height) continue;
        float h = anim(K(A_OPEN_CAT, (int)i), hov(A_OPEN_CAT, (int)i) ? 1.f : 0.f, 16.f);
        RectF rr = r; rr.Offset(0, -px(3) * h);
        uint64_t t = cat.total();
        bool empty = cat.items.empty();
        card(g, rr, px(20), h);
        if (h > 0.01f) {
            GraphicsState s2 = g.Save();
            GraphicsPath clip; roundPath(clip, rr, px(20)); g.SetClip(&clip, CombineModeIntersect);
            glowCircle(g, rr.X + px(40), rr.Y + px(40), px(130), cat.color, 40 * h);
            g.Restore(s2);
        }
        float al = empty ? 120.f : 255.f;
        iconBadge(g, cat.icon, rr.X + px(36), rr.Y + px(38), px(18), empty ? C_FAINT : cat.color);
        wstring tg; ARGB tc; levelTag(cat.level, tg, tc);
        chip(g, rr.X + rr.Width - px(16), rr.Y + px(16), tg, tc, 10.5f, IC_NONE, true);
        text(g, cat.name, RectF(rr.X + px(66), rr.Y + px(24), rr.Width - px(150), px(28)), 14.5f, CA(C_TEXT, al), F_SEMI);
        text(g, empty ? L"Nada que limpiar" : fmtNum(cat.count()) + (cat.count() == 1 ? L" elemento" : L" elementos"),
             RectF(rr.X + px(66), rr.Y + px(46), rr.Width - px(150), px(20)), 12.f, C(C_DIM));
        text(g, fmtSize(t), RectF(rr.X + px(20), rr.Y + px(74), rr.Width - px(40), px(38)), 25.f, CA(empty ? C_FAINT : C_TEXT, al), F_LIGHT);
        RectF pb(rr.X + px(20), rr.Y + rr.Height - px(22), rr.Width - px(40), px(5));
        fillRoundC(g, pb, px(3), Color(16, 255, 255, 255));
        if (t) fillRoundC(g, RectF(pb.X, pb.Y, std::max(px(5), pb.Width * (float)t / mx), pb.Height), px(3), C(cat.color));
        if (h > 0.01f) text(g, L"Revisar →", RectF(rr.X + rr.Width - px(120), rr.Y + px(82), px(100), px(22)), 12.f, CA(cat.color, 255 * h), F_SEMI, AL_R);
        RectF hit = r;
        if (hit.Y < view.Y) { hit.Height -= view.Y - hit.Y; hit.Y = view.Y; }
        if (hit.Y + hit.Height > view.Y + view.Height) hit.Height = view.Y + view.Height - hit.Y;
        if (hit.Height > 0) addHot(hit, A_OPEN_CAT, (int)i);
    }
    g.Restore(st);
    scrollbar(g, view, contentH);
}

// --------------------------------------------------------------- Detalle
static void pageDetail(Graphics& g, const RectF& c) {
    if (!g_detail) { navigate(PG_RESULTS); return; }
    Category& cat = *g_detail;
    float x = c.X + px(40), w = c.Width - px(80);
    button(g, RectF(x - px(8), px(24), px(150), px(32)), g_detailIsSearch ? L"Restos de programas" : L"Resultados", A_BACK, 0, B_GHOST, IC_BACK, true, 12.5f);
    iconBadge(g, cat.icon, x + px(22), px(84), px(20), cat.color);
    wstring tg; ARGB tc; levelTag(cat.level, tg, tc);
    float tw = std::min(textW(g, cat.name, 24.f, F_SEMI), w - px(300));
    text(g, cat.name, RectF(x + px(56), px(64), tw + px(6), px(38)), 24.f, C(C_TEXT), F_SEMI, AL_L, 1, TR_CHAR);
    chip(g, x + px(68) + tw, px(72), tg, tc, 10.5f);
    text(g, cat.desc, RectF(x + px(56), px(100), w - px(56), px(20)), 12.5f, C(C_DIM));

    float y = px(132);
    if (cat.level != LV_SAFE) {
        RectF wr(x, y, w, px(38));
        ARGB wc = cat.level == LV_PERSONAL ? C_DANGER : C_WARN;
        fillRoundC(g, wr, px(12), CA(wc, 26));
        Pen p(CA(wc, 70), px(1)); strokeRound(g, wr, px(12), p);
        icon(g, IC_WARNING, wr.X + px(22), wr.Y + wr.Height / 2, px(16), wc);
        text(g, cat.level == LV_PERSONAL ? L"Son tus archivos personales: revisa uno por uno antes de borrar. El borrado es definitivo."
                                         : L"Revisa cada elemento antes de borrarlo. Lo que aparece con candado está protegido y no se puede eliminar.",
             RectF(wr.X + px(42), wr.Y, wr.Width - px(54), wr.Height), 12.5f, C(mix(wc, C_TEXT, 0.35f)));
        y += px(50);
    }
    // Barra de herramientas
    RectF fr(x, y, std::min(px(300), w * 0.30f), px(42));
    inputBox(g, fr, g_edFilter, 1);
    icon(g, IC_SEARCH, fr.X + px(20), fr.Y + fr.Height / 2, px(16), C_DIM);
    g_rcEdFilter = RectF(fr.X + px(40), fr.Y + (fr.Height - px(22)) / 2, fr.Width - px(52), px(22));
    g_visEdFilter = true;
    float bx = fr.X + fr.Width + px(10);
    button(g, RectF(bx, y, px(124), px(42)), g_sortMode == 0 ? L"Por tamaño" : L"Por nombre", A_SORT, 0, B_SECONDARY, IC_SORT, true, 12.5f); bx += px(132);
    button(g, RectF(bx, y, px(78), px(42)), L"Todo", A_SEL_ALL, 0, B_SECONDARY, IC_NONE, true, 12.5f); bx += px(86);
    button(g, RectF(bx, y, px(86), px(42)), L"Ninguno", A_SEL_NONE, 0, B_SECONDARY, IC_NONE, true, 12.5f);
    size_t selN = 0; uint64_t selS = 0;
    for (auto& it : cat.items) if (it.selected && it.blocked.empty()) { ++selN; selS += it.size; }
    float dw = px(200);
    button(g, RectF(x + w - dw, y, dw, px(42)), selN ? L"Eliminar " + fmtSize(selS) : L"Eliminar", A_DELETE, 0, B_DANGER, IC_TRASH, selN > 0 && !g_busy);
    wstring selT = selN ? fmtNum(selN) + (selN == 1 ? L" seleccionado" : L" seleccionados") : fmtNum(cat.count()) + L" · " + fmtSize(cat.total());
    text(g, selT, RectF(bx + px(96), y, x + w - dw - (bx + px(96)) - px(14), px(42)), 12.5f, C(selN ? ACC1 : C_DIM), selN ? F_SEMI : F_REG, AL_R);

    // Lista
    float ly = y + px(56);
    RectF lc(x, ly, w, H - ly - px(26));
    card(g, lc, px(20));
    RectF view(lc.X + px(6), lc.Y + px(6), lc.Width - px(12), lc.Height - px(12));
    float rh = px(68);
    float contentH = g_view.size() * rh;
    if (g_view.empty()) {
        iconBadge(g, g_filter.empty() ? IC_CHECK : IC_SEARCH, lc.X + lc.Width / 2, lc.Y + lc.Height / 2 - px(30), px(26), g_filter.empty() ? C_OK : C_DIM);
        text(g, g_filter.empty() ? L"Nada por aquí: esta categoría está limpia" : L"Ningún elemento coincide con el filtro",
             RectF(lc.X, lc.Y + lc.Height / 2 + px(10), lc.Width, px(26)), 14.f, C(C_DIM), F_REG, AL_C);
        g_scrollMax = 0;
        return;
    }
    GraphicsState st = g.Save();
    g.SetClip(view, CombineModeIntersect);
    int first = std::max(0, (int)(g_scroll / rh));
    int last = std::min((int)g_view.size(), (int)((g_scroll + view.Height) / rh) + 2);
    for (int vi = first; vi < last; ++vi) {
        Item& it = cat.items[g_view[vi]];
        RectF r(view.X, view.Y + vi * rh - g_scroll, view.Width - px(10), rh);
        bool bl = !it.blocked.empty();
        bool hv = hov(A_ROW, vi) || hov(A_ROW_OPEN, vi);
        float h = anim(K(A_ROW, g_view[vi]), hv ? 1.f : 0.f, 20.f);
        if (it.selected) {
            LinearGradientBrush lb(r, CA(ACC1, 34), CA(ACC2, 10), LinearGradientModeHorizontal);
            fillRound(g, r, px(14), lb);
            fillRoundC(g, RectF(r.X + px(1), r.Y + px(16), px(3), r.Height - px(32)), px(2), C(ACC1));
        }
        if (h > 0.01f) fillRoundC(g, r, px(14), Color((BYTE)(12 * h), 255, 255, 255));
        if (vi + 1 < (int)g_view.size()) { Pen sp(Color(12, 255, 255, 255), px(1)); g.DrawLine(&sp, r.X + px(16), r.Y + rh - 0.5f, r.X + r.Width - px(16), r.Y + rh - 0.5f); }
        checkbox(g, r.X + px(16), r.Y + (rh - px(20)) / 2, px(20), it.selected, h, bl, g_view[vi]);
        int tic = bl ? IC_LOCK : it.kind == Kind::RecycleBin ? IC_TRASH : (it.kind == Kind::RegKey || it.kind == Kind::RegValue) ? IC_REGISTRY : it.isDir ? IC_FOLDER : IC_FILE;
        ARGB icol = bl ? C_DANGER : (it.kind == Kind::RegKey || it.kind == Kind::RegValue) ? 0xFFA78BFA : cat.color;
        iconBadge(g, tic, r.X + px(66), r.Y + rh / 2, px(15), icol);
        float tx = r.X + px(96), tw2 = r.Width - px(96) - px(250);
        float al = bl ? 140.f : 255.f;
        text(g, itemName(it), RectF(tx, r.Y + px(9), tw2, px(22)), 14.f, CA(C_TEXT, al), F_SEMI);
        text(g, itemParent(it), RectF(tx, r.Y + px(29), tw2, px(18)), 11.5f, CA(C_DIM, al), F_REG, AL_L, 1, TR_PATH);
        wstring sub = bl ? L"Bloqueado: " + it.blocked : it.note;
        text(g, sub, RectF(tx, r.Y + px(46), tw2, px(17)), 11.f, C(bl ? C_DANGER : C_FAINT));
        float sx = r.X + r.Width - px(240);
        text(g, fmtSize(it.size), RectF(sx, r.Y + px(14), px(150), px(24)), 15.f, CA(C_TEXT, al), F_SEMI, AL_R);
        RectF sb(sx + px(40), r.Y + px(42), px(110), px(5));
        fillRoundC(g, sb, px(3), Color(16, 255, 255, 255));
        if (!bl && it.size) fillRoundC(g, RectF(sb.X, sb.Y, std::max(px(5), sb.Width * (float)it.size / g_viewMax), sb.Height), px(3), C(cat.color));
        RectF ob(r.X + r.Width - px(66), r.Y + (rh - px(36)) / 2, px(48), px(36));
        RectF hit = r;
        if (hit.Y < view.Y) { hit.Height -= view.Y - hit.Y; hit.Y = view.Y; }
        if (hit.Y + hit.Height > view.Y + view.Height) hit.Height = view.Y + view.Height - hit.Y;
        if (hit.Height > 0) addHot(hit, A_ROW, vi, !bl);
        if (h > 0.01f && ob.Y >= view.Y && ob.Y + ob.Height <= view.Y + view.Height) {
            bool oh = hov(A_ROW_OPEN, vi);
            fillRoundC(g, ob, px(10), Color((BYTE)((oh ? 40 : 18) * h), 255, 255, 255));
            icon(g, IC_EXTERNAL, ob.X + ob.Width / 2, ob.Y + ob.Height / 2, px(16), oh ? ACC1 : C_DIM, 255 * h);
            addHot(ob, A_ROW_OPEN, vi);
        }
    }
    g.Restore(st);
    scrollbar(g, view, contentH);
}

// --------------------------------------------------------------- Restos de programas
static void pageSearch(Graphics& g, const RectF& c) {
    header(g, c, L"Restos de programas", L"Elimina todo rastro de un programa: carpetas, archivos, registro e inicio automático.");
    RectF in = innerRect(c);
    RectF hero(in.X, in.Y, in.Width, px(300));
    card(g, hero, px(24));
    {
        GraphicsState s = g.Save();
        GraphicsPath clip; roundPath(clip, hero, px(24)); g.SetClip(&clip, CombineModeIntersect);
        glowCircle(g, hero.X + px(90), hero.Y + px(80), px(260), 0xFFA78BFA, 40);
        glowCircle(g, hero.X + hero.Width, hero.Y + hero.Height, px(300), ACC1, 22);
        g.Restore(s);
    }
    float bob = sinf(GetTickCount64() / 600.f) * px(3);
    if (g_set.animations) g_needAnim = true;
    iconBadge(g, IC_GHOST, hero.X + px(70), hero.Y + px(72) + bob, px(30), 0xFFA78BFA);
    text(g, L"¿Qué programa quieres borrar del todo?", RectF(hero.X + px(124), hero.Y + px(40), hero.Width - px(150), px(32)), 21.f, C(C_TEXT), F_SEMI);
    text(g, L"Escribe su nombre (o parte de él). Se buscará en todas las unidades y en el registro de Windows; lo esencial aparecerá con candado.",
         RectF(hero.X + px(124), hero.Y + px(76), hero.Width - px(160), px(40)), 13.f, C(C_DIM), F_REG, AL_L, 0, TR_CHAR, true);
    float bw = px(190);
    RectF ib(hero.X + px(40), hero.Y + px(140), hero.Width - px(80) - bw - px(12), px(54));
    inputBox(g, ib, g_edSearch, 0);
    icon(g, IC_SEARCH, ib.X + px(26), ib.Y + ib.Height / 2, px(18), C_DIM);
    g_rcEdSearch = RectF(ib.X + px(50), ib.Y + (ib.Height - px(24)) / 2, ib.Width - px(64), px(24));
    g_visEdSearch = true;
    button(g, RectF(ib.X + ib.Width + px(12), ib.Y, bw, ib.Height), L"Buscar restos", A_SEARCH_GO, 0, B_PRIMARY, IC_SEARCH, !g_busy, 14.f);
    float cx = hero.X + px(40), cy = hero.Y + px(222);
    struct Ch { int ic; const wchar_t* s; };
    Ch chs[] = {{IC_DRIVE, L"Todas las unidades"}, {IC_FOLDER, L"AppData · Program Files · ProgramData"}, {IC_REGISTRY, L"Registro de Windows"},
                {IC_BOLT, L"Inicio automático"}, {IC_SHIELD, L"Copia .reg antes de borrar"}};
    for (auto& ch : chs) {
        float tw = textW(g, ch.s, 11.5f, F_SEMI) + px(38);
        if (cx + tw > hero.X + hero.Width - px(40)) { cx = hero.X + px(40); cy += px(32); }
        cx += chip(g, cx, cy, ch.s, mix(C_DIM, ACC1, 0.35f), 11.5f, ch.ic) + px(8);
    }

    float y = hero.Y + hero.Height + px(20);
    float gap = px(18), cw = (in.Width - gap) / 2;
    RectF a(in.X, y, cw, px(170));
    card(g, a, px(20));
    iconBadge(g, IC_HISTORY, a.X + px(36), a.Y + px(38), px(18), ACC1);
    text(g, L"Última búsqueda", RectF(a.X + px(66), a.Y + px(24), a.Width - px(86), px(28)), 15.f, C(C_TEXT), F_SEMI);
    if (g_searchDone) {
        text(g, L"\"" + g_searchTerm + L"\": " + fmtNum(g_search.count()) + L" elementos · " + fmtSize(g_search.total()),
             RectF(a.X + px(24), a.Y + px(70), a.Width - px(48), px(22)), 13.f, C(C_DIM));
        button(g, RectF(a.X + px(24), a.Y + a.Height - px(58), px(170), px(40)), L"Ver resultados", A_SEARCH_LAST, 0, B_SECONDARY, IC_GRID);
        button(g, RectF(a.X + px(204), a.Y + a.Height - px(58), px(150), px(40)), L"Repetir", A_VERIFY_AGAIN, 0, B_GHOST, IC_UPDATE, !g_busy);
    } else
        text(g, L"Aún no has buscado ningún programa en esta sesión.", RectF(a.X + px(24), a.Y + px(70), a.Width - px(48), px(40)), 13.f, C(C_DIM), F_REG, AL_L, 0, TR_CHAR, true);
    RectF b(in.X + cw + gap, y, cw, px(170));
    card(g, b, px(20));
    iconBadge(g, IC_INFO, b.X + px(36), b.Y + px(38), px(18), C_WARN);
    text(g, L"Cómo hacerlo bien", RectF(b.X + px(66), b.Y + px(24), b.Width - px(86), px(28)), 15.f, C(C_TEXT), F_SEMI);
    text(g, L"1. Si el programa sigue instalado, Optimizalis te ofrecerá abrir su desinstalador oficial.\n"
            L"2. Después busca sus restos y revisa la lista.\n"
            L"3. Al terminar, repite la búsqueda para comprobar que no queda nada.",
         RectF(b.X + px(24), b.Y + px(64), b.Width - px(48), px(96)), 12.5f, C(C_DIM), F_REG, AL_L, 0, TR_NONE, true);
}

// --------------------------------------------------------------- Protección
static void pageProtect(Graphics& g, const RectF& c) {
    header(g, c, L"Protección", L"Esto nunca se borra. Se vuelve a comprobar justo antes de eliminar cada elemento.");
    RectF in = innerRect(c);
    struct Rule { int ic; ARGB c; const wchar_t* t; const wchar_t* d; };
    Rule rules[] = {
        {IC_IMAGE, C_OK, L"Vídeos, fotos y audios", L"mp4, mov, mkv, avi, mp3, wav, flac, jpg, heic, raw, tiff, psd... nunca se borran: ni sueltos, ni dentro de carpetas, ni en la papelera. Si una carpeta los contiene, se borra lo demás y ellos se quedan."},
        {IC_WINDOW, 0xFF60A5FA, L"Windows y el arranque", L"La carpeta de Windows completa, Recovery, Boot, EFI y System Volume Information. Solo se permiten Temp, la caché de Windows Update, volcados y registros de mantenimiento."},
        {IC_LOCK, C_DANGER, L"Archivos vitales", L"pagefile.sys, hiberfil.sys, swapfile.sys, bootmgr, NTUSER.DAT, UsrClass.dat y cualquier archivo con atributo de sistema."},
        {IC_FOLDER, 0xFFFBBF24, L"Tus carpetas principales", L"Escritorio, Documentos, Descargas, Imágenes, Música, Vídeos, OneDrive y AppData, y cualquier carpeta que las contenga. Su contenido solo aparece en las categorías personales."},
        {IC_APPS, 0xFFA78BFA, L"Datos de Microsoft y apps", L"AppData\\Local\\Microsoft, Roaming\\Microsoft, Local\\Packages y ProgramData\\Microsoft, salvo cachés concretas como miniaturas o informes de errores."},
        {IC_GRID, ACC1, L"Programas instalados", L"Program Files y ProgramData solo se tocan al buscar restos, y nunca Windows, Microsoft, Common Files, WindowsApps, .NET ni controladores."},
        {IC_REGISTRY, 0xFFF472B6, L"Registro de Windows", L"Las claves de Microsoft, Windows, Classes, Policies... nunca se tocan. De cualquier otra se guarda antes una copia .reg para poder restaurarla."},
        {IC_EXTERNAL, 0xFF22D3EE, L"Enlaces y nube", L"Los enlaces y junctions nunca se siguen: se borra el enlace, jamás su destino. Los archivos de OneDrive que solo están en la nube se ignoran."},
        {IC_SHIELD, C_OK, L"Doble comprobación", L"Optimizalis vuelve a verificar todas estas reglas justo antes de borrar, y su propia carpeta también está protegida."}};
    float gap = px(18);
    int cols = in.Width > px(900) ? 2 : 1;
    float cw = (in.Width - gap * (cols - 1)) / cols, ch = px(128);
    int n = (int)(sizeof(rules) / sizeof(rules[0]));
    float contentH = ((n + cols - 1) / cols) * (ch + gap) + px(10);
    RectF view(in.X - px(10), in.Y - px(6), in.Width + px(20), in.Height + px(30));
    GraphicsState st = g.Save();
    g.SetClip(view, CombineModeIntersect);
    for (int i = 0; i < n; ++i) {
        RectF r(in.X + (i % cols) * (cw + gap), in.Y + (i / cols) * (ch + gap) - g_scroll, cw, ch);
        card(g, r, px(20));
        iconBadge(g, rules[i].ic, r.X + px(38), r.Y + px(40), px(19), rules[i].c);
        text(g, rules[i].t, RectF(r.X + px(72), r.Y + px(24), r.Width - px(96), px(28)), 15.f, C(C_TEXT), F_SEMI);
        text(g, rules[i].d, RectF(r.X + px(72), r.Y + px(54), r.Width - px(96), r.Height - px(62)), 12.5f, C(C_DIM), F_REG, AL_L, 0, TR_CHAR, true);
    }
    g.Restore(st);
    scrollbar(g, view, contentH);
}

// --------------------------------------------------------------- Historial
static void pageHistory(Graphics& g, const RectF& c) {
    header(g, c, L"Historial", L"Todo lo que Optimizalis ha eliminado queda anotado aquí.", px(420));
    button(g, RectF(c.X + c.Width - px(40) - px(196), px(40), px(196), px(42)), L"Copias del registro", A_OPEN_BACKUPS, 0, B_SECONDARY, IC_REGISTRY);
    button(g, RectF(c.X + c.Width - px(40) - px(196) - px(176), px(40), px(166), px(42)), L"Abrir registro", A_OPEN_LOG, 0, B_SECONDARY, IC_DOC);
    RectF in = innerRect(c);
    float gap = px(18), cw = (in.Width - 2 * gap) / 3;
    struct St { int ic; wstring v, l; ARGB c; };
    St sts[3] = {{IC_SPARKLE, fmtSize(g_histFreed), L"liberados en total", ACC1},
                 {IC_CHECK, fmtNum(g_histCleanups), L"limpiezas realizadas", C_OK},
                 {IC_TRASH, fmtNum(g_histItems), L"elementos eliminados", 0xFFF472B6}};
    for (int i = 0; i < 3; ++i) {
        RectF r(in.X + i * (cw + gap), in.Y, cw, px(88));
        card(g, r, px(18));
        iconBadge(g, sts[i].ic, r.X + px(38), r.Y + r.Height / 2, px(18), sts[i].c);
        text(g, sts[i].v, RectF(r.X + px(70), r.Y + px(16), r.Width - px(84), px(32)), 23.f, C(C_TEXT), F_SEMI);
        text(g, sts[i].l, RectF(r.X + px(70), r.Y + px(48), r.Width - px(84), px(20)), 12.f, C(C_DIM));
    }
    RectF lc(in.X, in.Y + px(106), in.Width, in.Height - px(106) + px(4));
    card(g, lc, px(20));
    if (g_log.empty()) {
        iconBadge(g, IC_HISTORY, lc.X + lc.Width / 2, lc.Y + lc.Height / 2 - px(26), px(26), C_DIM);
        text(g, L"Todavía no has eliminado nada", RectF(lc.X, lc.Y + lc.Height / 2 + px(14), lc.Width, px(24)), 14.f, C(C_DIM), F_REG, AL_C);
        return;
    }
    RectF view(lc.X + px(6), lc.Y + px(6), lc.Width - px(12), lc.Height - px(12));
    float rh = px(46);
    GraphicsState st = g.Save();
    g.SetClip(view, CombineModeIntersect);
    int first = std::max(0, (int)(g_scroll / rh));
    int last = std::min((int)g_log.size(), (int)((g_scroll + view.Height) / rh) + 2);
    for (int i = first; i < last; ++i) {
        auto& e = g_log[i];
        RectF r(view.X, view.Y + i * rh - g_scroll, view.Width - px(10), rh);
        if (i % 2 == 0) fillRoundC(g, r, px(10), Color(6, 255, 255, 255));
        ARGB ac = e.action == L"ELIMINADO" ? C_OK : e.action == L"RESUMEN" ? ACC1 : e.action == L"REGISTRO" ? 0xFFA78BFA : C_WARN;
        wstring lab = e.action == L"ELIMINADO" ? L"Eliminado" : e.action == L"RESUMEN" ? L"Limpieza" : e.action == L"REGISTRO" ? L"Registro" :
                      e.action == L"REINICIO" ? L"Al reiniciar" : e.action == L"PARCIAL" ? L"Parcial" : e.action;
        text(g, e.when, RectF(r.X + px(16), r.Y, px(150), rh), 11.5f, C(C_FAINT));
        float cwid = chip(g, r.X + px(170), r.Y + (rh - px(24)) / 2, lab, ac, 10.5f);
        (void)cwid;
        text(g, e.size, RectF(r.X + r.Width - px(120), r.Y, px(104), rh), 12.5f, C(C_TEXT), F_SEMI, AL_R);
        text(g, e.path, RectF(r.X + px(290), r.Y, r.Width - px(290) - px(130), rh), 12.5f, C(e.action == L"RESUMEN" ? C_TEXT : C_DIM), F_REG, AL_L, 1, TR_PATH);
    }
    g.Restore(st);
    scrollbar(g, view, g_log.size() * rh);
}

// --------------------------------------------------------------- Ajustes
static void pageSettings(Graphics& g, const RectF& c) {
    header(g, c, L"Ajustes", L"Personaliza el análisis y la apariencia. Se guardan automáticamente.");
    RectF in = innerRect(c);
    float gap = px(18);
    bool two = in.Width > px(960);
    float cw = two ? (in.Width - gap) * 0.55f : in.Width;
    RectF A(in.X, in.Y - g_scroll, cw, px(392));
    card(g, A, px(22));
    iconBadge(g, IC_RADAR, A.X + px(38), A.Y + px(38), px(18), ACC1);
    text(g, L"Análisis", RectF(A.X + px(70), A.Y + px(24), A.Width - px(90), px(28)), 16.f, C(C_TEXT), F_SEMI);
    struct Sv { const wchar_t* t; const wchar_t* d; wstring v; };
    Sv svs[4] = {
        {L"Archivos grandes desde", L"Tamaño mínimo para la categoría de archivos grandes", fmtNum(g_set.largeMB) + L" MB"},
        {L"Descargas antiguas", L"Días sin tocar para proponer borrar algo de Descargas", std::to_wstring(g_set.downloadDays) + L" días"},
        {L"Duplicados desde", L"Tamaño mínimo de los archivos que se comparan", fmtNum(g_set.dupMinMB) + L" MB"},
        {L"Restos de programas", L"Días sin uso para considerar una carpeta un resto", std::to_wstring(g_set.leftoverDays) + L" días"}};
    for (int i = 0; i < 4; ++i) {
        float ry = A.Y + px(78) + i * px(76);
        RectF row(A.X + px(20), ry, A.Width - px(40), px(66));
        fillRoundC(g, row, px(14), Color(8, 255, 255, 255));
        text(g, svs[i].t, RectF(row.X + px(18), row.Y + px(12), row.Width - px(220), px(22)), 13.5f, C(C_TEXT), F_SEMI);
        text(g, svs[i].d, RectF(row.X + px(18), row.Y + px(34), row.Width - px(220), px(20)), 11.5f, C(C_DIM));
        float sx = row.X + row.Width - px(196);
        button(g, RectF(sx, row.Y + px(15), px(36), px(36)), L"", A_SET_DEC, i, B_SECONDARY, IC_MINUS);
        text(g, svs[i].v, RectF(sx + px(40), row.Y, px(108), row.Height), 14.f, C(C_TEXT), F_SEMI, AL_C);
        button(g, RectF(sx + px(152), row.Y + px(15), px(36), px(36)), L"", A_SET_INC, i, B_SECONDARY, IC_PLUS);
    }
    RectF B = two ? RectF(in.X + cw + gap, in.Y - g_scroll, in.Width - cw - gap, px(212)) : RectF(in.X, A.Y + A.Height + gap, in.Width, px(212));
    card(g, B, px(22));
    iconBadge(g, IC_SPARKLE, B.X + px(38), B.Y + px(38), px(18), ACC2);
    text(g, L"Apariencia", RectF(B.X + px(70), B.Y + px(24), B.Width - px(90), px(28)), 16.f, C(C_TEXT), F_SEMI);
    text(g, L"Color de acento", RectF(B.X + px(24), B.Y + px(74), B.Width - px(48), px(20)), 12.5f, C(C_DIM));
    for (int i = 0; i < 6; ++i) {
        float sx = B.X + px(24) + i * px(48), sy = B.Y + px(102);
        RectF sr(sx, sy, px(34), px(34));
        bool sel = g_set.accent == i;
        float h = anim(K(A_ACCENT, i), hov(A_ACCENT, i) ? 1.f : 0.f, 18.f);
        if (sel || h > 0.01f) { RectF o = sr; o.Inflate(px(4), px(4)); Pen p(Color((BYTE)(sel ? 220 : 90 * h), 255, 255, 255), px(2)); g.DrawEllipse(&p, o); }
        LinearGradientBrush lb(sr, C(ACCENTS[i].a), C(ACCENTS[i].b), 45.f);
        g.FillEllipse(&lb, sr);
        addHot(sr, A_ACCENT, i);
    }
    text(g, L"Animaciones", RectF(B.X + px(24), B.Y + px(156), B.Width - px(120), px(30)), 13.5f, C(C_TEXT), F_SEMI);
    toggleSwitch(g, RectF(B.X + B.Width - px(24) - px(50), B.Y + px(158), px(50), px(28)), g_set.animations, A_TOGGLE_ANIM, 0);

    RectF Cc = two ? RectF(B.X, B.Y + B.Height + gap, B.Width, px(162)) : RectF(in.X, B.Y + B.Height + gap, in.Width, px(162));
    card(g, Cc, px(22));
    iconBadge(g, g_admin ? IC_SHIELD : IC_BOLT, Cc.X + px(38), Cc.Y + px(38), px(18), g_admin ? C_OK : C_WARN);
    text(g, L"Permisos y datos", RectF(Cc.X + px(70), Cc.Y + px(24), Cc.Width - px(90), px(28)), 16.f, C(C_TEXT), F_SEMI);
    text(g, g_admin ? L"Funcionando como administrador: acceso completo." : L"Modo usuario: Windows Update, el Temp de Windows y parte del registro necesitan permisos.",
         RectF(Cc.X + px(24), Cc.Y + px(64), Cc.Width - px(48), px(36)), 12.f, C(C_DIM), F_REG, AL_L, 0, TR_CHAR, true);
    float bw2 = (Cc.Width - px(48) - px(10)) / 2;
    if (!g_admin) button(g, RectF(Cc.X + px(24), Cc.Y + Cc.Height - px(54), bw2, px(38)), L"Elevar permisos", A_ELEVATE, 0, B_PRIMARY, IC_BOLT, true, 12.5f);
    else button(g, RectF(Cc.X + px(24), Cc.Y + Cc.Height - px(54), bw2, px(38)), L"Abrir registro", A_OPEN_LOG, 0, B_SECONDARY, IC_DOC, true, 12.5f);
    button(g, RectF(Cc.X + px(34) + bw2, Cc.Y + Cc.Height - px(54), bw2, px(38)), L"Carpeta del programa", A_OPEN_FOLDER, 0, B_SECONDARY, IC_FOLDER, true, 12.5f);
    float contentH = two ? std::max(A.Height, B.Height + gap + Cc.Height) : A.Height + B.Height + Cc.Height + 2 * gap;
    scrollbar(g, RectF(in.X, in.Y, in.Width + px(20), in.Height), contentH + px(10));
}

// ===================================================================
//  Ventanas modales
// ===================================================================
static RectF modalFrame(Graphics& g, float w, float h) {
    float t = ease(anim(KEY_MODAL, 1.f, 11.f));
    SolidBrush bd(Color((BYTE)(175 * t), 3, 5, 9));
    g.FillRectangle(&bd, 0.f, 0.f, (REAL)W, (REAL)H);
    addHot(RectF(0, 0, (REAL)W, (REAL)H), A_BLOCK, 0, false);
    RectF r((W - w) / 2, (H - h) / 2 + (1 - t) * px(22), w, h);
    card(g, r, px(26), 0, true);
    return r;
}
static void drawModal(Graphics& g) {
    if (g_modal == MD_NONE) return;
    if (g_modal == MD_CONFIRM) {
        uint64_t tot = 0, keptN = 0, keptB = 0; bool anyReg = false, anyHKLM = false;
        for (auto* i : g_pendingDel) {
            tot += i->size; keptN += i->keptFiles; keptB += i->keptBytes;
            if (i->kind == Kind::RegKey || i->kind == Kind::RegValue) { anyReg = true; if (i->hive == HKEY_LOCAL_MACHINE) anyHKLM = true; }
        }
        float mw = std::min((float)W - px(60), px(940));
        float mh = std::min((float)H - px(36), px(940));
        RectF r = modalFrame(g, mw, mh);
        glowCircle(g, r.X + px(60), r.Y + px(56), px(120), C_DANGER, 30);
        iconBadge(g, IC_TRASH, r.X + px(52), r.Y + px(52), px(22), C_DANGER);
        text(g, L"Revisa lo que se va a eliminar", RectF(r.X + px(90), r.Y + px(26), r.Width - px(116), px(32)), 21.f, C(C_TEXT), F_SEMI);
        text(g, fmtNum(g_pendingDel.size()) + (g_pendingDel.size() == 1 ? L" elemento · " : L" elementos · ") + fmtSize(tot) + L" a liberar · " +
                 fmtNum(g_cgroups.size()) + (g_cgroups.size() == 1 ? L" origen" : L" orígenes"),
             RectF(r.X + px(90), r.Y + px(58), r.Width - px(116), px(22)), 13.f, C(C_DIM));

        // Orígenes (resumen en fichas)
        float cx = r.X + px(28), cy = r.Y + px(96);
        int line = 0; size_t shownG = 0;
        for (auto& gr : g_cgroups) {
            wstring s = gr.name + L"  " + fmtSize(gr.size);
            float w = textW(g, s, 11.5f, F_SEMI) + px(38);
            if (cx + w > r.X + r.Width - px(28)) { if (++line >= 2) break; cx = r.X + px(28); cy += px(30); }
            cx += chip(g, cx, cy, s, gr.color, 11.5f, gr.icon) + px(8);
            ++shownG;
        }
        if (shownG < g_cgroups.size()) text(g, L"+" + fmtNum(g_cgroups.size() - shownG) + L" más", RectF(cx, cy, px(90), px(24)), 11.5f, C(C_DIM));
        float listTop = cy + px(36);
        text(g, L"De dónde sale cada cosa, qué es y por qué se propone borrarla. Desplázate para verlo todo:",
             RectF(r.X + px(28), listTop, r.Width - px(56), px(20)), 12.f, C(C_DIM));
        listTop += px(26);

        int nWarn = 1 + (g_confirmStrong ? 1 : 0) + (anyReg ? 1 : 0) + (anyHKLM && !g_admin ? 1 : 0) + (keptN ? 1 : 0);
        float bottomH = px(14) + nWarn * px(24) + px(8) + px(24) + px(50) + px(18) + px(64);
        RectF lb(r.X + px(24), listTop, r.Width - px(48), std::max(px(120), r.Y + r.Height - bottomH - listTop));
        fillRoundC(g, lb, px(16), Color(9, 255, 255, 255));
        { Pen p(Color(22, 255, 255, 255), px(1)); strokeRound(g, lb, px(16), p); }
        RectF view(lb.X + px(4), lb.Y + px(4), lb.Width - px(8), lb.Height - px(8));
        g_mScrollMax = std::max(0.f, px(g_crowsH) - view.Height);
        {
            GraphicsState st = g.Save();
            g.SetClip(view, CombineModeIntersect);
            float sc = g_mScroll / S;  // en unidades lógicas
            size_t first = 0;
            {   // primera fila visible (búsqueda binaria)
                size_t lo = 0, hi = g_crows.size();
                while (lo < hi) { size_t mid = (lo + hi) / 2; if (g_crows[mid].y + g_crows[mid].h < sc) lo = mid + 1; else hi = mid; }
                first = lo;
            }
            for (size_t ri = first; ri < g_crows.size(); ++ri) {
                const CRow& row = g_crows[ri];
                float ry = view.Y + px(row.y) - g_mScroll;
                if (ry > view.Y + view.Height) break;
                const CGroup& gr = g_cgroups[row.group];
                if (row.header) {
                    float top = ry + (row.group ? px(12) : 0);
                    RectF hr(view.X + px(4), top + px(4), view.Width - px(22), px(row.h - (row.group ? 12 : 0)) - px(8));
                    fillRoundC(g, hr, px(14), CA(gr.color, 24));
                    { Pen p(CA(gr.color, 60), px(1)); strokeRound(g, hr, px(14), p); }
                    iconBadge(g, gr.icon, hr.X + px(30), hr.Y + px(32), px(16), gr.color);
                    float nw = std::min(textW(g, gr.name, 15.f, F_SEMI), hr.Width - px(380));
                    text(g, gr.name, RectF(hr.X + px(56), hr.Y + px(8), nw + px(4), px(26)), 15.f, C(C_TEXT), F_SEMI);
                    wstring tg; ARGB tc; levelTag(gr.level, tg, tc);
                    chip(g, hr.X + px(66) + nw, hr.Y + px(10), tg, tc, 10.5f);
                    text(g, fmtNum(gr.items.size()) + (gr.items.size() == 1 ? L" elemento · " : L" elementos · ") + fmtSize(gr.size),
                         RectF(hr.X + hr.Width - px(260), hr.Y + px(8), px(244), px(26)), 13.f, C(C_TEXT), F_SEMI, AL_R);
                    text(g, L"Por qué: " + gr.why, RectF(hr.X + px(56), hr.Y + px(36), hr.Width - px(72), px(20)), 12.f, C(mix(gr.color, C_TEXT, 0.55f)));
                    if (gr.keptFiles) {
                        icon(g, IC_SHIELD, hr.X + px(64), hr.Y + px(68), px(13), C_OK);
                        text(g, L"Dentro hay " + fmtNum(gr.keptFiles) + L" vídeos, fotos o audios (" + fmtSize(gr.keptBytes) + L"): se conservan, no se borran",
                             RectF(hr.X + px(78), hr.Y + px(58), hr.Width - px(94), px(20)), 11.5f, C(C_OK));
                    }
                } else {
                    Item& it = *row.it;
                    RectF ir(view.X + px(4), ry, view.Width - px(22), px(row.h));
                    bool hv = hov(A_MROW, (int)ri) || hov(A_MROW_OPEN, (int)ri);
                    if (hv) fillRoundC(g, ir, px(12), Color(14, 255, 255, 255));
                    fillRoundC(g, RectF(ir.X + px(18), ir.Y + px(14), px(8), px(8)), px(4), C(gr.color));
                    float tw = ir.Width - px(40) - px(190);
                    wstring nm = it.kind == Kind::RecycleBin ? L"Papelera de reciclaje" : it.kind == Kind::RegValue ? it.valueName : nameOf(it.path);
                    text(g, nm, RectF(ir.X + px(36), ir.Y + px(6), tw, px(20)), 13.5f, C(C_TEXT), F_SEMI);
                    text(g, it.kind == Kind::RecycleBin ? L"Todas las unidades" : L"Ubicación: " + it.path,
                         RectF(ir.X + px(36), ir.Y + px(25), tw, px(18)), 11.5f, C(C_DIM), F_REG, AL_L, 1, TR_PATH);
                    wstring what = it.note.empty() ? gr.name : it.note;
                    text(g, L"Qué es: " + what, RectF(ir.X + px(36), ir.Y + px(42), tw, px(18)), 11.f, C(it.keptFiles ? C_OK : C_FAINT));
                    text(g, fmtSize(it.size), RectF(ir.X + ir.Width - px(190), ir.Y, px(130), ir.Height), 14.f, C(C_TEXT), F_SEMI, AL_R);
                    RectF hit = ir;
                    if (hit.Y < view.Y) { hit.Height -= view.Y - hit.Y; hit.Y = view.Y; }
                    if (hit.Y + hit.Height > view.Y + view.Height) hit.Height = view.Y + view.Height - hit.Y;
                    if (hit.Height > 0) addHot(hit, A_MROW, (int)ri, false);
                    RectF ob(ir.X + ir.Width - px(50), ir.Y + (ir.Height - px(34)) / 2, px(42), px(34));
                    if (hv && ob.Y >= view.Y && ob.Y + ob.Height <= view.Y + view.Height) {
                        bool oh = hov(A_MROW_OPEN, (int)ri);
                        fillRoundC(g, ob, px(10), Color(oh ? 40 : 20, 255, 255, 255));
                        icon(g, IC_EXTERNAL, ob.X + ob.Width / 2, ob.Y + ob.Height / 2, px(15), oh ? ACC1 : C_DIM);
                        addHot(ob, A_MROW_OPEN, (int)ri);
                    }
                }
            }
            g.Restore(st);
        }
        if (g_mScrollMax > 0) {  // barra de desplazamiento del cuadro
            float th = std::max(px(40), view.Height * view.Height / px(g_crowsH));
            float ty = view.Y + (view.Height - th) * (g_mScroll / g_mScrollMax);
            RectF tr(view.X + view.Width - px(8), ty, px(6), th);
            fillRoundC(g, tr, px(3), Color(hov(A_MSCROLL) || g_dragModal ? 130 : 70, 255, 255, 255));
            addHot(RectF(tr.X - px(6), tr.Y, px(18), tr.Height), A_MSCROLL, 0, false);
            g_mScrollRatio = g_mScrollMax / std::max(1.f, view.Height - th);
        }

        float y = lb.Y + lb.Height + px(14);
        auto warn = [&](ARGB c, int ic, const wstring& s) {
            icon(g, ic, r.X + px(40), y + px(11), px(15), c);
            text(g, s, RectF(r.X + px(58), y, r.Width - px(86), px(22)), 12.5f, C(mix(c, C_TEXT, 0.3f)));
            y += px(24);
        };
        if (keptN) warn(C_OK, IC_SHIELD, L"Se conservarán " + fmtNum(keptN) + L" vídeos, fotos y audios (" + fmtSize(keptB) + L"): Optimizalis nunca los borra.");
        warn(C_DANGER, IC_WARNING, L"No pasa por la Papelera y no se puede deshacer.");
        if (g_confirmStrong) warn(C_WARN, IC_WARNING, L"Incluye archivos personales o datos de programas: revísalo bien.");
        if (anyReg) warn(0xFFA78BFA, IC_REGISTRY, L"Se guarda una copia .reg de cada clave antes de borrarla.");
        if (anyHKLM && !g_admin) warn(C_WARN, IC_BOLT, L"Algunas claves del registro necesitan permisos de administrador.");
        y += px(10);
        text(g, L"Para confirmar, escribe ELIMINAR", RectF(r.X + px(28), y, r.Width - px(56), px(20)), 12.5f, C(C_TEXT), F_SEMI);
        RectF ib(r.X + px(28), y + px(26), r.Width - px(56), px(48));
        inputBox(g, ib, g_edConfirm, 2);
        g_rcEdConfirm = RectF(ib.X + px(18), ib.Y + (ib.Height - px(24)) / 2, ib.Width - px(36), px(24));
        g_visEdConfirm = true;
        bool ok = upper(trim(editText(g_edConfirm))) == L"ELIMINAR";
        float by = r.Y + r.Height - px(70);
        button(g, RectF(r.X + r.Width - px(28) - px(220), by, px(220), px(46)), L"Eliminar " + fmtSize(tot), A_MODAL_CONFIRM, 0, B_DANGER, IC_TRASH, ok);
        button(g, RectF(r.X + r.Width - px(28) - px(220) - px(140), by, px(130), px(46)), L"Cancelar", A_MODAL_CANCEL, 0, B_SECONDARY);
    } else if (g_modal == MD_DELETING) {
        RectF r = modalFrame(g, px(560), px(270));
        size_t i = g_work.delIndex, n = std::max<size_t>(1, g_work.delTotal);
        wstring cur; { std::lock_guard<std::mutex> l(g_work.m); cur = g_work.current; }
        spinner(g, r.X + px(56), r.Y + px(58), px(20), px(4), C_DANGER);
        text(g, g_work.stop ? L"Deteniendo…" : L"Eliminando…", RectF(r.X + px(96), r.Y + px(34), r.Width - px(120), px(32)), 21.f, C(C_TEXT), F_SEMI);
        text(g, fmtNum(i) + L" de " + fmtNum(n) + L" · " + fmtTime(GetTickCount64() - g_workStart), RectF(r.X + px(96), r.Y + px(66), r.Width - px(120), px(22)), 13.f, C(C_DIM));
        progressBar(g, RectF(r.X + px(30), r.Y + px(116), r.Width - px(60), px(10)), anim(K(66000, 1), (float)i / n, 8.f), 0xFFFB7185, ACC2);
        text(g, cur, RectF(r.X + px(30), r.Y + px(136), r.Width - px(60), px(22)), 12.f, C(C_DIM), F_REG, AL_L, 1, TR_PATH);
        button(g, RectF(r.X + (r.Width - px(160)) / 2, r.Y + r.Height - px(72), px(160), px(44)), L"Detener", A_DEL_STOP, 0, B_SECONDARY, IC_X, !g_work.stop);
    } else if (g_modal == MD_DELRESULT) {
        auto& d = g_delRes;
        int ne = (int)std::min<size_t>(4, d.errs.size());
        float mh = px(404) + (ne ? px(40) + ne * px(26) : 0) + (d.kept ? px(30) : 0);
        RectF r = modalFrame(g, px(600), mh);
        bool clean = d.fail == 0;
        ARGB mc = clean ? C_OK : C_WARN;
        float cx = r.X + r.Width / 2;
        glowCircle(g, cx, r.Y + px(78), px(130), mc, 46);
        float t = ease(anim(K(66000, 2), 1.f, 6.f));
        fillRoundC(g, RectF(cx - px(36), r.Y + px(42), px(72), px(72)), px(36), CA(mc, 40));
        Pen rp(CA(mc, 200), px(3)); g.DrawArc(&rp, cx - px(36), r.Y + px(42), px(72), px(72), -90, 360 * t);
        icon(g, clean ? IC_CHECK : IC_WARNING, cx, r.Y + px(78), px(34), mc, 255 * t);
        text(g, clean ? L"¡Listo! Espacio liberado" : L"Terminado con algunos avisos", RectF(r.X, r.Y + px(128), r.Width, px(30)), 19.f, C(C_TEXT), F_SEMI, AL_C);
        float fv = anim(KEY_FREED, (float)d.freed, 4.f);
        textGrad(g, fmtSize((uint64_t)fv), r.X, r.Y + px(160), px(64), 46.f, F_LIGHT, ACC1, ACC2, AL_C, r.Width);
        float sw = (r.Width - px(56) - px(20)) / 3, sy = r.Y + px(240);
        struct Sx { wstring v, l; ARGB c; };
        Sx sx[3] = {{fmtNum(d.ok), L"eliminados", C_OK}, {fmtNum(d.pending), L"al reiniciar", C_WARN}, {fmtNum(d.fail), L"con errores", d.fail ? C_DANGER : C_FAINT}};
        for (int i = 0; i < 3; ++i) {
            RectF b(r.X + px(28) + i * (sw + px(10)), sy, sw, px(64));
            fillRoundC(g, b, px(14), Color(10, 255, 255, 255));
            text(g, sx[i].v, RectF(b.X, b.Y + px(8), b.Width, px(28)), 20.f, C(sx[i].c), F_SEMI, AL_C);
            text(g, sx[i].l, RectF(b.X, b.Y + px(36), b.Width, px(18)), 11.5f, C(C_DIM), F_REG, AL_C);
        }
        float y = sy + px(80);
        if (d.kept) {
            icon(g, IC_SHIELD, r.X + px(40), y + px(11), px(15), C_OK);
            text(g, L"Se han conservado " + fmtNum(d.kept) + L" vídeos, fotos y audios (" + fmtSize(d.keptBytes) + L") que había dentro.",
                 RectF(r.X + px(58), y, r.Width - px(86), px(22)), 12.5f, C(C_OK));
            y += px(30);
        }
        if (ne) {
            text(g, g_admin ? L"Algunos elementos estaban en uso: cierra los programas y vuelve a intentarlo."
                            : L"Muchos errores se resuelven elevando permisos o cerrando los programas abiertos.",
                 RectF(r.X + px(28), y, r.Width - px(56), px(20)), 12.f, C(C_WARN));
            y += px(28);
            for (int i = 0; i < ne; ++i) {
                text(g, nameOf(d.errs[i].first) + L": " + d.errs[i].second, RectF(r.X + px(36), y, r.Width - px(72), px(24)), 11.5f, C(C_DIM));
                y += px(26);
            }
        }
        float by = r.Y + r.Height - px(70);
        button(g, RectF(r.X + r.Width - px(28) - px(140), by, px(140), px(46)), L"Aceptar", A_RESULT_OK, 0, B_PRIMARY);
        button(g, RectF(r.X + px(28), by, px(150), px(46)), L"Ver registro", A_OPEN_LOG, 0, B_GHOST, IC_DOC);
        if (g_delFromSearch) button(g, RectF(r.X + r.Width - px(28) - px(140) - px(200), by, px(190), px(46)), L"Verificar de nuevo", A_VERIFY_AGAIN, 0, B_SECONDARY, IC_SEARCH);
    } else if (g_modal == MD_INSTALLED) {
        RectF r = modalFrame(g, px(600), px(330));
        iconBadge(g, IC_WARNING, r.X + px(54), r.Y + px(56), px(24), C_WARN);
        wstring nm = g_installed.empty() ? g_searchTerm : g_installed[0].name;
        text(g, L"\"" + nm + L"\" sigue instalado", RectF(r.X + px(96), r.Y + px(32), r.Width - px(120), px(32)), 20.f, C(C_TEXT), F_SEMI);
        text(g, g_installed.size() > 1 ? L"Y " + fmtNum(g_installed.size() - 1) + L" programa(s) más con ese nombre." : L"Encontrado en Programas y características.",
             RectF(r.X + px(96), r.Y + px(64), r.Width - px(120), px(22)), 13.f, C(C_DIM));
        text(g, L"Para borrarlo bien, desinstálalo primero con su desinstalador oficial. Cuando termine, vuelve aquí y pulsa Buscar restos: Optimizalis encontrará y borrará todo lo que haya dejado.",
             RectF(r.X + px(30), r.Y + px(116), r.Width - px(60), px(80)), 13.f, C(C_TEXT), F_REG, AL_L, 0, TR_CHAR, true);
        float by = r.Y + r.Height - px(72);
        button(g, RectF(r.X + r.Width - px(28) - px(210), by, px(210), px(46)), L"Abrir desinstalador", A_UNINSTALL, 0, B_PRIMARY, IC_EXTERNAL);
        button(g, RectF(r.X + r.Width - px(28) - px(210) - px(210), by, px(200), px(46)), L"Buscar igualmente", A_SEARCH_ANYWAY, 0, B_SECONDARY, IC_SEARCH);
        button(g, RectF(r.X + px(24), by, px(110), px(46)), L"Cancelar", A_MODAL_CANCEL, 0, B_GHOST);
    } else if (g_modal == MD_INFO) {
        RectF r = modalFrame(g, px(520), px(250));
        iconBadge(g, IC_INFO, r.X + px(54), r.Y + px(56), px(24), ACC1);
        text(g, g_infoTitle, RectF(r.X + px(96), r.Y + px(40), r.Width - px(120), px(32)), 20.f, C(C_TEXT), F_SEMI);
        text(g, g_infoText, RectF(r.X + px(30), r.Y + px(104), r.Width - px(60), px(70)), 13.f, C(C_DIM), F_REG, AL_L, 0, TR_CHAR, true);
        button(g, RectF(r.X + r.Width - px(28) - px(130), r.Y + r.Height - px(70), px(130), px(44)), L"Entendido", A_INFO_OK, 0, B_PRIMARY);
    }
}

static void drawToast(Graphics& g) {
    if (g_toast.empty()) return;
    ULONGLONG age = GetTickCount64() - g_toastAt;
    if (age > 4200) { g_toast.clear(); return; }
    float t = ease(anim(KEY_TOAST, age > 3700 ? 0.f : 1.f, 10.f));
    g_needAnim = true;
    float tw = textW(g, g_toast, 13.f, F_SEMI);
    float w = tw + px(70), h = px(48);
    float cx = px(SIDEBAR) + (W - px(SIDEBAR)) / 2;
    RectF r(cx - w / 2, H - px(30) - h + (1 - t) * px(40), w, h);
    shadow(g, r, h / 2, 2.f * t);
    fillRoundC(g, r, h / 2, Color((BYTE)(245 * t), 22, 27, 38));
    Pen p(CA(g_toastColor, 120 * t), px(1)); strokeRound(g, r, h / 2, p);
    fillRoundC(g, RectF(r.X + px(12), r.Y + px(12), px(24), px(24)), px(12), CA(g_toastColor, 50 * t));
    icon(g, g_toastColor == C_WARN || g_toastColor == C_DANGER ? IC_INFO : IC_CHECK, r.X + px(24), r.Y + h / 2, px(14), g_toastColor, 255 * t);
    text(g, g_toast, RectF(r.X + px(46), r.Y, tw + px(8), h), 13.f, CA(C_TEXT, 255 * t), F_SEMI, AL_L, 1, TR_NONE);
}

// ===================================================================
//  Pintado
// ===================================================================
static void placeEdit(HWND e, const RectF& r, bool vis) {
    if (!e) return;
    if (!vis) { if (IsWindowVisible(e)) ShowWindow(e, SW_HIDE); return; }
    int x = (int)r.X, y = (int)r.Y, w = (int)r.Width, h = (int)r.Height;
    RECT cur; GetWindowRect(e, &cur); MapWindowPoints(nullptr, g_hwnd, (POINT*)&cur, 2);
    if (cur.left != x || cur.top != y || cur.right - cur.left != w || cur.bottom - cur.top != h)
        SetWindowPos(e, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
    if (!IsWindowVisible(e)) ShowWindow(e, SW_SHOWNA);
}
static void ensureBackbuffer(HDC hdc) {
    if (g_memDC) return;
    g_memDC = CreateCompatibleDC(hdc);
    g_memBmp = CreateCompatibleBitmap(hdc, W, H);
    g_memOld = (HBITMAP)SelectObject(g_memDC, g_memBmp);
}
static void freeBackbuffer() {
    if (!g_memDC) return;
    SelectObject(g_memDC, g_memOld);
    DeleteObject(g_memBmp);
    DeleteDC(g_memDC);
    g_memDC = nullptr;
}
static void paint(HDC hdc) {
    ensureBackbuffer(hdc);
    ULONGLONG now = GetTickCount64();
    g_dt = std::clamp((now - g_lastFrame) / 1000.f, 0.001f, 0.05f);
    g_lastFrame = now;
    g_needAnim = false;
    g_hots.clear();
    g_visEdSearch = g_visEdFilter = g_visEdConfirm = false;
    {
        Graphics g(g_memDC);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintAntiAlias);
        g.SetPixelOffsetMode(PixelOffsetModeHighQuality);
        if (!g_bgBmp) buildBg();
        if (!g_bgCached) g_bgCached = new CachedBitmap(g_bgBmp, &g);
        g.DrawCachedBitmap(g_bgCached, 0, 0);
        drawSidebar(g);

        RectF cr(px(SIDEBAR), 0, W - px(SIDEBAR), (REAL)H);
        g_scrollTarget = std::clamp(g_scrollTarget, 0.f, g_scrollMax);
        if (!g_dragThumb) {
            float k = g_set.animations ? 1.f - expf(-16.f * g_dt) : 1.f;
            g_scroll += (g_scrollTarget - g_scroll) * k;
            if (fabsf(g_scrollTarget - g_scroll) < 0.5f) g_scroll = g_scrollTarget; else g_needAnim = true;
        }
        if (g_modal == MD_CONFIRM) {
            g_mScrollTarget = std::clamp(g_mScrollTarget, 0.f, g_mScrollMax);
            if (!g_dragModal) {
                float k = g_set.animations ? 1.f - expf(-16.f * g_dt) : 1.f;
                g_mScroll += (g_mScrollTarget - g_mScroll) * k;
                if (fabsf(g_mScrollTarget - g_mScroll) < 0.5f) g_mScroll = g_mScrollTarget; else g_needAnim = true;
            }
        }
        float pt = ease(anim(KEY_PAGE, 1.f, 9.f));
        g_scrollMax = 0;
        GraphicsState st = g.Save();
        g.SetClip(cr);
        g.TranslateTransform(0, (1 - pt) * px(16));
        switch (g_page) {
        case PG_HOME: pageHome(g, cr); break;
        case PG_SCAN: pageScan(g, cr); break;
        case PG_RESULTS: pageResults(g, cr); break;
        case PG_DETAIL: pageDetail(g, cr); break;
        case PG_SEARCH: pageSearch(g, cr); break;
        case PG_PROTECT: pageProtect(g, cr); break;
        case PG_HISTORY: pageHistory(g, cr); break;
        case PG_SETTINGS: pageSettings(g, cr); break;
        }
        g.Restore(st);
        if (pt < 0.995f) {
            ColorMatrix cm = {{{1, 0, 0, 0, 0}, {0, 1, 0, 0, 0}, {0, 0, 1, 0, 0}, {0, 0, 0, 1.f - pt, 0}, {0, 0, 0, 0, 1}}};
            ImageAttributes ia; ia.SetColorMatrix(&cm);
            g.DrawImage(g_bgBmp, cr, cr.X, cr.Y, cr.Width, cr.Height, UnitPixel, &ia);
        }
        drawModal(g);
        drawToast(g);
        if (g_busy) g_needAnim = true;
        bool editsOk = pt > 0.98f;
        placeEdit(g_edSearch, g_rcEdSearch, g_visEdSearch && editsOk && g_modal == MD_NONE);
        placeEdit(g_edFilter, g_rcEdFilter, g_visEdFilter && editsOk && g_modal == MD_NONE);
        placeEdit(g_edConfirm, g_rcEdConfirm, g_visEdConfirm && g_modal == MD_CONFIRM && anim(KEY_MODAL, 1.f) > 0.9f);
    }
    BitBlt(hdc, 0, 0, W, H, g_memDC, 0, 0, SRCCOPY);
    bool want = g_needAnim || g_busy || !g_toast.empty();
    if (want && !g_timerOn) { SetTimer(g_hwnd, 1, 15, nullptr); g_timerOn = true; }
    else if (!want && g_timerOn) { KillTimer(g_hwnd, 1); g_timerOn = false; }
    hitTest();
}

// ===================================================================
//  Acciones
// ===================================================================
static void onAction(int id, int data, bool shift) {
    switch (id) {
    case A_NAV:
        if (data == PG_SEARCH && g_busy && g_busyKind == 1) { navigate(PG_SCAN); break; }
        navigate(data); break;
    case A_HOME_CARD: navigate(data == 1 ? PG_SEARCH : PG_PROTECT); break;
    case A_SCAN_QUICK: startScan(true); break;
    case A_SCAN_FULL: startScan(false); break;
    case A_RESCAN: startScan(g_lastQuick); break;
    case A_SAFE_CLEAN: if (!g_busy) safeCleanAsk(); break;
    case A_CANCEL_WORK: g_work.stop = true; invalidate(); break;
    case A_OPEN_CAT: if (data >= 0 && data < (int)g_cats.size()) openDetail(&g_cats[data], false); break;
    case A_BACK: navigate(g_detailIsSearch ? PG_SEARCH : PG_RESULTS); break;
    case A_ROW: {
        if (!g_detail || data < 0 || data >= (int)g_view.size()) break;
        Item& it = g_detail->items[g_view[data]];
        if (!it.blocked.empty()) { toast(L"Protegido: " + it.blocked, C_DANGER); break; }
        bool ns = !it.selected;
        if (shift && g_lastClick >= 0 && g_lastClick < (int)g_view.size()) {
            int a = std::min(g_lastClick, data), b = std::max(g_lastClick, data);
            for (int k = a; k <= b; ++k) { Item& x = g_detail->items[g_view[k]]; if (x.blocked.empty()) x.selected = ns; }
        } else it.selected = ns;
        g_lastClick = data;
        break;
    }
    case A_ROW_OPEN: if (g_detail && data >= 0 && data < (int)g_view.size()) openLocation(g_detail->items[g_view[data]]); break;
    case A_SEL_ALL: if (g_detail) { for (int i : g_view) if (g_detail->items[i].blocked.empty()) g_detail->items[i].selected = true; } break;
    case A_SEL_NONE: if (g_detail) for (auto& i : g_detail->items) i.selected = false; break;
    case A_SORT: g_sortMode = 1 - g_sortMode; rebuildView(); g_scrollTarget = 0; break;
    case A_DELETE: {
        if (!g_detail) break;
        std::vector<Item*> sel;
        for (auto& i : g_detail->items) if (i.selected && i.blocked.empty()) sel.push_back(&i);
        askDelete({{g_detail, sel}}, g_detailIsSearch);
        break;
    }
    case A_MODAL_CANCEL: g_modal = MD_NONE; SetFocus(g_hwnd); break;
    case A_MODAL_CONFIRM:
        if (g_modal == MD_CONFIRM && upper(trim(editText(g_edConfirm))) == L"ELIMINAR") startDelete();
        break;
    case A_DEL_STOP: g_work.stop = true; break;
    case A_MROW_OPEN:
        if (data >= 0 && data < (int)g_crows.size() && g_crows[data].it) openLocation(*g_crows[data].it);
        break;
    case A_RESULT_OK: g_modal = MD_NONE; break;
    case A_INFO_OK: g_modal = MD_NONE; break;
    case A_OPEN_LOG: openPath(logPath()); break;
    case A_OPEN_BACKUPS: CreateDirectoryW(backupDir().c_str(), nullptr); openPath(backupDir()); break;
    case A_OPEN_FOLDER: openPath(P.exeDir); break;
    case A_SEARCH_GO: if (!g_busy) searchGo(false); break;
    case A_SEARCH_ANYWAY: g_modal = MD_NONE; runSearch(g_searchTerm); break;
    case A_UNINSTALL:
        if (!g_installed.empty()) {
            bool ok = launchUninstaller(g_installed[0]);
            g_modal = MD_NONE;
            toast(ok ? L"Desinstalador abierto. Cuando termine, pulsa Buscar restos." : L"Se ha abierto Configuración > Aplicaciones", ok ? ACC1 : C_WARN);
        }
        break;
    case A_SEARCH_LAST: if (g_searchDone) openDetail(&g_search, true); break;
    case A_VERIFY_AGAIN:
        if (!g_searchTerm.empty() && !g_busy) { g_modal = MD_NONE; SetWindowTextW(g_edSearch, g_searchTerm.c_str()); runSearch(g_searchTerm); }
        break;
    case A_SET_DEC: case A_SET_INC: {
        int s = id == A_SET_INC ? 1 : -1;
        if (data == 0) g_set.largeMB = (uint64_t)std::clamp<long long>((long long)g_set.largeMB + s * (g_set.largeMB >= 1000 ? 250 : g_set.largeMB >= 200 ? 100 : 50), 50, 100000);
        if (data == 1) g_set.downloadDays = std::clamp(g_set.downloadDays + s * 15, 15, 3650);
        if (data == 2) g_set.dupMinMB = (uint64_t)std::clamp<long long>((long long)g_set.dupMinMB + s * (g_set.dupMinMB >= 10 ? 10 : 1), 1, 10000);
        if (data == 3) g_set.leftoverDays = std::clamp(g_set.leftoverDays + s * 15, 15, 3650);
        saveSettings();
        break;
    }
    case A_ACCENT:
        g_set.accent = data; ACC1 = ACCENTS[data].a; ACC2 = ACCENTS[data].b;
        saveSettings(); buildBg();
        break;
    case A_TOGGLE_ANIM: g_set.animations = !g_set.animations; saveSettings(); break;
    case A_ELEVATE:
        if (g_busy) { toast(L"Espera a que termine el trabajo en curso", C_WARN); break; }
        if (relaunchAdmin()) DestroyWindow(g_hwnd);
        else if (!g_admin) toast(L"No se concedieron permisos de administrador", C_WARN);
        break;
    case A_FOCUS_EDIT: SetFocus(data == 0 ? g_edSearch : data == 1 ? g_edFilter : g_edConfirm); break;
    default: break;
    }
    invalidate();
}

// ===================================================================
//  Ventana
// ===================================================================
static LRESULT CALLBACK EditProc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR) {
    if (m == WM_KEYDOWN) {
        if (w == VK_RETURN) {
            if (h == g_edSearch) onAction(A_SEARCH_GO, 0, false);
            else if (h == g_edConfirm) onAction(A_MODAL_CONFIRM, 0, false);
            return 0;
        }
        if (w == VK_ESCAPE) { SendMessageW(g_hwnd, WM_KEYDOWN, VK_ESCAPE, 0); return 0; }
    }
    if (m == WM_CHAR && (w == VK_RETURN || w == VK_ESCAPE)) return 0;
    if (m == WM_SETFOCUS || m == WM_KILLFOCUS) invalidate();
    return DefSubclassProc(h, m, w, l);
}
static void makeEditFont() {
    if (g_edFont) DeleteObject(g_edFont);
    g_edFont = CreateFontW(-(int)px(15), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    for (HWND e : {g_edSearch, g_edFilter, g_edConfirm}) if (e) SendMessageW(e, WM_SETFONT, (WPARAM)g_edFont, TRUE);
}
static HWND makeEdit(HWND parent, int id, const wchar_t* cue) {
    HWND e = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL, 0, 0, 10, 10, parent, (HMENU)(INT_PTR)id, g_inst, nullptr);
    SendMessageW(e, EM_SETCUEBANNER, TRUE, (LPARAM)cue);
    SendMessageW(e, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(0, 0));
    SetWindowSubclass(e, EditProc, 1, 0);
    return e;
}
static UINT dpiOf(HWND h) {
    typedef UINT(WINAPI * Fn)(HWND);
    static Fn fn = (Fn)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
    if (fn && h) return fn(h);
    HDC dc = GetDC(nullptr); UINT d = GetDeviceCaps(dc, LOGPIXELSX); ReleaseDC(nullptr, dc); return d;
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        g_hwnd = hwnd;
        S = dpiOf(hwnd) / 96.f;
        BOOL dark = TRUE; DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));
        COLORREF cap = RGB(9, 12, 18); DwmSetWindowAttribute(hwnd, 35, &cap, sizeof(cap));
        COLORREF tx = RGB(233, 237, 245); DwmSetWindowAttribute(hwnd, 36, &tx, sizeof(tx));
        int corner = 2; DwmSetWindowAttribute(hwnd, 33, &corner, sizeof(corner));
        g_inputBrush = CreateSolidBrush(RGB(0x12, 0x17, 0x22));
        g_edSearch = makeEdit(hwnd, 101, L"Nombre del programa: Spotify, Avast, uTorrent…");
        g_edFilter = makeEdit(hwnd, 102, L"Filtrar por nombre o ruta…");
        g_edConfirm = makeEdit(hwnd, 103, L"ELIMINAR");
        makeEditFont();
        refreshStats();
        return 0;
    }
    case WM_SIZE:
        if (wp == SIZE_MINIMIZED) return 0;
        W = std::max(1, (int)LOWORD(lp)); H = std::max(1, (int)HIWORD(lp));
        freeBackbuffer(); freeBg();
        invalidate();
        return 0;
    case WM_GETMINMAXINFO: {
        MINMAXINFO* mm = (MINMAXINFO*)lp;
        mm->ptMinTrackSize.x = (LONG)px(1180); mm->ptMinTrackSize.y = (LONG)px(760);
        return 0;
    }
    case WM_DPICHANGED: {
        S = HIWORD(wp) / 96.f;
        clearFonts(); makeEditFont();
        RECT* r = (RECT*)lp;
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        freeBackbuffer(); freeBg(); invalidate();
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
        paint(hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_TIMER: invalidate(); return 0;
    case WM_MOUSEMOVE: {
        g_mouse = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, hwnd, 0}; TrackMouseEvent(&t);
        if (g_dragThumb) {
            g_scrollTarget = g_scroll = std::clamp(g_dragScroll0 + (g_mouse.y - g_dragY0) * g_scrollRatio, 0.f, g_scrollMax);
            invalidate();
        }
        if (g_dragModal) {
            g_mScrollTarget = g_mScroll = std::clamp(g_dragScroll0 + (g_mouse.y - g_dragY0) * g_mScrollRatio, 0.f, g_mScrollMax);
            invalidate();
        }
        hitTest();
        return 0;
    }
    case WM_MOUSELEAVE: g_mouse = {-1, -1}; g_hovId = 0; g_hovData = 0; invalidate(); return 0;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) { SetCursor(LoadCursor(nullptr, g_hovHand ? IDC_HAND : IDC_ARROW)); return TRUE; }
        break;
    case WM_LBUTTONDOWN:
        g_mouse = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        hitTest();
        SetCapture(hwnd);
        g_prsId = g_hovId; g_prsData = g_hovData;
        if (g_hovId != A_FOCUS_EDIT) SetFocus(hwnd);
        if (g_hovId == A_SCROLLTHUMB) { g_dragThumb = true; g_dragY0 = (float)GET_Y_LPARAM(lp); g_dragScroll0 = g_scroll; }
        if (g_hovId == A_MSCROLL) { g_dragModal = true; g_dragY0 = (float)GET_Y_LPARAM(lp); g_dragScroll0 = g_mScroll; }
        invalidate();
        return 0;
    case WM_LBUTTONUP: {
        g_mouse = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        if (!g_dragThumb && !g_dragModal) hitTest();
        ReleaseCapture();
        bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        if (g_dragThumb) g_dragThumb = false;
        else if (g_dragModal) g_dragModal = false;
        else if (g_prsId && g_prsId == g_hovId && g_prsData == g_hovData) { int id = g_prsId, d = g_prsData; g_prsId = 0; onAction(id, d, shift); }
        g_prsId = 0;
        invalidate();
        return 0;
    }
    case WM_LBUTTONDBLCLK:
        if (g_hovId == A_ROW && g_detail && g_hovData < (int)g_view.size()) {
            Item& it = g_detail->items[g_view[g_hovData]];
            if (it.blocked.empty()) it.selected = !it.selected;  // deshace el clic anterior
            openLocation(it);
            invalidate();
        }
        return 0;
    case WM_MOUSEWHEEL:
        if (g_modal == MD_NONE) { g_scrollTarget -= GET_WHEEL_DELTA_WPARAM(wp) / 120.f * px(110); invalidate(); }
        else if (g_modal == MD_CONFIRM) { g_mScrollTarget -= GET_WHEEL_DELTA_WPARAM(wp) / 120.f * px(130); invalidate(); }
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) {
            if (g_modal == MD_CONFIRM || g_modal == MD_INSTALLED || g_modal == MD_INFO || g_modal == MD_DELRESULT) { g_modal = MD_NONE; SetFocus(hwnd); }
            else if (g_page == PG_DETAIL) onAction(A_BACK, 0, false);
            invalidate();
        } else if (wp == 'A' && (GetKeyState(VK_CONTROL) & 0x8000) && g_page == PG_DETAIL && g_modal == MD_NONE) onAction(A_SEL_ALL, 0, false);
        else if (wp == VK_NEXT) { g_scrollTarget += H * 0.7f; invalidate(); }
        else if (wp == VK_PRIOR) { g_scrollTarget -= H * 0.7f; invalidate(); }
        else if (wp == VK_HOME) { g_scrollTarget = 0; invalidate(); }
        else if (wp == VK_END) { g_scrollTarget = 1e9f; invalidate(); }
        return 0;
    case WM_COMMAND:
        if (HIWORD(wp) == EN_CHANGE) {
            if ((HWND)lp == g_edFilter) { g_filter = lower(trim(editText(g_edFilter))); rebuildView(); g_scrollTarget = g_scroll = 0; }
            invalidate();
        }
        return 0;
    case WM_CTLCOLOREDIT: {
        HDC dc = (HDC)wp;
        SetTextColor(dc, RGB(233, 237, 245));
        SetBkColor(dc, RGB(0x12, 0x17, 0x22));
        return (LRESULT)g_inputBrush;
    }
    case WM_SCAN_DONE: {
        joinWorker();
        bool cancelled = g_work.stop;
        { std::lock_guard<std::mutex> l(g_resMx); g_cats = std::move(g_resCats); g_resCats.clear(); }
        if (g_detail && !g_detailIsSearch) g_detail = nullptr;
        g_busy = false;
        g_scanned = true;
        g_scanWhen = stamp(false).substr(0, 16);
        g_page = -1;
        navigate(PG_RESULTS);
        toast(cancelled ? L"Análisis cancelado: se muestra lo encontrado hasta ahora" : L"Análisis terminado: " + fmtSize(allTotal()) + L" recuperables",
              cancelled ? C_WARN : C_OK);
        if (g_safeAfterScan && !cancelled) { g_safeAfterScan = false; safeCleanAsk(); }
        g_safeAfterScan = false;
        return 0;
    }
    case WM_SEARCH_DONE: {
        joinWorker();
        bool trunc;
        { std::lock_guard<std::mutex> l(g_resMx); g_search = std::move(g_resSearch); trunc = g_resTrunc; }
        g_busy = false;
        g_searchDone = true;
        openDetail(&g_search, true);
        if (trunc) showInfo(L"Demasiadas coincidencias", L"Se han encontrado más de 5.000 elementos y la búsqueda se ha detenido. Usa un nombre más concreto para no arrastrar archivos que no tienen nada que ver.");
        else toast(g_search.items.empty() ? L"No queda ningún resto de \"" + g_searchTerm + L"\"" : L"Encontrados " + fmtNum(g_search.items.size()) + L" elementos",
                   g_search.items.empty() ? C_OK : ACC1);
        return 0;
    }
    case WM_DEL_DONE: {
        joinWorker();
        { std::lock_guard<std::mutex> l(g_resMx); g_delRes = std::move(g_resDel); }
        g_busy = false;
        g_pendingDel.clear();
        for (auto& c : g_cats) pruneCat(c);
        pruneCat(g_search);
        rebuildView();
        refreshStats();
        g_modal = MD_DELRESULT;
        animSet(KEY_MODAL, 0); animSet(KEY_FREED, 0); animSet(K(66000, 2), 0);
        invalidate();
        return 0;
    }
    case WM_CLOSE:
        if (g_busy) { g_work.stop = true; joinWorker(); }
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int nShow) {
    g_inst = hInst;
    typedef BOOL(WINAPI * SetCtx)(HANDLE);
    if (auto f = (SetCtx)GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext")) f((HANDLE)-4);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);
    GdiplusStartupInput gsi; ULONG_PTR tok;
    GdiplusStartup(&tok, &gsi, nullptr);

    engineInit();
    ACC1 = ACCENTS[g_set.accent].a; ACC2 = ACCENTS[g_set.accent].b;
    animSet(KEY_GAUGE, 0); animSet(KEY_PAGE, 0);

    WNDCLASSEXW wc{sizeof(wc)};
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(1));
    wc.hIconSm = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(1), IMAGE_ICON, 16, 16, 0);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"OptimizalisVentana";
    RegisterClassExW(&wc);

    HDC dc = GetDC(nullptr); float sc = GetDeviceCaps(dc, LOGPIXELSX) / 96.f; ReleaseDC(nullptr, dc);
    RECT wa; SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int ww = std::min((int)(1340 * sc), (int)(wa.right - wa.left) - 40), wh = std::min((int)(860 * sc), (int)(wa.bottom - wa.top) - 40);
    int wx = wa.left + ((wa.right - wa.left) - ww) / 2, wy = wa.top + ((wa.bottom - wa.top) - wh) / 2;
    wstring title = g_admin ? L"Optimizalis (Administrador)" : L"Optimizalis";
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, title.c_str(), WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                                wx, wy, ww, wh, nullptr, nullptr, hInst, nullptr);
    if (!hwnd) return 1;
    ShowWindow(hwnd, nShow);
    UpdateWindow(hwnd);

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) { TranslateMessage(&m); DispatchMessageW(&m); }

    freeBg(); clearFonts();
    GdiplusShutdown(tok);
    CoUninitialize();
    return 0;
}
