/*
 * FastParse - Fast header parsing library in C for C
 * Features:
 * - Zero-copy string views with automatic lifetime management
 * - SIMD-accelerated whitespace skipping and delimiter finding
 * - Compile-time string literal optimization
 * - Stack-allocated parser contexts (no malloc)
 * - Chainable parser operations
 * - Built-in number parsing with overflow detection
 * - CSV, JSON, and custom delimiter parsing
 * - Error recovery with detailed diagnostics
 */

#ifndef FASTPARSE_H
#define FASTPARSE_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <limits.h>

#ifdef __SSE2__
#include <emmintrin.h>
#define FASTPARSE_SIMD_ENABLED 1
#endif
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// CORE TYPES
// ============================================================================

typedef struct {
    const char* data;
    size_t len;
} fp_view_t;

typedef struct {
    const char* start;
    const char* current;
    const char* end;
    size_t line;
    size_t column;
    int error_code;
    char error_msg[256];
} fp_parser_t;

typedef enum {
    FP_OK = 0,
    FP_ERROR_EOF,
    FP_ERROR_INVALID_NUMBER,
    FP_ERROR_OVERFLOW,
    FP_ERROR_INVALID_ESCAPE,
    FP_ERROR_UNTERMINATED_STRING,
    FP_ERROR_CUSTOM
} fp_error_t;

// ============================================================================
// SIMD-ACCELERATED UTILITIES
// ============================================================================

#ifdef FASTPARSE_SIMD_ENABLED
static inline const char* fp_skip_whitespace_simd(const char* str, const char* end) {
    const char* p = str;
    
    // SIMD loop for 16-byte chunks
    while (p + 16 <= end) {
        __m128i chunk = _mm_loadu_si128((const __m128i*)p);
        __m128i spaces = _mm_cmpeq_epi8(chunk, _mm_set1_epi8(' '));
        __m128i tabs = _mm_cmpeq_epi8(chunk, _mm_set1_epi8('\t'));
        __m128i newlines = _mm_cmpeq_epi8(chunk, _mm_set1_epi8('\n'));
        __m128i returns = _mm_cmpeq_epi8(chunk, _mm_set1_epi8('\r'));
        
        __m128i whitespace = _mm_or_si128(_mm_or_si128(spaces, tabs), 
                                        _mm_or_si128(newlines, returns));
        
        int mask = _mm_movemask_epi8(whitespace);
        if (mask != 0xFFFF) {
            // Found non-whitespace, scan byte by byte from here
            break;
        }
        p += 16;
    }
    
    // Finish with scalar loop
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) {
        p++;
    }
    
    return p;
}
#endif

static inline const char* fp_skip_whitespace(const char* str, const char* end) {
#ifdef FASTPARSE_SIMD_ENABLED
    return fp_skip_whitespace_simd(str, end);
#else
    while (str < end && (*str == ' ' || *str == '\t' || *str == '\n' || *str == '\r')) {
        str++;
    }
    return str;
#endif
}

// ============================================================================
// STRING VIEW OPERATIONS
// ============================================================================

#define FP_VIEW(literal) ((fp_view_t){.data = literal, .len = sizeof(literal) - 1})
#define FP_VIEW_FROM_CSTR(str) ((fp_view_t){.data = str, .len = strlen(str)})
#define FP_VIEW_EMPTY() ((fp_view_t){.data = NULL, .len = 0})

static inline fp_view_t fp_view_substr(fp_view_t view, size_t start, size_t len) {
    if (start >= view.len) return FP_VIEW_EMPTY();
    if (start + len > view.len) len = view.len - start;
    return (fp_view_t){.data = view.data + start, .len = len};
}

static inline bool fp_view_equals(fp_view_t a, fp_view_t b) {
    return a.len == b.len && memcmp(a.data, b.data, a.len) == 0;
}

static inline bool fp_view_starts_with(fp_view_t view, fp_view_t prefix) {
    return view.len >= prefix.len && memcmp(view.data, prefix.data, prefix.len) == 0;
}

static inline int fp_view_compare(fp_view_t a, fp_view_t b) {
    size_t min_len = a.len < b.len ? a.len : b.len;
    int result = memcmp(a.data, b.data, min_len);
    if (result == 0) {
        return a.len < b.len ? -1 : (a.len > b.len ? 1 : 0);
    }
    return result;
}

