/**
 * @file clog.h
 * @brief Logging functionality
 *
 * @defgroup clog Logging
 * @ingroup libcommon
 * @brief Logging functionality
 *
 * At the top of every source file include the following lines:
 * @code{.c}
 * #include <common/clog.h>
 * CLOG_MODULE("module_name")
 * @endcode
 *
 * Replace module_name with the name of your module. The same name can be used in multiple compilation units, in which
 * case all source files using the same module_name will share logging settings.
 *
 * Module names should be, short descriptive and unique. Try to group things when naming modules, so that it would be
 * easier to filter and organize messages belong to the same code. Use ':' to seperate module sub-groupings.
 *
 * For example the @ref cnaconfig library uses 'cnaconfig' for it's logging module name. While the @ref cfg_mon uses
 * 'cnaconfig:mon'. To indicate that its part of the cnaconfig library but still uses a unique name to have it's own
 * settings.
 *
 * Each application should call @ref c_log_init() to initialize the logging subystem. The logging will still work with
 * out the call, but it will use default values and will only log #C_LOG_LEVEL_ERR or bellow messages for all logging
 * modules.
 *
 * There are two types of log-levels, the first maps to syslog levels and follows its semantics. I.e. It will only log
 * message whose severity is equal or higher than the log-level set for the module. The second type is the user
 * log-level these are showed only if they are explicitly enabled by the user of the logging subsystem using the
 * @ref c_log_level_enable() function.
 *
 * There are some environment variables that can be used to control the logging subsystem:
 * - C_LOG_SKIP_CONFIG: If set to 1, the logging subsystem will not read the configuration file.
 * - C_LOG_MIN_LEVEL: Set the default log level for all modules.
 * - C_LOG_STDOUT: If set to 1, log messages will be sent to stdout.
 *
 * @section clog_example Adding new log-levels
 *
 * Because all of the log-levels below the @ref C_LOG_LEVEL_FIRST_ALWAYS are mapped to syslog levels, it is only
 * possible to add new debug as any log-level below @ref C_LOG_LEVEL_ALL. Any log-level above @ref C_LOG_LEVEL_DEBUG
 * is mapped to @ref C_LOG_LEVEL_DEBUG when sent to syslog and friends.
 *
 * Try to come up with a short but descriptive name for the new log-level. 3-4 is execellent, 5 is acceptable, 6 is
 * pushing it.
 *
 * If you can use a single letter prefix for the macro name, that would be great. If not, use as few characters as
 * possible while still being unique and at least somewhat related to the log-level name.
 *
 * @li Define new enum in @ref c_log_level.
 * @li Add a new case to @ref c_log_level_string() and  @ref c_log_string_to_level().
 * @li #define a new macro for the new log-level, ie. #define MYLOG(...) LOG_PRINTF(MY_LOG_LEVEL, __VA_ARGS__)
 *      - xxxLOG(...)
 *      - xxxLOG_EX(file, line, func, mod, ...)
 *      - xxxLOG_HEXDUMP(buf, len, ...)
 *      - xxxLOG_HEXDUMP_EX(buf, len, file, line, func, mod, ...)
 *      - xxxLOG_HEXDUMP_NO_ASCII(buf, len, ...)
 *      - xxxLOG_HEXDUMP_EX_NO_ASCII(buf, len, file, line, func, mod, ...)
 *
 *     Use the existing macros as a template, don't be lazy and add every variation of the macros. Try to keep the
 * macros in the same order as the existing ones.
 * @{
 */

/* -*-*- COPYRIGHT-GOES-HERE -*-*-* */
#ifndef __C_LOG_H__
#define __C_LOG_H__

#include <dll_export.h>
#include <sys/types.h>
#include <stdlib.h>
#include <stdbool.h>
#ifdef __unix__
#include <syslog.h>
#include <sys/uio.h>
#endif
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#ifndef __unix__
struct iovec
{
    void *iov_base; /**< Pointer to data */
    size_t iov_len; /**< Length of data */
};
#endif // __unix__

#ifndef TUBS_SOURCE_DIR
// add a dummy define so that the include files can be included from anywhere and the IDE will not complain about
// missing defines
#define TUBS_SOURCE_DIR ""
#endif

