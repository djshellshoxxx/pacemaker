// Embedded web assets (generated table, see web/CMakeLists.txt).
#pragma once
#include <cstddef>
#include <cstring>

namespace pacemaker { namespace webassets {

struct Asset { const char* name; const unsigned char* data; size_t size; };
extern const Asset kAssets[];

inline const Asset* find(const char* name)
{
    for (const Asset* a = kAssets; a->name; ++a)
        if (std::strcmp(a->name, name) == 0) return a;
    return nullptr;
}

} }
