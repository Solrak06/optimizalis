// =====================================================================
//  Optimizalis - motor: análisis, protección y eliminación
// =====================================================================
#include "engine.hpp"
#include <shlobj.h>
#include <shellapi.h>
#include <knownfolders.h>
#include <sddl.h>
#include <map>
#include <set>
#include <algorithm>
#include <functional>
#include <cstdio>
#include <cwchar>
#include <cwctype>

#ifndef FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS
#define FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS 0x00400000
#endif
#ifndef FILE_ATTRIBUTE_RECALL_ON_OPEN
#define FILE_ATTRIBUTE_RECALL_ON_OPEN 0x00040000
#endif

Paths P;
Settings g_set;
WorkState g_work;
bool g_admin = false;

// ===================================================================
//  Texto
// ===================================================================
static std::string toUtf8(const wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}
static wstring fromUtf8(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}
wstring lower(wstring s) { if (!s.empty()) CharLowerBuffW(&s[0], (DWORD)s.size()); return s; }
wstring upper(wstring s) { if (!s.empty()) CharUpperBuffW(&s[0], (DWORD)s.size()); return s; }
wstring trim(const wstring& s) {
    size_t a = s.find_first_not_of(L" \t\r\n");
    if (a == wstring::npos) return L"";
    size_t b = s.find_last_not_of(L" \t\r\n");
    return s.substr(a, b - a + 1);
}
static bool startsWith(const wstring& s, const wstring& p) { return s.size() >= p.size() && s.compare(0, p.size(), p) == 0; }
static bool endsWith(const wstring& s, const wstring& p) { return s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0; }
static bool contains(const wstring& s, const wstring& p) { return s.find(p) != wstring::npos; }
static wstring alnum(const wstring& s) { wstring r; for (wchar_t c : lower(s)) if (iswalnum(c)) r += c; return r; }
static WVec words(const wstring& s) {
    WVec v; wstring cur;
    for (wchar_t c : lower(s)) { if (iswalnum(c)) cur += c; else { if (!cur.empty()) v.push_back(cur); cur.clear(); } }
    if (!cur.empty()) v.push_back(cur);
    return v;
}
wstring fmtSize(uint64_t b) {
    const wchar_t* u[] = {L"B", L"KB", L"MB", L"GB", L"TB"};
    double v = (double)b; int i = 0;
    while (v >= 1024.0 && i < 4) { v /= 1024.0; ++i; }
    wchar_t buf[48];
    swprintf(buf, 48, i == 0 ? L"%.0f %ls" : (v >= 100 ? L"%.0f %ls" : L"%.1f %ls"), v, u[i]);
    wstring s = buf;
    for (auto& c : s) if (c == L'.') c = L',';
    return s;
}
wstring fmtNum(uint64_t n) {
    wstring s = std::to_wstring(n), r;
    int k = 0;
    for (int i = (int)s.size() - 1; i >= 0; --i) { r.insert(r.begin(), s[i]); if (++k % 3 == 0 && i > 0) r.insert(r.begin(), L'.'); }
    return r;
}
static wstring num(uint64_t n) { return std::to_wstring(n); }
static wstring shorten(const wstring& s, size_t n) {
    if (s.size() <= n) return s;
    size_t head = n / 3, tail = n - head - 1;
    return s.substr(0, head) + L"…" + s.substr(s.size() - tail);
}

// ===================================================================
//  Sistema de archivos
// ===================================================================
static wstring lp(const wstring& p) {
    if (startsWith(p, L"\\\\?\\")) return p;
    if (startsWith(p, L"\\\\")) return L"\\\\?\\UNC\\" + p.substr(2);
    return L"\\\\?\\" + p;
}
static wstring join(const wstring& a, const wstring& b) {
    if (a.empty()) return b;
    wchar_t c = a.back();
    if (c == L'\\' || c == L'/') return a + b;
    return a + L"\\" + b;
}
wstring parentOf(const wstring& p) {
    size_t k = p.find_last_of(L"\\/");
    if (k == wstring::npos) return L"";
    if (k == 2 && p.size() > 2 && p[1] == L':') return p.substr(0, 3);
    return p.substr(0, k);
}
wstring nameOf(const wstring& p) { size_t k = p.find_last_of(L"\\/"); return k == wstring::npos ? p : p.substr(k + 1); }
static wstring extOf(const wstring& p) {
    wstring n = nameOf(p);
    size_t k = n.find_last_of(L'.');
    return k == wstring::npos ? L"" : lower(n.substr(k));
}
// Vídeos, audios y fotos: Optimizalis NUNCA los elimina, estén donde estén.
// (.ts no se incluye porque casi siempre es código TypeScript)
static const std::set<wstring>& mediaExts() {
    static const std::set<wstring> s = {
        // vídeo
        L".mp4", L".m4v", L".mov", L".mkv", L".avi", L".wmv", L".webm", L".flv", L".f4v", L".mpg", L".mpeg",
        L".mpe", L".m2ts", L".mts", L".m2v", L".3gp", L".3g2", L".vob", L".ogv", L".mxf", L".hevc", L".h264",
        L".h265", L".divx", L".xvid", L".rm", L".rmvb", L".asf", L".dv", L".braw", L".r3d", L".insv", L".lrv",
        // audio
        L".mp3", L".wav", L".flac", L".m4a", L".aac", L".ogg", L".oga", L".opus", L".wma", L".aiff", L".aif",
        L".alac", L".ape", L".amr", L".m4b", L".mka", L".dsf", L".dff",
        // fotos
        L".jpg", L".jpeg", L".jfif", L".heic", L".heif", L".avif", L".dng", L".raw", L".cr2", L".cr3", L".crw",
        L".nef", L".nrw", L".arw", L".srf", L".sr2", L".orf", L".rw2", L".raf", L".pef", L".srw", L".x3f",
        L".tif", L".tiff", L".psd", L".psb"};
    return s;
}
bool isMediaFile(const wstring& path) { return mediaExts().count(extOf(path)) > 0; }
wstring mediaKinds() { return L"vídeos (mp4, mov, mkv, avi...), audios (mp3, wav, flac...) y fotos (jpg, heic, raw, tiff, psd...)"; }

static DWORD attrsOf(const wstring& p) { return GetFileAttributesW(lp(p).c_str()); }
static bool exists(const wstring& p) { return attrsOf(p) != INVALID_FILE_ATTRIBUTES; }
static bool isDirPath(const wstring& p) { DWORD a = attrsOf(p); return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY); }
static wstring norm(wstring p) {
    if (startsWith(p, L"\\\\?\\")) p = p.substr(4);
    for (auto& c : p) if (c == L'/') c = L'\\';
    p = lower(p);
    while (p.size() > 1 && p.back() == L'\\') p.pop_back();
    return p;
}
static bool inside(const wstring& n, const wstring& root) {
    if (root.empty() || n.size() < root.size()) return false;
    if (n.compare(0, root.size(), root) != 0) return false;
    return n.size() == root.size() || n[root.size()] == L'\\';
}
static bool insideStrict(const wstring& n, const wstring& root) { return n.size() > root.size() && inside(n, root); }
static uint64_t ftToU(const FILETIME& f) { return (uint64_t(f.dwHighDateTime) << 32) | f.dwLowDateTime; }
static uint64_t nowFT() { FILETIME f; GetSystemTimeAsFileTime(&f); return ftToU(f); }
static int daysSince(uint64_t ft) { uint64_t n = nowFT(); if (ft == 0 || ft >= n) return 0; return int((n - ft) / 864000000000ULL); }
static wstring fmtDate(uint64_t ft) {
    if (!ft) return L"?";
    FILETIME f{DWORD(ft & 0xFFFFFFFF), DWORD(ft >> 32)}, lf; SYSTEMTIME st;
    FileTimeToLocalFileTime(&f, &lf); FileTimeToSystemTime(&lf, &st);
    wchar_t b[32]; swprintf(b, 32, L"%02u/%02u/%04u", (unsigned)st.wDay, (unsigned)st.wMonth, (unsigned)st.wYear);
    return b;
}
static uint64_t fdSize(const WIN32_FIND_DATAW& fd) { return (uint64_t(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow; }
static bool cloudOnly(DWORD a) { return (a & (FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS | FILE_ATTRIBUTE_RECALL_ON_OPEN | FILE_ATTRIBUTE_OFFLINE)) != 0; }
static bool isDots(const wchar_t* n) { return n[0] == L'.' && (n[1] == 0 || (n[1] == L'.' && n[2] == 0)); }
static bool fileInfo(const wstring& p, uint64_t& size, uint64_t& mtime, DWORD& attrs) {
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (!GetFileAttributesExW(lp(p).c_str(), GetFileExInfoStandard, &d)) return false;
    size = (uint64_t(d.nFileSizeHigh) << 32) | d.nFileSizeLow;
    mtime = ftToU(d.ftLastWriteTime);
    attrs = d.dwFileAttributes;
    return true;
}
static wstring expandEnv(const wstring& s) {
    DWORD n = ExpandEnvironmentStringsW(s.c_str(), nullptr, 0);
    if (!n) return s;
    std::vector<wchar_t> b(n + 1);
    ExpandEnvironmentStringsW(s.c_str(), b.data(), n);
    return b.data();
}
static wstring envv(const wchar_t* n) {
    std::vector<wchar_t> b(32768);
    DWORD k = GetEnvironmentVariableW(n, b.data(), 32768);
    return (k && k < 32768) ? wstring(b.data(), k) : L"";
}
static wstring known(REFKNOWNFOLDERID id) {
    PWSTR p = nullptr; wstring r;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &p)) && p) r = p;
    if (p) CoTaskMemFree(p);
    return r;
}
static wstring errMsg(DWORD e) {
    wchar_t* buf = nullptr;
    DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                             nullptr, e, 0, (LPWSTR)&buf, 0, nullptr);
    wstring s = (n && buf) ? trim(wstring(buf, n)) : L"Error " + std::to_wstring(e);
    if (buf) LocalFree(buf);
    return s;
}
static void listDir(const wstring& dir, WVec& out, bool dirsOnly = false) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW(lp(join(dir, L"*")).c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (isDots(fd.cFileName)) continue;
        bool d = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (dirsOnly && (!d || (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))) continue;
        out.push_back(join(dir, fd.cFileName));
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}
static bool hasGlob(const wstring& dir, const wchar_t* pattern) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(lp(join(dir, pattern)).c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return false;
    FindClose(h);
    return true;
}
static WVec fixedDrives() {
    WVec v; wchar_t buf[512];
    DWORD n = GetLogicalDriveStringsW(511, buf);
    if (!n || n > 511) return v;
    for (wchar_t* p = buf; *p; p += wcslen(p) + 1)
        if (GetDriveTypeW(p) == DRIVE_FIXED) v.push_back(p);
    return v;
}

