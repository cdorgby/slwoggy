/**
 * @file log_exit_handler.hpp
 * @brief Exit and signal handlers to flush logs on crash/exit
 * @author dorgby.net
 * @copyright Copyright (c) 2025 dorgby.net. Licensed under MIT License, see LICENSE for details.
 */
#pragma once

#include <csignal>
#include <cstdlib>
#include <cstring> // for strlen, memset
#include <atomic>
#include "log_dispatcher.hpp"

// Platform-specific includes and defines
#ifdef _WIN32
    #include <io.h>     // for _write
    #include <windows.h> // for Windows API
    #define WRITE_FUNC _write
    #define STDERR_FD  2
#else
    #include <unistd.h> // for write
    #define WRITE_FUNC write
    #define STDERR_FD  STDERR_FILENO
#endif

namespace slwoggy
{

/**
 * @brief Exit handler to ensure logs are flushed on program termination
 *
 * This class registers platform-specific handlers:
 * 
 * Windows:
 * - SetConsoleCtrlHandler for Ctrl+C, close, logoff, shutdown
 * - SetUnhandledExceptionFilter for access violations, stack overflow, etc.
 * - signal() handlers for C runtime errors (abort, etc.)
 * 
 * POSIX:
 * - sigaction() for signals (SIGSEGV, SIGABRT, SIGBUS, SIGILL, SIGFPE)
 * - sigaction() for termination (SIGTERM, SIGINT)
 * 
 * Both:
 * - Optional atexit() handler (may crash during static destruction)
 *
 * Signal/exception handlers are always safe because they run before static destruction.
 * The atexit handler is optional and can cause crashes if static objects
 * (like the buffer pool or dispatcher) are destroyed before the handler runs.
 */
class log_exit_handler
{
  private:
    static std::atomic<bool> handlers_installed_;
    static std::atomic<bool> in_handler_; // Prevent recursive calls

#ifdef _WIN32
    // Windows-specific: Console control handler for graceful shutdown
    static BOOL WINAPI console_ctrl_handler(DWORD ctrl_type)
    {
        const char* event_name = nullptr;
        switch (ctrl_type)
        {
        case CTRL_C_EVENT:
            event_name = "CTRL_C";
            break;
        case CTRL_BREAK_EVENT:
            event_name = "CTRL_BREAK";
            break;
        case CTRL_CLOSE_EVENT:
            event_name = "CONSOLE_CLOSE";
            break;
        case CTRL_LOGOFF_EVENT:
            event_name = "LOGOFF";
            break;
        case CTRL_SHUTDOWN_EVENT:
            event_name = "SHUTDOWN";
            break;
        default:
            return FALSE;
        }

        // Write notification
        const char msg[] = "\n[slwoggy] Console control event: ";
        WRITE_FUNC(STDERR_FD, msg, sizeof(msg) - 1);
        WRITE_FUNC(STDERR_FD, event_name, static_cast<unsigned int>(strlen(event_name)));
        const char msg2[] = ", flushing logs...\n";
        WRITE_FUNC(STDERR_FD, msg2, sizeof(msg2) - 1);

        emergency_flush();
        
        // Return TRUE to indicate we handled it
        // For CTRL_CLOSE_EVENT, CTRL_LOGOFF_EVENT, and CTRL_SHUTDOWN_EVENT,
        // Windows will terminate the process after this returns
        return TRUE;
    }

    // Windows-specific: Unhandled exception filter for crashes
    static LONG WINAPI unhandled_exception_filter(EXCEPTION_POINTERS* exception_info)
    {
        const char* exception_name = "UNKNOWN";
        
        if (exception_info && exception_info->ExceptionRecord)
        {
            switch (exception_info->ExceptionRecord->ExceptionCode)
            {
            case EXCEPTION_ACCESS_VIOLATION:
                exception_name = "ACCESS_VIOLATION";
                break;
            case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
                exception_name = "ARRAY_BOUNDS_EXCEEDED";
                break;
            case EXCEPTION_STACK_OVERFLOW:
                exception_name = "STACK_OVERFLOW";
                break;
            case EXCEPTION_INT_DIVIDE_BY_ZERO:
                exception_name = "DIVIDE_BY_ZERO";
                break;
            case EXCEPTION_INT_OVERFLOW:
                exception_name = "INT_OVERFLOW";
                break;
            case EXCEPTION_ILLEGAL_INSTRUCTION:
                exception_name = "ILLEGAL_INSTRUCTION";
                break;
            }
        }

        // Write crash notification
        const char msg[] = "\n[slwoggy] Unhandled exception: ";
        WRITE_FUNC(STDERR_FD, msg, sizeof(msg) - 1);
        WRITE_FUNC(STDERR_FD, exception_name, static_cast<unsigned int>(strlen(exception_name)));
        const char msg2[] = ", flushing logs...\n";
        WRITE_FUNC(STDERR_FD, msg2, sizeof(msg2) - 1);

        emergency_flush();

        // Continue search to allow default handler to create crash dump
        return EXCEPTION_CONTINUE_SEARCH;
    }
#endif

    // Flush function called by all handlers
    static void emergency_flush()
    {
        // Prevent recursive calls if we crash during flush
        bool expected = false;
        if (!in_handler_.compare_exchange_strong(expected, true))
        {
            return; // Already in handler
        }

        // Try to flush all pending logs
        // This is safe for signal handlers since buffers are pre-allocated
        // Only issue is with atexit handlers during static destruction
        try
        {
            log_line_dispatcher::instance().flush();
        }
        catch (...)
        {
            // Ignore exceptions during emergency flush
            // This can happen during atexit if statics are already destroyed
        }
    }

