#include "slz.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

/* Useful internal helpers */
#define IMPOSSIBLE do { assert(0); abort(); } while (0)

static void slz_raise(slz_ctx_t *ctx)
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

static void perrorish(const char *s, const char *errdesc)
{
    if (s) fprintf(stderr, "%s: ", s);
    fprintf(stderr, "%s\n", errdesc);
    fflush(stderr);
}


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

      case SLZ_VERSION_MISMATCH:
        assert (ctx->origin_type == SLZ_SRC);
        perrorish(s, "libslz: version mismatch when deserializing");
        break;

      case SLZ_BAD_MAGIC_NUMBER:
        assert (ctx->origin_type == SLZ_SRC);
        perrorish(s, "libslz: bad magic number when deserializing");
        break;

      case SLZ_UNKNOWN_ERROR:
        perrorish(s, "libslz: Unknown I/O error! :(");
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
void slz_src_from_file(slz_ctx_t *ctx, slz_src_t *src, FILE *file)
{
    /* TODO: check that file is open for reading. */
    /* FIXME: check file for error conditions. */
    (void) ctx;
    src->type = SLZ_SRC_FILE;
    src->error = false;
    src->src.file = file;
    /* FIXME: read & check magic number */
}

void slz_sink_from_file(slz_ctx_t *ctx, slz_sink_t *sink, FILE *file)
{
    /* TODO: check that file is open for writing */
    /* FIXME: check file for error conditions */
    (void) ctx;
    sink->type = SLZ_SINK_FILE;
    sink->error = false;
    sink->sink.file = file;
    /* FIXME: write magic number */
}


/* Serialization */
void slz_put_bytes(slz_ctx_t *ctx, slz_sink_t *sink, size_t len, char *data)
{
    assert (!slz_error(ctx));
    switch ((slz_sink_type_t) sink->type) {
      case SLZ_SINK_FILE: (void) 0;
        FILE *f = sink->sink.file;
        size_t written = fwrite(data, 1, len, f);
        if (written != len) {
            /* Error! */
            ctx->origin_type = SLZ_SINK;
            ctx->origin.sink = sink;
            if (ferror(f)) {
                ctx->state = SLZ_ERRNO;
                ctx->info.saved_errno = errno;
            }
            else
                ctx->state = SLZ_UNKNOWN_ERROR;
            slz_raise(ctx);
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
void slz_get_bytes(slz_ctx_t *ctx, slz_src_t *src, size_t len, char *out)
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
            slz_raise(ctx);
        }
        break;
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

int32_t slz_get_int2(slz_ctx_t *ctx, slz_src_t *src) {
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


