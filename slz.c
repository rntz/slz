/* feature test macro to get XSI-compliant strerror_r */
#define _POSIX_C_SOURCE 200112L

#include "slz.h"

#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#define ARRAY_LEN(arr) (sizeof(arr) / sizeof((arr)[0]))

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
    assert (!slz_ok(ctx));
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

static void *slz_malloc(slz_ctx_t *ctx, size_t sz) {
    void *p = malloc(sz);
    if (!p) {
        ctx->state = SLZ_OOM;
        slz_reraise(ctx);
    }
    assert (p);
    return p;
}

static void perrorish(const char *s, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);

    if (s) fprintf(stderr, "%s: ", s);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    fflush(stderr);

    va_end(ap);
}


/* Miscellany. */
slz_version_t slz_version(void)
{
    return ((slz_version_t) { .major = SLZ_VERSION_MAJOR,
                .minor = SLZ_VERSION_MINOR,
                .bugfix = SLZ_VERSION_BUGFIX
                });
}

bool slz_compatible_version(slz_version_t version)
{
    /* TODO: be more relaxed about this */
    return (version.major == SLZ_VERSION_MAJOR &&
            version.minor == SLZ_VERSION_MINOR &&
            version.bugfix == SLZ_VERSION_BUGFIX);
}


/* slz_ctx_t management */
void slz_init(slz_ctx_t *ctx,
              void (*handler)(slz_ctx_t*, void*),
              void *userdata)
{
    assert (handler);
    ctx->state = SLZ_OK;
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
    ctx->state = SLZ_OK;
}


/* Oh my god error message printing oh my god */
#define INIT_PERROR_BUF_SIZE 100

static char *xrealloc(const char *s, char *p, size_t len)
{
    char *pn = realloc(p, len);
    if (!pn) {
        perrorish(s,
                  "libslz: error occurred, but could not allocate space "
                  "for error message!");
        free(p);
    }
    return pn;
}

static void do_perror(
    const char *s,
    size_t (*strerrorfunc)(void *obj, char *buf, size_t buflen), void *obj)
{
    static const size_t sbuflen = INIT_PERROR_BUF_SIZE;
    char sbuf[sbuflen];
    char *p = sbuf;
    bool freeit = false;

    size_t needed = strerrorfunc(obj, sbuf, sbuflen);
    if (needed) {
        if (needed < SIZE_MAX) {
            if (!(p = xrealloc(s, NULL, needed)))
                return;
            freeit = true;
            if (strerrorfunc(obj, p, needed)) {
                free(p);
                freeit = false;
                p = "unknown error; underlying IO mechanism "
                    "being inconsistent about error message length";
            }
        }
        else {
            p = NULL;
            freeit = true;
            size_t buflen = needed;
            do {
                if (buflen * 2 < buflen) {
                    freeit = false;
                    p = "unknown error; error message length overflows!";
                    break;
                }
                if (!(p = xrealloc(s, p, buflen *= 2)))
                    return;
            } while (strerrorfunc(obj, p, buflen));
        }
    }

    assert(p);
    perrorish(s, "libslz: %s", p);
    if (freeit) free(p);
}

void slz_perror(slz_ctx_t *ctx, const char *s)
{
    assert (!slz_ok(ctx));
    switch ((enum slz_state) ctx->state) {
      case SLZ_IO_ERROR:
        if (ctx->origin_type == SLZ_SRC)
            do_perror(s, ctx->origin.src->funcs->strerror,
                      ctx->origin.src->obj);
        else {
            assert(ctx->origin_type == SLZ_SINK);
            do_perror(s, ctx->origin.sink->funcs->strerror,
                      ctx->origin.sink->obj);
        }
        break;

      case SLZ_BAD_HEADER:
        assert (ctx->origin_type == SLZ_SRC);
        perrorish(s, "libslz: bad magic number or malformed header");
        break;

      case SLZ_UNFULFILLED_EXPECTATIONS:
        /* TODO: better error message */
        perrorish(s, "libslz: unexpected value");
        break;

      case SLZ_OOM:
        perrorish(s, "libslz: out of memory");
        break;

      case SLZ_OK: IMPOSSIBLE;
    }
}

void slz_end_catch(slz_ctx_t *ctx)
{
    assert (ctx->have_env && slz_ok(ctx));
    ctx->have_env = false;
}


/* Initializing & destroying sources & sinks. */
void slz_src_init(
    slz_ctx_t *ctx, slz_src_t *src, slz_src_funcs_t *funcs, void *obj)
{
    assert (funcs);
    src->error = false;
    src->funcs = funcs;
    src->obj = obj;
    (void) ctx;                 /* unused */
}

void slz_sink_init(
    slz_ctx_t *ctx, slz_sink_t *sink, slz_sink_funcs_t *funcs, void *obj)
{
    assert (funcs);
    sink->error = false;
    sink->funcs = funcs;
    sink->obj = obj;
    (void) ctx;                 /* unused */
}

