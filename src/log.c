#include <log.h>
CLOG_MODULE("cnalog")

#include <log_internal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdarg.h>
#include <ctype.h>
#include <string.h>
#include <assert.h>
#include <utstring.h>

#ifdef __unix__
#include <sys/uio.h>
#endif

#ifndef CNA_MIN
#define CNA_MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#ifndef CNA_MAX
#define CNA_MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

const char *cna_paths_basename(const char *path)
{
    const char *p = strrchr(path, '/');
    if (p)
    {
        return p + 1;
    }
    return path;
}

static struct log_privates *log_private_data = NULL;

static struct c_log_module no_name_module = {
    .name          = "<noname>",
    .min_log_level = C_LOG_LEVEL_ERR,
};

// 2k buffer for log messages, anything larger will be malloc'd
#define LOG_BUFFER_SIZE 2048
#define LOG_HEX_DUMP_BYTES_PER_LINE 16
#define LOG_HEX_DUMP_MAX_BYTES_PER_LINE 32

_Thread_local char log_buffer[LOG_BUFFER_SIZE];

c_log_handle_t c_log_get_handle()
{
    const char *p;
    if (!log_private_data)
    {
        log_private_data = malloc(sizeof(struct log_privates));
        memset(log_private_data, 0, sizeof(struct log_privates));

        log_private_data->log_file         = true;
        log_private_data->log_line         = true;
        log_private_data->log_level        = true;
        log_private_data->log_func         = false;
        log_private_data->log_module       = true;
        log_private_data->pad              = false;
        log_private_data->short_file_name  = true;

        log_private_data->longest_mod_name = strlen(no_name_module.name);

        p = getenv("C_LOG_MIN_LEVEL");
        if (p != NULL)
        {
            int level = atoi(p);

            if (level >= C_LOG_LEVEL_MIN && level <= C_LOG_LEVEL_FIRST_ALWAYS)
            {
                log_private_data->default_log_level  = level;
                log_private_data->log_level_from_env = true;
            }
        }
        p = getenv("C_LOG_TO_STDOUT");
        if (p != NULL)
        {
            int log = atoi(p);

            if (log == 0 || log == 1)
            {
                log_private_data->log_to_stdio = stderr;
            }
        }
        no_name_module.min_log_level = log_private_data->default_log_level;

        log_private_data->log_to_syslog = true;

        // go through all log levels and figure out the longest name
        for (int i = C_LOG_LEVEL_MIN; i < C_LOG_LEVEL_MAX; i++)
        {
            if (strlen(c_log_level_string(i)) > log_private_data->longest_log_level_name)
            {
                log_private_data->longest_log_level_name = strlen(c_log_level_string(i));
            }
        }
    }

    return log_private_data;
}

void c_log_to_stdio(FILE *filehandle)
{
    c_log_handle_t handle = c_log_get_handle();

    handle->log_to_stdio = filehandle;
    DLOG("Logging to an stdio handle enabled: %s", (filehandle ? "true" : "false"));
}

void c_log_to_stdio_set_level(enum c_log_level level)
{
    c_log_handle_t handle = c_log_get_handle();

    if (level < C_LOG_LEVEL_MIN || level >= C_LOG_LEVEL_FIRST_ALWAYS)
    {
        /* this will skip the 'log always' levels */
        if (level > C_LOG_LEVEL_FIRST_ALWAYS)
        {
            WLOG("Attempt to set log level for stdio past the max syslog level, leaving at: %s",
                 c_log_level_string(handle->log_to_stdio_level));
        }
        return;
    }
    if (level == C_LOG_LEVEL_ALL)
    {
        level = C_LOG_LEVEL_ALL - 1;
    }

    handle->log_to_stdio_level = level;
    DLOG("Logging to an stdio handle level set to: %s", c_log_level_string(level));
}

