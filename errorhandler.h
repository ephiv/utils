#ifndef ERROR_HANDLER_H
#define ERROR_HANDLER_H

#ifdef __cplusplus
extern "C" {
#endif

/* Windows-specific includes */
#ifdef _WIN32
    #include <windows.h>
    #include <dbghelp.h>
    #include <excpt.h>
    #include <signal.h>
    #pragma comment(lib, "dbghelp.lib")
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <assert.h>

/* Configuration macros */
#ifndef EH_MAX_ERROR_MSG_SIZE
    #define EH_MAX_ERROR_MSG_SIZE 1024
#endif

#ifndef EH_MAX_STACK_FRAMES
    #define EH_MAX_STACK_FRAMES 64
#endif

#ifndef EH_LOG_FILE_PATH
    #define EH_LOG_FILE_PATH "error_log.txt"
#endif

/* Error severity levels */
typedef enum {
    EH_SEVERITY_INFO = 0,
    EH_SEVERITY_WARNING,
    EH_SEVERITY_ERROR,
    EH_SEVERITY_CRITICAL,
    EH_SEVERITY_PANIC
} eh_severity_t;

/* Error codes */
typedef enum {
    EH_SUCCESS = 0,
    EH_ERROR_GENERIC = -1,
    EH_ERROR_MEMORY = -2,
    EH_ERROR_FILE_IO = -3,
    EH_ERROR_INVALID_PARAM = -4,
    EH_ERROR_NETWORK = -5,
    EH_ERROR_TIMEOUT = -6,
    EH_ERROR_ACCESS_DENIED = -7,
    EH_ERROR_NOT_FOUND = -8,
    EH_ERROR_ALREADY_EXISTS = -9,
    EH_ERROR_CORRUPTED_DATA = -10,
    EH_ERROR_SYSTEM_CALL = -999
} eh_error_code_t;

/* Error context structure */
typedef struct {
    eh_error_code_t code;
    eh_severity_t severity;
    char message[EH_MAX_ERROR_MSG_SIZE];
    char function[128];
    char file[256];
    int line;
    DWORD win32_error;
    time_t timestamp;
    int call_depth;
} eh_error_context_t;

/* Configuration structure */
typedef struct {
    int enable_logging;
    int enable_console_output;
    int enable_debug_output;
    int enable_stack_trace;
    int enable_crash_dumps;
    int abort_on_panic;
    char log_file_path[512];
    FILE* log_file_handle;
} eh_config_t;

/* Global configuration */
static eh_config_t g_eh_config = {
    .enable_logging = 1,
    .enable_console_output = 1,
    .enable_debug_output = 1,
    .enable_stack_trace = 1,
    .enable_crash_dumps = 1,
    .abort_on_panic = 1,
    .log_file_path = EH_LOG_FILE_PATH,
    .log_file_handle = NULL
};

/* Function pointer for custom error handlers */
typedef void (*eh_custom_handler_t)(const eh_error_context_t* context);
static eh_custom_handler_t g_custom_handler = NULL;

/* Internal state tracking */
static int g_eh_initialized = 0;
static int g_error_count = 0;
static int g_warning_count = 0;
static eh_error_context_t g_last_error = {0};

/* Forward declarations */
static void eh_internal_log(const eh_error_context_t* context);
static void eh_print_stack_trace(void);
static const char* eh_severity_to_string(eh_severity_t severity);
static const char* eh_error_code_to_string(eh_error_code_t code);
static void eh_write_crash_dump(EXCEPTION_POINTERS* exception_pointers);
static LONG WINAPI eh_unhandled_exception_filter(EXCEPTION_POINTERS* exception_pointers);

/* Initialization and cleanup */
static inline int eh_init(void) {
    if (g_eh_initialized) return EH_SUCCESS;
    
#ifdef _WIN32
    /* Initialize symbol handler for stack traces */
    if (g_eh_config.enable_stack_trace) {
        SymInitialize(GetCurrentProcess(), NULL, TRUE);
        SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
    }
    
    /* Set up unhandled exception filter */
    SetUnhandledExceptionFilter(eh_unhandled_exception_filter);
    
    /* Set up console control handler for graceful shutdown */
    SetConsoleCtrlHandler(NULL, FALSE);
#endif
    
    /* Open log file if logging is enabled */
    if (g_eh_config.enable_logging && !g_eh_config.log_file_handle) {
        g_eh_config.log_file_handle = fopen(g_eh_config.log_file_path, "a");
        if (g_eh_config.log_file_handle) {
            fprintf(g_eh_config.log_file_handle, "\n=== Error Handler Initialized [%s] ===\n", 
                   __TIMESTAMP__);
            fflush(g_eh_config.log_file_handle);
        }
    }
    
    g_eh_initialized = 1;
    return EH_SUCCESS;
}

static inline void eh_cleanup(void) {
    if (!g_eh_initialized) return;
    
    if (g_eh_config.log_file_handle) {
        fprintf(g_eh_config.log_file_handle, "=== Error Handler Shutdown ===\n");
        fprintf(g_eh_config.log_file_handle, "Total Errors: %d, Warnings: %d\n", 
               g_error_count, g_warning_count);
        fclose(g_eh_config.log_file_handle);
        g_eh_config.log_file_handle = NULL;
    }
    
#ifdef _WIN32
    if (g_eh_config.enable_stack_trace) {
        SymCleanup(GetCurrentProcess());
    }
#endif
    
    g_eh_initialized = 0;
}

/* Configuration functions */
static inline void eh_set_config(const eh_config_t* config) {
    if (config) {
        g_eh_config = *config;
        if (g_eh_initialized && g_eh_config.enable_logging && !g_eh_config.log_file_handle) {
            g_eh_config.log_file_handle = fopen(g_eh_config.log_file_path, "a");
        }
    }
}

static inline void eh_set_custom_handler(eh_custom_handler_t handler) {
    g_custom_handler = handler;
}

/* Core error handling function */
static inline void eh_handle_error_internal(eh_error_code_t code, eh_severity_t severity,
                                          const char* function, const char* file, int line,
                                          const char* format, va_list args) {
    if (!g_eh_initialized) eh_init();
    
    eh_error_context_t context = {0};
    context.code = code;
    context.severity = severity;
    context.line = line;
    context.timestamp = time(NULL);
    context.call_depth = 0; /* Could be enhanced with call stack analysis */
    
#ifdef _WIN32
    context.win32_error = GetLastError();
#endif
    
    /* Copy function and file names safely */
    strncpy(context.function, function ? function : "unknown", sizeof(context.function) - 1);
    strncpy(context.file, file ? file : "unknown", sizeof(context.file) - 1);
    
    /* Format the error message */
    vsnprintf(context.message, sizeof(context.message), format, args);
    
    /* Update statistics */
    if (severity >= EH_SEVERITY_ERROR) g_error_count++;
    else if (severity == EH_SEVERITY_WARNING) g_warning_count++;
    
    /* Store as last error */
    g_last_error = context;
    
    /* Call custom handler if set */
    if (g_custom_handler) {
        g_custom_handler(&context);
    }
    
    /* Internal logging */
    eh_internal_log(&context);
    
    /* Handle panic condition */
    if (severity == EH_SEVERITY_PANIC) {
        if (g_eh_config.enable_stack_trace) {
            eh_print_stack_trace();
        }
        
        if (g_eh_config.abort_on_panic) {
            if (g_eh_config.enable_console_output) {
                fprintf(stderr, "\n*** PANIC: Application will terminate ***\n");
            }
            eh_cleanup();
            abort();
        }
    }
}

/* Variadic wrapper functions */
static inline void eh_handle_error(eh_error_code_t code, eh_severity_t severity,
                                 const char* function, const char* file, int line,
                                 const char* format, ...) {
    va_list args;
    va_start(args, format);
    eh_handle_error_internal(code, severity, function, file, line, format, args);
    va_end(args);
}

/* Convenience macros */
#define EH_INFO(code, ...) \
    eh_handle_error(code, EH_SEVERITY_INFO, __FUNCTION__, __FILE__, __LINE__, __VA_ARGS__)

#define EH_WARN(code, ...) \
    eh_handle_error(code, EH_SEVERITY_WARNING, __FUNCTION__, __FILE__, __LINE__, __VA_ARGS__)

#define EH_ERROR(code, ...) \
    eh_handle_error(code, EH_SEVERITY_ERROR, __FUNCTION__, __FILE__, __LINE__, __VA_ARGS__)

#define EH_CRITICAL(code, ...) \
    eh_handle_error(code, EH_SEVERITY_CRITICAL, __FUNCTION__, __FILE__, __LINE__, __VA_ARGS__)

#define EH_PANIC(...) \
    eh_handle_error(EH_ERROR_GENERIC, EH_SEVERITY_PANIC, __FUNCTION__, __FILE__, __LINE__, __VA_ARGS__)

/* Assertion macros with better error reporting */
#define EH_ASSERT(condition, ...) \
    do { \
        if (!(condition)) { \
            EH_PANIC("Assertion failed: " #condition ". " __VA_ARGS__); \
        } \
    } while(0)

#define EH_ASSERT_NOT_NULL(ptr, ...) \
    EH_ASSERT((ptr) != NULL, "Null pointer: " #ptr ". " __VA_ARGS__)

/* Memory allocation with error handling */
#define EH_MALLOC(size) eh_safe_malloc(size, __FUNCTION__, __FILE__, __LINE__)
#define EH_CALLOC(count, size) eh_safe_calloc(count, size, __FUNCTION__, __FILE__, __LINE__)
#define EH_REALLOC(ptr, size) eh_safe_realloc(ptr, size, __FUNCTION__, __FILE__, __LINE__)
#define EH_FREE(ptr) eh_safe_free((void**)&(ptr))

static inline void* eh_safe_malloc(size_t size, const char* function, const char* file, int line) {
    void* ptr = malloc(size);
    if (!ptr && size > 0) {
        eh_handle_error(EH_ERROR_MEMORY, EH_SEVERITY_CRITICAL, function, file, line,
                       "Memory allocation failed: %zu bytes", size);
    }
    return ptr;
}

static inline void* eh_safe_calloc(size_t count, size_t size, const char* function, const char* file, int line) {
    void* ptr = calloc(count, size);
    if (!ptr && count > 0 && size > 0) {
        eh_handle_error(EH_ERROR_MEMORY, EH_SEVERITY_CRITICAL, function, file, line,
                       "Memory allocation failed: %zu x %zu bytes", count, size);
    }
    return ptr;
}

static inline void* eh_safe_realloc(void* ptr, size_t size, const char* function, const char* file, int line) {
    void* new_ptr = realloc(ptr, size);
    if (!new_ptr && size > 0) {
        eh_handle_error(EH_ERROR_MEMORY, EH_SEVERITY_CRITICAL, function, file, line,
                       "Memory reallocation failed: %zu bytes", size);
        return ptr; /* Return original pointer to avoid memory leak */
    }
    return new_ptr;
}

static inline void eh_safe_free(void** ptr) {
    if (ptr && *ptr) {
        free(*ptr);
        *ptr = NULL;
    }
}

/* File operations with error handling */
static inline FILE* eh_safe_fopen(const char* filename, const char* mode, 
                                 const char* function, const char* file, int line) {
    FILE* fp = fopen(filename, mode);
    if (!fp) {
        eh_handle_error(EH_ERROR_FILE_IO, EH_SEVERITY_ERROR, function, file, line,
                       "Failed to open file '%s' with mode '%s': %s", 
                       filename, mode, strerror(errno));
    }
    return fp;
}

#define EH_FOPEN(filename, mode) eh_safe_fopen(filename, mode, __FUNCTION__, __FILE__, __LINE__)

/* Windows-specific error handling */
#ifdef _WIN32
static inline void eh_handle_win32_error(const char* function, const char* file, int line,
                                        const char* operation) {
    DWORD error = GetLastError();
    if (error != ERROR_SUCCESS) {
        char* message = NULL;
        FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
                      NULL, error, 0, (LPSTR)&message, 0, NULL);
        
        eh_handle_error(EH_ERROR_SYSTEM_CALL, EH_SEVERITY_ERROR, function, file, line,
                       "Win32 error in %s: %s (Code: %lu)", 
                       operation, message ? message : "Unknown error", error);
        
        if (message) LocalFree(message);
    }
}

#define EH_WIN32_CHECK(operation) \
    eh_handle_win32_error(__FUNCTION__, __FILE__, __LINE__, #operation)
#endif

/* Utility functions */
static inline const eh_error_context_t* eh_get_last_error(void) {
    return &g_last_error;
}

static inline int eh_get_error_count(void) {
    return g_error_count;
}

static inline int eh_get_warning_count(void) {
    return g_warning_count;
}

/* Internal implementation functions */
static void eh_internal_log(const eh_error_context_t* context) {
    char timestamp_str[64];
    struct tm* tm_info = localtime(&context->timestamp);
    strftime(timestamp_str, sizeof(timestamp_str), "%Y-%m-%d %H:%M:%S", tm_info);
    
    const char* severity_str = eh_severity_to_string(context->severity);
    const char* code_str = eh_error_code_to_string(context->code);
    
    char log_line[2048];
    snprintf(log_line, sizeof(log_line),
            "[%s] %s (%s) in %s() at %s:%d - %s",
            timestamp_str, severity_str, code_str,
            context->function, context->file, context->line, context->message);
    
#ifdef _WIN32
    if (context->win32_error != 0) {
        char win32_msg[512];
        snprintf(win32_msg, sizeof(win32_msg), " [Win32: %lu]", context->win32_error);
        strncat(log_line, win32_msg, sizeof(log_line) - strlen(log_line) - 1);
    }
#endif
    
    /* Console output */
    if (g_eh_config.enable_console_output) {
        FILE* output = (context->severity >= EH_SEVERITY_ERROR) ? stderr : stdout;
        fprintf(output, "%s\n", log_line);
        fflush(output);
    }
    
    /* Debug output (Windows) */
#ifdef _WIN32
    if (g_eh_config.enable_debug_output && IsDebuggerPresent()) {
        OutputDebugStringA(log_line);
        OutputDebugStringA("\n");
    }
#endif
    
    /* File logging */
    if (g_eh_config.enable_logging && g_eh_config.log_file_handle) {
        fprintf(g_eh_config.log_file_handle, "%s\n", log_line);
        fflush(g_eh_config.log_file_handle);
    }
}

static void eh_print_stack_trace(void) {
#ifdef _WIN32
    if (!g_eh_config.enable_stack_trace) return;
    
    HANDLE process = GetCurrentProcess();
    HANDLE thread = GetCurrentThread();
    
    CONTEXT context;
    memset(&context, 0, sizeof(CONTEXT));
    context.ContextFlags = CONTEXT_FULL;
    RtlCaptureContext(&context);
    
    STACKFRAME64 stack_frame;
    memset(&stack_frame, 0, sizeof(STACKFRAME64));
    
#ifdef _M_IX86
    DWORD machine_type = IMAGE_FILE_MACHINE_I386;
    stack_frame.AddrPC.Offset = context.Eip;
    stack_frame.AddrPC.Mode = AddrModeFlat;
    stack_frame.AddrFrame.Offset = context.Ebp;
    stack_frame.AddrFrame.Mode = AddrModeFlat;
    stack_frame.AddrStack.Offset = context.Esp;
    stack_frame.AddrStack.Mode = AddrModeFlat;
#elif _M_X64
    DWORD machine_type = IMAGE_FILE_MACHINE_AMD64;
    stack_frame.AddrPC.Offset = context.Rip;
    stack_frame.AddrPC.Mode = AddrModeFlat;
    stack_frame.AddrFrame.Offset = context.Rsp;
    stack_frame.AddrFrame.Mode = AddrModeFlat;
    stack_frame.AddrStack.Offset = context.Rsp;
    stack_frame.AddrStack.Mode = AddrModeFlat;
#endif
    
    if (g_eh_config.enable_console_output) {
        fprintf(stderr, "\n=== Stack Trace ===\n");
    }
    
    for (int frame_num = 0; frame_num < EH_MAX_STACK_FRAMES; frame_num++) {
        if (!StackWalk64(machine_type, process, thread, &stack_frame, 
                        &context, NULL, SymFunctionTableAccess64, 
                        SymGetModuleBase64, NULL)) {
            break;
        }
        
        if (stack_frame.AddrPC.Offset == 0) break;
        
        char symbol_buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR)];
        PSYMBOL_INFO symbol = (PSYMBOL_INFO)symbol_buffer;
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = MAX_SYM_NAME;
        
        DWORD64 displacement = 0;
        if (SymFromAddr(process, stack_frame.AddrPC.Offset, &displacement, symbol)) {
            IMAGEHLP_LINE64 line;
            DWORD line_displacement = 0;
            line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
            
            if (SymGetLineFromAddr64(process, stack_frame.AddrPC.Offset, 
                                   &line_displacement, &line)) {
                if (g_eh_config.enable_console_output) {
                    fprintf(stderr, "  #%d: %s() at %s:%lu (0x%016llX)\n",
                           frame_num, symbol->Name, line.FileName, 
                           line.LineNumber, stack_frame.AddrPC.Offset);
                }
            } else {
                if (g_eh_config.enable_console_output) {
                    fprintf(stderr, "  #%d: %s() (0x%016llX)\n",
                           frame_num, symbol->Name, stack_frame.AddrPC.Offset);
                }
            }
        } else {
            if (g_eh_config.enable_console_output) {
                fprintf(stderr, "  #%d: <unknown> (0x%016llX)\n",
                       frame_num, stack_frame.AddrPC.Offset);
            }
        }
    }
    
    if (g_eh_config.enable_console_output) {
        fprintf(stderr, "===================\n\n");
    }
#endif
}

static const char* eh_severity_to_string(eh_severity_t severity) {
    switch (severity) {
        case EH_SEVERITY_INFO: return "INFO";
        case EH_SEVERITY_WARNING: return "WARN";
        case EH_SEVERITY_ERROR: return "ERROR";
        case EH_SEVERITY_CRITICAL: return "CRITICAL";
        case EH_SEVERITY_PANIC: return "PANIC";
        default: return "UNKNOWN";
    }
}

static const char* eh_error_code_to_string(eh_error_code_t code) {
    switch (code) {
        case EH_SUCCESS: return "SUCCESS";
        case EH_ERROR_GENERIC: return "GENERIC";
        case EH_ERROR_MEMORY: return "MEMORY";
        case EH_ERROR_FILE_IO: return "FILE_IO";
        case EH_ERROR_INVALID_PARAM: return "INVALID_PARAM";
        case EH_ERROR_NETWORK: return "NETWORK";
        case EH_ERROR_TIMEOUT: return "TIMEOUT";
        case EH_ERROR_ACCESS_DENIED: return "ACCESS_DENIED";
        case EH_ERROR_NOT_FOUND: return "NOT_FOUND";
        case EH_ERROR_ALREADY_EXISTS: return "ALREADY_EXISTS";
        case EH_ERROR_CORRUPTED_DATA: return "CORRUPTED_DATA";
        case EH_ERROR_SYSTEM_CALL: return "SYSTEM_CALL";
        default: return "UNKNOWN";
    }
}

/* Windows crash dump generation */
#ifdef _WIN32
static void eh_write_crash_dump(EXCEPTION_POINTERS* exception_pointers) {
    if (!g_eh_config.enable_crash_dumps) return;
    
    char dump_filename[512];
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    strftime(dump_filename, sizeof(dump_filename), 
            "crash_dump_%Y%m%d_%H%M%S.dmp", tm_info);
    
    HANDLE dump_file = CreateFileA(dump_filename, GENERIC_WRITE, 0, NULL,
                                  CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    
    if (dump_file != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION dump_info;
        dump_info.ThreadId = GetCurrentThreadId();
        dump_info.ExceptionPointers = exception_pointers;
        dump_info.ClientPointers = FALSE;
        
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(),
                         dump_file, MiniDumpNormal, &dump_info, NULL, NULL);
        
        CloseHandle(dump_file);
        
        if (g_eh_config.enable_console_output) {
            fprintf(stderr, "Crash dump written: %s\n", dump_filename);
        }
    }
}

static LONG WINAPI eh_unhandled_exception_filter(EXCEPTION_POINTERS* exception_pointers) {
    EH_PANIC("Unhandled exception: 0x%08X at address 0x%016llX",
            exception_pointers->ExceptionRecord->ExceptionCode,
            (unsigned long long)exception_pointers->ExceptionRecord->ExceptionAddress);
    
    eh_write_crash_dump(exception_pointers);
    
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

#ifdef __cplusplus
}
#endif

#endif /* ERROR_HANDLER_H */