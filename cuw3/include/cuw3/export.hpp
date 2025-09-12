#pragma once

#if defined _WIN32
    #if defined CUW3_EXPORTS
        #define CUW3_API __declspec(dllexport)
    #elif defined CUW3_IMPORTS
        #define CUW3_API __declspec(dllimport)
    #else
        #define CUW3_API
    #endif
#else
    #define CUW3_API __attribute__((visibility("default")))
#endif