void c_log_ext_callback(c_log_cb_t cb, void *cb_data)
{
    c_log_handle_t handle = c_log_get_handle();
    handle->cb              = cb;
    handle->cb_data         = cb_data;
}

void c_log_to_syslog(bool enable)
{

    c_log_handle_t handle = c_log_get_handle();

    handle->log_to_syslog = enable;
}

c_log_module_t c_log_get_module(const char *name)
{
    c_log_handle_t handle       = c_log_get_handle();
    struct c_log_module *module = NULL;

    if (name == NULL || *name == '\0')
    {
        return &no_name_module;
    }
    HASH_FIND_STR(handle->modules, name, module);

    if (module)
    {
        return module;
    }

    module = malloc(sizeof(*module));
    memset(module, 0, sizeof(*module));
    module->name = strdup(name);
    size_t len   = strlen(name);
    if (len > handle->longest_mod_name)
    {
        handle->longest_mod_name = len;
    }
    module->min_log_level = handle->default_log_level;

    HASH_ADD_KEYPTR(hh, handle->modules, module->name, len, module);
    memcpy(module->enabled, handle->enabled, sizeof(handle->enabled));

    DLOG2("Loading default log module settings '%s' min log level: %s len: %zd",
          module->name,
          c_log_level_string(module->min_log_level),
          strlen(module->name));
    return module;
}

void c_log_init(const char *ident, enum c_log_facility facility, enum c_log_level default_log_level)
{
    c_log_handle_t handle = c_log_get_handle();

    if (handle->log_level_from_env == false)
    {
        struct c_log_module *module = NULL, *tmp = NULL;
        // update log levels for all modules to the new default only if the module's log level is the same as the old
        // default
        HASH_ITER(hh, handle->modules, module, tmp)
        {
            if (module->min_log_level == handle->default_log_level)
            {
                module->min_log_level = default_log_level;
            }
        }
        handle->default_log_level = default_log_level;
    }

#ifdef __unix__
    openlog(ident, LOG_NDELAY | LOG_PID, facility);
#endif
}

void c_log_module_set_log_level(c_log_module_t module, enum c_log_level log_level)
{
    c_log_handle_t handle    = c_log_get_handle();
    struct c_log_module *tmp = NULL;

    if (log_level < C_LOG_LEVEL_MIN && log_level >= C_LOG_LEVEL_FIRST_ALWAYS)
    {
        return;
    }

    if (module == NULL)
    {
        // update the default log level as well
        handle->default_log_level = log_level;
        HASH_ITER(hh, handle->modules, module, tmp)
        {
            module->min_log_level = handle->default_log_level;
        }

        no_name_module.min_log_level = handle->default_log_level;
    }
    else
    {
        module->min_log_level = log_level;
    }
}

void c_log_show_log_level(c_log_module_t module, enum c_log_level log_level, bool show)
{
    c_log_handle_t handle = c_log_get_handle();
    handle->log_level       = show;
}

void c_log_show_module(c_log_module_t module, enum c_log_level log_level, bool show)
{
    c_log_handle_t handle = c_log_get_handle();
    handle->log_module      = show;
}

void c_log_show_file(c_log_module_t module, enum c_log_level log_level, bool show)
{
    c_log_handle_t handle = c_log_get_handle();
    handle->log_file        = show;
}

void c_log_show_func(c_log_module_t module, enum c_log_level log_level, bool show)
{
    c_log_handle_t handle = c_log_get_handle();
    handle->log_func        = show;
}

void c_log_show_line(c_log_module_t module, enum c_log_level log_level, bool show)
{
    c_log_handle_t handle = c_log_get_handle();
    handle->log_line        = show;
}

void c_log_show_short_file(c_log_module_t module, enum c_log_level log_level, bool show)
{
    c_log_handle_t handle = c_log_get_handle();
    handle->short_file_name = show;
}