C_START_HEADER_DECL
/**
 * @brief This map common functionality to syslog facilities. They are not used internally for anything and are
 * provided to syslog directly. The syslogd's configuration the can specify filtering criteria based on the facility
 *
 * @deprecated Syslog facilities are fairly useless for clog since the log module concept allows for similar filtering
 * based on module name. This is probably going away
 *
 */
enum c_log_facility
{
    C_LOG_GENERIC     = 0,
    C_LOG_MAX,
};

/**
 * @brief Allowed and known log-level.
 * 
 * Higher importance messages have the lowest number. When setting a module's minimal log level it is setting the log
 * level above which message will be ignored.
 * 
 */
enum c_log_level
{
    C_LOG_LEVEL_MIN   = 0,
    C_LOG_LEVEL_EMERG = C_LOG_LEVEL_MIN, /**< This error indicated that the kernel is about to crash, probably */
    C_LOG_LEVEL_EMERGENCY = C_LOG_LEVEL_EMERG,

    C_LOG_LEVEL_ALERT,   /**< Log a system level error, ie when a critical compoenent of a system is no longer
                                functioning */
    C_LOG_LEVEL_CRIT,    /**< Logs an error that is about to cause the application to exit/crash */
    C_LOG_LEVEL_CRITICAL = C_LOG_LEVEL_CRIT,

    C_LOG_LEVEL_ERR,     /**< Logs an error that prevented some function from completing, but the application will run
                                possibly in a reduced state */
    C_LOG_LEVEL_ERROR = C_LOG_LEVEL_ERR,

    C_LOG_LEVEL_WARNING, /**< Log a warning, usually used to indicate inconsistent state */
    C_LOG_LEVEL_NOTICE,  /**< Indicates a state change that affects the primary function of the software. Ie, network
                                links goes away, or a new code-unit is discovered */
    C_LOG_LEVEL_INFO,    /**< Indicates an internal state change, not meant for customer */
    C_LOG_LEVEL_DEBUG,   /**< A debug message... not meant for customers */
    C_LOG_LEVEL_DEBUG2,  /**< When previous log-level starts producing too much output to be useful, start moving old
                                messages to this level */
    C_LOG_LEVEL_DEBUG3,  /**< ditto */

    C_LOG_LEVEL_ALL, /**< Special log level that applies to all log levels when used to set module's settings, should
                            not be used in log messages */

    C_LOG_LEVEL_FIRST_ALWAYS, /**< First log level that is always logged, used for internal bookkeeping, not to be
                                     used by API consumers */
    C_LOG_LEVEL_IO_TRACE = C_LOG_LEVEL_FIRST_ALWAYS, /**< I/O tracing*/
    C_LOG_LEVEL_SLOG,                                  /**< Statistic? logging */

    C_LOG_LEVEL_MAX = C_LOG_LEVEL_SLOG, /**< Maximum log level, don't forget to update this when adding new log levels */
};

/**
 * Internal macros to convert between log levels and indexes for mapping within modules
*/

#define C_LOG_ALWAYS_TO_INDEX(__ll) \
    (((__ll) >= C_LOG_LEVEL_FIRST_ALWAYS && (__ll) <= C_LOG_LEVEL_MAX) ? (__ll) - C_LOG_LEVEL_FIRST_ALWAYS : 0)
#define C_LOG_INDEX_TO_ALWAYS(__idx) \
    (((__idx) + C_LOG_LEVEL_FIRST_ALWAYS) <= C_LOG_LEVEL_MAX ? (__idx) + C_LOG_LEVEL_FIRST_ALWAYS : 0)
#define C_LOG_LEVEL_ALWAYS_NUM    (C_LOG_LEVEL_MAX - C_LOG_LEVEL_FIRST_ALWAYS + 1)
#define C_LOG_LEVEL_ALWAYS_ARRAY(__name) bool __name[C_LOG_LEVEL_ALWAYS_NUM]
#define C_LOG_LEVEL_ALWAYS_SET(__arr, __ll, __val) \
    ((__ll) >= C_LOG_LEVEL_FIRST_ALWAYS && (__ll) <= C_LOG_LEVEL_MAX ? (__arr)[(__ll) - C_LOG_LEVEL_FIRST_ALWAYS] = (__val) : false)
