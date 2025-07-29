
/* -*-*- COPYRIGHT-GOES-HERE -*-*-* */
#define CNALOG_SKIP_CONSTRUCTORS
#include <log.h>
#include <stdint.h>
#include <uthash.h>


/**
 * @addtogroup cnalog Logging
 * @{
 */

/**
 * @brief Width of the padding for filenames in logging output
 */
#define FILENAME_PAD    20

/**
 * @brief Information and configuraiton settings for log modules
 */
struct c_log_module
{
    char *name;                          /**< Name of the module */
    enum c_log_level min_log_level;    /**< Module's minimal log level */
    C_LOG_LEVEL_ALWAYS_ARRAY(enabled); /**< Array of enabled log levels */

    UT_hash_handle hh; /**< Hash table pointer */
};

/**
 * @brief This is the state information for the entire logging subsysutems
 * 
 */
struct log_privates 
{
    enum c_log_level default_log_level;  /**< Default log level to assign to modules */
    struct c_log_module *modules;        /**< A hash table of modules */
    uint32_t longest_mod_name;             /**< Length of the longer module name */
    uint32_t longest_log_level_name;       /**< Length of the longest log level name */
    FILE *log_to_stdio;                    /**< Enable logging to stdout */
    enum c_log_level log_to_stdio_level; /**< Log level for stdout */
    bool log_to_syslog;                    /**< Enable logging to syslog */
    bool log_level_from_env;               /**< @p default_log_level came from the environment and shouldn't be changed by init */
    struct cna_config_s *cfg_ctx;          /**< Pointer to @ref cna_config_t */
    C_LOG_LEVEL_ALWAYS_ARRAY(enabled);   /**< Array of default enabled log levels */

    c_log_cb_t cb;
    void *cb_data;

    bool log_level;       /**< Show log level */
    bool log_module;      /**< Show module name */
    bool log_file;        /**< Show file name */
    bool log_line;        /**< Show line number */
    bool log_func;        /**< Show function name */
    bool short_file_name; /**< Show short path to file name, (ie. only show the filename itself) */
    bool pad;             /**< pad output when showing module and (short) filenames */

};

/**
 * @brief Logging subsystem itnernal data handle
 */
typedef struct log_privates *c_log_handle_t;
/**
 * @brief Access the logging internal data handle
 * 
 * @return Return a struct @ref log_privates
 */
PUB_API struct log_privates *c_log_get_handle();

/** @} */