void slz_src_destroy(slz_ctx_t *ctx, slz_src_t *src) {
    src->funcs->free(src->obj);
    (void) ctx;
}

void slz_sink_destroy(slz_ctx_t *ctx, slz_sink_t *sink) {
    sink->funcs->free(sink->obj);
    (void) ctx;
}


/* File vtables and methods. */
typedef struct { FILE *file; bool eof; int saved_errno; } file_t;

static bool FILE_read(void *objp, char *buf, size_t buflen)
{
    file_t *obj = objp;
    size_t read = fread(buf, 1, buflen, obj->file);
    if (read != buflen) {
        if (feof(obj->file)) obj->eof = true;
        else obj->saved_errno = errno;
        return false;
    }
    return true;
}

static bool FILE_write(void *objp, const char *buf, size_t buflen)
{
    file_t *obj = objp;
    size_t written = fwrite(buf, 1, buflen, obj->file);
    if (written != buflen) {
        obj->saved_errno = errno;
        return false;
    }
    return true;
}

static size_t FILE_strerror(void *objp, char *buf, size_t buflen)
{
    file_t *obj = objp;
    if (obj->eof) {
        static const char msg[] = "end-of-file reached";
        if (buflen < ARRAY_LEN(msg))
            return ARRAY_LEN(msg);
        memcpy(buf, msg, ARRAY_LEN(msg));
        return 0;
    }

    return strerror_r(obj->saved_errno, buf, buflen) ? SIZE_MAX : 0;
}

static slz_src_funcs_t FILE_src_funcs = {
    .read = FILE_read,
    .strerror = FILE_strerror,
    .free = free
};

static slz_sink_funcs_t FILE_sink_funcs = {
    .write = FILE_write,
    .strerror = FILE_strerror,
    .free = free
};

/* File initializers. */
void slz_src_from_file(slz_ctx_t *ctx, slz_src_t *src, FILE *file)
{
    file_t *f = slz_malloc(ctx, sizeof(file_t));
    f->file = file;
    f->eof = false;
    slz_src_init(ctx, src, &FILE_src_funcs, (void*) f);
    /* FIXME: check that file is open for reading. */
    /* FIXME: check file for error conditions. */
}

void slz_sink_from_file(slz_ctx_t *ctx, slz_sink_t *sink, FILE *file)
{
    file_t *f = slz_malloc(ctx, sizeof(file_t));
    f->file = file;
    f->eof = false;
    /* FIXME: check that file is open for writing */
    /* FIXME: check file for error conditions */
    slz_sink_init(ctx, sink, &FILE_sink_funcs, (void*) f);
}


/* Serialization */
void slz_put_bytes(
    slz_ctx_t *ctx, slz_sink_t *sink, size_t len, const char *data)
{
    assert (slz_ok(ctx));
    assert (!sink->error);

    if (!sink->funcs->write(sink->obj, data, len)) {
        ctx->state = SLZ_IO_ERROR;
        slz_raise(ctx, SLZ_SINK, sink);
    }
}

void slz_put_magic(slz_ctx_t *ctx, slz_sink_t *sink)
{
    /* Write magic number & version info. */
    slz_put_bytes(ctx, sink, strlen(magic), magic);
    /* sizeof rather than strlen to include the terminating null byte. */
    slz_put_bytes(ctx, sink, sizeof version_string, version_string);
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
    assert (slz_ok(ctx));
    assert (!src->error);

    if (src->funcs->read(src->obj, out, len))
        return true;

    ctx->origin_type = SLZ_SRC;
    ctx->origin.src = src;
    ctx->state = SLZ_IO_ERROR;
    return false;
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
    slz_ctx_t *ctx, slz_src_t *src, size_t len, const char *data)
{
    if (try_expect_bytes(ctx, src, len, data))
        return;
    ctx->state = SLZ_UNFULFILLED_EXPECTATIONS;
    slz_reraise(ctx);
}

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

slz_version_t slz_get_magic(slz_ctx_t *ctx, slz_src_t *src)
{
    char c;
    slz_version_t v = {0, 0, 0};
    if (!(try_expect_bytes(ctx, src, strlen(magic), magic) &&
          get_version_frag(ctx, src, &v.major, &c) && c == '.' &&
          get_version_frag(ctx, src, &v.minor, &c) && c == '.' &&
          get_version_frag(ctx, src, &v.bugfix, &c) && c == '\0')) {
        ctx->state = SLZ_BAD_HEADER;
        slz_raise(ctx, SLZ_SRC, src);
    }
    return v;
}

void slz_expect_magic(slz_ctx_t *ctx, slz_src_t *src)
{
    if (!slz_compatible_version(slz_get_magic(ctx, src))) {
        ctx->state = SLZ_UNFULFILLED_EXPECTATIONS;
        slz_raise(ctx, SLZ_SRC, src);
    }
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
