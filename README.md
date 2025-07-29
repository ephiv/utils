# utils

use in stead of beating around the bush and hard-code utility systems in your program.

## toc

- [errorhandler.h](#errorhandlerh) - error handling and logging
- [fastparse.h](#fastparseh) - zero-copy string parsing with simd
- [timer.h](#timerh) - timing and benchmarking

---

## errorHhndler.h

error handling system for windows (only thing i use)

### features

- **severity**: info, warning, error, critical, panic
- **autolog**: file and console output with timestamps
- **stack traces**: windows-specific stack unwinding with symbols
- **memory-safe**: safe allocation wrappers with error checking
- **crash dumps**: automatic minidump generation on crashes
- **custom handlers**: your own error handling logic

### quick start

```c
#include "errorhandler.h"

int main() {
    // init eh
    eh_init();
    
    // log severities
    EH_INFO(EH_SUCCESS, "Application started");
    EH_WARN(EH_ERROR_TIMEOUT, "Connection slow: %d ms", 1500);
    EH_ERROR(EH_ERROR_FILE_IO, "Failed to open %s", filename);
    
    // safe malloc
    char* buffer = EH_MALLOC(1024);
    if (!buffer) {
        // autologged
        return -1;
    }
    
    // safe file actions
    FILE* fp = EH_FOPEN("data.txt", "r");
    if (!fp) {
        // autologged
        EH_FREE(buffer);
        return -1;
    }
    
    // assertions with context
    EH_ASSERT_NOT_NULL(buffer, "Buffer allocation failed");
    
    EH_FREE(buffer);
    fclose(fp);
    eh_cleanup();
    return 0;
}
```

### config

```c
// custom error handler behavior
eh_config_t config = {
    .enable_logging = 1,
    .enable_console_output = 1,
    .enable_stack_trace = 1,
    .enable_crash_dumps = 1,
    .abort_on_panic = 1,
    .log_file_path = "my_app.log"
};
eh_set_config(&config);

// set
void my_error_handler(const eh_error_context_t* context) {
    // Send to logging service, email alert, etc.
    printf("Custom: [%s] %s\n", 
           eh_severity_to_string(context->severity), 
           context->message);
}
eh_set_custom_handler(my_error_handler);
```

### Error Codes

| Code | Description |
|------|-------------|
| `EH_SUCCESS` | op successful |
| `EH_ERROR_MEMORY` | memory alloc fail |
| `EH_ERROR_FILE_IO` | file op failed |
| `EH_ERROR_INVALID_PARAM` | invalid parameter |
| `EH_ERROR_NETWORK` | network op failed |
| `EH_ERROR_TIMEOUT` | op timedout |
| `EH_ERROR_ACCESS_DENIED` | permission denied |
| `EH_ERROR_NOT_FOUND` | resource not found |

---

## fastparse.h

string parsing header-only lib with simd for no reason

### features

- **zero-copy**: no unnecessary mallocs
- **simd acceleration**: whitespace skipping
- **stack-based**: everything on stacks
- **chainable api**: interface for complex parsing
- **built-in parsers**: CSV, JSON, numbers with overflow detection
- **error recovery**: error messages with line/column info

### quick start

```c
#include "fastparse.h"

// basic number parsing
fp_parser_t p = fp_init_cstr("42 3.14 hello");

int64_t integer;
if (fp_parse_int64(&p, &integer)) {
    printf("Parsed integer: %lld\n", integer);
}

double number;
if (fp_parse_double(&p, &number)) {
    printf("Parsed double: %.2f\n", number);
}

// string views (zero-copy)
fp_view_t view = FP_VIEW("hello world");
fp_view_t sub = fp_view_substr(view, 0, 5); // "hello"

if (fp_view_equals(sub, FP_VIEW("hello"))) {
    printf("Match!\n");
}
```

### CSV parsing

```c
const char* csv_data = "name,age,city\n\"John Doe\",30,\"New York\"\nJane,25,Boston\n";
fp_parser_t parser = fp_init_cstr(csv_data);

fp_view_t fields[FP_CSV_MAX_FIELDS];

// parse header
size_t header_count = fp_parse_csv_line(&parser, fields);
printf("Headers: ");
for (size_t i = 0; i < header_count; i++) {
    printf("%.*s ", (int)fields[i].len, fields[i].data);
}
printf("\n");

// parse data rows
while (!fp_at_end(&parser)) {
    size_t field_count = fp_parse_csv_line(&parser, fields);
    if (field_count > 0) {
        printf("Row: ");
        for (size_t i = 0; i < field_count; i++) {
            printf("%.*s ", (int)fields[i].len, fields[i].data);
        }
        printf("\n");
    }
}
```

### JSON Parsing

```c
const char* json = "{\"name\":\"John\",\"age\":30,\"scores\":[85,92,78]}";
fp_parser_t parser = fp_init_cstr(json);

// skip JSON object
if (fp_skip_json_value(&parser)) {
    printf("Valid JSON structure\n");
}

// parse specific values
parser = fp_init_cstr("\"John Doe\"");
fp_view_t name;
if (fp_parse_json_string(&parser, &name)) {
    printf("Name: %.*s\n", (int)name.len, name.data);
}
```

### chainable parser api

```c
fp_parser_t parser = fp_init_cstr("  { \"key\" : \"value\" }  ");

fp_chain_t chain = FP_CHAIN_BEGIN(&parser)
    .then_skip_ws()           // skip leading whitespace
    .then_expect_char('{')    // expect '{'
    .then_skip_ws()           // skip whitespace
    .then_parse_string();     // parse quoted string

if (FP_CHAIN_OK(chain)) {
    fp_view_t result = FP_CHAIN_RESULT(chain);
    printf("Parsed: %.*s\n", (int)result.len, result.data);
}
```

### string view ops

```c
// create views
fp_view_t literal = FP_VIEW("compile-time string");
fp_view_t runtime = FP_VIEW_FROM_CSTR(some_string);
fp_view_t empty = FP_VIEW_EMPTY();

// operations
fp_view_t sub = fp_view_substr(literal, 8, 4); // "time"
bool equals = fp_view_equals(sub, FP_VIEW("time"));
bool starts = fp_view_starts_with(literal, FP_VIEW("compile"));
int cmp = fp_view_compare(sub, FP_VIEW("time")); // 0 for equal

// convert to C string (allocates memory!)
char* cstr = fp_view_to_cstr(sub);
printf("C string: %s\n", cstr);
free(cstr);
```

---

## timer.h

timing library for performance measurement and benchmarking.

### features

- **precise**: ns resolution on supported platforms
- **cross-platform(?)**: windows (QueryPerformanceCounter) and posix (clock_gettime)
- **units**: ns, ?s, ms, s
- **benchmark**: built-in performance testing
- **macros**: convenient timing of code blocks and functions

### quick start

```c
#include "timer.h"

int main() {
    // basic timing
    timer_t timer;
    timer_init(&timer);
    
    timer_start(&timer);
    // ... do some work ...
    timer_stop(&timer);
    
    printf("Elapsed: %.3f ms\n", timer_elapsed_ms(&timer));
    
    // automatic printing with appropriate units
    timer_print(&timer, "Work completed");
    
    return 0;
}
```

### macros

```c
// time a block of code
TIME_BLOCK("Database query", {
    // Database operation here
    result = query_database(sql);
    process_results(result);
});

// time a single function call
TIME_FUNCTION(expensive_computation(), "Computation");

// quick timer creation and measurement
TIMER_START(my_timer);
// ... code to time ...
TIMER_STOP_AND_PRINT(my_timer, "My operation");
```

### benchmarking

```c
void test_function() {
    // function to benchmark
    for (int i = 0; i < 1000; i++) {
        // blah blah
    }
}

// benchmark the function
double avg_time = benchmark_code(test_function, 100, "Test Function");
printf("Average time per call: %.6f ms\n", avg_time);
```

### precise sleeps

```c
// delays
sleep_ns(500000);    // 500ns
sleep_us(500);       // 500?s
sleep_ms(1);         // 1ms

// useful for precise timing loops
timer_t loop_timer;
timer_init(&loop_timer);

for (int i = 0; i < 60; i++) {  // 60 fps loop
    timer_start(&loop_timer);
    
    // frame works
    update_game();
    render_frame();
    
    // sleep ?time (lock fps?)
    double elapsed = timer_elapsed_ms(&loop_timer);
    if (elapsed < 16.67) {
        sleep_ms((uint64_t)(16.67 - elapsed));
    }
}
```

### timestamps

```c
uint64_t start = get_timestamp_ns();
// ... work ...
uint64_t end = get_timestamp_ns();
uint64_t duration = end - start;

printf("Duration: %llu nanoseconds\n", duration);
```

---

## integrations

### error handling + timer

```c
#include "errorhandler.h"
#include "timer.h"

int process_file(const char* filename) {
    eh_init();
    
    TIME_BLOCK("File processing", {
        FILE* fp = EH_FOPEN(filename, "r");
        if (!fp) {
            return EH_ERROR_FILE_IO;
        }
        
        char* buffer = EH_MALLOC(4096);
        if (!buffer) {
            fclose(fp);
            return EH_ERROR_MEMORY;
        }
        
        // process file...
        
        EH_FREE(buffer);
        fclose(fp);
    });
    
    EH_INFO(EH_SUCCESS, "File processed successfully: %s", filename);
    eh_cleanup();
    return EH_SUCCESS;
}
```

### fastparse and error handling

```c
#include "fastparse.h"
#include "errorhandler.h"

int parse_config(const char* config_text) {
    eh_init();
    fp_parser_t parser = fp_init_cstr(config_text);
    
    fp_view_t fields[FP_CSV_MAX_FIELDS];
    size_t count = fp_parse_csv_line(&parser, fields);
    
    if (fp_has_error(&parser)) {
        EH_ERROR(EH_ERROR_CORRUPTED_DATA, "Parse error: %s at line %zu", 
                parser.error_msg, parser.line);
        return -1;
    }
    
    EH_INFO(EH_SUCCESS, "Parsed %zu configuration fields", count);
    return 0;
}
```

## building and compiling

because they're headers, simply include them in your project:

```c
#include "errorhandler.h"
#include "fastparse.h" 
#include "timer.h"
```

### compiler flags

fastparse optimal:
```bash
gcc -O3 -msse2 -DFASTPARSE_SIMD_ENABLED your_program.c
```

errorhandler:
```bash
gcc your_program.c -ldbghelp
```

### platforms (felt the need)

- **errorhandler.h**: windows (full features), other platforms (basic features)
- **fastparse.h**: all platforms, simd acceleration on x86/x64 with sse2
- **timer.h**: windows and posix-compliant systems

---

## license

free to use!!
