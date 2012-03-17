#include "slz.h"

#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* The magic bytes (not including version identifier) that we expect at the
 * beginning of a slz value.
 *
 * Don't change this, ever.
 */
static const char magic[] = "slz-";

#define S2(x) #x
#define S(x) S2(x)
#define ID(x) x          /* this is here to make emacs' auto indentation work */
static const char version_string[] =
    ID(S(SLZ_VERSION_MAJOR) "." S(SLZ_VERSION_MINOR) "." S(SLZ_VERSION_BUGFIX));

/* Useful internal helpers */
#define IMPOSSIBLE do { assert(0); abort(); } while (0)

static void slz_reraise(slz_ctx_t *ctx)
{
    assert (slz_error(ctx));
    if (ctx->have_env) {
        ctx->have_env = false;
        longjmp(ctx->env, 1);
    }
    else {
        (*ctx->toplevel_error_handler)(ctx, ctx->userdata);
        abort();
    }
}

static void slz_raise(slz_ctx_t *ctx, slz_origin_t origin_type, void *origin)
{
    ctx->origin_type = (uint8_t) origin_type;
    switch (origin_type) {
      case SLZ_SRC: ctx->origin.src = (slz_src_t*) origin; break;
      case SLZ_SINK: ctx->origin.sink = (slz_sink_t*) origin; break;
    }
    slz_reraise(ctx);
}

static void perrorish(const char *s, const char *errdesc)
{
    if (s) fprintf(stderr, "%s: ", s);
    fprintf(stderr, "%s\n", errdesc);
    fflush(stderr);
}

/* Some forward declarations. */
static bool try_expect_bytes(
    slz_ctx_t *ctx, slz_src_t *src, size_t len, const char *data);

static bool try_get_bytes(
    slz_ctx_t *ctx, slz_src_t *src, size_t len, char *out);


/* Miscellany. */
slz_version_t slz_version(void)
{
    return ((slz_version_t) { .major = SLZ_VERSION_MAJOR,
                .minor = SLZ_VERSION_MINOR,
                .bugfix = SLZ_VERSION_BUGFIX
                });
}


/* Error handling. */
void slz_init(slz_ctx_t *ctx,
              void (*handler)(slz_ctx_t*, void*),
              void *userdata)
{
    ctx->state = SLZ_NO_ERROR;
    ctx->have_env = false;
    ctx->toplevel_error_handler = handler;
    ctx->userdata = userdata;
}

static void perror_handler(slz_ctx_t *ctx, void *data) {
    slz_perror(ctx, (const char*) data);
}

void slz_init_with_perror(slz_ctx_t *ctx, const char *s) {
    slz_init(ctx, perror_handler, (void*) s);
}

void slz_clear_error(slz_ctx_t *ctx) {
    ctx->state = SLZ_NO_ERROR;
}

void slz_perror(slz_ctx_t *ctx, const char *s)
{
    assert (slz_error(ctx));
    switch ((slz_state_t) ctx->state) {
      case SLZ_ERRNO:
        errno = ctx->info.saved_errno;
        perror(s);
        break;

      case SLZ_BAD_HEADER:
        assert (ctx->origin_type == SLZ_SRC);
        perrorish(s, "libslz: bad magic number or malformed header");
        break;

      case SLZ_VERSION_MISMATCH:
        assert (ctx->origin_type == SLZ_SRC);
        perrorish(s, "libslz: version mismatch when deserializing");
        break;

      case SLZ_UNKNOWN_ERROR:
        perrorish(s, "libslz: Unknown I/O error! :(");
        break;

      case SLZ_UNFULFILLED_EXPECTATIONS:
        /* TODO: better error message */
        perrorish(s, "libslz: unexpected value");
        break;

      case SLZ_NO_ERROR: IMPOSSIBLE;
    }
}

void slz_end_catch(slz_ctx_t *ctx)
{
    assert (ctx->have_env && !slz_error(ctx));
    ctx->have_env = false;
}


/* Source & sink initialization. */
static inline bool get_version_frag(
    slz_ctx_t *ctx, slz_src_t *src, uint16_t *nump, char *cp)
{
    assert (*nump == 0);
    for (;;) {
        if (!try_get_bytes(ctx, src, 1, cp))
            return false;
        if (*cp < '0' || '9' < *cp)
            return true;
        *nump = *nump * 10 + (*cp - '0');
    }
}

static inline bool okay_version_number(
     uint16_t major, uint16_t minor, uint16_t bugfix)
{
    /* TODO: be more relaxed about this */
    return (major == SLZ_VERSION_MAJOR &&
            minor == SLZ_VERSION_MINOR &&
            bugfix == SLZ_VERSION_BUGFIX);
}

