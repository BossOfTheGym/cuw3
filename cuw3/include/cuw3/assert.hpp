#pragma once

#include <cassert>
#include <iostream>

// TODO : proper fmt printing (or just msg... would work just fine)

#define _CUW3_ASSERT(cond, fmt, ...) assert(!!(cond) && fmt)

#define _CUW3_ABORT(fmt, ...) \
    do {\
        std::cerr << (fmt) << std::endl;\
        std::abort();\
    } while(0)

#define _CUW3_CHECK(cond, fmt, ...) \
    do {\
        if (!(cond)) {\
            _CUW3_ABORT(fmt __VA_OPT__(,) __VA_ARGS__);\
        }\
    } while(0)


#define _CUW3_CHECK_RETURN_VAL(cond, value, fmt, ...) \
    do { \
        if (!(cond)) { \
            std::cerr << #cond << " " << fmt << std::endl; \
            return value; \
        } \
    } while(0)

#define _CUW3_CHECK_RETURN_VAL_NO_MSG(cond, value) \
    do { \
        if (!(cond)) { \
            return value; \
        } \
    } while(0)

#define _CUW3_CHECK_RETURN_VOID(cond, fmt, ...) \
    do { \
        if (!(cond)) { \
            std::cerr << #cond << " " << fmt << std::endl; \
            return; \
        } \
    } while(0)

#define _CUW3_CHECK_GOTO(cond, label, fmt, ...) \
    do { \
        if (!(cond)) { \
            std::cerr << #cond << " " << fmt << std::endl; \
            goto label; \
        } \
    } while(0)

#define _CUW3_ABORT_CRITICAL(fmt, ...) _CUW3_ABORT(fmt __VA_OPT__(,) __VA_ARGS__)
#define _CUW3_CHECK_CRITICAL(cond, fmt, ...) _CUW3_CHECK(cond, fmt __VA_OPT__(,) __VA_ARGS__)


#ifdef CUW3_DISABLE_ALL_CHECKS
    #define CUW3_DISABLE_GENERAL_CHECKS
    #define CUW3_DISABLE_CRITICAL_CHECKS
#endif

#ifndef CUW3_DISABLE_ALL_CHECKS
    #define CUW3_ASSERT(cond, fmt, ...) _CUW3_ASSERT(cond, fmt __VA_OPT__(,)  __VA_ARGS__)
    #define CUW3_ABORT(fmt, ...) _CUW3_ABORT(fmt __VA_OPT__(,)  __VA_ARGS__)
    #define CUW3_CHECK(cond, fmt, ...) _CUW3_CHECK(cond, fmt __VA_OPT__(,)  __VA_ARGS__)
    #define CUW3_CHECK_RETURN_VAL(cond, value, fmt, ...) _CUW3_CHECK_RETURN_VAL(cond, value, fmt __VA_OPT__(,)  __VA_ARGS__)
    #define CUW3_CHECK_RETURN_VAL_NO_MSG(cond, value) _CUW3_CHECK_RETURN_VAL_NO_MSG(cond, value)
    #define CUW3_CHECK_RETURN_VOID(cond, fmt, ...) _CUW3_CHECK_RETURN_VOID(cond, fmt __VA_OPT__(,)  __VA_ARGS__)
    #define CUW3_CHECK_GOTO(cond, label, fmt, ...) _CUW3_CHECK_GOTO(cond, label, fmt __VA_OPT__(,)  __VA_ARGS__)
#else
    #define CUW3_ASSERT(cond, fmt, ...) 
    #define CUW3_ABORT(fmt, ...)
    #define CUW3_CHECK(cond, fmt, ...)
    #define CUW3_CHECK_RETURN_VAL(cond, value, fmt, ...) 
    #define CUW3_CHECK_RETURN_VAL_NO_MSG(cond, value)
    #define CUW3_CHECK_RETURN_VOID(cond, fmt, ...) 
    #define CUW3_CHECK_GOTO(cond, label, fmt, ...) 
#endif

#ifndef CUW3_DISABLE_CRITICAL_CHECKS
    #define CUW3_ABORT_CRITICAL(fmt, ...) _CUW3_ABORT_CRITICAL(fmt __VA_OPT__(,)  __VA_ARGS__)
    #define CUW3_CHECK_CRITICAL(cond, fmt, ...) _CUW3_CHECK_CRITICAL(cond, fmt __VA_OPT__(,)  __VA_ARGS__)
#else
    #define CUW3_ABORT_CRITICAL(fmt, ...)
    #define CUW3_CHECK_CRITICAL(cond, fmt, ...)
#endif
