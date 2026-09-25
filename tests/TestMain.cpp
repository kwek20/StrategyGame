#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>

#if defined(_WIN32)
#include <Windows.h>
#include <crtdbg.h>
#include <stdlib.h>
#endif

int strategyTestMain();

namespace {

[[noreturn]] void exitTestProcess(const char* message) noexcept {
    std::fputs(message, stderr);
    std::fputc('\n', stderr);
    std::fflush(stderr);
    std::_Exit(EXIT_FAILURE);
}

void handleAbortSignal(int) {
    exitTestProcess("Test process aborted");
}

void handleTermination() noexcept {
    if (const std::exception_ptr failure = std::current_exception()) {
        try {
            std::rethrow_exception(failure);
        } catch (const std::exception& error) {
            std::fprintf(stderr, "Test terminated with an uncaught exception: %s\n",
                         error.what());
        } catch (...) {
            std::fputs("Test terminated with an unknown uncaught exception\n", stderr);
        }
        std::fflush(stderr);
    } else {
        std::fputs("Test called std::terminate without an active exception\n", stderr);
        std::fflush(stderr);
    }
    std::_Exit(EXIT_FAILURE);
}

#if defined(_WIN32)
void handleInvalidParameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned int,
                            std::uintptr_t) {
    exitTestProcess("Test encountered an invalid C runtime parameter");
}

void handlePureCall() {
    exitTestProcess("Test attempted a pure virtual function call");
}
#endif

void installTestFailureHandlers() {
    std::set_terminate(handleTermination);
    std::signal(SIGABRT, handleAbortSignal);
#if defined(_WIN32)
    SetErrorMode(GetErrorMode() | SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _set_invalid_parameter_handler(handleInvalidParameter);
    _set_purecall_handler(handlePureCall);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
#endif
}

} // namespace

int main() noexcept {
    installTestFailureHandlers();
    try {
        return strategyTestMain();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Test failed with an uncaught exception: %s\n", error.what());
    } catch (...) {
        std::fputs("Test failed with an unknown uncaught exception\n", stderr);
    }
    return EXIT_FAILURE;
}
