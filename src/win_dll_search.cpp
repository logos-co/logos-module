#include "win_dll_search.h"

#ifdef _WIN32

#include <QDebug>
#include <QDir>
#include <QFileInfo>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <string>

// Present since Windows 8 (and Windows 7 + KB2533623), but some mingw-w64
// header sets only declare them under a higher _WIN32_WINNT.
#ifndef LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR
#define LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR 0x00000100
#endif
#ifndef LOAD_LIBRARY_SEARCH_DEFAULT_DIRS
#define LOAD_LIBRARY_SEARCH_DEFAULT_DIRS 0x00001000
#endif

#endif

namespace ModuleLib {

#ifdef _WIN32

// Windows resolves a DLL's imports from the EXECUTABLE's directory, never from
// the directory of the DLL doing the importing. Logos installs every module
// into its own targetDir/<moduleName>/ with its vendored libraries beside it,
// so a plain QPluginLoader::load() fails with the famously unhelpful "The
// specified module could not be found" -- which names the plugin, not the
// dependency that could not be resolved. Measured on real Windows against our
// pinned Qt 6.11.1: Qt still calls bare LoadLibrary().
//
// Mapping the plugin ourselves with LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR ADDS the
// plugin's own directory to the search for that one load.
// QPluginLoader then requests the same absolute path, the loader sees the image
// is already mapped and returns the existing handle, so its load succeeds with
// the vendored DLLs already bound.
//
// DLL_LOAD_DIR must be OR'd with DEFAULT_DIRS, and the pairing is the whole
// point. LOAD_WITH_ALTERED_SEARCH_PATH -- the obvious-looking flag, and what
// this used to do -- SUBSTITUTES the plugin's directory for the application
// directory rather than adding to it. Measured consequence: loading
// capability_module (whose Qt6RemoteObjects.dll lives only in the host's bin/)
// failed the pre-load with ERROR_MOD_NOT_FOUND, because the exe directory had
// been dropped from the search. It looked fine in a synthetic test only because
// there the shared DLLs were already loaded by the host, and an
// already-loaded module binds by base name before any directory is searched.
// DEFAULT_DIRS keeps the application directory in play; DLL_LOAD_DIR adds the
// module's own. Both are per-call, so no process-wide policy changes.
//
// The alternative -- SetDefaultDllDirectories + AddDllDirectory -- also works,
// but changes the DLL search policy PROCESS-WIDE and accumulates one directory
// per module. With several modules in one process (Basecamp's in-process UI
// plugins) module A's vendored foo.dll would then satisfy module B's import of
// foo.dll, silently destroying the per-module isolation that the
// directory-per-module layout exists to provide. This is scoped to one load.
void* preloadPluginWithOwnDirSearch(const QString& pluginPath)
{
    // The LOAD_LIBRARY_SEARCH_* flags are ignored unless the path is absolute.
    const QString absolute = QFileInfo(pluginPath).absoluteFilePath();
    const std::wstring native = QDir::toNativeSeparators(absolute).toStdWString();

    HMODULE handle = ::LoadLibraryExW(
        native.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!handle) {
        // Soft failure on purpose: QPluginLoader is left to produce its own
        // error, which is more informative than anything invented here.
        qWarning() << "LogosModule: dependency pre-load failed for" << absolute
                   << "GetLastError:" << ::GetLastError();
    }
    return handle;
}

void releasePluginPreload(void* handle)
{
    if (handle) {
        ::FreeLibrary(static_cast<HMODULE>(handle));
    }
}

#else

void* preloadPluginWithOwnDirSearch(const QString&)
{
    // Not Windows: ELF and Mach-O carry an rpath, and the module builder
    // already sets $ORIGIN / @loader_path, so a plugin's own directory is
    // searched for its dependencies without help.
    return nullptr;
}

void releasePluginPreload(void*)
{
}

#endif

} // namespace ModuleLib
