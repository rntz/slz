#ifndef _SLZ_H_
#define _SLZ_H_

#include <assert.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/* NB. The rest of this comment is a collection of lies, serving as an extended
 * TODO. Currently, if the version of the library and the encoded value do not
 * match, an error occurs. (YAGNI.)
 *
 * TODO:
 *
 * Consider a library with version "ml.il.bl" (format: "major.minor.bugfix").
 * Suppose it is attempting to read a chunk serialized by library with version
 * "sml.sil.sbl".
 *
 * If ml != sml, an error occurs. Major version changes indicate complete
 * incompatibilities. Otherwise:
 *
 * If il < sil, then a "soft" error occurs. Minor version increments indicate
 * new features which an older library may not be able to handle. However, the
 * library can be forced to try anyway, as it may succeed if the new minor
 * version's features were not exercised; hence, a "soft" error.
 *
 * Otherwise, everything is OK, unless "sml.sil.sbl" is known to have buggy
 * serialization, in which case a "soft" error is signalled.
 *
 * See slz_src_from_file and slz_sink_from_file for information about how errors
 * are signalled.
 */

#define SLZ_VERSION_MAJOR  0
#define SLZ_VERSION_MINOR  0
#define SLZ_VERSION_BUGFIX 0

typedef struct { uint16_t major, minor, bugfix; } slz_version_t;

typedef enum { SLZ_SRC_FILE } slz_src_type_t;
typedef enum { SLZ_SINK_FILE } slz_sink_type_t;

typedef struct {
    uint8_t type;               /* slz_src_type_t */
    bool error;
    union { FILE *file; } src;
} slz_src_t;

typedef struct {
    uint8_t type;               /* slz_sink_type_t */
    bool error;
    union { FILE *file; } sink;
} slz_sink_t;

/* Types of errors that can occur. */
typedef enum {
    SLZ_NO_ERROR,
    SLZ_UNKNOWN_ERROR,
    SLZ_ERRNO,
    SLZ_BAD_HEADER,
    SLZ_VERSION_MISMATCH,
    SLZ_UNFULFILLED_EXPECTATIONS,
} slz_state_t;

typedef enum { SLZ_SRC, SLZ_SINK } slz_origin_t;

typedef struct slz_ctx slz_ctx_t;
struct slz_ctx {
    uint8_t state;        /* slz_state_t */
    uint8_t origin_type;  /* slz_origin_t */
    bool have_env;
    union {
        int saved_errno;
        slz_version_t version;
        void *user_info;
    } info;
    union {
        slz_src_t *src;
        slz_sink_t *sink;
    } origin;
    jmp_buf env;
    /* if this returns, we abort the program. */
    void (*toplevel_error_handler)(slz_ctx_t *ctx, void *userdata);
    void *userdata;
};


/* Miscellany. */
slz_version_t slz_version(void);


/* Error handling. */
void slz_init(
    slz_ctx_t *ctx,
    void (*handler)(slz_ctx_t*, void*),
    void *userdata);

/* Initializes `ctx' with a top-level error handler that prints the error,
 * perror-style (using `s'), and then aborts. */
void slz_init_with_perror(slz_ctx_t *ctx, const char *s);

static inline bool slz_error(slz_ctx_t *ctx) {
    return ctx->state != SLZ_NO_ERROR;
}

/* Precondition: slz_error(ctx).
 * NB. doesn't clear the error from src or sink that caused it.
 */
void slz_clear_error(slz_ctx_t *ctx);
/* Precondition: slz_error(ctx). */
void slz_perror(slz_ctx_t *ctx, const char *s);

/* Example use:
 *
 *     slz_ctx_t ctx;
 *     slz_src_t src;
 *     ...
 *     if (slz_catch(&ctx)) {
 *         // An error occurred.
 *         slz_perror(&ctx, "myprog");
 *         exit(1);
 *     }
 *     // This is the code that can cause an error.
 *     int32_t val = slz_read_int32(&ctx, &src);
 *     ...
 *     slz_end_catch(&ctx);
 *
 * Note how the catch comes /before/ the code that can cause the error.
 */
#define slz_catch(ctx) ((bool) (setjmp(slz_PRIVATE_pre_catch((ctx))->env)))
void slz_end_catch(slz_ctx_t *ctx);

/* INTERNAL FUNCTION DO NOT USE.  */
static inline slz_ctx_t *slz_PRIVATE_pre_catch(slz_ctx_t *ctx) {
    assert (!slz_error(ctx) && !ctx->have_env);
    ctx->have_env = true;
    return ctx;
}


/* Sources & sinks. */
void slz_src_from_file(slz_ctx_t *ctx, slz_src_t *src, FILE *file);
void slz_sink_from_file(slz_ctx_t *ctx, slz_sink_t *sink, FILE *file);


/* Serialization. */
void slz_put_bytes(
    slz_ctx_t *ctx, slz_sink_t *sink, size_t len, const char *data);

void slz_put_bool  (slz_ctx_t *ctx, slz_sink_t *sink,     bool val);
void slz_put_uint8 (slz_ctx_t *ctx, slz_sink_t *sink,  uint8_t val);
void slz_put_int8  (slz_ctx_t *ctx, slz_sink_t *sink,   int8_t val);
void slz_put_uint16(slz_ctx_t *ctx, slz_sink_t *sink, uint16_t val);
void slz_put_int16 (slz_ctx_t *ctx, slz_sink_t *sink,  int16_t val);
void slz_put_uint32(slz_ctx_t *ctx, slz_sink_t *sink, uint32_t val);
void slz_put_int32 (slz_ctx_t *ctx, slz_sink_t *sink,  int32_t val);
void slz_put_uint64(slz_ctx_t *ctx, slz_sink_t *sink, uint64_t val);
void slz_put_int64 (slz_ctx_t *ctx, slz_sink_t *sink,  int64_t val);


/* Deserialization. */
void slz_get_bytes(slz_ctx_t *ctx, slz_src_t *src, size_t len, char *out);

void slz_expect_bytes(
    slz_ctx_t *ctx, slz_src_t *src, size_t len, const char *data,
    /* user_info is put in ctx->info.user_info on error. */
    void *user_info);

    bool slz_get_bool  (slz_ctx_t *ctx, slz_src_t *src);
 uint8_t slz_get_uint8 (slz_ctx_t *ctx, slz_src_t *src);
  int8_t slz_get_int8  (slz_ctx_t *ctx, slz_src_t *src);
uint16_t slz_get_uint16(slz_ctx_t *ctx, slz_src_t *src);
 int16_t slz_get_int16 (slz_ctx_t *ctx, slz_src_t *src);
uint32_t slz_get_uint32(slz_ctx_t *ctx, slz_src_t *src);
 int32_t slz_get_int32 (slz_ctx_t *ctx, slz_src_t *src);
uint64_t slz_get_uint64(slz_ctx_t *ctx, slz_src_t *src);
 int64_t slz_get_int64 (slz_ctx_t *ctx, slz_src_t *src);

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