void c_log_pad_when_writing(c_log_module_t module, enum c_log_level log_level, bool pad)
{
    c_log_handle_t handle = c_log_get_handle();
    handle->pad             = pad;
}

void c_log_level_enable(c_log_module_t module, enum c_log_level log_level, bool enable)
{
    c_log_handle_t handle = c_log_get_handle();
    if (log_level < C_LOG_LEVEL_FIRST_ALWAYS)
    {
        WLOG("Attempt to enable/disable a non-always log level, ignoring");
        return;
    }

    if (module != NULL)
    {
        C_LOG_LEVEL_ALWAYS_SET(module->enabled, log_level, enable);
    }
    else
    {
        struct c_log_module *tmp = NULL;
        HASH_ITER(hh, handle->modules, module, tmp)
        {
            C_LOG_LEVEL_ALWAYS_SET(module->enabled, log_level, enable);
        }

        /* set the global default */
        C_LOG_LEVEL_ALWAYS_SET(handle->enabled, log_level, enable);
    }
}

bool c_log_level_enabled(c_log_module_t module, enum c_log_level log_level)
{
    // c_log_handle_t handle = c_log_get_handle();
    bool ret = false;

    ret = C_LOG_LEVEL_ALWAYS_ENABLED(module->enabled, log_level);
    return ret;
}

const char *c_log_module_name(c_log_module_t module)
{
    return module->name;
}

// Callback function for iterating over all cna log modules
typedef void (*c_log_iterate_cb_t)(struct c_log_module *module, void * cb_data);

// Callback data for setting log level for all cna log modules matching a name (or part of a name)
typedef struct cnalog_module_cb_data_set_log_level_by_name
{
    const char *name;
    enum c_log_level log_level;
} cnalog_module_cb_data_set_log_level_by_name_t;

/**
 * @brief Callback function for setting the log level for all cna log modules matching a name (or part of a name)
 */
static void c_log_set_matched_module_name_log_level_cb(struct c_log_module *module, void * cb_data)
{
    if((module == NULL) || (cb_data == NULL))
    {
        return;
    }
    cnalog_module_cb_data_set_log_level_by_name_t cb_data_set_log_level = *(cnalog_module_cb_data_set_log_level_by_name_t *)cb_data;
    if(cb_data_set_log_level.name == NULL)
    {
        return;
    }
    if(cb_data_set_log_level.log_level < C_LOG_LEVEL_MIN || cb_data_set_log_level.log_level >= C_LOG_LEVEL_ALL)
    {
        return;
    }
    if(strlen(cb_data_set_log_level.name) > strlen(module->name))
    {
        return;
    }
    if(!strncmp(module->name, cb_data_set_log_level.name, strlen(cb_data_set_log_level.name)))
    {
        DLOG2("Setting log level for module '%s' to %s", module->name, c_log_level_string(cb_data_set_log_level.log_level));
        module->min_log_level = cb_data_set_log_level.log_level;
    }
}

/**
 * @brief Iterate over all cna log modules and call the callback function
 */
static void c_log_iterate(c_log_iterate_cb_t cb, void *cb_data)
{
    c_log_handle_t handle = c_log_get_handle();
    struct c_log_module *module = NULL, *tmp = NULL;

    HASH_ITER(hh, handle->modules, module, tmp)
    {
        if (cb != NULL)
        {
            cb(module, cb_data);
        }
    }
}

void c_log_set_matched_module_name_log_level(const char *name, enum c_log_level log_level)
{
    cnalog_module_cb_data_set_log_level_by_name_t cb_data = {
        .name = name,
        .log_level = log_level,
    };
    c_log_iterate(c_log_set_matched_module_name_log_level_cb, (void *)&cb_data);
}