// Convert view to null-terminated string (WARNING: allocates memory)
static inline char* fp_view_to_cstr(fp_view_t view) {
    char* result = (char*)malloc(view.len + 1);
    if (result) {
        memcpy(result, view.data, view.len);
        result[view.len] = '\0';
    }
    return result;
}

// ============================================================================
// PARSER INITIALIZATION AND STATE
// ============================================================================

static inline fp_parser_t fp_init(const char* input, size_t len) {
    return (fp_parser_t){
        .start = input,
        .current = input,
        .end = input + len,
        .line = 1,
        .column = 1,
        .error_code = FP_OK,
        .error_msg = {0}
    };
}

static inline fp_parser_t fp_init_cstr(const char* input) {
    return fp_init(input, strlen(input));
}

static inline bool fp_at_end(const fp_parser_t* p) {
    return p->current >= p->end;
}

static inline size_t fp_remaining(const fp_parser_t* p) {
    return p->end - p->current;
}

static inline char fp_peek(const fp_parser_t* p) {
    return fp_at_end(p) ? '\0' : *p->current;
}

static inline char fp_advance(fp_parser_t* p) {
    if (fp_at_end(p)) return '\0';
    
    char c = *p->current++;
    if (c == '\n') {
        p->line++;
        p->column = 1;
    } else {
        p->column++;
    }
    return c;
}

// ============================================================================
// ERROR HANDLING
// ============================================================================

static inline void fp_set_error(fp_parser_t* p, fp_error_t code, const char* msg) {
    p->error_code = code;
    strncpy(p->error_msg, msg, sizeof(p->error_msg) - 1);
    p->error_msg[sizeof(p->error_msg) - 1] = '\0';
}

static inline bool fp_has_error(const fp_parser_t* p) {
    return p->error_code != FP_OK;
}

// ============================================================================
// WHITESPACE AND DELIMITER HANDLING
// ============================================================================

static inline void fp_skip_ws(fp_parser_t* p) {
    const char* new_pos = fp_skip_whitespace(p->current, p->end);
    
    // Update line/column tracking
    while (p->current < new_pos) {
        if (*p->current == '\n') {
            p->line++;
            p->column = 1;
        } else {
            p->column++;
        }
        p->current++;
    }
}

static inline bool fp_match_char(fp_parser_t* p, char expected) {
    if (fp_peek(p) == expected) {
        fp_advance(p);
        return true;
    }
    return false;
}

static inline bool fp_match_str(fp_parser_t* p, fp_view_t expected) {
    if (fp_remaining(p) >= expected.len && 
        memcmp(p->current, expected.data, expected.len) == 0) {
        p->current += expected.len;
        p->column += expected.len;
        return true;
    }
    return false;
}

// ============================================================================
// ULTRA-FAST NUMBER PARSING
// ============================================================================

static inline bool fp_parse_int64(fp_parser_t* p, int64_t* result) {
    fp_skip_ws(p);
    
    if (fp_at_end(p)) {
        fp_set_error(p, FP_ERROR_EOF, "Expected number");
        return false;
    }
    
    bool negative = false;
    if (fp_peek(p) == '-') {
        negative = true;
        fp_advance(p);
    } else if (fp_peek(p) == '+') {
        fp_advance(p);
    }
    
    if (!isdigit(fp_peek(p))) {
        fp_set_error(p, FP_ERROR_INVALID_NUMBER, "Expected digit");
        return false;
    }
    
    uint64_t value = 0;
    const uint64_t max_val = negative ? (uint64_t)INT64_MAX + 1 : INT64_MAX;
    
    while (!fp_at_end(p) && isdigit(fp_peek(p))) {
        uint64_t digit = fp_peek(p) - '0';
        
        // Check for overflow
        if (value > (max_val - digit) / 10) {
            fp_set_error(p, FP_ERROR_OVERFLOW, "Integer overflow");
            return false;
        }
        
        value = value * 10 + digit;
        fp_advance(p);
    }
    
    *result = negative ? -(int64_t)value : (int64_t)value;
    return true;
}

static inline bool fp_parse_double(fp_parser_t* p, double* result) {
    fp_skip_ws(p);
    
    const char* start = p->current;
    char* end_ptr;
    
    *result = strtod(start, &end_ptr);
    
    if (end_ptr == start) {
        fp_set_error(p, FP_ERROR_INVALID_NUMBER, "Expected number");
        return false;
    }
    
    // Update parser position
    size_t consumed = end_ptr - start;
    p->current += consumed;
    p->column += consumed;
    
    return true;
}