// ===================================================================
//  Progreso y recorrido (nunca sigue enlaces/junctions)
// ===================================================================
static void tick(const wstring& where) {
    g_work.count.fetch_add(1, std::memory_order_relaxed);
    static thread_local ULONGLONG last = 0;
    ULONGLONG t = GetTickCount64();
    if (t - last >= 70) {
        last = t;
        std::lock_guard<std::mutex> l(g_work.m);
        g_work.current = where;
    }
}
typedef std::function<bool(const wstring&, const WIN32_FIND_DATAW&, int)> WalkFn;
static void walk(const wstring& dir, const WalkFn& fn, int depth = 0) {
    if (g_work.stop || depth > 300) return;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW(lp(join(dir, L"*")).c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch,
                                nullptr, FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE) return;
    WVec subs;
    do {
        if (isDots(fd.cFileName)) continue;
        wstring p = join(dir, fd.cFileName);
        tick(p);
        bool d = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        bool rp = (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
        bool go = fn(p, fd, depth);
        if (d && !rp && go) subs.push_back(p);
        if (g_work.stop) break;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    for (auto& s : subs) walk(s, fn, depth + 1);
}

struct SizeInfo { uint64_t bytes = 0, files = 0, newest = 0, keptFiles = 0, keptBytes = 0; bool hasExe = false; };
static void measure(const wstring& p, SizeInfo& si) {
    uint64_t sz, mt; DWORD a;
    if (!fileInfo(p, sz, mt, a)) return;
    if (mt > si.newest) si.newest = mt;
    if (!(a & FILE_ATTRIBUTE_DIRECTORY)) {
        if (isMediaFile(p)) { si.keptFiles++; if (!cloudOnly(a)) si.keptBytes += sz; return; }
        if (!cloudOnly(a)) si.bytes += sz;
        si.files++;
        wstring e = extOf(p);
        if (e == L".exe" || e == L".dll") si.hasExe = true;
        return;
    }
    if (a & FILE_ATTRIBUTE_REPARSE_POINT) return;
    walk(p, [&](const wstring& q, const WIN32_FIND_DATAW& fd, int) {
        uint64_t t = ftToU(fd.ftLastWriteTime);
        if (t > si.newest) si.newest = t;
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            if (isMediaFile(q)) { si.keptFiles++; if (!cloudOnly(fd.dwFileAttributes)) si.keptBytes += fdSize(fd); return true; }
            if (!cloudOnly(fd.dwFileAttributes)) si.bytes += fdSize(fd);
            si.files++;
            if (!si.hasExe) { wstring e = extOf(q); if (e == L".exe" || e == L".dll") si.hasExe = true; }
        }
        return true;
    });
}

// ===================================================================
//  Rutas del sistema
// ===================================================================
static wstring longName(const wstring& p) {
    std::vector<wchar_t> b(32768);
    DWORD n = GetLongPathNameW(p.c_str(), b.data(), 32768);
    return (n && n < 32768) ? wstring(b.data(), n) : p;
}
static wstring stripSlash(wstring p) { while (p.size() > 3 && (p.back() == L'\\' || p.back() == L'/')) p.pop_back(); return p; }

static void initPaths() {
    P.windir = envv(L"SystemRoot");
    if (P.windir.empty()) { wchar_t b[MAX_PATH]; GetWindowsDirectoryW(b, MAX_PATH); P.windir = b; }
    P.sysdrive = P.windir.substr(0, 2) + L"\\";
    P.profile = known(FOLDERID_Profile);
    P.local = known(FOLDERID_LocalAppData);
    P.roaming = known(FOLDERID_RoamingAppData);
    P.localLow = known(FOLDERID_LocalAppDataLow);
    P.programData = known(FOLDERID_ProgramData);
    P.pf = known(FOLDERID_ProgramFiles);
    P.pf86 = known(FOLDERID_ProgramFilesX86);
    P.desktop = known(FOLDERID_Desktop);
    P.documents = known(FOLDERID_Documents);
    P.downloads = known(FOLDERID_Downloads);
    P.pictures = known(FOLDERID_Pictures);
    P.music = known(FOLDERID_Music);
    P.videos = known(FOLDERID_Videos);
    P.publicDir = known(FOLDERID_Public);
    P.onedrive = envv(L"OneDrive");
    P.appdata = join(P.profile, L"AppData");
    P.users = parentOf(P.profile);
    wchar_t t[MAX_PATH + 2];
    DWORD n = GetTempPathW(MAX_PATH + 1, t);
    P.temp = n ? stripSlash(longName(wstring(t, n))) : join(P.local, L"Temp");
    std::vector<wchar_t> b(32768);
    DWORD k = GetModuleFileNameW(nullptr, b.data(), 32768);
    P.exePath = wstring(b.data(), k);
    P.exeDir = parentOf(P.exePath);
    P.smUser = join(P.roaming, L"Microsoft\\Windows\\Start Menu\\Programs");
    P.smAll = join(P.programData, L"Microsoft\\Windows\\Start Menu\\Programs");
}

// ===================================================================
//  PROTECCIÓN: lo esencial queda bloqueado
// ===================================================================
static bool vendorProtected(const wstring& firstLower) {
    static const std::set<wstring> s = {
        L"common files", L"internet explorer", L"reference assemblies", L"dotnet", L"msbuild",
        L"modifiablewindowsapps", L"uninstall information", L"windowsapps", L"packages", L"package cache",
        L"regid.1991-06.com.microsoft", L"ssh", L"usoshared", L"usoprivate", L"desktop", L"documents",
        L"start menu", L"templates", L"application data", L"softwaredistribution", L"drivers", L"default",
        L"all users", L"comms", L"intel", L"amd", L"nvidia", L"nvidia corporation", L"realtek", L"dell",
        L"hp", L"hewlett-packard", L"lenovo", L"asus", L"acer", L"synaptics", L"elan", L"waves",
        L"dolby", L"qualcomm", L"mediatek", L"broadcom", L"conexant", L"vmware", L"oracle"};
    if (startsWith(firstLower, L"windows") || startsWith(firstLower, L"microsoft")) return true;
    return s.count(firstLower) > 0;
}

struct Guard {
    WVec exact, hard, allowStrict, allowTree, explicitRoots;
    std::set<wstring> allowExact, badNames;
    std::mutex treeMx;

    void ex(const wstring& p) { if (!p.empty()) exact.push_back(norm(p)); }
    void hd(const wstring& p) { if (!p.empty()) hard.push_back(norm(p)); }
    void al(const wstring& p) { if (!p.empty()) { allowStrict.push_back(norm(p)); ex(p); } }
    void tree(const wstring& p) {
        if (p.empty()) return;
        std::lock_guard<std::mutex> l(treeMx);
        wstring n = norm(p);
        if (std::find(allowTree.begin(), allowTree.end(), n) == allowTree.end()) allowTree.push_back(n);
    }

    void init() {
        for (const wstring& p : {P.windir, P.profile, P.appdata, P.local, P.roaming, P.localLow, P.programData,
                                 P.pf, P.pf86, P.temp, P.desktop, P.documents, P.downloads, P.pictures, P.music,
                                 P.videos, P.onedrive, P.users, P.publicDir, P.smUser, P.smAll, P.exeDir})
            ex(p);
        for (const wchar_t* n : {L"Favorites", L"Contacts", L"Links", L"Saved Games", L"Searches", L"3D Objects",
                                 L"OneDrive", L"Desktop", L"Documents", L"Downloads", L"Pictures", L"Music", L"Videos"})
            ex(join(P.profile, n));
        ex(join(P.local, L"Programs"));
        for (const wstring& sm : {P.smUser, P.smAll})
            for (const wchar_t* n : {L"Startup", L"Administrative Tools", L"Accessibility", L"Accessories",
                                     L"System Tools", L"Windows PowerShell", L"Windows Tools", L"Maintenance"})
                ex(join(sm, n));

        hd(P.windir);
        for (const wchar_t* n : {L"Recovery", L"Boot", L"EFI", L"$WinREAgent", L"Config.Msi", L"Documents and Settings"})
            hd(join(P.sysdrive, n));
        hd(P.exeDir);
        hd(join(P.local, L"Microsoft"));
        hd(join(P.roaming, L"Microsoft"));
        hd(join(P.localLow, L"Microsoft"));
        hd(join(P.local, L"Packages"));
        hd(join(P.programData, L"Microsoft"));
        hd(join(P.pf, L"WindowsApps"));

        al(join(P.windir, L"Temp"));
        al(join(P.windir, L"SoftwareDistribution\\Download"));
        al(join(P.windir, L"Minidump"));
        al(join(P.windir, L"LiveKernelReports"));
        al(join(P.windir, L"Logs\\CBS"));
        al(join(P.windir, L"Logs\\DISM"));
        al(join(P.windir, L"ServiceProfiles\\NetworkService\\AppData\\Local\\Microsoft\\Windows\\DeliveryOptimization\\Cache"));
        al(join(P.programData, L"Microsoft\\Windows\\WER\\ReportArchive"));
        al(join(P.programData, L"Microsoft\\Windows\\WER\\ReportQueue"));
        al(join(P.local, L"Microsoft\\Windows\\WER"));
        al(join(P.local, L"Microsoft\\Windows\\Explorer"));
        al(join(P.local, L"Microsoft\\Windows\\INetCache"));
        al(P.smUser);
        al(P.smAll);
        allowExact.insert(norm(join(P.windir, L"MEMORY.DMP")));

        for (const wchar_t* n : {L"pagefile.sys", L"hiberfil.sys", L"swapfile.sys", L"bootmgr", L"bootnxt",
                                 L"bootsect.bak", L"ntldr", L"boot.ini", L"usrclass.dat", L"usrclass.dat.log1",
                                 L"usrclass.dat.log2", L"dumpstack.log", L"dumpstack.log.tmp"})
            badNames.insert(n);

        for (const wstring& p : {P.pf, P.pf86, P.programData, join(P.local, L"Programs")})
            if (!p.empty()) explicitRoots.push_back(norm(p));
    }

    // "" = se puede eliminar; si no, el motivo del bloqueo.
    wstring check(const wstring& path, bool explicitMode) {
        wstring n = norm(path);
        if (n.size() <= 3) return L"Raíz de una unidad";
        if (contains(n, L"\\system volume information") || contains(n, L"\\$recycle.bin"))
            return L"Zona reservada del sistema";
        for (auto& e : exact) if (n == e) return L"Carpeta esencial del sistema o del usuario";
        for (auto& e : exact) if (insideStrict(e, n)) return L"Contiene carpetas esenciales";
        for (auto& e : hard) if (insideStrict(e, n)) return L"Contiene carpetas esenciales";
        wstring nm = nameOf(n);
        if (badNames.count(nm) || startsWith(nm, L"ntuser.")) return L"Archivo esencial de Windows";
        if (allowExact.count(n)) return L"";
        DWORD a = attrsOf(path);
        if (a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY) && (a & FILE_ATTRIBUTE_SYSTEM))
            return L"Archivo marcado como de sistema";
        if (a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY) && isMediaFile(n))
            return L"Vídeo, audio o foto: Optimizalis nunca los elimina";
        {
            std::lock_guard<std::mutex> l(treeMx);
            for (auto& r : allowTree) if (inside(n, r)) return L"";
        }
        for (auto& r : allowStrict) if (insideStrict(n, r)) return L"";
        for (auto& h : hard) if (inside(n, h)) return L"Zona protegida de Windows";
        for (auto& r : explicitRoots) {
            if (insideStrict(n, r)) {
                wstring first = n.substr(r.size() + 1);
                first = first.substr(0, first.find(L'\\'));
                if (vendorProtected(first)) return L"Componente de Windows, Microsoft o controladores";
                if (!explicitMode) return L"Carpeta de programas (solo en búsqueda de restos)";
            }
        }
        return L"";
    }
} G;