static char *_logging_prefix(const char *src_dir,
                            const char *file_name,
                            int line,
                            const char *func_name,
                            c_log_module_t log_module,
                            enum c_log_level log_level,
                            const char *fmt,
                            const char *suffix,
                            bool *free_prefix)
{
    c_log_handle_t handle = c_log_get_handle();

    char *ret              = NULL;
    bool include_module    = true;
    bool include_log_level = false;
    bool include_file      = false;
    bool include_line      = false;
    bool include_func      = false;
    bool short_file_name   = true;
    bool do_pad            = false;
    size_t expected_len    = 0;

    include_module    = handle->log_module;
    include_log_level = handle->log_level;
    include_file      = handle->log_file;
    include_line      = handle->log_line;
    include_func      = handle->log_func;
    short_file_name   = handle->short_file_name;
    do_pad            = handle->pad;

    if (include_log_level)
    {
        if (do_pad)
        {
            expected_len += handle->longest_log_level_name + 2;
        }
        else
        {
            expected_len += strlen(c_log_level_string(log_level));
        }
    }

    if (include_module)
    {
        if (expected_len > 0)
        {
            expected_len += 1;
        }

        if (do_pad)
        {
            expected_len += handle->longest_mod_name + 2;
        }
        else
        {
            expected_len += strlen(log_module->name);
        }
    }

    if (include_file)
    {
        if (expected_len > 0)
        {
            expected_len += 1;
        }
        size_t len = strlen((short_file_name ? cna_paths_basename(file_name) : file_name));

        if (do_pad)
        {
            expected_len += CNA_MAX(FILENAME_PAD, len);
        }
        else
        {
            expected_len += len;
        }
    }

    if (include_line)
    {
        if (expected_len > 0)
        {
            expected_len += 1;
        }
        if (do_pad)
        {
            expected_len += snprintf(NULL, 0, "%-4d", line);
        }
        else
        {
            expected_len += snprintf(NULL, 0, "%d", line);
        }
    }

    if (include_func)
    {
        if (expected_len > 0)
        {
            /* +3 for space and () */
            expected_len += 3;
        }
        expected_len += strlen(func_name);
    }

    if (suffix != NULL)
    {
        expected_len += strlen(suffix);
    }

    if (expected_len > 0)
    {
        int i = 0;
        /* +3 for ': ' null byte */
        expected_len += (3 + strlen(fmt));
        if (expected_len > LOG_BUFFER_SIZE)
        {
            ret = malloc(expected_len);
            *free_prefix = true;
        }
        else
        {
            ret = log_buffer;
        }
        if (include_log_level)
        {
            if (do_pad)
            {
                i += snprintf(ret + i,
                              expected_len - i,
                              "%s%-*s",
                              (i > 0 ? " " : ""),
                              handle->longest_log_level_name,
                              c_log_level_string(log_level));
            }
            else
            {
                i += snprintf(ret + i, expected_len - i, "%s%s", (i > 0 ? " " : ""), c_log_level_string(log_level));
            }
            assert(i <= expected_len);
        }

        if (include_module)
        {
            if (do_pad)
            {
                i += snprintf(ret + i,
                              expected_len - i,
                              "%s[%-*s]",
                              (i > 0 ? " " : ""),
                              handle->longest_mod_name,
                              log_module->name);
            }
            else
            {
                i += snprintf(ret + i, expected_len - i, "%s%s", (i > 0 ? " " : ""), log_module->name);
            }
            assert(i <= expected_len);
        }

        if (include_file)
        {
            if (do_pad)
            {
                size_t len = strlen((short_file_name ? cna_paths_basename(file_name) : file_name));
                len        = CNA_MAX(FILENAME_PAD, len);
                i += snprintf(ret + i,
                              expected_len - i,
                              "%s%*s%s",
                              (i > 0 ? " " : ""),
                              (int)len,
                              (short_file_name ? cna_paths_basename(file_name) : file_name),
                              (include_line ? ":" : ""));
            }
            else
            {
                i += snprintf(ret + i,
                              expected_len - i,
                              "%s%s%s",
                              (i > 0 ? " " : ""),
                              (short_file_name ? cna_paths_basename(file_name) : file_name),
                              (include_line ? ":" : ""));
            }
            assert(i <= expected_len);
        }
        if (include_line)
        {

            if (do_pad)
            {

                i += snprintf(ret + i, expected_len - i, "%-4d", line);
            }
            else
            {
                i += snprintf(ret + i, expected_len - i, "%d", line);
            }
            assert(i <= expected_len);
        }
        if (include_func)
        {
            i += snprintf(ret + i, expected_len - i, "%s%s()", (i > 0 ? " " : ""), func_name);
            assert(i <= expected_len);
        }

        i += snprintf(ret + i, expected_len - i, "%s%s", (i > 0 ? ": " : ""), fmt);
        assert(i <= expected_len);

        if (suffix != NULL)
        {
            i += snprintf(ret + i, expected_len - i, "%s", suffix);
        }
    }
    return ret;
}