#define C_LOG_LEVEL_ALWAYS_ENABLED(__arr, __ll) \
    ((__ll) >= C_LOG_LEVEL_FIRST_ALWAYS && (__ll) <= C_LOG_LEVEL_MAX ? (__arr)[(__ll) - C_LOG_LEVEL_FIRST_ALWAYS] : false)

/**
 * @brief Convert @ref c_log_level to a string
 * 
 * @param level Log level
 * @return A c-string if @p level is valid, or NULL
 */
static inline const char * c_log_level_string(enum c_log_level level)
{
    switch (level)
    {
    case C_LOG_LEVEL_EMERG:
        return "EMRG";
        break;
    case C_LOG_LEVEL_ALERT:
        return "ALRT";
        break;
    case C_LOG_LEVEL_CRIT:
        return "CRIT";
        break;
    case C_LOG_LEVEL_ERR:
        return "ERRO";
        break;
    case C_LOG_LEVEL_WARNING:
        return "WARN";
        break;
    case C_LOG_LEVEL_NOTICE:
        return "NOTI";
        break;
    case C_LOG_LEVEL_INFO:
        return "INFO";
        break;
    case C_LOG_LEVEL_DEBUG:
        return "DEBG";
        break;
    case C_LOG_LEVEL_DEBUG2:
        return "DEBG2";
        break;
    case C_LOG_LEVEL_DEBUG3:
        return "DEBG3";
        break;
    case C_LOG_LEVEL_ALL:
        return "ALL";
        break;
    case C_LOG_LEVEL_IO_TRACE:
        return "IO";
        break;
    case C_LOG_LEVEL_SLOG:
        return "SLOG";
        break;
    }

    return "NONE";
}

/**
 * @brief convert value returned by @ref c_log_level_string back into @ref c_log_level
 * 
 * @param lvl A c string
 * @return enum c_log_level 
 */
static inline enum c_log_level c_log_string_to_level(const char *lvl)
{
    if (lvl == NULL)
    {
        return C_LOG_LEVEL_ALL;
    }

    if (strcasecmp("EMRG", lvl) == 0)
    {
        return C_LOG_LEVEL_EMERG;
    }
    if (strcasecmp("ALRT", lvl) == 0)
    {
        return C_LOG_LEVEL_ALERT;
    }
    if (strcasecmp("CRIT", lvl) == 0)
    {
        return C_LOG_LEVEL_CRIT;
    }
    if (strcasecmp("ERRO", lvl) == 0)
    {
        return C_LOG_LEVEL_ERR;
    }
    if (strcasecmp("WARN", lvl) == 0)
    {
        return C_LOG_LEVEL_WARNING;
    }
    if (strcasecmp("NOTI", lvl) == 0)
    {
        return C_LOG_LEVEL_NOTICE;
    }
    if (strcasecmp("INFO", lvl) == 0)
    {
        return C_LOG_LEVEL_INFO;
    }
    if (strcasecmp("DEBG", lvl) == 0)
    {
        return C_LOG_LEVEL_DEBUG;
    }
    if (strcasecmp("DEBG2", lvl) == 0)
    {
        return C_LOG_LEVEL_DEBUG2;
    }
    if (strcasecmp("DEBG3", lvl) == 0)
    {
        return C_LOG_LEVEL_DEBUG3;
    }
    if (strcasecmp("ALL", lvl) == 0)
    {
        return C_LOG_LEVEL_ALL;
    }
    if (strcasecmp("IO", lvl) == 0)
    {
        return C_LOG_LEVEL_IO_TRACE;
    }
    if (strcasecmp("SLOG", lvl) == 0)
    {
        return C_LOG_LEVEL_SLOG;
    }

    return C_LOG_LEVEL_ALL;
}

/**
 * @brief Handle to a log-module information
 */
typedef struct c_log_module *c_log_module_t;

typedef void (*c_log_cb_t)(void * cb_data, const char * fmt, va_list va);

/**
 * @brief Initialize the logging API
 * 
 * @param ident How the application should be identified (usually the process name)
 * @param facility Which logging facility this application belongs to
 * @param default_log_level The default minimal log-level.
 * @return PUB_API 
 */