static bool regNameProtected(const wstring& l) {
    static const std::set<wstring> s = {
        L"microsoft", L"classes", L"policies", L"wow6432node", L"clients", L"registeredapplications",
        L"windows", L"windows nt", L"description", L"setup", L"system", L"hardware", L"sam", L"security",
        L"odbc", L"partner", L"intel", L"realtek", L"nvidia corporation", L"amd", L"ati technologies",
        L"khronos", L"appdatalow", L"default", L"environment", L"control panel", L"keyboard layout",
        L"printers", L"network", L"console", L"volatile environment", L"appevents", L"eudc", L"identities",
        L"software", L"drivers", L"dell", L"lenovo", L"hp", L"asus"};
    return s.count(l) > 0;
}

// ===================================================================
//  Elementos
// ===================================================================
static Item* addPathItem(Category& c, const wstring& p, const wstring& note, bool explicitOk = false,
                         bool showBlocked = false, int minAgeDays = 0) {
    DWORD a = attrsOf(p);
    if (a == INVALID_FILE_ATTRIBUTES) return nullptr;
    Item it;
    it.path = p; it.note = note; it.explicitOk = explicitOk;
    it.isDir = (a & FILE_ATTRIBUTE_DIRECTORY) != 0;
    it.blocked = G.check(p, explicitOk);
    if (!it.blocked.empty() && !showBlocked) return nullptr;
    SizeInfo si; measure(p, si);
    it.size = si.bytes;
    if (minAgeDays > 0 && si.newest && daysSince(si.newest) < minAgeDays) return nullptr;
    if (it.isDir && si.keptFiles) {
        if (si.files == 0 && it.blocked.empty()) return nullptr;  // solo contiene multimedia: no hay nada que borrar
        it.keptFiles = si.keptFiles; it.keptBytes = si.keptBytes;
        it.note += L" · se conservará" + wstring(si.keptFiles == 1 ? L" 1 vídeo, foto o audio (" : L"n " + fmtNum(si.keptFiles) + L" vídeos, fotos o audios (") +
                   fmtSize(si.keptBytes) + L")";
    }
    c.items.push_back(it);
    return &c.items.back();
}
static void addChildren(Category& c, const wstring& dir, const wstring& note, int minAgeDays = 0) {
    if (!isDirPath(dir)) return;
    WVec kids; listDir(dir, kids);
    for (auto& k : kids) { if (g_work.stop) return; addPathItem(c, k, note, false, false, minAgeDays); }
}
static void addCache(Category& c, const wstring& dir, const wstring& note) {
    if (!isDirPath(dir)) return;
    G.tree(dir);
    addPathItem(c, dir, note);
}

// ===================================================================
//  Registro de Windows
// ===================================================================
static wstring hiveName(HKEY h) { return h == HKEY_LOCAL_MACHINE ? L"HKLM" : L"HKCU"; }
static wstring regDisplay(HKEY h, REGSAM view, const wstring& sub) {
    wstring s = sub;
    if (h == HKEY_LOCAL_MACHINE && view == KEY_WOW64_32KEY && startsWith(lower(s), L"software\\"))
        s = s.substr(0, 8) + L"\\WOW6432Node" + s.substr(8);
    return hiveName(h) + L"\\" + s;
}
static WVec regEnumKeys(HKEY root, const wstring& sub, REGSAM view) {
    WVec v; HKEY h;
    if (RegOpenKeyExW(root, sub.c_str(), 0, KEY_READ | view, &h) != ERROR_SUCCESS) return v;
    wchar_t name[512];
    for (DWORD i = 0;; ++i) {
        DWORD len = 512;
        LONG e = RegEnumKeyExW(h, i, name, &len, nullptr, nullptr, nullptr, nullptr);
        if (e == ERROR_NO_MORE_ITEMS) break;
        if (e == ERROR_SUCCESS) v.push_back(wstring(name, len));
        else if (e != ERROR_MORE_DATA) break;
    }
    RegCloseKey(h);
    return v;
}
static wstring regStr(HKEY h, const wchar_t* val) {
    DWORD type = 0, sz = 0;
    if (RegQueryValueExW(h, val, nullptr, &type, nullptr, &sz) != ERROR_SUCCESS) return L"";
    if (type != REG_SZ && type != REG_EXPAND_SZ) return L"";
    std::vector<wchar_t> b(sz / 2 + 2, 0);
    if (RegQueryValueExW(h, val, nullptr, &type, (BYTE*)b.data(), &sz) != ERROR_SUCCESS) return L"";
    wstring s(b.data());
    if (type == REG_EXPAND_SZ) s = expandEnv(s);
    return s;
}
static DWORD regDword(HKEY h, const wchar_t* val) {
    DWORD type = 0, v = 0, sz = sizeof(v);
    if (RegQueryValueExW(h, val, nullptr, &type, (BYTE*)&v, &sz) != ERROR_SUCCESS || type != REG_DWORD) return 0;
    return v;
}
static bool regKeyExists(HKEY root, const wstring& sub, REGSAM view) {
    HKEY h;
    if (RegOpenKeyExW(root, sub.c_str(), 0, KEY_READ | view, &h) != ERROR_SUCCESS) return false;
    RegCloseKey(h);
    return true;
}
static std::vector<Uninst> readUninstall() {
    std::vector<Uninst> v;
    struct R { HKEY h; REGSAM view; } rs[] = {{HKEY_LOCAL_MACHINE, KEY_WOW64_64KEY},
                                              {HKEY_LOCAL_MACHINE, KEY_WOW64_32KEY},
                                              {HKEY_CURRENT_USER, 0}};
    const wstring base = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall";
    for (auto& r : rs) {
        for (auto& k : regEnumKeys(r.h, base, r.view)) {
            HKEY hk; wstring sub = base + L"\\" + k;
            if (RegOpenKeyExW(r.h, sub.c_str(), 0, KEY_READ | r.view, &hk) != ERROR_SUCCESS) continue;
            Uninst u;
            u.name = regStr(hk, L"DisplayName");
            u.publisher = regStr(hk, L"Publisher");
            u.location = trim(regStr(hk, L"InstallLocation"));
            u.location.erase(std::remove(u.location.begin(), u.location.end(), L'"'), u.location.end());
            u.location = stripSlash(u.location);
            u.uninstall = regStr(hk, L"UninstallString");
            u.sysComp = regDword(hk, L"SystemComponent") == 1;
            RegCloseKey(hk);
            if (u.name.empty()) continue;
            u.hive = r.h; u.view = r.view; u.sub = sub;
            v.push_back(u);
        }
    }
    return v;
}
static wstring exeFromCommand(wstring cmd) {
    cmd = trim(expandEnv(cmd));
    if (cmd.empty()) return L"";
    if (cmd[0] == L'"') { size_t e = cmd.find(L'"', 1); return e == wstring::npos ? cmd.substr(1) : cmd.substr(1, e - 1); }
    wstring l = lower(cmd);
    size_t k = l.find(L".exe");
    if (k != wstring::npos) return cmd.substr(0, k + 4);
    size_t sp = cmd.find(L' ');
    return sp == wstring::npos ? cmd : cmd.substr(0, sp);
}
static bool uninstallerExists(const Uninst& u) {
    wstring exe = exeFromCommand(u.uninstall);
    if (exe.empty() || exe.find(L'\\') == wstring::npos) return true;
    wstring n = lower(nameOf(exe));
    if (n == L"msiexec.exe" || n == L"rundll32.exe" || n == L"cmd.exe" || n == L"powershell.exe" || n == L"wscript.exe")
        return true;
    return exists(exe);
}

// ===================================================================
//  Registro de operaciones, copias del registro y ajustes
// ===================================================================
wstring stamp(bool compact) {
    SYSTEMTIME s; GetLocalTime(&s); wchar_t b[64];
    if (compact) swprintf(b, 64, L"%04u%02u%02u_%02u%02u%02u", (unsigned)s.wYear, (unsigned)s.wMonth, (unsigned)s.wDay, (unsigned)s.wHour, (unsigned)s.wMinute, (unsigned)s.wSecond);
    else swprintf(b, 64, L"%04u-%02u-%02u %02u:%02u:%02u", (unsigned)s.wYear, (unsigned)s.wMonth, (unsigned)s.wDay, (unsigned)s.wHour, (unsigned)s.wMinute, (unsigned)s.wSecond);
    return b;
}
wstring logPath() { return join(P.exeDir, L"registro_eliminaciones.txt"); }
wstring backupDir() { return join(P.exeDir, L"copias_registro"); }
static std::mutex g_logMx;
static void logLine(const wstring& s) {
    std::lock_guard<std::mutex> l(g_logMx);
    HANDLE h = CreateFileW(logPath().c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    LARGE_INTEGER sz{}; GetFileSizeEx(h, &sz);
    std::string u;
    if (sz.QuadPart == 0) u = "\xEF\xBB\xBF";
    u += toUtf8(stamp(false) + L"  " + s + L"\r\n");
    DWORD w; WriteFile(h, u.data(), (DWORD)u.size(), &w, nullptr);
    CloseHandle(h);
}
static bool runHidden(const wstring& cmd, DWORD timeoutMs, DWORD& code) {
    STARTUPINFOW si{}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> buf(cmd.begin(), cmd.end()); buf.push_back(0);
    if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) return false;
    WaitForSingleObject(pi.hProcess, timeoutMs);
    code = 1; GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    return true;
}
static bool regBackup(HKEY hive, const wstring& sub, REGSAM view, wstring& file) {
    CreateDirectoryW(backupDir().c_str(), nullptr);
    static int counter = 0;
    file = join(backupDir(), stamp(true) + L"_" + std::to_wstring(++counter) + L".reg");
    wstring cmd = L"reg.exe export \"" + hiveName(hive) + L"\\" + sub + L"\" \"" + file + L"\" /y";
    if (view == KEY_WOW64_32KEY) cmd += L" /reg:32";
    else if (view == KEY_WOW64_64KEY) cmd += L" /reg:64";
    DWORD code = 1;
    return runHidden(cmd, 60000, code) && code == 0 && exists(file);
}
static wstring iniPath() { return join(P.exeDir, L"ajustes.ini"); }
static int iniInt(const wchar_t* k, int def) { return (int)GetPrivateProfileIntW(L"Optimizalis", k, def, iniPath().c_str()); }
static void iniSet(const wchar_t* k, long long v) { WritePrivateProfileStringW(L"Optimizalis", k, std::to_wstring(v).c_str(), iniPath().c_str()); }
static void loadSettings() {
    g_set.largeMB = (uint64_t)std::max(10, iniInt(L"ArchivosGrandesMB", 500));
    g_set.downloadDays = std::max(1, iniInt(L"DiasDescargas", 60));
    g_set.dupMinMB = (uint64_t)std::max(1, iniInt(L"DuplicadosMinMB", 1));
    g_set.leftoverDays = std::max(7, iniInt(L"DiasRestos", 60));
    g_set.accent = std::min(5, std::max(0, iniInt(L"Acento", 0)));
    g_set.animations = iniInt(L"Animaciones", 1) != 0;
}
void saveSettings() {
    iniSet(L"ArchivosGrandesMB", (long long)g_set.largeMB);
    iniSet(L"DiasDescargas", g_set.downloadDays);
    iniSet(L"DuplicadosMinMB", (long long)g_set.dupMinMB);
    iniSet(L"DiasRestos", g_set.leftoverDays);
    iniSet(L"Acento", g_set.accent);
    iniSet(L"Animaciones", g_set.animations ? 1 : 0);
}

