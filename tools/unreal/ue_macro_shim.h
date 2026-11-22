// Force-included into the "Unreal compatibility" build of the core
// (cricket_unreal_compat_check): Unreal defines these as MACROS, so core code
// must never use them as names. If a core file does, this build fails here on
// Linux instead of later inside Unreal.
#pragma once
#define check(expr) ((void)(expr))
#define verify(expr) ((void)(expr))
#define ensure(expr) (!!(expr))
#define checkf(expr, ...) ((void)(expr))
#define TEXT(x) L##x
#define PI (3.1415926535897932f)
#define SMALL_NUMBER (1.e-8f)
#define KINDA_SMALL_NUMBER (1.e-4f)
#define BIG_NUMBER (3.4e+38f)
#define INDEX_NONE (-1)
#define UNUSED_VARIABLE_GUARD
