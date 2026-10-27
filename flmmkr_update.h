// flmmkr_update.h - shared by every FLMMKR plugin (identical copy in each plugin repository).
//
// "Procurar atualizações" button: OpenFX gives a plugin no way to open its own windows inside the
// host, so the button starts the FLMMKR-FX app in update mode. That app is the modal: it reads the
// public manifest on Cloudflare R2, downloads, checks the SHA-256 and installs, or says the plugins
// are up to date and shows the last update. The plugin itself never downloads anything and holds
// no credentials.
#pragma once
#include <cstdlib>
#include <string>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#else
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
extern char** environ;
#endif

namespace flmmkr {

// Where the FLMMKR-FX installer puts its app (it is also the updater).
inline std::string updaterPath() {
#if defined(_WIN32)
    const char* pf = std::getenv("ProgramFiles");
    return std::string(pf ? pf : "C:\\Program Files") + "\\FLMMKR-FX\\FLMMKR-FX.exe";
#elif defined(__APPLE__)
    return "/Applications/FLMMKR-FX.app";
#else
    return "/opt/flmmkr-fx/FLMMKR-FX";
#endif
}

inline bool updaterInstalled() {
#if defined(_WIN32)
    DWORD a = GetFileAttributesA(updaterPath().c_str());
    return a != INVALID_FILE_ATTRIBUTES;
#else
    struct stat st;
    return stat(updaterPath().c_str(), &st) == 0;
#endif
}

#if !defined(_WIN32)
// Runs argv[0] and waits for it (it returns at once: `open` on macOS, `sh -c '... &'` on Linux),
// so the updater is fully detached from the host and no zombie process is left behind.
inline bool spawnAndWait(const char* const argv[]) {
    pid_t pid;
    if (posix_spawn(&pid, argv[0], nullptr, nullptr, const_cast<char* const*>(argv), environ) != 0) return false;
    int status = 0;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}
#endif

// Opens the FLMMKR-FX updater window. `plugin` is shown in the window ("aberto pelo FLMMKR SKIN").
inline bool launchUpdater(const char* plugin, std::string& err) {
    if (!updaterInstalled()) {
        err = "FLMMKR-FX nao encontrado em " + updaterPath() + ". Instale o FLMMKR-FX para atualizar os plugins.";
        return false;
    }
#if defined(_WIN32)
    std::string args = std::string("--update --from \"") + plugin + "\"";
    HINSTANCE r = ShellExecuteA(nullptr, "open", updaterPath().c_str(), args.c_str(), nullptr, SW_SHOWNORMAL);
    if ((INT_PTR)r <= 32) { err = "nao foi possivel abrir o FLMMKR-FX (codigo " + std::to_string((INT_PTR)r) + ")"; return false; }
    return true;
#elif defined(__APPLE__)
    const std::string app = updaterPath();
    const char* argv[] = {"/usr/bin/open", "-n", "-a", app.c_str(), "--args", "--update", "--from", plugin, nullptr};
    if (!spawnAndWait(argv)) { err = "nao foi possivel abrir " + app; return false; }
    return true;
#else
    // Single quotes are not allowed in the path/name we control, so this quoting is safe.
    const std::string cmd = "'" + updaterPath() + "' --update --from '" + plugin + "' >/dev/null 2>&1 &";
    const char* argv[] = {"/bin/sh", "-c", cmd.c_str(), nullptr};
    if (!spawnAndWait(argv)) { err = "nao foi possivel abrir " + updaterPath(); return false; }
    return true;
#endif
}

}  // namespace flmmkr