static void _print_log(c_log_handle_t handle, enum c_log_level log_level, const char *prefix, va_list ap)
{
    va_list ap_copy;

    if (handle->log_to_stdio != NULL && log_level <= handle->log_to_stdio_level)
    {
        va_copy(ap_copy, ap);
        vfprintf(handle->log_to_stdio, prefix, ap_copy);
        va_end(ap_copy);
        fputc('\n', handle->log_to_stdio);
    }

    // adjust log_level to accommodate syslog
    if (log_level > C_LOG_LEVEL_DEBUG && log_level < C_LOG_LEVEL_FIRST_ALWAYS)
    {
        /* syslog doesn't know anything log level above debug */
        log_level = C_LOG_LEVEL_DEBUG;
    }
    else if (log_level >= C_LOG_LEVEL_FIRST_ALWAYS)
    {
        log_level = C_LOG_LEVEL_INFO;
    }

    if (handle->log_to_syslog)
    {
        va_copy(ap_copy, ap);
#ifdef __unix__
        vsyslog(log_level, prefix, ap_copy);
#else
        {
            // Fallback for non-unix systems, use printf
            vprintf(prefix, ap_copy);
            fputc('\n', stdout);
        }
#endif // __unix__
        va_end(ap_copy);
    }

    if (handle->cb != NULL)
    {
        va_copy(ap_copy, ap);
        handle->cb(handle->cb_data, prefix, ap_copy);
        va_end(ap_copy);
    }
}

static bool _can_log(c_log_module_t log_module, enum c_log_level log_level)
{
    // allow logs that match the minimum log level severity and the always log levels to be logged
    if (log_level > log_module->min_log_level && (log_level < C_LOG_LEVEL_FIRST_ALWAYS))
    {
        return false;
    }
    else if (log_level >= C_LOG_LEVEL_FIRST_ALWAYS && C_LOG_LEVEL_ALWAYS_ENABLED(log_module->enabled, log_level) == false)
    {
        return false;
    }

    return true;
}

static bool _log_printf(const char *src_dir,
                        const char *file_name,
                        int line,
                        const char *func_name,
                        c_log_module_t log_module,
                        enum c_log_level log_level,
                        const char *fmt,
                        va_list ap)
{
    const char *prefix      = fmt;
    char *new_prefix        = NULL;
    c_log_handle_t handle = c_log_get_handle();
    bool free_prefix        = false;

    if (!_can_log(log_module, log_level))
    {
        return false;
    }

    new_prefix = _logging_prefix(src_dir, file_name, line, func_name, log_module, log_level, fmt, NULL, &free_prefix);
    if (new_prefix != NULL)
    {
        prefix = new_prefix;
    }

    _print_log(handle, log_level, prefix, ap);

    if (new_prefix != NULL && free_prefix)
    {
        free(new_prefix);
    }
    return true;
}