void engineInit() {
    initPaths();
    G.init();
    g_admin = IsUserAnAdmin() != FALSE;
    loadSettings();
    logLine(L"INICIO     Optimizalis 2.0" + wstring(g_admin ? L" (administrador)" : L" (usuario)"));
}

// ===================================================================
//  Motor de eliminación
// ===================================================================
static bool delFile(const wstring& p, uint64_t sz, DelResult& r) {
    if (isMediaFile(p)) { r.kept++; r.keptBytes += sz; return false; }  // regla absoluta: nunca se borra
    wstring L = lp(p);
    SetFileAttributesW(L.c_str(), FILE_ATTRIBUTE_NORMAL);
    if (DeleteFileW(L.c_str())) { r.freed += sz; return true; }
    DWORD e = GetLastError();
    if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) return true;
    if (MoveFileExW(p.size() < MAX_PATH ? p.c_str() : L.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT)) { r.pending++; return true; }
    r.err(p, errMsg(e));
    return false;
}
static bool delDir(const wstring& p, DelResult& r) {
    wstring L = lp(p);
    DWORD a = GetFileAttributesW(L.c_str());
    if (a == INVALID_FILE_ATTRIBUTES) return true;
    if (a & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_HIDDEN))
        SetFileAttributesW(L.c_str(), FILE_ATTRIBUTE_DIRECTORY);
    if (a & FILE_ATTRIBUTE_REPARSE_POINT) {  // se quita el enlace, nunca el destino
        if (RemoveDirectoryW(L.c_str())) return true;
        r.err(p, errMsg(GetLastError()));
        return false;
    }
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW(lp(join(p, L"*")).c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
    struct Child { wstring path; bool dir; uint64_t size; };
    std::vector<Child> kids;
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (isDots(fd.cFileName)) continue;
            bool d = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            kids.push_back({join(p, fd.cFileName), d, d ? 0 : fdSize(fd)});
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    uint64_t kept0 = r.kept;
    for (auto& k : kids) { if (k.dir) delDir(k.path, r); else delFile(k.path, k.size, r); }
    if (r.kept > kept0) return false;  // contiene vídeos/fotos/audios conservados: la carpeta se queda con ellos
    if (RemoveDirectoryW(L.c_str())) return true;
    DWORD e = GetLastError();
    if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) return true;
    if (MoveFileExW(p.size() < MAX_PATH ? p.c_str() : L.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT)) { r.pending++; return true; }
    if (e != ERROR_DIR_NOT_EMPTY) r.err(p, errMsg(e));
    return false;
}
// --- Papelera de reciclaje: se analiza por dentro para no borrar nunca vídeos, audios ni fotos ---
static wstring userSid() {
    HANDLE tok; wstring r;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) return r;
    DWORD n = 0;
    GetTokenInformation(tok, TokenUser, nullptr, 0, &n);
    std::vector<BYTE> b(n ? n : 1);
    if (n && GetTokenInformation(tok, TokenUser, b.data(), n, &n)) {
        LPWSTR s = nullptr;
        if (ConvertSidToStringSidW(((TOKEN_USER*)b.data())->User.Sid, &s)) { r = s; LocalFree(s); }
    }
    CloseHandle(tok);
    return r;
}
static WVec recycleDirs() {
    WVec v; wstring sid = userSid();
    if (sid.empty()) return v;
    for (auto& d : fixedDrives()) { wstring p = join(d, L"$Recycle.Bin\\" + sid); if (isDirPath(p)) v.push_back(p); }
    return v;
}
static void recycleScan(uint64_t& mediaN, uint64_t& mediaB, uint64_t& otherB) {
    mediaN = mediaB = otherB = 0;
    for (auto& bin : recycleDirs()) {
        WVec kids; listDir(bin, kids);
        for (auto& k : kids) {
            if (!startsWith(upper(nameOf(k)), L"$R")) continue;
            SizeInfo si; measure(k, si);
            mediaN += si.keptFiles; mediaB += si.keptBytes; otherB += si.bytes;
        }
    }
}
static void recycleEmptyKeepingMedia(DelResult& r) {
    for (auto& bin : recycleDirs()) {
        WVec kids; listDir(bin, kids);
        for (auto& k : kids) {
            wstring nm = nameOf(k);
            if (!startsWith(upper(nm), L"$R")) continue;
            DWORD a = attrsOf(k);
            if (a == INVALID_FILE_ATTRIBUTES) continue;
            if (a & FILE_ATTRIBUTE_DIRECTORY) delDir(k, r);
            else { uint64_t sz = 0, mt; DWORD at; fileInfo(k, sz, mt, at); delFile(k, sz, r); }
            if (!exists(k)) {  // su ficha $I solo se borra si el contenido se ha ido del todo
                wstring info = join(bin, L"$I" + nm.substr(2));
                SetFileAttributesW(lp(info).c_str(), FILE_ATTRIBUTE_NORMAL);
                DeleteFileW(lp(info).c_str());
            }
        }
        SHChangeNotify(SHCNE_UPDATEDIR, SHCNF_PATHW, bin.c_str(), nullptr);
    }
}

static bool deleteRegKey(Item& it, DelResult& r) {
    if (!regKeyExists(it.hive, it.subkey, it.view)) return true;
    wstring file;
    if (!regBackup(it.hive, it.subkey, it.view, file)) {
        r.err(it.path, L"No se pudo hacer copia de seguridad; por seguridad no se toca la clave");
        return false;
    }
    size_t k = it.subkey.find_last_of(L'\\');
    if (k == wstring::npos) { r.err(it.path, L"Clave raíz: protegida"); return false; }
    wstring parent = it.subkey.substr(0, k), child = it.subkey.substr(k + 1);
    HKEY h;
    LONG e = RegOpenKeyExW(it.hive, parent.c_str(), 0, KEY_ALL_ACCESS | it.view, &h);
    if (e != ERROR_SUCCESS) { r.err(it.path, errMsg((DWORD)e)); return false; }
    e = RegDeleteTreeW(h, child.c_str());
    if (e == ERROR_SUCCESS || e == ERROR_FILE_NOT_FOUND) { RegDeleteKeyExW(h, child.c_str(), it.view, 0); e = ERROR_SUCCESS; }
    RegCloseKey(h);
    if (e != ERROR_SUCCESS) { r.err(it.path, errMsg((DWORD)e)); return false; }
    logLine(L"REGISTRO   clave eliminada  " + it.path + L"  (copia: " + file + L")");
    return true;
}
static bool deleteRegValue(Item& it, DelResult& r) {
    wstring file;
    if (!regBackup(it.hive, it.subkey, it.view, file)) {
        r.err(it.path, L"No se pudo hacer copia de seguridad; por seguridad no se toca el valor");
        return false;
    }
    HKEY h;
    LONG e = RegOpenKeyExW(it.hive, it.subkey.c_str(), 0, KEY_SET_VALUE | it.view, &h);
    if (e != ERROR_SUCCESS) { r.err(it.path, errMsg((DWORD)e)); return false; }
    e = RegDeleteValueW(h, it.valueName.c_str());
    RegCloseKey(h);
    if (e != ERROR_SUCCESS && e != ERROR_FILE_NOT_FOUND) { r.err(it.path, errMsg((DWORD)e)); return false; }
    logLine(L"REGISTRO   valor eliminado  " + it.path + L"  (copia: " + file + L")");
    return true;
}

DelResult runDelete(std::vector<Item*>& sel) {
    DelResult r;
    g_work.stop = false;
    g_work.delTotal = sel.size();
    g_work.delIndex = 0;
    for (Item* it : sel) {
        if (g_work.stop) break;
        g_work.delIndex.fetch_add(1);
        { std::lock_guard<std::mutex> l(g_work.m); g_work.current = it->path; }
        switch (it->kind) {
        case Kind::Path: {
            wstring why = G.check(it->path, it->explicitOk);  // comprobación final de seguridad
            if (!why.empty()) { r.err(it->path, L"BLOQUEADO: " + why); r.fail++; break; }
            DWORD a = attrsOf(it->path);
            if (a == INVALID_FILE_ATTRIBUTES) { it->done = true; r.ok++; break; }
            uint64_t freed0 = r.freed, pend0 = r.pending, kept0 = r.kept, keptB0 = r.keptBytes;
            size_t errs0 = r.errs.size();
            if (a & FILE_ATTRIBUTE_DIRECTORY) delDir(it->path, r);
            else { uint64_t sz = 0, mt; DWORD at; fileInfo(it->path, sz, mt, at); delFile(it->path, sz, r); }
            uint64_t got = r.freed - freed0;
            if (!exists(it->path)) {
                it->done = true; r.ok++;
                logLine(L"ELIMINADO  " + fmtSize(got) + L"  " + it->path);
            } else if (r.kept > kept0 && r.errs.size() == errs0) {
                it->done = true; r.ok++;
                logLine(L"ELIMINADO  " + fmtSize(got) + L"  " + it->path + L"  (conservados " + fmtNum(r.kept - kept0) +
                        L" vídeos/fotos/audios, " + fmtSize(r.keptBytes - keptB0) + L")");
            } else if (r.pending > pend0) {
                it->done = true; r.ok++;
                logLine(L"REINICIO   " + fmtSize(got) + L"  " + it->path);
            } else {
                r.fail++;
                if (got) logLine(L"PARCIAL    " + fmtSize(got) + L"  " + it->path);
                if (r.errs.size() == errs0) r.err(it->path, L"No se pudo eliminar por completo (archivos en uso o sin permiso)");
            }
            break;
        }
        case Kind::RecycleBin: {
            uint64_t mediaN = 0, mediaB = 0, other = 0;
            recycleScan(mediaN, mediaB, other);
            if (mediaN == 0) {
                HRESULT hr = SHEmptyRecycleBinW(nullptr, nullptr, SHERB_NOCONFIRMATION | SHERB_NOPROGRESSUI | SHERB_NOSOUND);
                if (SUCCEEDED(hr) || hr == E_UNEXPECTED) {
                    r.freed += it->size; r.ok++; it->done = true;
                    logLine(L"ELIMINADO  " + fmtSize(it->size) + L"  Papelera de reciclaje vaciada");
                } else { r.fail++; r.err(it->path, errMsg((DWORD)hr)); }
            } else {
                // Vaciado selectivo: se borra todo menos los vídeos, audios y fotos
                uint64_t freed0 = r.freed, kept0 = r.kept;
                size_t errs0 = r.errs.size();
                recycleEmptyKeepingMedia(r);
                it->done = true; r.ok++;
                logLine(L"ELIMINADO  " + fmtSize(r.freed - freed0) + L"  Papelera de reciclaje (conservados " +
                        fmtNum(r.kept - kept0) + L" vídeos/fotos/audios)");
                if (r.errs.size() > errs0) r.fail++;
            }
            break;
        }
        case Kind::RegKey:
            if (deleteRegKey(*it, r)) { it->done = true; r.ok++; } else r.fail++;
            break;
        case Kind::RegValue:
            if (deleteRegValue(*it, r)) { it->done = true; r.ok++; } else r.fail++;
            break;
        }
    }
    logLine(L"RESUMEN    " + num(r.ok) + L" eliminados, " + num(r.fail) + L" con errores, " + fmtSize(r.freed) +
            L" liberados" + (r.kept ? L", " + num(r.kept) + L" multimedia conservados" : L"") + L" [bytes=" + num(r.freed) + L"]");
    return r;
}

