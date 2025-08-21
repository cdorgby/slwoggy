#include <slwoggy/log.hpp>
#include <slwoggy/log_sinks.hpp>

using namespace slwoggy;

int main()
{
    // Add a simple stdout sink
    log_line_dispatcher::instance().add_sink(make_stdout_sink());
    
    // Test basic logging
    LOG(info) << "Integration test successful!" << endl;
    LOG(debug) << "Debug message" << endl;
    LOG(warning) << "Warning message" << endl;
    
    // Test structured logging
    LOG(info) << "User login" << KV("user_id", 12345) << KV("ip", "192.168.1.1") << endl;
    
    // Ensure logs are flushed
    log_line_dispatcher::instance().flush();
    
    return 0;
}