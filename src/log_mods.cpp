#include "slwoggy.hpp"

using namespace slwoggy;

int main(int argc, char *argv[])
{
    // Test the default LOG macro
    LOG(info) << "Starting log_mods test";

    // Test LOG_MODULE_BASE with text format
    LOG_MOD_TEXT(info, "network").add("user_id", 321) << "Testing network module with text format";

    // Test LOG_MODULE_BASE with structured format
    LOG_MOD_STRUCT(debug, "database").add("user_id", 123).add("query_time_ms", 45) << "Query executed "
                                                                                      "successfully";

    // Test with compile-time module name (runtime variables not supported with static approach)
    LOG_MOD_TEXT(warn, "auth") << "Authentication warning from auth module";

    return 0;
}