PUB_API void c_log_init(const char * ident, enum c_log_facility facility, enum c_log_level default_log_level);

/**
 * @brief Enable sending of logging messages to std out
 * 
 * @param filehandle A pointer to @e FILE, stdio handle. NULL to disable logging.
 */
PUB_API void c_log_to_stdio(FILE * filehandle);

PUB_API void c_log_to_stdio_set_level(enum c_log_level level);

/**
 * @brief Set a callback to be invoked for each log message.
 * 
 * @param cb Pointer to callback
 * @param cb_data Passed to cb
 */
PUB_API void c_log_ext_callback(c_log_cb_t cb, void *cb_data);

/**
 * @brief Enable sending of logging message to syslog
 * 
 * @param enable Pass @e true to enable logging to syslog, or @e false to disable
 */
PUB_API void c_log_to_syslog(bool enable);

/**
 * @brief Retrieve the pointer to @ref c_log_module_t for module specified in @p name
 * 
 * Will create a new module info if it does not exist
 * 
 * @param name Name of the module to find
 * @return Pointer to @ref c_log_module_t
 */
PUB_API c_log_module_t c_log_get_module(const char *name);

/**
 * @brief Change log module's minimal log level, message with log_level higer than @p log_level will be ignored when
 * generated by @p module
 * 
 * @param module Pointer to a log module, can be @e NULL then it will be applied to all known modules
 * @param log_level One of @ref c_log_level constants
 * @param log_level The new log level for the module
 */
PUB_API void c_log_module_set_log_level(c_log_module_t module, enum c_log_level log_level);

/**
 * @brief Enable showing of the log-level when message are printed
 * 
 * @param module Pointer to a log module. Can be @e NULL in that case applied to all known modules
 * @param log_level One of @ref c_log_level constants
 * @param show True to show, false to not to show
 */
PUB_API void c_log_show_log_level(c_log_module_t module, enum c_log_level log_level, bool show);
/**
 * @brief Enable showing of the module name when message are printed
 *
 * @param module Pointer to a log module. Can be @e NULL in that case applied to all known modules
 * @param log_level One of @ref c_log_level constants
 * @param show True to show, false to not to show
 */
PUB_API void c_log_show_module(c_log_module_t module, enum c_log_level log_level, bool show);
/**
 * @brief Enable showing of the filename when message are printed
 * 
 * @param module Pointer to a log module. Can be @e NULL in that case applied to all known modules
 * @param log_level One of @ref c_log_level constants
 * @param show True to show, false to not to show
 */
PUB_API void c_log_show_file(c_log_module_t module, enum c_log_level log_level, bool show);
/**
 * @brief Enable showing the name of the function where the log message originated
 * 
 * @param module Pointer to a log module. Can be @e NULL in that case applied to all known modules
 * @param log_level One of @ref c_log_level constants
 * @param show True to show, false to not to show
 */
PUB_API void c_log_show_func(c_log_module_t module, enum c_log_level log_level, bool show);
/**
 * @brief Show base filename when message are printeda, as apposed to full path
 * 
 * @param module Pointer to a log module. Can be @e NULL in that case applied to all known modules
 * @param log_level One of @ref c_log_level constants
 * @param show True to short file name, otherwise full path will be printed
 */
PUB_API void c_log_show_short_file(c_log_module_t module, enum c_log_level log_level, bool show);
/**
 * @brief Enable showing of the line number when message are printed
 * 
 * @param module Pointer to a log module. Can be @e NULL in that case applied to all known modules
 * @param log_level One of @ref c_log_level constants
 * @param show True to show, false to not to show
 */
PUB_API void c_log_show_line(c_log_module_t module, enum c_log_level log_level, bool show);
/**
 * @brief Enable padding of log module name and filenames
 * 
 * @param module Pointer to a log module. Can be @e NULL in that case applied to all known modules
 * @param log_level One of @ref c_log_level constants
 * @param pad @e true to enable padding of file name and module names
 */
PUB_API void c_log_pad_when_writing(c_log_module_t module, enum c_log_level log_level, bool pad);
/**
 * Enables or disables logging for a specific module at a given log level.
 *
 * @param module The module for which logging needs to be enabled or disabled. if @e NULL, the setting will be applied to all modules.
 * @param log_level The log level to enable or disable. Must @e C_LOG_LEVEL_ALL or >= @e C_LOG_LEVEL_FIRST_ALWAYS
 * @param enable Set to @e true to enable logging, @e false to disable logging.
 */