void pruneCat(Category& c) {
    c.items.erase(std::remove_if(c.items.begin(), c.items.end(), [](const Item& i) {
        if (i.done) return true;
        if (i.kind == Kind::Path && !exists(i.path)) return true;
        return false;
    }), c.items.end());
}
void sortCat(Category& c) {
    std::stable_sort(c.items.begin(), c.items.end(), [](const Item& a, const Item& b) {
        bool ab = !a.blocked.empty(), bb = !b.blocked.empty();
        if (ab != bb) return bb;
        return a.size > b.size;
    });
}

// ===================================================================
//  ESCÁNERES
// ===================================================================
static void scanUserTemp(Category& c) { addChildren(c, P.temp, L"Temporal del usuario", 1); }
static void scanWinTemp(Category& c) { addChildren(c, join(P.windir, L"Temp"), L"Temporal de Windows", 1); }

static void scanBrowsers(Category& c) {
    struct B { wstring base, label; };
    std::vector<B> bs = {
        {join(P.local, L"Google\\Chrome\\User Data"), L"Chrome"},
        {join(P.local, L"Microsoft\\Edge\\User Data"), L"Edge"},
        {join(P.local, L"BraveSoftware\\Brave-Browser\\User Data"), L"Brave"},
        {join(P.local, L"Vivaldi\\User Data"), L"Vivaldi"},
        {join(P.local, L"Chromium\\User Data"), L"Chromium"},
        {join(P.local, L"Yandex\\YandexBrowser\\User Data"), L"Yandex"}};
    const wchar_t* profSubs[] = {L"Cache", L"Code Cache", L"GPUCache", L"Service Worker\\CacheStorage",
                                 L"Service Worker\\ScriptCache", L"DawnCache", L"DawnGraphiteCache",
                                 L"DawnWebGPUCache", L"GrShaderCache"};
    for (auto& b : bs) {
        if (!isDirPath(b.base)) continue;
        WVec kids; listDir(b.base, kids, true);
        for (auto& k : kids) {
            wstring n = nameOf(k);
            if (n == L"Default" || startsWith(n, L"Profile ") || n == L"Guest Profile" || n == L"System Profile")
                for (auto s : profSubs) addCache(c, join(k, s), b.label + L" (" + n + L") · cierra el navegador antes");
        }
        for (auto s : {L"ShaderCache", L"GrShaderCache", L"GraphiteDawnCache"})
            addCache(c, join(b.base, s), b.label + L" · caché de gráficos");
    }
    for (auto& o : {join(P.local, L"Opera Software\\Opera Stable"), join(P.local, L"Opera Software\\Opera GX Stable")})
        for (auto s : profSubs) addCache(c, join(o, s), L"Opera · cierra el navegador antes");
    WVec profs; listDir(join(P.local, L"Mozilla\\Firefox\\Profiles"), profs, true);
    for (auto& p : profs)
        for (auto s : {L"cache2", L"startupCache", L"thumbnails", L"jumpListCache"})
            addCache(c, join(p, s), L"Firefox · cierra el navegador antes");
}

static void scanAppCaches(Category& c) {
    struct A { wstring path, note; };
    std::vector<A> v = {
        {join(P.roaming, L"discord\\Cache"), L"Discord"}, {join(P.roaming, L"discord\\Code Cache"), L"Discord"},
        {join(P.roaming, L"discord\\GPUCache"), L"Discord"},
        {join(P.local, L"Spotify\\Data"), L"Spotify (canciones en caché)"},
        {join(P.local, L"Spotify\\Browser\\Cache"), L"Spotify"},
        {join(P.roaming, L"Code\\Cache"), L"VS Code"}, {join(P.roaming, L"Code\\CachedData"), L"VS Code"},
        {join(P.roaming, L"Code\\CachedExtensionVSIXs"), L"VS Code"}, {join(P.roaming, L"Code\\Code Cache"), L"VS Code"},
        {join(P.roaming, L"Code\\GPUCache"), L"VS Code"}, {join(P.roaming, L"Code\\logs"), L"VS Code (registros)"},
        {join(P.roaming, L"Slack\\Cache"), L"Slack"}, {join(P.roaming, L"Slack\\Code Cache"), L"Slack"},
        {join(P.roaming, L"Slack\\Service Worker\\CacheStorage"), L"Slack"},
        {join(P.roaming, L"Microsoft\\Teams\\Cache"), L"Teams clásico"},
        {join(P.roaming, L"Microsoft\\Teams\\Code Cache"), L"Teams clásico"},
        {join(P.roaming, L"Microsoft\\Teams\\GPUCache"), L"Teams clásico"},
        {join(P.roaming, L"Microsoft\\Teams\\Service Worker\\CacheStorage"), L"Teams clásico"},
        {join(P.local, L"Steam\\htmlcache"), L"Steam (navegador interno)"},
        {join(P.local, L"D3DSCache"), L"Caché de sombreadores DirectX"},
        {join(P.local, L"NVIDIA\\DXCache"), L"Caché de sombreadores NVIDIA"},
        {join(P.local, L"NVIDIA\\GLCache"), L"Caché de sombreadores NVIDIA"},
        {join(P.local, L"AMD\\DxCache"), L"Caché de sombreadores AMD"},
        {join(P.local, L"AMD\\DxcCache"), L"Caché de sombreadores AMD"},
        {join(P.local, L"AMD\\VkCache"), L"Caché de sombreadores AMD"},
        {join(P.local, L"AMD\\GLCache"), L"Caché de sombreadores AMD"},
        {join(P.localLow, L"NVIDIA\\PerDriverVersion\\DXCache"), L"Caché de sombreadores NVIDIA"}};
    for (auto& a : v) addCache(c, a.path, a.note);
    addChildren(c, join(P.local, L"Microsoft\\Windows\\INetCache"), L"Caché de Internet de Windows");
}

static void scanDevCaches(Category& c) {
    struct A { wstring path, note; };
    std::vector<A> v = {
        {join(P.local, L"npm-cache"), L"npm (se descarga de nuevo al instalar)"},
        {join(P.roaming, L"npm-cache"), L"npm (se descarga de nuevo al instalar)"},
        {join(P.local, L"pip\\cache"), L"pip / Python"}, {join(P.profile, L".cache\\pip"), L"pip / Python"},
        {join(P.local, L"uv\\cache"), L"uv / Python"}, {join(P.local, L"pypoetry\\Cache"), L"Poetry / Python"},
        {join(P.local, L"NuGet\\v3-cache"), L"NuGet"}, {join(P.local, L"NuGet\\Cache"), L"NuGet"},
        {join(P.local, L"NuGet\\plugins-cache"), L"NuGet"},
        {join(P.profile, L".nuget\\packages"), L"NuGet (paquetes; se descargan al compilar)"},
        {join(P.local, L"Yarn\\Cache"), L"Yarn"}, {join(P.profile, L".bun\\install\\cache"), L"Bun"},
        {join(P.profile, L".gradle\\caches"), L"Gradle"}, {join(P.profile, L".gradle\\wrapper\\dists"), L"Gradle (wrapper)"},
        {join(P.profile, L".m2\\repository"), L"Maven (se descarga al compilar)"},
        {join(P.profile, L".cargo\\registry\\cache"), L"Cargo / Rust"}, {join(P.local, L"go-build"), L"Go"},
        {join(P.local, L"electron\\Cache"), L"Electron"}, {join(P.local, L"electron-builder\\Cache"), L"electron-builder"},
        {join(P.local, L"node-gyp\\Cache"), L"node-gyp"},
        {join(P.local, L"ms-playwright"), L"Navegadores de Playwright (se descargan de nuevo)"},
        {join(P.local, L"Microsoft\\vscode-cpptools\\ipch"), L"IntelliSense de C/C++ (VS Code)"}};
    for (auto& a : v) addCache(c, a.path, a.note);
}

static void scanUpdate(Category& c) {
    addChildren(c, join(P.windir, L"SoftwareDistribution\\Download"), L"Actualización ya instalada");
    addChildren(c, join(P.windir, L"ServiceProfiles\\NetworkService\\AppData\\Local\\Microsoft\\Windows\\DeliveryOptimization\\Cache"),
                L"Optimización de distribución");
}

static void scanErrors(Category& c) {
    addChildren(c, join(P.local, L"CrashDumps"), L"Volcado de un programa que se cerró");
    addChildren(c, join(P.local, L"Microsoft\\Windows\\WER"), L"Informe de errores de Windows");
    addChildren(c, join(P.programData, L"Microsoft\\Windows\\WER\\ReportArchive"), L"Informe de errores archivado");
    addChildren(c, join(P.programData, L"Microsoft\\Windows\\WER\\ReportQueue"), L"Informe de errores en cola");
    addChildren(c, join(P.windir, L"Minidump"), L"Minivolcado (pantallazo azul)");
    addPathItem(c, join(P.windir, L"MEMORY.DMP"), L"Volcado completo de memoria");
    addChildren(c, join(P.windir, L"LiveKernelReports"), L"Informe del kernel");
}

static void scanThumbs(Category& c) {
    WVec kids; listDir(join(P.local, L"Microsoft\\Windows\\Explorer"), kids);
    for (auto& k : kids) {
        wstring n = lower(nameOf(k));
        if ((startsWith(n, L"thumbcache_") || startsWith(n, L"iconcache_")) && endsWith(n, L".db"))
            addPathItem(c, k, L"Se regenera sola · en uso por el Explorador (puede quedar para el reinicio)");
    }
}

static void scanLogs(Category& c) {
    struct D { wstring dir; std::set<wstring> exts; };
    std::vector<D> v = {{join(P.windir, L"Logs\\CBS"), {L".log", L".cab"}}, {join(P.windir, L"Logs\\DISM"), {L".log"}}};
    for (auto& d : v) {
        WVec kids; listDir(d.dir, kids);
        for (auto& k : kids) if (d.exts.count(extOf(k))) addPathItem(c, k, L"Registro de mantenimiento de Windows");
    }
}

static void scanOldWindows(Category& c) {
    for (auto n : {L"Windows.old", L"$Windows.~BT", L"$Windows.~WS", L"$GetCurrent", L"$SysReset"})
        addPathItem(c, join(P.sysdrive, n), L"Restos de una instalación o actualización anterior de Windows");
}

static void scanRecycle(Category& c) {
    SHQUERYRBINFO q{}; q.cbSize = sizeof(q);
    if (SUCCEEDED(SHQueryRecycleBinW(nullptr, &q)) && q.i64NumItems > 0) {
        Item it; it.kind = Kind::RecycleBin;
        it.path = L"Papelera de reciclaje (" + fmtNum((uint64_t)q.i64NumItems) + L" elementos, todas las unidades)";
        uint64_t mediaN, mediaB, other;
        recycleScan(mediaN, mediaB, other);
        if (mediaN) {
            it.size = other;
            it.keptFiles = mediaN; it.keptBytes = mediaB;
            it.note = L"Se vacía todo excepto " + fmtNum(mediaN) + L" vídeos/fotos/audios (" + fmtSize(mediaB) + L"), que se conservan en la papelera";
            if (!other) return;
        } else {
            it.size = (uint64_t)q.i64Size;
            it.note = L"Se vacía por completo (no contiene vídeos, fotos ni audios)";
        }
        c.items.push_back(it);
    }
}

