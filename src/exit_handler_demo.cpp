#include "slwoggy.hpp"
#include <iostream>
#include <thread>

using namespace slwoggy;

void test_normal_exit() {
    LOG(info) << "Testing normal exit - this should be flushed" << endl;
    LOG(warn) << "Even without explicit flush call" << endl;
}

void test_crash() {
    LOG(error) << "About to crash - this should still be flushed" << endl;
    
    // Cause a segfault
    int* p = nullptr;
    *p = 42;
}

void test_abort() {
    LOG(error) << "About to abort - this should still be flushed" << endl;
    std::abort();
}

void test_divide_by_zero() {
    LOG(error) << "About to divide by zero - this should still be flushed" << endl;
    volatile int zero = 0;
    volatile int result = 42 / zero;
    (void)result;
}

int main(int argc, char* argv[]) {
    // Install signal handlers (always safe)
    // Pass true as parameter to also install atexit handler (may crash during static destruction)
    bool install_atexit = false;  // Set to true if you want automatic flush on normal exit
    if (log_exit_handler::install(install_atexit)) {
        std::cout << "Signal handlers installed successfully\n";
        if (install_atexit) {
            std::cout << "Note: atexit handler also installed (may crash during static destruction)\n";
        }
    }
    
    LOG(info) << "Exit handler test starting" << endl;
    
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <test_type>\n";
        std::cout << "  normal - Normal exit\n";
        std::cout << "  crash  - Segmentation fault\n";
        std::cout << "  abort  - Call abort()\n";
        std::cout << "  divzero - Divide by zero\n";
        return 1;
    }
    
    std::string test_type = argv[1];
    
    if (test_type == "normal") {
        test_normal_exit();
        LOG(info) << "Exiting normally" << endl;
        // Explicitly flush since we don't install atexit handler by default
        log_line_dispatcher::instance().flush();
        return 0;
    } else if (test_type == "crash") {
        test_crash();
        // Should never reach here
        LOG(error) << "Segfault didn't crash? Something is wrong!" << endl;
        return 2;
    } else if (test_type == "abort") {
        test_abort();
        // Should never reach here
        LOG(error) << "Abort didn't terminate? Something is wrong!" << endl;
        return 2;
    } else if (test_type == "divzero") {
        test_divide_by_zero();
        // Might reach here on some platforms where divide by zero doesn't signal
        LOG(warn) << "Divide by zero didn't crash - not all platforms signal on div/0" << endl;
        log_line_dispatcher::instance().flush();
        return 0;
    } else {
        LOG(error) << "Unknown test type: " << test_type << endl;
        return 1;
    }
    
    // Should never reach here
    return 0;
}