PUB_API void c_log_level_enable(c_log_module_t module, enum c_log_level log_level, bool enable);
/**
 * @brief Checks if the specified log level is enabled for the given module.
 *
 * @param module The module for which the log level is being checked. Can't be @e NULL.
 * @param log_level The log level to check. Must be >= @e C_LOG_LEVEL_FIRST_ALWAYS
 * @return @e true if the log level is enabled for the module, @e false otherwise.
 */
PUB_API bool c_log_level_enabled(c_log_module_t module, enum c_log_level log_level);

/**
 * Retrieves the name of the specified log module.
 *
 * @param module The log module.
 * @return The name of the log module as a null-terminated string.
 */
PUB_API const char *c_log_module_name(c_log_module_t module);

/**
 * @brief Set the log level for a specific module
 * @param name Name of the module (or part of the name to match more than one)
 * @param log_level The new log level for the module
 */
PUB_API void c_log_set_matched_module_name_log_level(const char *name, enum c_log_level log_level);

/**
 * @brief Process a log message and send it on it's way
 *
 */
PUB_API bool log_printf(const char *src_dir,
                        const char *file_name,
                        int line,
                        const char *func_name,
                        c_log_module_t log_module,
                        enum c_log_level log_level,
                        const char *fmt,
                        ...) __attribute__((format(printf, 7, 8)));
/**
 * @brief Produces a formated hexdump output
 */
PUB_API bool log_hexdump(const char *src_dir,
                         const char *file_name,
                         int line,
                         const char *func_name,
                         c_log_module_t log_module,
                         enum c_log_level log_level,
                         const char *buf,
                         ssize_t len,
                         bool show_ascii,
                         const char *fmt,
                         ...) __attribute__((format(printf, 10, 11)));

#ifdef __unix__
PUB_API bool log_hexdumpiov(const char *src_dir,
                            const char *file_name,
                            int line,
                            const char *func_name,
                            c_log_module_t log_module,
                            enum c_log_level log_level,
                            struct iovec *iov,
                            int iovcnt,
                            ssize_t len,
                            bool show_ascii,
                            const char *fmt,
                            ...) __attribute__((format(printf, 11, 12)));
#endif // __unix__

PUB_API void c_log_cleanup();

/**
 * @brief Wrapper around log_printf()
 *
 */
#define _LOG_PRINTF(_src_dir, _file, _line, _func_name, _log_module, _log_level, ...) \
    log_printf(_src_dir, _file, _line, _func_name, _log_module, _log_level, __VA_ARGS__)

/**
 * @brief Log a message to an arbitrary log level, used as a wrapper by @e XLOG() macros
 */
#define LOG_PRINTF(_log_level, ...) \
    _LOG_PRINTF(TUBS_SOURCE_DIR, __FILE__, __LINE__, __func__, __c_log_module__, _log_level, __VA_ARGS__)

#define LOG_PRINTF_EX(_log_level, _file, _line, _func, _mod, ...) \
    _LOG_PRINTF(TUBS_SOURCE_DIR, _file, _line, _func, _mod, _log_level, __VA_ARGS__)

/**
 * @brief Warapper for @ref log_hexdump function
 * 
 */
#define _LOG_HEXDUMP(_src_dir, _file, _line, _func_name, _log_module, _log_level, _buf, _len, _show_ascii, ...) \
    log_hexdump(_src_dir, _file, _line, _func_name, _log_module, _log_level, (const char *)_buf, _len, _show_ascii, __VA_ARGS__)

#define _LOG_HEXDUMPIOV(_src_dir, _file, _line, _func_name, _log_module, _log_level, _iov, _iovcnt, _len, _show_ascii, ...) \
    log_hexdumpiov(_src_dir, _file, _line, _func_name, _log_module, _log_level, _iov, _iovcnt, _len, _show_ascii, __VA_ARGS__)

/**
 * @brief Wrapper macro used by @e XXX_HEXDUMP macros, or can be used if calling one of those macros is not an option
 * 
 */
