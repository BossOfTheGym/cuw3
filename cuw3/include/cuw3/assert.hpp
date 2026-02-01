#pragma once

#include <cassert>
#include <iostream>

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

// TODO : print out the message
// TODO : conter-intuitive 
#define CUW3_ALERT_RETURN_VAL(cond, value, ...) \
    do { \
        if ((cond)) { \
            std::cerr << #cond << std::endl; \
            return value; \
        } \
    } while(0)

// TODO : conter-intuitive 
#define CUW3_ALERT_RETURN_VOID(cond, ...) \
    do { \
        if ((cond)) { \
            std::cerr << #cond << std::endl; \
            return; \
        } \
    } while(0)

#define CUW3_CHECK_RETURN_VAL(cond, value, ...) \
    do { \
        if (!(cond)) { \
            std::cerr << #cond << std::endl; \
            return value; \
        } \
    } while(0)

#define CUW3_CHECK_RETURN_VOID(cond, ...) \
    do { \
        if (!(cond)) { \
            std::cerr << #cond << std::endl; \
            return; \
        } \
    } while(0)

namespace cuw3 {}