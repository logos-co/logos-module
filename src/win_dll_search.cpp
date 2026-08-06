#include "win_dll_search.h"

#ifdef _WIN32

#include <QDebug>
#include <QDir>
#include <QFileInfo>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <string>

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
// Mapping the plugin ourselves with LOAD_WITH_ALTERED_SEARCH_PATH makes Windows
// use the plugin's OWN directory as the search root for that one load.
// QPluginLoader then requests the same absolute path, the loader sees the image
// is already mapped and returns the existing handle, so its load succeeds with
// the vendored DLLs already bound.
//
// The alternative -- SetDefaultDllDirectories + AddDllDirectory -- also works,
// but changes the DLL search policy PROCESS-WIDE and accumulates one directory
// per module. With several modules in one process (Basecamp's in-process UI
// plugins) module A's vendored foo.dll would then satisfy module B's import of
// foo.dll, silently destroying the per-module isolation that the
// directory-per-module layout exists to provide. This is scoped to one load.
void* preloadPluginWithOwnDirSearch(const QString& pluginPath)
{
    // LOAD_WITH_ALTERED_SEARCH_PATH is ignored unless the path is absolute.
    const QString absolute = QFileInfo(pluginPath).absoluteFilePath();
    const std::wstring native = QDir::toNativeSeparators(absolute).toStdWString();

    HMODULE handle = ::LoadLibraryExW(native.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
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