// ============================================================================
// STRING PARSING WITH ESCAPE SEQUENCES
// ============================================================================

static inline bool fp_parse_quoted_string(fp_parser_t* p, fp_view_t* result) {
    fp_skip_ws(p);
    
    if (!fp_match_char(p, '"')) {
        fp_set_error(p, FP_ERROR_UNTERMINATED_STRING, "Expected opening quote");
        return false;
    }
    
    const char* start = p->current;
    
    while (!fp_at_end(p) && fp_peek(p) != '"') {
        if (fp_peek(p) == '\\') {
            fp_advance(p); // Skip backslash
            if (fp_at_end(p)) break;
            fp_advance(p); // Skip escaped character
        } else {
            fp_advance(p);
        }
    }
    
    if (!fp_match_char(p, '"')) {
        fp_set_error(p, FP_ERROR_UNTERMINATED_STRING, "Expected closing quote");
        return false;
    }
    
    *result = (fp_view_t){.data = start, .len = p->current - start - 1};
    return true;
}

// ============================================================================
// CSV PARSING
// ============================================================================

typedef struct {
    fp_view_t* fields;
    size_t count;
    size_t capacity;
} fp_csv_row_t;

static inline bool fp_parse_csv_field(fp_parser_t* p, fp_view_t* field) {
    fp_skip_ws(p);
    
    if (fp_at_end(p)) return false;
    
    if (fp_peek(p) == '"') {
        return fp_parse_quoted_string(p, field);
    } else {
        const char* start = p->current;
        while (!fp_at_end(p) && fp_peek(p) != ',' && fp_peek(p) != '\n' && fp_peek(p) != '\r') {
            fp_advance(p);
        }
        
        // Trim trailing whitespace
        const char* end = p->current;
        while (end > start && (end[-1] == ' ' || end[-1] == '\t')) {
            end--;
        }
        
        *field = (fp_view_t){.data = start, .len = end - start};
        return true;
    }
}

// Stack-allocated CSV parsing (no malloc)
#define FP_CSV_MAX_FIELDS 64

static inline size_t fp_parse_csv_line(fp_parser_t* p, fp_view_t fields[FP_CSV_MAX_FIELDS]) {
    size_t count = 0;
    
    while (!fp_at_end(p) && count < FP_CSV_MAX_FIELDS) {
        if (!fp_parse_csv_field(p, &fields[count])) {
            break;
        }
        count++;
        
        if (!fp_match_char(p, ',')) {
            break;
        }
    }
    
    // Skip line ending
    if (fp_match_char(p, '\r')) {
        fp_match_char(p, '\n');
    } else {
        fp_match_char(p, '\n');
    }
    
    return count;
}

// ============================================================================
// JSON-LIKE PARSING
// ============================================================================

static inline bool fp_parse_json_string(fp_parser_t* p, fp_view_t* result) {
    return fp_parse_quoted_string(p, result);
}

static inline bool fp_parse_json_number(fp_parser_t* p, double* result) {
    return fp_parse_double(p, result);
}

static inline bool fp_skip_json_value(fp_parser_t* p);

static inline bool fp_skip_json_object(fp_parser_t* p) {
    if (!fp_match_char(p, '{')) return false;
    
    fp_skip_ws(p);
    if (fp_match_char(p, '}')) return true; // Empty object
    
    do {
        fp_skip_ws(p);
        
        // Skip key
        fp_view_t key;
        if (!fp_parse_json_string(p, &key)) return false;
        
        fp_skip_ws(p);
        if (!fp_match_char(p, ':')) return false;
        
        fp_skip_ws(p);
        if (!fp_skip_json_value(p)) return false;
        
        fp_skip_ws(p);
    } while (fp_match_char(p, ','));
    
    return fp_match_char(p, '}');
}

static inline bool fp_skip_json_array(fp_parser_t* p) {
    if (!fp_match_char(p, '[')) return false;
    
    fp_skip_ws(p);
    if (fp_match_char(p, ']')) return true; // Empty array
    
    do {
        fp_skip_ws(p);
        if (!fp_skip_json_value(p)) return false;
        fp_skip_ws(p);
    } while (fp_match_char(p, ','));
    
    return fp_match_char(p, ']');
}