static void expect_header(slz_ctx_t *ctx, slz_src_t *src)
{
    char c;
    uint16_t major = 0, minor = 0, bugfix = 0;
    if (!(try_expect_bytes(ctx, src, strlen(magic), magic) &&
          get_version_frag(ctx, src, &major, &c) && c == '.' &&
          get_version_frag(ctx, src, &minor, &c) && c == '.' &&
          get_version_frag(ctx, src, &bugfix, &c) && c == '\0')) {
        ctx->state = SLZ_BAD_HEADER;
        slz_raise(ctx, SLZ_SRC, src);
    }

    /* Check version number is the one we expect. */
    if (!okay_version_number(major, minor, bugfix)) {
        ctx->state = SLZ_VERSION_MISMATCH;
        ctx->info.version = ((slz_version_t) {
                .major = major, .minor = minor, .bugfix = bugfix });
        slz_raise(ctx, SLZ_SRC, src);
    }
}

void slz_src_from_file(slz_ctx_t *ctx, slz_src_t *src, FILE *file)
{
    /* TODO: check that file is open for reading. */
    /* FIXME: check file for error conditions. */
    (void) ctx;
    src->type = SLZ_SRC_FILE;
    src->error = false;
    src->src.file = file;
    expect_header(ctx, src);
}

static inline size_t asciilen(uint16_t num)
{
    size_t r = 1;
    while (num >= 10) num /= 10, ++r;
    return r;
}

static void slz_put_header(slz_ctx_t *ctx, slz_sink_t *sink)
{
    /* Write magic number & version info. */
    slz_put_bytes(ctx, sink, strlen(magic), magic);
    /* sizeof rather than strlen to include the terminating null byte. */
    slz_put_bytes(ctx, sink, sizeof version_string, version_string);
}

void slz_sink_from_file(slz_ctx_t *ctx, slz_sink_t *sink, FILE *file)
{
    /* TODO: check that file is open for writing */
    /* FIXME: check file for error conditions */
    sink->type = SLZ_SINK_FILE;
    sink->error = false;
    sink->sink.file = file;
    slz_put_header(ctx, sink);
}


/* Serialization */
void slz_put_bytes(
    slz_ctx_t *ctx, slz_sink_t *sink, size_t len, const char *data)
{
    assert (!slz_error(ctx));
    switch ((slz_sink_type_t) sink->type) {
      case SLZ_SINK_FILE: (void) 0;
        FILE *f = sink->sink.file;
        size_t written = fwrite(data, 1, len, f);
        if (written != len) {
            /* Error! */
            if (ferror(f)) {
                ctx->state = SLZ_ERRNO;
                ctx->info.saved_errno = errno;
            }
            else
                ctx->state = SLZ_UNKNOWN_ERROR;
            slz_raise(ctx, SLZ_SINK, sink);
        }
        break;
    }
}

void slz_put_bool(slz_ctx_t *ctx, slz_sink_t *sink, bool val)
{
    /* TODO: read C spec. is this really OK? */
    char byte = val ? 1 : 0;
    slz_put_bytes(ctx, sink, 1, &byte);
}

void slz_put_uint8(slz_ctx_t *ctx, slz_sink_t *sink, uint8_t val)
{
    /* TODO: read C spec. is this really OK? */
    slz_put_bytes(ctx, sink, 1, (char*)&val);
}

void slz_put_int8(slz_ctx_t *ctx, slz_sink_t *sink, int8_t val)
{
    /* TODO: read C spec. is this really OK? */
    slz_put_uint8(ctx, sink, (uint8_t) val);
}

void slz_put_uint16(slz_ctx_t *ctx, slz_sink_t *sink, uint16_t val)
{
    /* NB. big-endian. */
    char bytes[] = { val >> 8, val % 256 };
    slz_put_bytes(ctx, sink, 2, bytes);
}

void slz_put_int16(slz_ctx_t *ctx, slz_sink_t *sink, int16_t val)
{
    /* TODO: read C spec. is this really OK? */
    slz_put_uint16(ctx, sink, (uint16_t) val);
}

void slz_put_uint32(slz_ctx_t *ctx, slz_sink_t *sink, uint32_t val)
{
    /* NB. big-endian. */
    char bytes[] = {val >> 24, (val >> 16) % 256, (val >> 8) % 256, val % 256};
    slz_put_bytes(ctx, sink, sizeof bytes, bytes);
}