static wstring projectKind(const wstring& dir, const wstring& n) {
    wstring par = parentOf(dir);
    auto has = [&](const wchar_t* f) { return exists(join(par, f)); };
    if (n == L"node_modules") return L"Dependencias de Node.js (se reinstalan con npm install)";
    if (n == L"__pycache__" || n == L".pytest_cache" || n == L".mypy_cache" || n == L".ruff_cache") return L"Caché de Python";
    if ((n == L".next" || n == L".nuxt" || n == L".svelte-kit" || n == L".parcel-cache" || n == L".turbo" || n == L".angular") && has(L"package.json"))
        return L"Caché de compilación web";
    if ((n == L".venv" || n == L"venv" || n == L"env") && exists(join(dir, L"pyvenv.cfg"))) return L"Entorno virtual de Python (se recrea)";
    if (n == L"target" && has(L"Cargo.toml")) return L"Compilación de Rust (cargo build)";
    if (n == L"target" && has(L"pom.xml")) return L"Compilación de Maven";
    if ((n == L"build" || n == L"dist") && (has(L"package.json") || has(L"CMakeLists.txt") || has(L"build.gradle") ||
                                           has(L"build.gradle.kts") || has(L"pyproject.toml") || has(L"setup.py")))
        return L"Carpeta de compilación generada";
    if ((n == L"bin" || n == L"obj") && (hasGlob(par, L"*.csproj") || hasGlob(par, L"*.vbproj") || hasGlob(par, L"*.fsproj")))
        return L"Compilación de .NET";
    if (n == L".vs" && hasGlob(par, L"*.sln")) return L"Caché de Visual Studio";
    if (n == L".gradle" && (has(L"build.gradle") || has(L"build.gradle.kts") || has(L"settings.gradle") || has(L"settings.gradle.kts")))
        return L"Caché de Gradle del proyecto";
    if (startsWith(n, L"cmake-build-")) return L"Compilación de CMake (CLion)";
    return L"";
}

struct DupCand { wstring path; uint64_t size; uint64_t mtime; };

static void scanUserFiles(Category& proj, Category& large, std::vector<DupCand>& dups) {
    WVec roots; roots.push_back(P.profile);
    std::set<wstring> sysSkip = {L"windows", L"program files", L"program files (x86)", L"programdata", L"users",
        L"$recycle.bin", L"system volume information", L"recovery", L"boot", L"efi", L"$winreagent", L"$windows.~bt",
        L"$windows.~ws", L"windows.old", L"config.msi", L"documents and settings", L"perflogs", L"msocache",
        L"onedrivetemp", L"$getcurrent", L"$sysreset", L"intel", L"amd", L"nvidia", L"drivers", L"swsetup", L"inetpub"};
    WVec top; listDir(P.sysdrive, top, true);
    for (auto& t : top) if (!sysSkip.count(lower(nameOf(t)))) roots.push_back(t);
    wstring sysN = norm(P.sysdrive);
    for (auto& d : fixedDrives()) if (norm(d) != sysN) roots.push_back(d);

    const wstring nApp = norm(P.appdata);
    const uint64_t largeMin = g_set.largeMB * 1024ULL * 1024ULL, dupMin = std::max<uint64_t>(1, g_set.dupMinMB * 1024ULL * 1024ULL);
    std::set<wstring> skipNames = {L"$recycle.bin", L"system volume information", L".git", L".svn", L".hg",
        L"windowsapps", L"$windows.~bt", L"$windows.~ws", L"windows.old", L"$winreagent", L"config.msi", L"wpsystem"};

    for (auto& r : roots) {
        bool isDrive = r.size() <= 3;
        walk(r, [&](const wstring& p, const WIN32_FIND_DATAW& fd, int depth) -> bool {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) return false;
                wstring ln = lower(fd.cFileName);
                if (skipNames.count(ln)) return false;
                if (isDrive && depth == 0 && (ln == L"windows" || startsWith(ln, L"program files") || ln == L"programdata"))
                    return false;
                if (ln == L"appdata" && norm(p) == nApp) return false;
                wstring kind = projectKind(p, ln);
                if (!kind.empty()) {
                    addPathItem(proj, p, kind + L" · proyecto: " + shorten(parentOf(p), 70));
                    return false;
                }
                return true;
            }
            if (cloudOnly(fd.dwFileAttributes)) return false;  // solo en la nube: no ocupa disco
            if (isMediaFile(p)) return false;                   // vídeos, fotos y audios: nunca se proponen
            uint64_t sz = fdSize(fd);
            uint64_t mt = ftToU(fd.ftLastWriteTime);
            if (sz >= largeMin) {
                wstring e = extOf(p);
                addPathItem(large, p, L"Modificado el " + fmtDate(mt) + (e.empty() ? L"" : L" · tipo " + e));
            }
            if (sz >= dupMin) dups.push_back({p, sz, mt});
            return false;
        });
        if (g_work.stop) return;
    }
}