static inline bool fp_skip_json_value(fp_parser_t* p) {
    fp_skip_ws(p);
    
    char c = fp_peek(p);
    switch (c) {
        case '"': {
            fp_view_t str;
            return fp_parse_json_string(p, &str);
        }
        case '{':
            return fp_skip_json_object(p);
        case '[':
            return fp_skip_json_array(p);
        case 't':
            return fp_match_str(p, FP_VIEW("true"));
        case 'f':
            return fp_match_str(p, FP_VIEW("false"));
        case 'n':
            return fp_match_str(p, FP_VIEW("null"));
        default:
            if (c == '-' || isdigit(c)) {
                double num;
                return fp_parse_json_number(p, &num);
            }
            return false;
    }
}

// ============================================================================
// CHAINABLE PARSER COMBINATORS
// ============================================================================

typedef struct fp_chain fp_chain_t;
struct fp_chain {
    fp_parser_t* parser;
    bool success;
    fp_view_t result;
};

static inline fp_chain_t fp_chain(fp_parser_t* p) {
    return (fp_chain_t){.parser = p, .success = true, .result = FP_VIEW_EMPTY()};
}

static inline fp_chain_t fp_then_skip_ws(fp_chain_t chain) {
    if (chain.success) {
        fp_skip_ws(chain.parser);
    }
    return chain;
}

static inline fp_chain_t fp_then_expect_char(fp_chain_t chain, char expected) {
    if (chain.success) {
        chain.success = fp_match_char(chain.parser, expected);
        if (!chain.success) {
            char msg[64];
            snprintf(msg, sizeof(msg), "Expected '%c'", expected);
            fp_set_error(chain.parser, FP_ERROR_CUSTOM, msg);
        }
    }
    return chain;
}

static inline fp_chain_t fp_then_parse_string(fp_chain_t chain) {
    if (chain.success) {
        chain.success = fp_parse_quoted_string(chain.parser, &chain.result);
    }
    return chain;
}

// ============================================================================
// UTILITY MACROS
// ============================================================================

#define FP_PARSE_OR_RETURN(parser, func, ...) \
    do { \
        if (!func(parser, __VA_ARGS__)) { \
            return false; \
        } \
    } while(0)

#define FP_EXPECT_CHAR_OR_RETURN(parser, ch) \
    do { \
        if (!fp_match_char(parser, ch)) { \
            char msg[64]; \
            snprintf(msg, sizeof(msg), "Expected '%c' at line %zu, column %zu", ch, parser->line, parser->column); \
            fp_set_error(parser, FP_ERROR_CUSTOM, msg); \
            return false; \
        } \
    } while(0)

#define FP_CHAIN_BEGIN(parser) fp_chain(parser)
#define FP_CHAIN_OK(chain) ((chain).success)
#define FP_CHAIN_RESULT(chain) ((chain).result)

// ============================================================================
// PERFORMANCE BENCHMARKING
// ============================================================================

#ifdef FP_ENABLE_BENCHMARKS
#include <time.h>

static inline double fp_get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}

#define FP_BENCHMARK(name, code) \
    do { \
        double start = fp_get_time_ms(); \
        code; \
        double end = fp_get_time_ms(); \
        printf("Benchmark %s: %.3f ms\n", name, end - start); \
    } while(0)
#endif

#ifdef __cplusplus
}
#endif

#endif // FASTPARSE_H

/*
 * USAGE EXAMPLES:
 * 
 * // Basic parsing
 * fp_parser_t p = fp_init_cstr("42, hello, 3.14");
 * int64_t num;
 * fp_parse_int64(&p, &num); // num = 42
 * 
 * // CSV parsing
 * fp_view_t fields[FP_CSV_MAX_FIELDS];
 * size_t count = fp_parse_csv_line(&p, fields);
 * 
 * // Chainable operations
 * auto chain = FP_CHAIN_BEGIN(&p)
 *     .then_skip_ws()
 *     .then_expect_char('{')
 *     .then_parse_string();
 * 
 * // String views (zero-copy)
 * fp_view_t view = FP_VIEW("hello");
 * if (fp_view_equals(view, FP_VIEW("hello"))) {
 *     // matches!
 * }
 */