bool log_printf(const char *src_dir,
                const char *file_name,
                int line,
                const char *func_name,
                c_log_module_t log_module,
                enum c_log_level log_level,
                const char *fmt,
                ...)
{
    va_list ap;
    bool ret;

    va_start(ap, fmt);
    ret = _log_printf(src_dir, file_name, line, func_name, log_module, log_level, fmt, ap);
    va_end(ap);
    
    return ret;
}

static void _log_line(c_log_handle_t handle, enum c_log_level log_level, const char *fmt, ...)
{
    va_list ap;


    if (handle->log_to_stdio != NULL && log_level <= handle->log_to_stdio_level)
    {
        va_start(ap, fmt);
        vfprintf(handle->log_to_stdio, fmt, ap);
        fputc('\n', handle->log_to_stdio);
        va_end(ap);
    }

    if (handle->log_to_syslog)
    {
        va_start(ap, fmt);
#ifdef __unix__
        vsyslog(LOG_INFO, fmt, ap);
#else
        {
            // Fallback for non-unix systems, use printf
            vprintf(fmt, ap);
            fputc('\n', stdout);
        }
#endif // __unix__
        va_end(ap);
    }

    if (handle->cb != NULL)
    {
        va_start(ap, fmt);
        handle->cb(handle->cb_data, fmt, ap);
        va_end(ap);
    }

}

/**
 * @brief Generate a hex dump line
 */
static void _gen_hex_line(UT_string *str, const char *buf, uint32_t buf_len, int bytes_per_line, uint32_t pos, bool show_ascii)
{

    utstring_clear(str);
    utstring_printf(str, "%04d: ", pos);
    for (int i = 0; i < bytes_per_line; i++)
    {
        if (i % 8 == 0)
        {
            utstring_bincpy(str, " ", 1);
        }
        if (i < buf_len)
        {
            utstring_printf(str, " %02x", 0xff & (unsigned)buf[i]);
        }
        else
        {
            utstring_bincpy(str, "   ", 3);
        }
    }

    if (show_ascii)
    {
        utstring_bincpy(str, " |", 2);
        for (int i = 0; i < bytes_per_line; i++)
        {
            if (i != 0 && i % 8 == 0)
            {
                utstring_bincpy(str, " ", 1);
            }

            if (i < buf_len)
            {
                if (isprint(buf[i]))
                {
                    utstring_bincpy(str, &buf[i], 1);
                }
                else
                {
                    utstring_bincpy(str, ". ", 1);
                }
            }
            else
            {
                utstring_bincpy(str, "  ", 1);
            }
        }
        utstring_bincpy(str, "|", 1);
    }
}

/**
 * @brief Implments the actual hexdumping of the buffer
 * 
 * Generates a 'hexdump -C' style output of the buffer.
 * 
 */