void slz_put_int32(slz_ctx_t *ctx, slz_sink_t *sink, int32_t val)
{
    /* TODO: read C spec. is this really OK? */
    slz_put_uint32(ctx, sink, (uint32_t) val);
}

void slz_put_uint64(slz_ctx_t *ctx, slz_sink_t *sink, uint64_t val)
{
    /* NB. big-endian. */
    slz_put_uint32(ctx, sink, val >> 32);
    slz_put_uint32(ctx, sink, val % (((uint64_t)1) << 32));
}

void slz_put_int64(slz_ctx_t *ctx, slz_sink_t *sink, int64_t val)
{
    /* TODO: read C spec. is this really OK?  */
    slz_put_uint64(ctx, sink, (uint64_t) val);
}


/* Deserialization. */
static bool try_get_bytes(
    slz_ctx_t *ctx, slz_src_t *src, size_t len, char *out)
{
    assert (!slz_error(ctx));
    switch ((slz_src_type_t) src->type) {
      case SLZ_SRC_FILE: (void) 0;
        FILE *f = src->src.file;
        size_t read = fread(out, 1, len, f);
        if (read != len) {
            /* Error! */
            ctx->origin_type = SLZ_SRC;
            ctx->origin.src = src;
            if (feof(f) || ferror(f)) {
                ctx->state = SLZ_ERRNO;
                ctx->info.saved_errno = errno;
            }
            else
                ctx->state = SLZ_UNKNOWN_ERROR;
            return false;
        }
        break;
    }
    return true;
}

static bool try_expect_bytes(
    slz_ctx_t *ctx, slz_src_t *src, size_t len, const char *data)
{
    /* TODO: for large values of len, this is probably a bad idea. */
    char buf[len];
    if (!try_get_bytes(ctx, src, len, buf))
        return false;           /* couldn't read enough data */
    if (!memcmp(data, buf, len))
        return true;            /* all is well */
    /* data not as expected */
    ctx->origin_type = SLZ_SRC;
    ctx->origin.src = src;
    ctx->state = SLZ_UNFULFILLED_EXPECTATIONS;
    return false;
}

void slz_get_bytes(slz_ctx_t *ctx, slz_src_t *src, size_t len, char *out)
{
    if (!try_get_bytes(ctx, src, len, out))
        slz_reraise(ctx);
}

void slz_expect_bytes(
    slz_ctx_t *ctx, slz_src_t *src, size_t len, const char *data,
    void *user_info)
{
    if (try_expect_bytes(ctx, src, len, data))
        return;
    ctx->state = SLZ_UNFULFILLED_EXPECTATIONS;
    ctx->info.user_info = user_info;
    slz_reraise(ctx);
}

/* TODO: many of these functions are dubiously portable. */
bool slz_get_bool(slz_ctx_t *ctx, slz_src_t *src)
{
    char c;
    slz_get_bytes(ctx, src, 1, &c);
    return c ? true : false;
}

uint8_t slz_get_uint8(slz_ctx_t *ctx, slz_src_t *src)
{
    uint8_t v;
    slz_get_bytes(ctx, src, 1, (char*)&v);
    return v;
}

int8_t slz_get_int8(slz_ctx_t *ctx, slz_src_t *src) {
    return (int8_t) slz_get_uint8(ctx, src);
}

uint16_t slz_get_uint16(slz_ctx_t *ctx, slz_src_t *src)
{
    char bytes[sizeof(uint16_t)];
    slz_get_bytes(ctx, src, sizeof bytes, bytes);
    return (((uint16_t) bytes[0]) << 8) + (uint16_t) bytes[1];
}

int16_t slz_get_int16(slz_ctx_t *ctx, slz_src_t *src) {
    return (int16_t) slz_get_uint16(ctx, src);
}

uint32_t slz_get_uint32(slz_ctx_t *ctx, slz_src_t *src)
{
    uint32_t hi = slz_get_uint16(ctx, src);
    uint32_t lo = slz_get_uint16(ctx, src);
    return (hi << 16) + lo;
}

int32_t slz_get_int32(slz_ctx_t *ctx, slz_src_t *src) {
    return (int32_t) slz_get_uint32(ctx, src);
}

uint64_t slz_get_uint64(slz_ctx_t *ctx, slz_src_t *src)
{
    uint64_t hi = slz_get_uint32(ctx, src);
    uint64_t lo = slz_get_uint32(ctx, src);
    return (hi << 32) + lo;
}

int64_t slz_get_int64(slz_ctx_t *ctx, slz_src_t *src) {
    return (int64_t) slz_get_uint64(ctx, src);
}