#define LOG_HEXDUMP(_log_level, _buf, _len, ...) \
    _LOG_HEXDUMP(TUBS_SOURCE_DIR, __FILE__, __LINE__, __func__, __c_log_module__, _log_level, _buf, _len, true, __VA_ARGS__)

#define LOG_HEXDUMP_NO_ASCII(_log_level, _buf, _len, ...) \
    _LOG_HEXDUMP(TUBS_SOURCE_DIR, __FILE__, __LINE__, __func__, __c_log_module__, _log_level, _buf, _len, false, __VA_ARGS__)

#define LOG_HEXDUMP_EX(_log_level, _file, _line, _func, _mod, _buf, _len, ...) \
    _LOG_HEXDUMP(TUBS_SOURCE_DIR, _file, _line, _func, _mod, _log_level, _buf, _len, true, __VA_ARGS__)

#define LOG_HEXDUMP_EX_NO_ASCII(_log_level, _file, _line, _func, _mod, _buf, _len, ...) \
    _LOG_HEXDUMP(TUBS_SOURCE_DIR, _file, _line, _func, _mod, _log_level, _buf, _len, false, __VA_ARGS__)

#define LOG_HEXDUMPIOV(_log_level, _iov, _iovcnt, _len, ...) \
    _LOG_HEXDUMPIOV(TUBS_SOURCE_DIR, __FILE__, __LINE__, __func__, __c_log_module__, _log_level, _iov, _iovcnt, _len, true, __VA_ARGS__)

#define LOG_HEXDUMPIOV_NO_ASCII(_log_level, _iov, _iovcnt, _len, ...) \
    _LOG_HEXDUMPIOV(TUBS_SOURCE_DIR, __FILE__, __LINE__, __func__, __c_log_module__, _log_level, _iov, _iovcnt, _len, false, __VA_ARGS__)

#define LOG_HEXDUMPIOV_EX(_log_level, _file, _line, _func, _mod, _iov, _iovcnt, _len, ...) \
    _LOG_HEXDUMPIOV(TUBS_SOURCE_DIR, _file, _line, _func, _mod, _log_level, _iov, _iovcnt, _len, true, __VA_ARGS__)

#define LOG_HEXDUMPIOV_EX_NO_ASCII(_log_level, _file, _line, _func, _mod, _iov, _iovcnt, _len, ...) \
    _LOG_HEXDUMPIOV(TUBS_SOURCE_DIR, _file, _line, _func, _mod, _log_level, _iov, _iovcnt, _len, false, __VA_ARGS__)


/**
 * @brief Log a message to an arbitrary log level, used as a wrapper around the @e XLOG() macros
 * 
 * @example
 * @code{.c}
 * LOG_ONCE(ALOG("This will only be logged once"));
 * @endcode
 * 
*/
#define LOG_ONCE(...) \
    do { \
        static bool __rz_once_rz__ = false; \
        if (!__rz_once_rz__) { \
            __rz_once_rz__ = true; \
            __VA_ARGS__; \
        } \
    } while (0)

/**
 * @brief Display a traffic dump/decoding of known protocols using DEBUG log level 
 * 
 */
#define DLOG_TRAFDUMP(type, buf, len, ...)  LOG_HEXDUMP(C_LOG_LEVEL_DEBUG, buf, len, __VA_ARGS__)

/* suppress INTELLISENSE errors about attribute taking arguments */
#if __INTELLISENSE__
#pragma diag_suppress 1094
#endif

#ifndef CLOG_SKIP_CONSTRUCTORS
/**
 * @brief Setup the logging module information for the current source file.
 * Every .c file should have this macro called right after including clog.h. This macro sets up constructor that will
 * be invoked after __setup_c_log_var() and will set @p __c_log_module__ to point to log module information
 * specific to the module specified in @p name.
 *
 */
#define CLOG_MODULE(name)                                                    \
    __attribute__((constructor(1002))) static void __setup_c_log_var_named() \
    {                                                                          \
        /* overwrite with module based on provided name */                     \
        __c_log_module__ = c_log_get_module(name);                         \
    }

