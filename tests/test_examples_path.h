#pragma once

#include <cstdlib>
#include <string>

// Where tests/examples/ is. The compile-time default is the source tree CMake
// configured from, which is gone once a nix build finishes — so CI, which runs
// the INSTALLED test binary against its own checkout, overrides it.
//
// There is deliberately no fallback search and no skip: a fixture that cannot
// be found must fail the run, since a skipped check renders as a pass.
inline std::string testExamplesDir()
{
    const char* env = std::getenv("LOGOS_MODULE_TEST_EXAMPLES");
    if (env != nullptr && env[0] != '\0') {
        return env;
    }
    return LOGOS_MODULE_TEST_EXAMPLES;
}