    // Normal exit handler
    static void exit_handler() { emergency_flush(); }

    // Signal handler for crashes
    static void signal_handler(int sig)
    {
        // Write a crash message if possible
        // Note: We can't use LOG() here as it might allocate
        const char *sig_name = "UNKNOWN";
        switch (sig)
        {
        case SIGSEGV: sig_name = "SIGSEGV"; break;
        case SIGABRT: sig_name = "SIGABRT"; break;
#ifndef _WIN32
        case SIGBUS: sig_name = "SIGBUS"; break;
#endif
        case SIGILL: sig_name = "SIGILL"; break;
        case SIGFPE: sig_name = "SIGFPE"; break;
        case SIGTERM: sig_name = "SIGTERM"; break;
        case SIGINT: sig_name = "SIGINT"; break;
        }

        // Try to write crash notice directly to stderr
        // This is signal-safe on POSIX, best effort on Windows
        const char msg[] = "\n[slwoggy] Caught signal ";
        WRITE_FUNC(STDERR_FD, msg, sizeof(msg) - 1);
        WRITE_FUNC(STDERR_FD, sig_name, static_cast<unsigned int>(strlen(sig_name)));
        const char msg2[] = ", flushing logs...\n";
        WRITE_FUNC(STDERR_FD, msg2, sizeof(msg2) - 1);

        emergency_flush();

        // Re-raise the signal with default handler to get core dump
        signal(sig, SIG_DFL);
        raise(sig);
    }

  public:
    /**
     * @brief Install exit and signal handlers
     *
     * Call this once at program startup to ensure logs are flushed on exit/crash.
     * Multiple calls are safe (handlers will only be installed once).
     *
     * Note: Normal exit handler is NOT installed by default to avoid static
     * destruction order issues. Only signal handlers are installed.
     *
     * @param install_atexit If true, also install normal exit handler (use with caution)
     * @return true if handlers were installed, false if already installed
     */
    static bool install(bool install_atexit = false)
    {
        bool expected = false;
        if (!handlers_installed_.compare_exchange_strong(expected, true))
        {
            return false; // Already installed
        }

        // Optionally register normal exit handler
        // WARNING: This can cause crashes during static destruction
        if (install_atexit) { std::atexit(exit_handler); }

#ifdef _WIN32
        // Windows: Use Windows-specific handlers for better coverage
        
        // Set console control handler for graceful shutdown events
        // This handles Ctrl+C, Ctrl+Break, console window close, logoff, and system shutdown
        if (!SetConsoleCtrlHandler(console_ctrl_handler, TRUE))
        {
            // If we're not a console app, this will fail, which is OK
            // We'll still have the other handlers
        }

        // Set unhandled exception filter for crashes
        // This catches access violations, stack overflows, etc.
        SetUnhandledExceptionFilter(unhandled_exception_filter);

        // Also set C runtime signal handlers as a fallback
        // These handle abort() and some other C runtime errors
        signal(SIGABRT, signal_handler);
        signal(SIGTERM, signal_handler);
        signal(SIGINT, signal_handler);
        signal(SIGILL, signal_handler);
        signal(SIGSEGV, signal_handler);
        signal(SIGFPE, signal_handler);
#else
        // POSIX: Use sigaction for more control
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = signal_handler;
        sigemptyset(&sa.sa_mask);

        // Add all signals to the mask to prevent other signals during handling
        sigaddset(&sa.sa_mask, SIGSEGV);
        sigaddset(&sa.sa_mask, SIGABRT);
        sigaddset(&sa.sa_mask, SIGBUS);
        sigaddset(&sa.sa_mask, SIGILL);
        sigaddset(&sa.sa_mask, SIGFPE);
        sigaddset(&sa.sa_mask, SIGTERM);
        sigaddset(&sa.sa_mask, SIGINT);

        // Use SA_RESETHAND to reset handler after first signal
        // This prevents infinite loops if we crash during handling
        sa.sa_flags = SA_RESETHAND;

        // Install handlers for crash signals
        sigaction(SIGSEGV, &sa, nullptr);
        sigaction(SIGABRT, &sa, nullptr);
        sigaction(SIGBUS, &sa, nullptr);
        sigaction(SIGILL, &sa, nullptr);
        sigaction(SIGFPE, &sa, nullptr);

        // For SIGTERM and SIGINT, we might want to handle gracefully
        sa.sa_flags = 0; // Don't reset these handlers
        sigaction(SIGTERM, &sa, nullptr);
        sigaction(SIGINT, &sa, nullptr);
#endif

        return true;
    }

    /**
     * @brief Check if handlers are installed
     */
    static bool is_installed() { return handlers_installed_.load(); }

    /**
     * @brief Manually trigger emergency flush
     *
     * Can be called before a known crash or exit point
     */
    static void flush() { emergency_flush(); }
};

// Static member definitions
inline std::atomic<bool> log_exit_handler::handlers_installed_{false};
inline std::atomic<bool> log_exit_handler::in_handler_{false};

/**
 * @brief RAII helper to automatically install exit handlers
 *
 * Create a static instance of this class to automatically install handlers:
 * @code
 * static log_exit_handler_installer installer;
 * @endcode
 */
struct log_exit_handler_installer
{
    log_exit_handler_installer() { log_exit_handler::install(); }
};

} // namespace slwoggy