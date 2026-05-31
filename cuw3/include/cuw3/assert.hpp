#pragma once

#include <cassert>
#include <iostream>

// TODO : proper fmt printing (or just msg... would work just fine)

#define CUW3_ASSERT_(cond, fmt, ...) assert(!!(cond) && fmt)

#define CUW3_ABORT_(fmt, ...) \
    do {\
        std::cerr << (fmt) << "\n";\
        std::abort();\
    } while(0)

#define CUW3_CHECK_(cond, fmt, ...) \
    do {\
        if (!(cond)) {\
            CUW3_ABORT_(fmt __VA_OPT__(,) __VA_ARGS__);\
        }\
    } while(0)


#define CUW3_CHECK_RETURN_VAL_(cond, value, fmt, ...) \
    do { \
        if (!(cond)) { \
            std::cerr << #cond << " " << fmt << "\n"; \
            return value; \
        } \
    } while(0)

#define CUW3_CHECK_RETURN_VAL_NO_MSG_(cond, value) \
    do { \
        if (!(cond)) { \
            return value; \
        } \
    } while(0)

#define CUW3_CHECK_RETURN_VOID_(cond, fmt, ...) \
    do { \
        if (!(cond)) { \
            std::cerr << #cond << " " << fmt << "\n"; \
            return; \
        } \
    } while(0)

#define CUW3_CHECK_GOTO_(cond, label, fmt, ...) \
    do { \
        if (!(cond)) { \
            std::cerr << #cond << " " << fmt << "\n"; \
            goto label; \
        } \
    } while(0)

#define CUW3_ABORT_CRITICAL_(fmt, ...) CUW3_ABORT_(fmt __VA_OPT__(,) __VA_ARGS__)
#define CUW3_CHECK_CRITICAL_(cond, fmt, ...) CUW3_CHECK_(cond, fmt __VA_OPT__(,) __VA_ARGS__)


#ifdef CUW3_DISABLE_ALL_CHECKS
    #define CUW3_DISABLE_GENERAL_CHECKS
    #define CUW3_DISABLE_CRITICAL_CHECKS
#endif

#ifndef CUW3_DISABLE_ALL_CHECKS
    #define CUW3_ASSERT(cond, fmt, ...) CUW3_ASSERT_(cond, fmt __VA_OPT__(,)  __VA_ARGS__)
    #define CUW3_ABORT(fmt, ...) CUW3_ABORT_(fmt __VA_OPT__(,)  __VA_ARGS__)
    #define CUW3_CHECK(cond, fmt, ...) CUW3_CHECK_(cond, fmt __VA_OPT__(,)  __VA_ARGS__)
    #define CUW3_CHECK_RETURN_VAL(cond, value, fmt, ...) CUW3_CHECK_RETURN_VAL_(cond, value, fmt __VA_OPT__(,)  __VA_ARGS__)
    #define CUW3_CHECK_RETURN_VAL_NO_MSG(cond, value) CUW3_CHECK_RETURN_VAL_NO_MSG_(cond, value)
    #define CUW3_CHECK_RETURN_VOID(cond, fmt, ...) CUW3_CHECK_RETURN_VOID_(cond, fmt __VA_OPT__(,)  __VA_ARGS__)
    #define CUW3_CHECK_GOTO(cond, label, fmt, ...) CUW3_CHECK_GOTO_(cond, label, fmt __VA_OPT__(,)  __VA_ARGS__)
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
    #define CUW3_ABORT_CRITICAL(fmt, ...) CUW3_ABORT_CRITICAL_(fmt __VA_OPT__(,)  __VA_ARGS__)
    #define CUW3_CHECK_CRITICAL(cond, fmt, ...) CUW3_CHECK_CRITICAL_(cond, fmt __VA_OPT__(,)  __VA_ARGS__)
#else
    #define CUW3_ABORT_CRITICAL(fmt, ...)
    #define CUW3_CHECK_CRITICAL(cond, fmt, ...)
#endif
