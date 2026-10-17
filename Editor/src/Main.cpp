#include <Axle/Application.hpp>
#include <Axle/Log.hpp>
#include <Axle/ImGuiLayer.hpp>
#include "EditorLayer.hpp"
#include <exception>
#include <csignal>
#include <cstdio>

#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")

static void WriteCrashInfo(const char* title, DWORD code, void* addr, PEXCEPTION_POINTERS ep) {
    FILE* f = fopen("D:\\WorkSpace\\AxleEngine\\crash.log", "w");
    if (!f) f = fopen("crash.log", "w");
    if (f) {
        fprintf(f, "=== %s ===\n", title);
        fprintf(f, "Exception Code: 0x%08lX\n", (unsigned long)code);
        fprintf(f, "Faulting Address: 0x%p\n", addr);
        if (ep && ep->ExceptionRecord && code == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters >= 2) {
            ULONG_PTR accessType = ep->ExceptionRecord->ExceptionInformation[0];
            ULONG_PTR targetAddr = ep->ExceptionRecord->ExceptionInformation[1];
            fprintf(f, "Access Violation: Attempted to %s memory at 0x%p\n",
                accessType == 0 ? "READ" : (accessType == 1 ? "WRITE" : "EXECUTE"),
                (void*)targetAddr);
        }

        void* stack[64];
        unsigned short frames = CaptureStackBackTrace(0, 64, stack, NULL);
        fprintf(f, "\nCallstack Backtrace (%u frames):\n", frames);
        for (unsigned short i = 0; i < frames; ++i) {
            fprintf(f, "  [%02u] 0x%p\n", i, stack[i]);
        }
        fflush(f);
        fclose(f);
    }
}

static LONG WINAPI VectoredCrashHandler(PEXCEPTION_POINTERS pExceptionPointers) {
    if (!pExceptionPointers || !pExceptionPointers->ExceptionRecord) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    DWORD code = pExceptionPointers->ExceptionRecord->ExceptionCode;
    // Intercept fatal hardware exceptions and access violations
    if (code == EXCEPTION_ACCESS_VIOLATION ||
        code == EXCEPTION_ILLEGAL_INSTRUCTION ||
        code == EXCEPTION_PRIV_INSTRUCTION ||
        code == EXCEPTION_STACK_OVERFLOW ||
        code == EXCEPTION_DATATYPE_MISALIGNMENT ||
        code == EXCEPTION_ARRAY_BOUNDS_EXCEEDED ||
        code == EXCEPTION_FLT_DIVIDE_BY_ZERO ||
        code == EXCEPTION_INT_DIVIDE_BY_ZERO) {

        WriteCrashInfo("HARDWARE/SEH CRASH DETECTED VIA VEH", code, pExceptionPointers->ExceptionRecord->ExceptionAddress, pExceptionPointers);
        AXLE_CRITICAL("CRASH DETECTED! Code: 0x{0:X}, Address: 0x{1:X}", code, (uint64_t)pExceptionPointers->ExceptionRecord->ExceptionAddress);
        fflush(stderr);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

static void TerminateHandler() {
    WriteCrashInfo("CRT TERMINATE CALLED (unhandled exception or abort)", 0xE0000001, nullptr, nullptr);
    try {
        auto ex = std::current_exception();
        if (ex) std::rethrow_exception(ex);
    } catch (const std::exception& e) {
        FILE* f = fopen("D:\\WorkSpace\\AxleEngine\\crash.log", "a");
        if (f) {
            fprintf(f, "Exception what(): %s\n", e.what());
            fclose(f);
        }
    } catch (...) {
        FILE* f = fopen("D:\\WorkSpace\\AxleEngine\\crash.log", "a");
        if (f) {
            fprintf(f, "Exception what(): Unknown exception\n");
            fclose(f);
        }
    }
}

static void AbortHandler(int sig) {
    WriteCrashInfo("CRT SIGABRT CALLED", static_cast<DWORD>(sig), nullptr, nullptr);
}
#endif

int main() {
#ifdef _WIN32
    AddVectoredExceptionHandler(1, VectoredCrashHandler);
    SetUnhandledExceptionFilter(VectoredCrashHandler);
    std::set_terminate(TerminateHandler);
    std::signal(SIGABRT, AbortHandler);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
    try {
        Axle::ApplicationSpecification spec{
            .Name = "Axle Editor",
            .Width = 1280,
            .Height = 720,
            .VSync = true
        };

        auto app = std::make_unique<Axle::Application>(spec);
        app->SetUIContext(std::make_unique<Axle::ImGuiLayer>());
        app->PushLayer(std::make_shared<EditorLayer>());
        app->Run();
    } catch (const std::exception& e) {
        AXLE_CRITICAL("Unhandled C++ Exception: {0}", e.what());
        fprintf(stderr, "[FATAL] Unhandled C++ Exception: %s\n", e.what());
        FILE* f = fopen("D:\\WorkSpace\\AxleEngine\\crash.log", "w");
        if (f) {
            fprintf(f, "Unhandled C++ Exception: %s\n", e.what());
            fclose(f);
        }
        fflush(stderr);
        return 1;
    } catch (...) {
        AXLE_CRITICAL("Unhandled unknown exception caught in main!");
        fprintf(stderr, "[FATAL] Unhandled unknown exception caught in main!\n");
        FILE* f = fopen("D:\\WorkSpace\\AxleEngine\\crash.log", "w");
        if (f) {
            fprintf(f, "Unhandled unknown exception caught in main!\n");
            fclose(f);
        }
        fflush(stderr);
        return 1;
    }

    return 0;
}