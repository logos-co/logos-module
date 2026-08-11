#ifndef LOGOS_WIN_DLL_SEARCH_H
#define LOGOS_WIN_DLL_SEARCH_H

#include <QDir>
#include <QFileInfo>
#include <QString>
#include <string>

namespace ModuleLib {

// Windows-only helpers for resolving a plugin's PRIVATE DLL dependencies.
//
// HEADER-ONLY, and it declares the two Win32 entry points itself rather than
// including <windows.h>. Both choices are deliberate:
//
//   * No <windows.h>. That header defines `interface` as a macro expanding to
//     `struct`, which breaks any translation unit using `interface` as an
//     identifier -- logos_module.cpp does exactly that -- and winnt.h declares
//     an enumerator literally named TokenSource which collides with the host's
//     TokenSource namespace. Declaring the two functions we need keeps every
//     consumer clean.
//   * Header-only. Consumers (logos-basecamp's PluginLoader and window.cpp)
//     use logos-module's HEADERS but do not link liblogos_module.a. Requiring
//     the archive just for this would add a static library to two more link
//     lines, which is precisely how liblogos_core ended up colliding with
//     liblogos_qt_sdk over LogosAPI.
//
// Both functions are no-ops off Windows, so callers need no #ifdef.

#ifdef _WIN32

extern "C" {
__declspec(dllimport) void* __stdcall LoadLibraryExW(const wchar_t* lpLibFileName,
                                                     void* hFile,
                                                     unsigned long dwFlags);
__declspec(dllimport) int __stdcall FreeLibrary(void* hLibModule);
__declspec(dllimport) unsigned long __stdcall GetLastError(void);
}

// Windows resolves a DLL's imports from the EXECUTABLE's directory, never from
// the directory of the DLL doing the importing. Logos installs every module
// into its own targetDir/<moduleName>/ with its vendored libraries beside it,
// so a plain QPluginLoader::load() fails with the famously unhelpful "The
// specified module could not be found" -- which names the plugin, not the
// dependency that could not be resolved. Measured on real Windows against our
// pinned Qt 6.11.1: Qt still calls bare LoadLibrary().
//
// Mapping the plugin ourselves with LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR ADDS the
// plugin's own directory to the search for that one load. QPluginLoader then
// requests the same absolute path, the loader sees the image already mapped and
// returns the existing handle, so its load succeeds with the vendored DLLs
// bound.
//
// DLL_LOAD_DIR must be OR'd with DEFAULT_DIRS. LOAD_WITH_ALTERED_SEARCH_PATH --
// the obvious-looking flag -- SUBSTITUTES the plugin's directory for the
// application directory instead of adding to it, which breaks any plugin whose
// dependencies live beside the host executable. Both are per-call, so no
// process-wide search policy changes; SetDefaultDllDirectories +
// AddDllDirectory would be process-wide AND cumulative, letting one module's
// vendored foo.dll satisfy another's import of foo.dll.
inline void* preloadPluginWithOwnDirSearch(const QString& pluginPath)
{
    constexpr unsigned long kSearchDllLoadDir = 0x00000100;
    constexpr unsigned long kSearchDefaultDirs = 0x00001000;

    // The LOAD_LIBRARY_SEARCH_* flags are ignored unless the path is absolute.
    const QString absolute = QFileInfo(pluginPath).absoluteFilePath();
    const std::wstring native = QDir::toNativeSeparators(absolute).toStdWString();

    return LoadLibraryExW(native.c_str(), nullptr,
                          kSearchDllLoadDir | kSearchDefaultDirs);
}

inline void releasePluginPreload(void* handle)
{
    if (handle) {
        FreeLibrary(handle);
    }
}

inline unsigned long lastPreloadError() { return GetLastError(); }

#else

// Not Windows: ELF and Mach-O carry an rpath, and the module builder already
// sets $ORIGIN / @loader_path, so a plugin's own directory is searched for its
// dependencies without help.
inline void* preloadPluginWithOwnDirSearch(const QString&) { return nullptr; }
inline void releasePluginPreload(void*) { }
inline unsigned long lastPreloadError() { return 0; }

#endif

} // namespace ModuleLib

#endif // LOGOS_WIN_DLL_SEARCH_H