bool _log_hexdumpiov(const char *src_dir,
                     const char *file_name,
                     int line,
                     const char *func_name,
                     c_log_module_t log_module,
                     enum c_log_level log_level,
                     struct iovec *iov,
                     int iovcnt,
                     ssize_t len,
                     bool show_ascii,
                     int bytes_per_line,
                     const char *fmt,
                     va_list ap)
{
    size_t pos               = 0;
    int iov_pos              = 0;
    c_log_handle_t handle  = c_log_get_handle();
    int buf_pos              = 0;
    int buf_len              = 0;
    int lines_skipped        = 0;
    bool first_line          = true;
    UT_string str;

    if (len <= 0)
    {
        len = 0;
        // calculate the total length of the IOV
        for (int i = 0; i < iovcnt; i++)
        {
            len += iov[i].iov_len;
        }
    }

    if (len <= 0)
    {
        return false;
    }

    if (bytes_per_line <= 0)
    {
        bytes_per_line = LOG_HEX_DUMP_BYTES_PER_LINE;
    }
    else if (bytes_per_line > LOG_HEX_DUMP_MAX_BYTES_PER_LINE)
    {
        bytes_per_line = LOG_HEX_DUMP_MAX_BYTES_PER_LINE;
    }

    char prev_buf[bytes_per_line];
    char buf[bytes_per_line];

    memset(prev_buf, 0, bytes_per_line);
    memset(buf, 0, bytes_per_line);

    if (!_log_printf(src_dir, file_name, line, func_name, log_module, log_level, fmt, ap))
    {
        return false;
    }

    utstring_init(&str);

    while (pos < len)
    {
        // initial buffer setup or everything was read from the current IOV
        if (buf_pos >= buf_len)
        {
            if (iov_pos >= iovcnt)
            {
                // no more data to read, we're done
                break;
            }

            buf_pos = 0;
            buf_len = iov[iov_pos].iov_len;
            iov_pos++;
        }
        int to_copy = CNA_MIN(bytes_per_line, buf_len - buf_pos);
        memcpy(buf, iov[iov_pos - 1].iov_base + buf_pos, to_copy);
        buf_pos += to_copy;

        while(to_copy < bytes_per_line)
        {
            if (iov_pos < iovcnt)
            {
                buf_pos = 0;
                buf_len = iov[iov_pos].iov_len;
                iov_pos++;
                int remaining = CNA_MIN(bytes_per_line - to_copy, buf_len);
                memcpy(buf + to_copy, iov[iov_pos - 1].iov_base + buf_pos, remaining);
                to_copy += remaining;
                buf_pos += remaining;
            }
            else
            {
                // no more data to copy, fill the rest with zeros
                memset(buf + to_copy, 0, bytes_per_line - to_copy);
                break;
            }
        }

        // handle repeated lines
        if (!first_line && memcmp(prev_buf, buf, bytes_per_line) == 0)
        {
            lines_skipped++;

            if (lines_skipped == 1)
            {
                utstring_clear(&str);
                utstring_printf(&str, "*");
            }
            else
            {
                // skip after the first line
                continue;
            }
        }
        else
        {
            if (lines_skipped > 0)
            {
                lines_skipped = 0;
            }

            memcpy(prev_buf, buf, bytes_per_line);

            _gen_hex_line(&str, buf, to_copy, bytes_per_line, pos, show_ascii);
        }

        first_line = false;

        pos += bytes_per_line;

        _log_line(handle, log_level, "%s", utstring_body(&str));
    }

    utstring_done(&str);
    return true;
}

bool log_hexdumpiov(const char *src_dir,
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
                   ...)
{
    va_list ap;

    va_start(ap, fmt);
    bool ret = _log_hexdumpiov(src_dir, file_name, line, func_name, log_module, log_level, iov, iovcnt, len, show_ascii, 16, fmt, ap);
    va_end(ap);
    return ret;
}

bool log_hexdump(const char *src_dir,
                const char *file_name,
                int line,
                const char *func_name,
                c_log_module_t log_module,
                enum c_log_level log_level,
                const char *buf,
                ssize_t len,
                bool show_ascii,
                const char *fmt,
                ...)
{
    va_list ap;
    struct iovec iov = {
        .iov_base = (void *)buf,
        .iov_len  = len,
    };

    va_start(ap, fmt);
    bool ret = _log_hexdumpiov(src_dir, file_name, line, func_name, log_module, log_level, &iov, 1, len, show_ascii, 16, fmt, ap);
    va_end(ap);
    return ret;
}

__attribute__((destructor(1001))) static void __cleanup_c_log_var()
{
    if (log_private_data)
    {
        struct c_log_module *module = NULL, *tmp = NULL;

        HASH_ITER(hh, log_private_data->modules, module, tmp)
        {
            HASH_DEL(log_private_data->modules, module);
            free(module->name);
            free(module);
        }
        free(log_private_data);
        log_private_data = NULL;
    }

#ifdef __unix__
    closelog();
#endif // __unix__
}

void c_log_cleanup()
{
    __cleanup_c_log_var();
}