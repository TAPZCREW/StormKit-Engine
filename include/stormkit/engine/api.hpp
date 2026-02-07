#ifndef STORMKIT_API_HPP
#define STORMKIT_API_HPP

#include <stormkit/core/platform_macro.hpp>

#ifdef STORMKIT_ENGINE_BUILD
    #define STORMKIT_ENGINE_API STORMKIT_EXPORT
#else
    #define STORMKIT_ENGINE_API STORMKIT_IMPORT
#endif

#endif
