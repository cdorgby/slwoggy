/**
 * @file dll_export.h
 */
/* -*-*- COPYRIGHT-GOES-HERE -*-*-* */
#ifndef __DLL_EXPORT_H__
#define __DLL_EXPORT_H__

/**
 * @brief Mark the symbols as exported (for shared librareis)
 *
 * @code{.c}
 * PUB_API int my_fancy_function(int a, char * b);
 * @endcode
 */
#define PUB_API __attribute__((visibility("default")))

/**
 * @brief Mark the symbols as hidden (for shared librareis)
 */
#define PRIV_API __attribute__((visibility("hidden")))

#ifdef __cplusplus
    #define C_START_HEADER_DECL extern "C" {
    #define C_END_HEADER_DECL }
#else
    #define C_START_HEADER_DECL
    #define C_END_HEADER_DECL
#endif

/**
 * @brief mark function as unused
 * 
 */
#define C_UNUSED __attribute__((unused))

#endif // __DLL_EXPORT_H__
