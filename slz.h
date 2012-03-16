#ifndef _SLZ_H_
#define _SLZ_H_

#include <assert.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef struct slz_src slz_src_t;
typedef struct slz_sink slz_sink_t;
typedef struct slz_err slz_err_t;

/* Types of errors that can occur. */
typedef enum {
    SLZ_NO_ERROR,
    SLZ_ERRNO,
    SLZ_UNKNOWN_ERROR,
} slz_errstate_t;

struct slz_err {
    uint8_t state;              /* one of the above enum values. */
    bool have_env;
    union {
        int saved_errno;
    } data;
    jmp_buf env;
};

struct slz_src {
    enum { SLZ_SRC_NONE, SLZ_SRC_FILE } type;
    slz_err_t err;
    union {
        FILE *file;
    } src;
};

struct slz_sink {
    enum { SLZ_SINK_NONE, SLZ_SINK_FILE } type;
    slz_err_t err;
    union {
        FILE *file;
    } sink;
};


/* Error handling. */
static inline bool slz_has_error(slz_err_t *err) {
    return err->state != SLZ_NO_ERROR;
}

static inline slz_err_t *slz_src_err(slz_src_t *src) { return &src->err; }
static inline slz_err_t *slz_sink_err(slz_sink_t *sink) { return &sink->err; }

/* Precondition: slz_has_error(err). */
void slz_perror(const char *s, slz_err_t *err);

/* Example use:
 *
 *     slz_src_t *src = ...;
 *
 *     if (slz_catch_src(src)) {
 *         // An error occurred.
 *         slz_perror("myprog", slz_src_err(src));
 *         exit(1);
 *     }
 *     // This is the code that can cause an error.
 *     int32_t val;
 *     slz_read_int32(src, &val);
 *     ...
 *
 * Note how the catch comes /before/ the code that can cause the error.
 */

#define slz_catch(err) \
    (assert(!slz_has_error(err) && !(err)->have_env),   \
     (err)->have_env = true,                            \
     (bool) setjmp((err)->jmp_buf, 0))

#define slz_catch_src(src) slz_catch(slz_src_err(src))
#define slz_catch_sink(sink) slz_catch(slz_sink_err(sink))


/* Sources & sinks. */
void slz_src_from_file(slz_src_t *src, FILE *file);
void slz_sink_from_file(slz_sink_t *sink, FILE *file);


/* Serialization. */
void slz_put_bytes(slz_sink_t *sink, size_t len, char *data);

void slz_put_bool(slz_sink_t *sink, bool val);
void slz_put_uint8(slz_sink_t *sink, uint8_t val);
void slz_put_int8(slz_sink_t *sink, int8_t val);
void slz_put_uint16(slz_sink_t *sink, uint16_t val);
void slz_put_int16(slz_sink_t *sink, int16_t val);
void slz_put_uint32(slz_sink_t *sink, uint32_t val);
void slz_put_int32(slz_sink_t *sink, int32_t val);
void slz_put_uint64(slz_sink_t *sink, uint64_t val);
void slz_put_int64(slz_sink_t *sink, int64_t val);


/* Deserialization. */
void slz_get_bytes(slz_src_t *src, size_t len, char *out);

void slz_get_bool(slz_src_t *src, bool *out);
void slz_get_uint8(slz_src_t *src, uint8_t *out);
void slz_get_int8(slz_src_t *src, int8_t *out);
void slz_get_uint16(slz_src_t *src, uint16_t *out);
void slz_get_int16(slz_src_t *src, int16_t *out);
void slz_get_uint32(slz_src_t *src, uint32_t *out);
void slz_get_int32(slz_src_t *src, int32_t *out);
void slz_get_uint64(slz_src_t *src, uint64_t *out);
void slz_get_int64(slz_src_t *src, int64_t *out);

/* /\* Reads a C string of arbitrary length, allocating a buffer for it using
 *  * malloc() and putting it in `*out'. *\/
 * void slz_get_cstring_malloc(slz_src_t *src, char **out);
 *
 * /\* Reads a C string into a preexisting buffer `buf'. `size' is the size of
 *  * `buf', so this will read c strings of length up to `size - 1'. `buf' will
 *  * ALWAYS be null-terminated when this function returns. Returns true if the
 *  * entire C string fit into `buf' and false if it was truncated. Regardless,
 *  * when this function returns the C string has been entirely read out of `src'.
 *  *
 *  * NB: If this function returns false DATA HAS BEEN IRREVOCABLY LOST.
 *  *\/
 * bool slz_get_cstring_upto(slz_src_t *src, size_t size, char *buf);
 *
 * /\* Reads a C string of up to length `size - 1' into `buf'. If the C string has
 *  * length greater than that, raises an error.
 *  *\/
 * void slz_get_cstring(slz_src_t *src, size_t size, char *buf);
 *
 * /\* Like `slz_get_cstring_upto', but reads a length-delimited string and stores
 *  * the length into `*len'. *\/
 * bool slz_get_string_upto(slz_src_t *src, size_t size, size_t *len, char *buf);
 *
 * /\* Analogous to `slz_get_cstring'. *\/
 * void slz_get_string(slz_src_t *src, size_t size, size_t *len, char *buf);
 *
 * /\* Analogous to `slz_get_cstring_malloc'. *\/
 * void slz_get_string_malloc(slz_src_t *src, size_t *len, char **out); */

#endif
