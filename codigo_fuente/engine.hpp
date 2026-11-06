// =====================================================================
//  Optimizalis - motor: análisis, protección y eliminación
// =====================================================================
#pragma once
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include <cstdint>
#include <utility>

using std::wstring;
typedef std::vector<wstring> WVec;

// Iconos (los dibuja la interfaz)
enum Icon {
    IC_NONE, IC_TEMP, IC_WINTEMP, IC_GLOBE, IC_APPS, IC_CODE, IC_UPDATE, IC_BUG, IC_IMAGE, IC_DOC,
    IC_WINDOW, IC_TRASH, IC_PROJECT, IC_BIG, IC_DUP, IC_DOWNLOAD, IC_GHOST, IC_HOME, IC_RADAR, IC_GRID,
    IC_SHIELD, IC_SETTINGS, IC_HISTORY, IC_LOCK, IC_CHECK, IC_REGISTRY, IC_FILE, IC_FOLDER, IC_EXTERNAL,
    IC_BACK, IC_WARNING, IC_BOLT, IC_SPARKLE, IC_SEARCH, IC_X, IC_SORT, IC_DRIVE, IC_INFO, IC_PLUS, IC_MINUS
};

enum class Kind { Path, RegKey, RegValue, RecycleBin };
struct Item {
    Kind kind = Kind::Path;
    wstring path, note, blocked;
    bool isDir = false, selected = false, done = false, explicitOk = false;
    uint64_t size = 0;
    uint64_t keptFiles = 0, keptBytes = 0;  // vídeos, audios y fotos que contiene y que se conservarán
    HKEY hive = nullptr; REGSAM view = 0; wstring subkey, valueName;
};
enum Level { LV_SAFE = 0, LV_REVIEW = 1, LV_PERSONAL = 2 };
struct Category {
    wstring name, desc;
    int icon = IC_NONE;
    uint32_t color = 0xFF2DD4BF;
    int level = LV_SAFE;
    std::vector<Item> items;
    uint64_t total() const { uint64_t t = 0; for (auto& i : items) if (i.blocked.empty()) t += i.size; return t; }
    size_t count() const { size_t c = 0; for (auto& i : items) if (i.blocked.empty()) ++c; return c; }
};
struct Settings {
    uint64_t largeMB = 500; int downloadDays = 60; uint64_t dupMinMB = 1; int leftoverDays = 60;
    int accent = 0; bool animations = true;
};
struct StepInfo { wstring name; int status = 0; uint64_t count = 0, size = 0; };  // status: 0 pendiente, 1 en curso, 2 hecho
struct WorkState {
    std::atomic<bool> stop{false};
    std::atomic<uint64_t> count{0};
    std::atomic<size_t> delIndex{0}, delTotal{0};
    std::mutex m;
    wstring current;
    std::vector<StepInfo> steps;
};
struct DelResult {
    uint64_t freed = 0, ok = 0, fail = 0, pending = 0, kept = 0, keptBytes = 0;
    std::vector<std::pair<wstring, wstring>> errs;
    void err(const wstring& p, const wstring& m) { if (errs.size() < 500) errs.push_back({p, m}); }
};
struct Uninst { wstring name, publisher, location, uninstall, sub; HKEY hive = nullptr; REGSAM view = 0; bool sysComp = false; };
struct DriveInfo { wstring root; uint64_t total = 0, freeB = 0; };
struct LogEntry { wstring when, action, size, path; };
struct Paths {
    wstring windir, sysdrive, profile, appdata, local, roaming, localLow, programData, pf, pf86, temp,
            desktop, documents, downloads, pictures, music, videos, onedrive, users, publicDir,
            exePath, exeDir, smUser, smAll;
};

extern Paths P;
extern Settings g_set;
extern WorkState g_work;
extern bool g_admin;

void engineInit();
void saveSettings();

// Trabajos largos (se ejecutan en un hilo aparte)
std::vector<Category> scanAll(bool quick);
Category searchProgram(const wstring& term, bool& truncated);
DelResult runDelete(std::vector<Item*>& sel);

// Utilidades
bool isMediaFile(const wstring& path);   // vídeo, audio o foto: NUNCA se elimina
wstring mediaKinds();                    // lista legible de las extensiones protegidas
void pruneCat(Category& c);
void sortCat(Category& c);
std::vector<Uninst> installedMatching(const wstring& term);
bool launchUninstaller(const Uninst& u);
bool validTerm(const wstring& term, wstring& why);
void openLocation(const Item& it);
void openPath(const wstring& p);
std::vector<DriveInfo> drives();
std::vector<LogEntry> readLog(size_t maxLines);
void historyStats(uint64_t& freed, uint64_t& cleanups, uint64_t& items);
wstring logPath();
wstring backupDir();
bool relaunchAdmin();

wstring fmtSize(uint64_t b);
wstring fmtNum(uint64_t n);
wstring lower(wstring s);
wstring upper(wstring s);
wstring trim(const wstring& s);
wstring nameOf(const wstring& p);
wstring parentOf(const wstring& p);
wstring stamp(bool compact);
