#pragma once

#include <cassert>
#include <iostream>

// TODO : add check_critical
// TODO : add macro that disables all checks but critical
// TODO : describe when to use each of the utilities

// TODO : something more fancy, this is mostly a stub
#define CUW3_ASSERT(cond, fmt, ...) assert(!!(cond))

// TODO : proper fmt printing (or just msg... would work just fine)
#define CUW3_ABORT(fmt, ...) \
do {\
    std::cerr << (fmt) << std::endl;\
    std::abort();\
} while(0)

#define CUW3_CHECK(cond, fmt, ...) \
do {\
    if (!(cond)) {\
        CUW3_ABORT(fmt __VA_OPT__(,) __VA_ARGS__);\
    }\
} while(0)

#define CUW3_CHECK_RETURN_VAL(cond, value, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << #cond << " " << msg << std::endl; \
            return value; \
        } \
    } while(0)

#define CUW3_CHECK_RETURN_VOID(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << #cond << " " << msg << std::endl; \
            return; \
        } \
    } while(0)

#define CUW3_CHECK_GOTO(cond, label, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << #cond << " " << msg << std::endl; \
            goto label; \
        } \
    } while(0)