/**
 * @brief Setup the logging module information for the current source file, with a default log level
 *
 * The @p def_log_level will be used as the default log level for the module. Otherwise it does the same thing as
 * @ref CLOG_MODULE().
 *
 * With the way the code is usually setup, @p def_log_level will override the default log level set by
 * @ref c_log_init() or from the environment variable. But @ref cnaconfig_setup_logging_hooks() will override the
 * log level set by this macro. Allowing the config file to override all other settings.
 */
#define CLOG_MODULE_EX(name, def_log_level)                                                \
    __attribute__((constructor(1002))) static void __setup_c_log_var_named()               \
    {                                                                                        \
        /* overwrite with module based on provided name */                                   \
        __c_log_module__ = c_log_get_module(name);                                       \
        /* set default log level, will get overwritten by cnaconfig_setup_logging_hooks() */ \
        c_log_module_set_log_level(__c_log_module__, def_log_level);                     \
    }

#define CLOG_MODULE_EX2(name, def_log_level, log_module_var)                                    \
    __attribute__((constructor(1002))) static void __setup_c_log_var_named ## log_module_var () \
    {                                                                                             \
        /* overwrite with module based on provided name */                                        \
        log_module_var = c_log_get_module(name);                                                \
        /* set default log level, will get overwritten by cnaconfig_setup_logging_hooks() */      \
        c_log_module_set_log_level(log_module_var, def_log_level);                              \
    }
/**
 * @brief Creates a static variables that is use by the varios LOG marcros above to identify the log module the current
 * compilation unit belongs to.
 */
static c_log_module_t __c_log_module__;

/**
 * @brief This is the constructor that always gets called when clog.h is included
 * 
 * This function setup the @p __c_log_module__ to point to a log module with no
 * name
 * @note 1001 is the priority for the constructor, lower numbered constructors are called first. This should be
 * the lowest number in the entire namespace
 */
__attribute__ ((constructor(1001))) static void __setup_c_log_var()
{
    __c_log_module__ = c_log_get_module(NULL);
}
#endif

/**
 * @brief Make a different log module active inside a block of code.
 * 
 * This macro is used to change the active log module for the duration of the block of code. This is useful when you
 * want to log messages to a different module than the one that is active for the current compilation unit.
 * 
 * @note This creates a new scope, so any variables declared inside the block will not be available outside of it.
 * 
 * @warning Since this macro gets expanded into a block of code, it should not be used in the middle of a statement.
 * 
 * @warning If the module name used was not previously used in the compilation unit, the log level will be set to the
 * default log level and it will save the settings for the module since the log module is created on the fly and will
 * not exist when @ref cnaconfig_setup_logging_hooks() is called.
 * 
 * @example 
 * @code{.c}
 * CLOG_MODULE_PUSH("module_name")
 * DLOG("This will be logged to module_name");
 * CLOG_MODULE_POP()
*/
#define CLOG_MODULE_PUSH(name)                           \
    {                                                      \
        static c_log_module_t __c_log_module__ = NULL; \
        if (__c_log_module__ == NULL)                    \
            __c_log_module__ = c_log_get_module(name);

#define CLOG_MODULE_PUSH_EX(name, default_log_level)                           \
    {                                                                            \
        static c_log_module_t __c_log_module__ = NULL;                       \
        if (__c_log_module__ == NULL)                                          \
        {                                                                        \
            __c_log_module__ = c_log_get_module(name);                       \
            c_log_module_set_log_level(__c_log_module__, default_log_level); \
        }

/**
 * @brief Make a different log module active inside a block of code. Slightly less optimal than @ref
 * CLOG_MODULE_PUSH()
 *
 * Use if the name of the module is not known at compile time.
 */
#define CLOG_MODULE_PUSH_ALWAYS(name) \
    {                                   \
        c_log_module_t __c_log_module__ = c_log_get_module(name);

/**
 * @brief Restore the active log module to the one that was active before the last @ref CLOG_MODULE_PUSH() call
 * 
 * @note This macro should be used at the end of the block of code that was started with @ref CLOG_MODULE_PUSH().
 * 
 * @warning Since this macro closes the block of code it is important to remember that any variables declared inside
 * the block will not be available outside of it.
 */
#define CLOG_MODULE_POP() }

C_END_HEADER_DECL
#include "gen_log_macros.h"

/** @} */
#endif