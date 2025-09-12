#pragma once

#include <cassert>

// TODO : something more fancy, this is mostly a stub
#define CUW3_ASSERT(cond, fmt, ...) assert(!!(cond))
#define CUW3_ABORT(fmt, ...) std::abort()
#define CUW3_CHECK(cond, fmt, ...) do {\
    if (!(cond)) {\
        CUW3_ABORT(fmt __VA_OPT__(,) __VA_ARGS__);\
    }\
} while(0)

namespace cuw3 {}