static uint64_t fnv(uint64_t h, const unsigned char* d, size_t n) {
    for (size_t i = 0; i < n; ++i) { h ^= d[i]; h *= 1099511628211ULL; }
    return h;
}
static bool hashFile(const wstring& p, uint64_t size, bool full, uint64_t& out) {
    HANDLE h = CreateFileW(lp(p).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    static thread_local std::vector<unsigned char> buf(1 << 20);
    uint64_t hh = 1469598103934665603ULL ^ size;
    DWORD rd = 0;
    if (!full) {
        if (!ReadFile(h, buf.data(), 65536, &rd, nullptr)) { CloseHandle(h); return false; }
        hh = fnv(hh, buf.data(), rd);
        if (size > 65536) {
            LARGE_INTEGER li; li.QuadPart = (LONGLONG)size - 65536;
            SetFilePointerEx(h, li, nullptr, FILE_BEGIN);
            rd = 0;
            if (ReadFile(h, buf.data(), 65536, &rd, nullptr)) hh = fnv(hh, buf.data(), rd);
        }
    } else {
        for (;;) {
            if (g_work.stop || !ReadFile(h, buf.data(), (DWORD)buf.size(), &rd, nullptr)) { CloseHandle(h); return false; }
            if (rd == 0) break;
            hh = fnv(hh, buf.data(), rd);
            tick(p);
        }
    }
    CloseHandle(h);
    out = hh;
    return true;
}
static void findDuplicates(Category& c, std::vector<DupCand>& v) {
    std::map<uint64_t, std::vector<size_t>> bySize;
    for (size_t i = 0; i < v.size(); ++i) bySize[v[i].size].push_back(i);
    for (auto& bs : bySize) {
        if (g_work.stop) return;
        if (bs.second.size() < 2) continue;
        std::map<uint64_t, std::vector<size_t>> byPart;
        for (size_t id : bs.second) { uint64_t h; tick(v[id].path); if (hashFile(v[id].path, bs.first, false, h)) byPart[h].push_back(id); }
        for (auto& bp : byPart) {
            if (bp.second.size() < 2) continue;
            std::map<uint64_t, std::vector<size_t>> byFull;
            for (size_t id : bp.second) { uint64_t h; if (hashFile(v[id].path, bs.first, true, h)) byFull[h].push_back(id); }
            for (auto& bf : byFull) {
                auto grp = bf.second;
                if (grp.size() < 2) continue;
                std::sort(grp.begin(), grp.end(), [&](size_t a, size_t b) {
                    if (v[a].path.size() != v[b].path.size()) return v[a].path.size() < v[b].path.size();
                    return v[a].path < v[b].path;
                });
                for (size_t k = 1; k < grp.size(); ++k)
                    addPathItem(c, v[grp[k]].path, L"Copia idéntica de: " + v[grp[0]].path);
            }
        }
    }
}

static void scanDownloads(Category& c) {
    static const std::set<wstring> inst = {L".exe", L".msi", L".msix", L".msixbundle", L".appx", L".iso",
                                           L".zip", L".rar", L".7z", L".dmg", L".img"};
    WVec kids; listDir(P.downloads, kids);
    for (auto& k : kids) {
        uint64_t sz, mt; DWORD a;
        if (!fileInfo(k, sz, mt, a)) continue;
        int age = daysSince(mt);
        bool isInst = !(a & FILE_ATTRIBUTE_DIRECTORY) && inst.count(extOf(k));
        if (age >= g_set.downloadDays || isInst)
            addPathItem(c, k, wstring(isInst ? L"Instalador o comprimido · " : L"") + L"sin tocar hace " + std::to_wstring(age) + L" días");
    }
}

static void scanLeftovers(Category& c) {
    auto ins = readUninstall();
    WVec tokens, locs;
    for (auto& u : ins) {
        for (auto& s : {u.name, u.publisher}) {
            wstring a = alnum(s);
            if (a.size() >= 3) tokens.push_back(a);
            for (auto& w : words(s)) if (w.size() >= 4) tokens.push_back(w);
        }
        if (!u.location.empty()) { locs.push_back(norm(u.location)); wstring a = alnum(nameOf(u.location)); if (a.size() >= 3) tokens.push_back(a); }
    }
    for (auto& sm : {P.smUser, P.smAll})
        walk(sm, [&](const wstring& p, const WIN32_FIND_DATAW&, int) {
            wstring n = nameOf(p);
            size_t k = n.find_last_of(L'.');
            if (k != wstring::npos) n = n.substr(0, k);
            wstring a = alnum(n); if (a.size() >= 3) tokens.push_back(a);
            for (auto& w : words(n)) if (w.size() >= 4) tokens.push_back(w);
            return true;
        });
    static const std::set<wstring> wl = {
        L"microsoft", L"windows", L"packages", L"temp", L"programs", L"comms", L"connecteddevicesplatform",
        L"d3dscache", L"crashdumps", L"history", L"inetcache", L"publishers", L"virtualstore",
        L"placeholdertilelogofolder", L"peerdistrepub", L"microsoftedge", L"google", L"mozilla", L"adobe",
        L"apple", L"applecomputer", L"intel", L"nvidia", L"nvidiacorporation", L"amd", L"ati", L"realtek",
        L"packagecache", L"ssh", L"usoshared", L"usoprivate", L"softwaredistribution", L"windowsholographicdevices",
        L"sun", L"oracle", L"java", L"identities", L"squirreltemp", L"npm", L"npmcache", L"pip", L"nuget", L"yarn",
        L"nodegyp", L"pnpm", L"python", L"code", L"jetbrains", L"github", L"githubdesktop", L"git", L"docker",
        L"dockerdesktop", L"steam", L"epicgameslauncher", L"unity", L"nodejs", L"dotnet", L"referenceassemblies",
        L"msbuild", L"commonfiles", L"internetexplorer", L"windowsdefender", L"windowsmail", L"windowsmediaplayer",
        L"windowsnt", L"windowsphotoviewer", L"windowssidebar", L"windowsapps", L"modifiablewindowsapps",
        L"uninstallinformation", L"windowspowershell", L"dell", L"hp", L"hewlettpackard", L"lenovo", L"asus",
        L"acer", L"msi", L"samsung", L"logitech", L"razer", L"corsair", L"steelseries", L"synaptics", L"elan",
        L"waves", L"dolby", L"dts", L"conexant", L"qualcomm", L"mediatek", L"broadcom", L"claude", L"anthropic",
        L"claudecode", L"desktop", L"documents", L"startmenu", L"templates", L"applicationdata",
        L"regid199106commicrosoft", L"onedrive", L"fontcache", L"cache", L"caches", L"logs", L"log", L"crashpad",
        L"electron", L"default", L"allusers", L"public", L"installer", L"downloads", L"winget", L"chocolatey",
        L"scoop", L"tools", L"bin", L"lib", L"vulkan", L"vulkanrt", L"openal", L"directx", L"iisexpress",
        L"microsoftsqlserver", L"windowskits", L"msys64", L"mingw64", L"cygwin", L"gtk", L"ffmpeg",
        L"vmware", L"virtualbox", L"wsl", L"ubuntu", L"pkgs", L"uv", L"cargo", L"rustup", L"go", L"deno",
        L"blender", L"blenderfoundation", L"obsstudio", L"elevenlabs", L"remotion", L"playwright", L"msplaywright"};
    auto matches = [&](const wstring& f) {
        if (f.size() < 3) return true;
        for (auto& t : tokens) {
            if (t.size() < 3) continue;
            if (t.find(f) != wstring::npos) return true;
            if (t.size() >= 4 && f.find(t) != wstring::npos) return true;
        }
        return false;
    };
    struct Root { wstring path; bool binRoot; };
    std::vector<Root> roots = {{P.local, false}, {P.roaming, false}, {P.localLow, false}, {P.programData, false},
                               {P.pf, true}, {P.pf86, true}, {join(P.local, L"Programs"), true}};
    for (auto& r : roots) {
        WVec kids; listDir(r.path, kids, true);
        for (auto& k : kids) {
            if (g_work.stop) return;
            wstring nm = nameOf(k);
            wstring a = alnum(nm);
            if (nm.empty() || nm[0] == L'.' || nm[0] == L'{') continue;
            if (wl.count(a) || matches(a)) continue;
            wstring nk = norm(k);
            bool used = false;
            for (auto& l : locs) if (inside(l, nk) || inside(nk, l)) { used = true; break; }
            if (used) continue;
            if (!G.check(k, true).empty()) continue;
            SizeInfo si; measure(k, si);
            if (si.newest && daysSince(si.newest) < g_set.leftoverDays) continue;
            if (r.binRoot && si.hasExe) continue;
            Item it; it.path = k; it.isDir = true; it.size = si.bytes; it.explicitOk = true;
            it.note = L"Sin programa instalado asociado · última modificación " + fmtDate(si.newest);
            c.items.push_back(it);
        }
    }
    for (auto& u : ins) {
        if (u.sysComp || uninstallerExists(u)) continue;
        if (!u.location.empty() && exists(u.location)) continue;
        Item it; it.kind = Kind::RegKey; it.hive = u.hive; it.view = u.view; it.subkey = u.sub;
        it.path = regDisplay(u.hive, u.view, u.sub);
        it.note = L"Entrada huérfana de desinstalación: \"" + u.name + L"\" (el programa ya no está en disco)";
        if (u.hive == HKEY_LOCAL_MACHINE && !g_admin) it.note += L" · requiere administrador";
        c.items.push_back(it);
    }
}

// ===================================================================
//  Análisis completo / rápido
// ===================================================================
static void setSteps(const WVec& names) {
    std::lock_guard<std::mutex> l(g_work.m);
    g_work.steps.clear();
    for (auto& n : names) { StepInfo s; s.name = n; g_work.steps.push_back(s); }
    g_work.current.clear();
}
static void stepBegin(size_t i) { std::lock_guard<std::mutex> l(g_work.m); if (i < g_work.steps.size()) g_work.steps[i].status = 1; }
static void stepEnd(size_t i, uint64_t count, uint64_t size) {
    std::lock_guard<std::mutex> l(g_work.m);
    if (i < g_work.steps.size()) { g_work.steps[i].status = 2; g_work.steps[i].count = count; g_work.steps[i].size = size; }
}

std::vector<Category> scanAll(bool quick) {
    g_work.stop = false;
    g_work.count = 0;
    struct Def { wstring name, desc; int icon; uint32_t color; int level; std::function<void(Category&)> fn; };
    std::vector<Def> defs = {
        {L"Temporales del usuario", L"Archivos temporales que dejan los programas. Se omiten los de las últimas 24 horas.", IC_TEMP, 0xFF2DD4BF, LV_SAFE, scanUserTemp},
        {L"Temporales de Windows", L"Carpeta Temp del sistema. Requiere administrador.", IC_WINTEMP, 0xFF38BDF8, LV_SAFE, scanWinTemp},
        {L"Caché de navegadores", L"Chrome, Edge, Brave, Opera, Vivaldi y Firefox. No toca contraseñas, historial ni marcadores.", IC_GLOBE, 0xFF60A5FA, LV_SAFE, scanBrowsers},
        {L"Caché de aplicaciones", L"Discord, Spotify, VS Code, Slack, Teams, Steam, sombreadores de la gráfica y caché de Internet.", IC_APPS, 0xFF818CF8, LV_SAFE, scanAppCaches},
        {L"Cachés de desarrollo", L"npm, pip, NuGet, Gradle, Maven, Cargo, Go... Se vuelven a descargar cuando hagan falta.", IC_CODE, 0xFFA78BFA, LV_SAFE, scanDevCaches},
        {L"Windows Update", L"Actualizaciones ya instaladas y caché de optimización de distribución. Requiere administrador.", IC_UPDATE, 0xFF22D3EE, LV_SAFE, scanUpdate},
        {L"Errores y volcados", L"Volcados de memoria e informes de fallos que ya no sirven.", IC_BUG, 0xFFF472B6, LV_SAFE, scanErrors},
        {L"Miniaturas e iconos", L"Caché de miniaturas del Explorador. Se regenera sola.", IC_IMAGE, 0xFFFBBF24, LV_SAFE, scanThumbs},
        {L"Registros de Windows", L"Archivos .log y .cab de mantenimiento (CBS, DISM).", IC_DOC, 0xFF94A3B8, LV_SAFE, scanLogs},
        {L"Windows anteriores", L"Windows.old y restos de actualizaciones. Tras borrarlos no podrás volver a la versión anterior.", IC_WINDOW, 0xFF4ADE80, LV_SAFE, scanOldWindows},
        {L"Papelera de reciclaje", L"Vacía la papelera de todas las unidades.", IC_TRASH, 0xFF34D399, LV_SAFE, scanRecycle}};
    WVec names;
    for (auto& d : defs) names.push_back(d.name);
    if (!quick) {
        names.push_back(L"Proyectos y archivos grandes");
        names.push_back(L"Archivos duplicados");
        names.push_back(L"Descargas antiguas");
        names.push_back(L"Restos de programas");
    }
    setSteps(names);

    std::vector<Category> cats;
    cats.reserve(32);
    auto mk = [&](const wstring& name, const wstring& desc, int icon, uint32_t color, int level) -> Category& {
        cats.push_back(Category{});
        Category& c = cats.back();
        c.name = name; c.desc = desc; c.icon = icon; c.color = color; c.level = level;
        return c;
    };
    size_t si = 0;
    for (auto& d : defs) {
        if (g_work.stop) break;
        Category& c = mk(d.name, d.desc, d.icon, d.color, d.level);
        stepBegin(si);
        d.fn(c);
        stepEnd(si, c.count(), c.total());
        ++si;
    }
    if (!quick && !g_work.stop) {
        std::vector<DupCand> dups;
        Category& proj = mk(L"Proyectos", L"node_modules, entornos virtuales, carpetas build, target u obj... Se regeneran reinstalando o compilando el proyecto.", IC_PROJECT, 0xFFF59E0B, LV_REVIEW);
        Category& large = mk(L"Archivos grandes", L"Tus archivos de " + fmtNum(g_set.largeMB) + L" MB o más (los vídeos, fotos y audios nunca se proponen). Revisa cada uno: son tus datos.", IC_BIG, 0xFFF43F5E, LV_PERSONAL);
        stepBegin(si);
        scanUserFiles(proj, large, dups);
        stepEnd(si, proj.count() + large.count(), proj.total() + large.total());
        ++si;
        if (!g_work.stop) {
            Category& dup = mk(L"Archivos duplicados", L"Copias idénticas (mismo contenido byte a byte). El original indicado se conserva siempre.", IC_DUP, 0xFFFB7185, LV_PERSONAL);
            stepBegin(si); findDuplicates(dup, dups); stepEnd(si, dup.count(), dup.total()); ++si;
        }
        if (!g_work.stop) {
            Category& dl = mk(L"Descargas antiguas", L"Elementos de Descargas sin tocar hace " + std::to_wstring(g_set.downloadDays) + L" días o más, e instaladores y comprimidos.", IC_DOWNLOAD, 0xFFE879F9, LV_PERSONAL);
            stepBegin(si); scanDownloads(dl); stepEnd(si, dl.count(), dl.total()); ++si;
        }
        if (!g_work.stop) {
            Category& lo = mk(L"Restos de programas", L"Carpetas sin programa instalado asociado y sin uso reciente, y entradas huérfanas del registro. Revisa antes de borrar.", IC_GHOST, 0xFFFACC15, LV_REVIEW);
            stepBegin(si); scanLeftovers(lo); stepEnd(si, lo.count(), lo.total()); ++si;
        }
    }
    for (auto& c : cats) sortCat(c);
    return cats;
}

// ===================================================================
//  Búsqueda exhaustiva de restos de un programa
// ===================================================================
static void searchRegistry(Category& c, const wstring& term) {
    struct Root { HKEY h; wstring sub; REGSAM view; };
    std::vector<Root> roots = {{HKEY_CURRENT_USER, L"Software", 0},
                               {HKEY_LOCAL_MACHINE, L"SOFTWARE", KEY_WOW64_64KEY},
                               {HKEY_LOCAL_MACHINE, L"SOFTWARE", KEY_WOW64_32KEY}};
    std::set<wstring> seen;
    auto addKey = [&](HKEY h, REGSAM view, const wstring& sub, const wstring& note, bool prot) {
        wstring disp = regDisplay(h, view, sub);
        if (!seen.insert(lower(disp)).second) return;
        Item it; it.kind = Kind::RegKey; it.hive = h; it.view = view; it.subkey = sub; it.path = disp; it.note = note;
        if (prot) it.blocked = L"Clave esencial del registro";
        else if (h == HKEY_LOCAL_MACHINE && !g_admin) it.note += L" · requiere administrador";
        c.items.push_back(it);
    };
    for (auto& r : roots) {
        for (auto& n1 : regEnumKeys(r.h, r.sub, r.view)) {
            wstring l1 = lower(n1);
            bool prot = regNameProtected(l1);
            if (contains(l1, term)) { addKey(r.h, r.view, r.sub + L"\\" + n1, L"Configuración del programa en el registro", prot); continue; }
            if (prot) continue;
            for (auto& n2 : regEnumKeys(r.h, r.sub + L"\\" + n1, r.view))
                if (contains(lower(n2), term))
                    addKey(r.h, r.view, r.sub + L"\\" + n1 + L"\\" + n2, L"Configuración en el registro (fabricante: " + n1 + L")", regNameProtected(lower(n2)));
        }
    }
    for (auto& u : readUninstall()) {
        if (!contains(lower(u.name), term)) continue;
        wstring note = L"Entrada de \"Programas y características\": " + u.name;
        if (uninstallerExists(u)) note += L" · el programa parece seguir instalado";
        addKey(u.hive, u.view, u.sub, note, u.sysComp);
    }
    struct RR { HKEY h; REGSAM view; } rr[] = {{HKEY_CURRENT_USER, 0}, {HKEY_LOCAL_MACHINE, KEY_WOW64_64KEY}, {HKEY_LOCAL_MACHINE, KEY_WOW64_32KEY}};
    for (auto& r : rr) {
        for (auto sub : {L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\RunOnce"}) {
            HKEY h;
            if (RegOpenKeyExW(r.h, sub, 0, KEY_READ | r.view, &h) != ERROR_SUCCESS) continue;
            DWORD maxName = 0, maxData = 0;
            RegQueryInfoKeyW(h, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &maxName, &maxData, nullptr, nullptr);
            std::vector<wchar_t> name(maxName + 2);
            std::vector<BYTE> data(maxData + 4);
            for (DWORD i = 0;; ++i) {
                DWORD nl = maxName + 1, dl = maxData + 2, type = 0;
                LONG e = RegEnumValueW(h, i, name.data(), &nl, nullptr, &type, data.data(), &dl);
                if (e == ERROR_NO_MORE_ITEMS) break;
                if (e != ERROR_SUCCESS) continue;
                wstring vn(name.data(), nl), vd;
                if (type == REG_SZ || type == REG_EXPAND_SZ) { data[dl] = 0; data[dl + 1] = 0; vd = (const wchar_t*)data.data(); }
                if (contains(lower(vn), term) || contains(lower(vd), term)) {
                    Item it; it.kind = Kind::RegValue; it.hive = r.h; it.view = r.view; it.subkey = sub; it.valueName = vn;
                    it.path = regDisplay(r.h, r.view, sub) + L"\\" + vn;
                    it.note = L"Arranque automático: " + shorten(vd, 90);
                    if (r.h == HKEY_LOCAL_MACHINE && !g_admin) it.note += L" · requiere administrador";
                    if (seen.insert(lower(it.path)).second) c.items.push_back(it);
                }
            }
            RegCloseKey(h);
        }
    }
}

bool validTerm(const wstring& termIn, wstring& why) {
    wstring term = lower(trim(termIn));
    if (term.size() < 3) { why = L"Escribe al menos 3 letras para no arrastrar archivos que no tienen nada que ver."; return false; }
    static const std::set<wstring> forbidden = {L"windows", L"microsoft", L"system", L"system32", L"program",
        L"users", L"appdata", L"local", L"roaming", L"temp", L"files", L"data", L"desktop", L"documents", L"exe", L"dll",
        L"program files", L"programdata", L"usuarios"};
    if (forbidden.count(term)) { why = L"Ese término es demasiado genérico y afectaría a Windows."; return false; }
    return true;
}

std::vector<Uninst> installedMatching(const wstring& termIn) {
    wstring term = lower(trim(termIn));
    std::vector<Uninst> v;
    for (auto& u : readUninstall())
        if (contains(lower(u.name), term) && !u.sysComp && !u.uninstall.empty() && uninstallerExists(u)) v.push_back(u);
    return v;
}

bool launchUninstaller(const Uninst& u) {
    STARTUPINFOW si{}; si.cb = sizeof(si); PROCESS_INFORMATION pi{};
    std::vector<wchar_t> b(u.uninstall.begin(), u.uninstall.end()); b.push_back(0);
    if (CreateProcessW(nullptr, b.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
        return true;
    }
    ShellExecuteW(nullptr, L"open", L"ms-settings:appsfeatures", nullptr, nullptr, SW_SHOWNORMAL);
    return false;
}

Category searchProgram(const wstring& termIn, bool& truncated) {
    wstring term = lower(trim(termIn));
    g_work.stop = false;
    g_work.count = 0;
    Category c;
    c.name = L"Restos de \"" + trim(termIn) + L"\"";
    c.desc = L"Todo lo que contiene \"" + trim(termIn) + L"\" en su nombre, en disco y en el registro. Comprueba que cada elemento pertenece de verdad a ese programa.";
    c.icon = IC_GHOST; c.color = 0xFFA78BFA; c.level = LV_REVIEW;
    WVec drv = fixedDrives(), names;
    for (auto& d : drv) names.push_back(L"Unidad " + d.substr(0, 2));
    names.push_back(L"Registro de Windows");
    setSteps(names);
    const wstring nWin = norm(P.windir);
    const size_t limit = 5000;
    truncated = false;
    size_t si = 0;
    for (auto& d : drv) {
        if (g_work.stop) break;
        stepBegin(si);
        size_t before = c.items.size();
        walk(d, [&](const wstring& p, const WIN32_FIND_DATAW& fd, int) -> bool {
            bool isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            wstring ln = lower(fd.cFileName);
            if (isDir) {
                if (ln == L"system volume information" || ln == L"$recycle.bin") return false;
                if (ln == L"windows" && inside(norm(p), nWin)) return false;
            }
            if (contains(ln, term)) {
                addPathItem(c, p, isDir ? L"Carpeta" : L"Archivo", true, true);
                if (c.items.size() >= limit) { truncated = true; g_work.stop = true; }
                return false;
            }
            return true;
        });
        uint64_t sz = 0; for (size_t k = before; k < c.items.size(); ++k) if (c.items[k].blocked.empty()) sz += c.items[k].size;
        stepEnd(si, c.items.size() - before, sz);
        ++si;
    }
    bool userStop = g_work.stop && !truncated;
    if (!userStop) {
        g_work.stop = false;
        stepBegin(si);
        size_t before = c.items.size();
        searchRegistry(c, term);
        stepEnd(si, c.items.size() - before, 0);
    }
    sortCat(c);
    if (userStop) g_work.stop = true;
    return c;
}

// ===================================================================
//  Varios
// ===================================================================
void openPath(const wstring& p) { ShellExecuteW(nullptr, L"open", p.c_str(), nullptr, nullptr, SW_SHOWNORMAL); }

void openLocation(const Item& it) {
    if (it.kind == Kind::Path) {
        wstring args = L"/select,\"" + it.path + L"\"";
        ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
    } else if (it.kind == Kind::RecycleBin) {
        ShellExecuteW(nullptr, L"open", L"shell:RecycleBinFolder", nullptr, nullptr, SW_SHOWNORMAL);
    } else {
        HKEY h;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Applets\\Regedit", 0, nullptr, 0,
                            KEY_SET_VALUE, nullptr, &h, nullptr) == ERROR_SUCCESS) {
            wstring k = (it.hive == HKEY_LOCAL_MACHINE ? L"HKEY_LOCAL_MACHINE\\" : L"HKEY_CURRENT_USER\\") +
                        regDisplay(it.hive, it.view, it.subkey).substr(5);
            RegSetValueExW(h, L"LastKey", 0, REG_SZ, (const BYTE*)k.c_str(), (DWORD)((k.size() + 1) * sizeof(wchar_t)));
            RegCloseKey(h);
        }
        ShellExecuteW(nullptr, L"open", L"regedit.exe", L"/m", nullptr, SW_SHOWNORMAL);
    }
}

std::vector<DriveInfo> drives() {
    std::vector<DriveInfo> v;
    for (auto& d : fixedDrives()) {
        ULARGE_INTEGER fa, tot, tf;
        if (!GetDiskFreeSpaceExW(d.c_str(), &fa, &tot, &tf) || tot.QuadPart == 0) continue;
        DriveInfo di; di.root = d; di.total = tot.QuadPart; di.freeB = tf.QuadPart;
        v.push_back(di);
    }
    return v;
}

static WVec logLines() {
    WVec out;
    HANDLE h = CreateFileW(logPath().c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return out;
    LARGE_INTEGER sz{}; GetFileSizeEx(h, &sz);
    if (sz.QuadPart <= 0 || sz.QuadPart > 64LL * 1024 * 1024) { CloseHandle(h); return out; }
    std::string data((size_t)sz.QuadPart, '\0');
    DWORD rd = 0; ReadFile(h, &data[0], (DWORD)data.size(), &rd, nullptr);
    CloseHandle(h);
    data.resize(rd);
    if (data.size() >= 3 && (unsigned char)data[0] == 0xEF) data = data.substr(3);
    wstring w = fromUtf8(data);
    size_t a = 0;
    while (a < w.size()) {
        size_t b = w.find(L'\n', a);
        if (b == wstring::npos) b = w.size();
        wstring line = w.substr(a, b - a);
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        if (!line.empty()) out.push_back(line);
        a = b + 1;
    }
    return out;
}

std::vector<LogEntry> readLog(size_t maxLines) {
    std::vector<LogEntry> v;
    WVec lines = logLines();
    for (size_t i = lines.size(); i-- > 0 && v.size() < maxLines;) {
        const wstring& l = lines[i];
        if (l.size() < 21) continue;
        LogEntry e;
        e.when = l.substr(0, 19);
        wstring rest = trim(l.substr(19));
        size_t sp = rest.find(L' ');
        e.action = sp == wstring::npos ? rest : rest.substr(0, sp);
        rest = sp == wstring::npos ? L"" : trim(rest.substr(sp));
        if (e.action == L"INICIO") continue;
        if (e.action == L"ELIMINADO" || e.action == L"REINICIO" || e.action == L"PARCIAL") {
            size_t s1 = rest.find(L' ');
            size_t s2 = s1 == wstring::npos ? wstring::npos : rest.find(L' ', s1 + 1);
            if (s2 != wstring::npos) { e.size = rest.substr(0, s2); e.path = trim(rest.substr(s2)); }
            else e.path = rest;
        } else if (e.action == L"RESUMEN") {
            size_t k = rest.find(L" [bytes=");
            e.path = k == wstring::npos ? rest : rest.substr(0, k);
        } else e.path = rest;
        v.push_back(e);
    }
    return v;
}

void historyStats(uint64_t& freed, uint64_t& cleanups, uint64_t& items) {
    freed = cleanups = items = 0;
    for (auto& l : logLines()) {
        size_t k = l.find(L"RESUMEN");
        if (k == wstring::npos) continue;
        ++cleanups;
        wstring rest = trim(l.substr(k + 7));
        items += (uint64_t)_wtoi64(rest.c_str());
        size_t b = l.find(L"[bytes=");
        if (b != wstring::npos) freed += (uint64_t)_wtoi64(l.c_str() + b + 7);
    }
}

bool relaunchAdmin() {
    if (g_admin) return false;
    SHELLEXECUTEINFOW sei{}; sei.cbSize = sizeof(sei);
    sei.lpVerb = L"runas"; sei.lpFile = P.exePath.c_str(); sei.nShow = SW_SHOWNORMAL;
    return ShellExecuteExW(&sei) != FALSE;
}
