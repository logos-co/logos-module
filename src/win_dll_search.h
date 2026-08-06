#ifndef LOGOS_WIN_DLL_SEARCH_H
#define LOGOS_WIN_DLL_SEARCH_H

#include <QString>

namespace ModuleLib {

// Windows-only helpers for resolving a plugin's PRIVATE DLL dependencies.
//
// These are declared here and defined in win_dll_search.cpp specifically so
// that <windows.h> never reaches logos_module.cpp. Windows headers define
// `interface` as a macro expanding to `struct`, and logos_module.cpp uses
// `interface` as a parameter name -- including <windows.h> there breaks the
// file in a way whose error message points nowhere near the include. The same
// class of collision bites the host's TokenSource namespace via winnt.h.
// Keeping every Windows header in this one small translation unit is what makes
// that impossible rather than merely unlikely.
//
// Both functions are no-ops off Windows, so callers need no #ifdef.

// Maps the plugin at `pluginPath` with the plugin's own directory as the search
// root for its imports, and returns the resulting module reference (nullptr on
// failure -- callers should let QPluginLoader report the error). Pass the result
// to releasePluginPreload() to balance the reference.
void* preloadPluginWithOwnDirSearch(const QString& pluginPath);

// Releases a reference returned by preloadPluginWithOwnDirSearch().
void releasePluginPreload(void* handle);

} // namespace ModuleLib

#endif // LOGOS_WIN_DLL_SEARCH_H
