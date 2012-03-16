#include "slz.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#define IMPOSSIBLE do { assert(0); abort(); } while (0)

/* Useful internal helpers */
static void slz_init_err(slz_err_t *err)
{
    err->state = SLZ_NO_ERROR;
    err->have_env = false;
}

static void slz_raise(slz_err_t *err)
{
    if (err->have_env)
        longjmp(err->env, 1);
    else {
        /* We can't know what program name we should put here, so we're putting
         * the library name for now. TODO: figure out a better way to handle
         * this. */
        slz_perror("libsrlz", err);
        abort();
    }
}


void slz_perror(const char *s, slz_err_t *err)
{
    assert (slz_has_error(err));
    switch (err->state) {
      case SLZ_ERRNO:
        errno = err->data.saved_errno;
        perror(s);
        break;

      case SLZ_UNKNOWN_ERROR:
        fprintf(stderr, "%s: Unknown I/O error! :(\n", s);
        break;

      default: IMPOSSIBLE;
    }
}


void slz_src_from_file(slz_src_t *src, FILE *file)
{
    /* TODO: check that file is open for reading. */
    /* FIXME: check file for error conditions. */
    src->type = SLZ_SRC_FILE;
    slz_init_err(&src->err);
    src->src.file = file;
}

void slz_sink_from_file(slz_sink_t *sink, FILE *file)
{
    /* TODO: check that file is open for writing */
    /* FIXME: check file for error conditions */
    sink->type = SLZ_SINK_FILE;
    slz_init_err(&sink->err);
    sink->sink.file = file;
}


/* Serialization */
void slz_put_bytes(slz_sink_t *sink, size_t len, char *data)
{
    switch (sink->type) {
      case SLZ_SINK_FILE: (void) 0;
        FILE *f = sink->sink.file;
        size_t written = fwrite(data, 1, len, f);
        if (written != len) {
            /* Error! */
            if (ferror(f)) {
                sink->err.state = SLZ_ERRNO;
                sink->err.data.saved_errno = errno;
            }
            else
                sink->err.state = SLZ_UNKNOWN_ERROR;
            slz_raise(&sink->err);
        }
        break;

      default: IMPOSSIBLE;
    }
}

void slz_put_bool(slz_sink_t *sink, bool val)
{
    /* TODO: read C spec. is this really OK? */
    char byte = val ? 1 : 0;
    slz_put_bytes(sink, 1, &byte);
}

void slz_put_uint8(slz_sink_t *sink, uint8_t val)
{
    /* TODO: read C spec. is this really OK? */
    slz_put_bytes(sink, 1, (char*)&val);
}

void slz_put_int8(slz_sink_t *sink, int8_t val)
{
    /* TODO: read C spec. is this really OK? */
    slz_put_uint8(sink, (uint8_t) val);
}

void slz_put_uint16(slz_sink_t *sink, uint16_t val)
{
    /* NB. big-endian. */
    char bytes[] = { val >> 8, val % 256 };
    slz_put_bytes(sink, 2, bytes);
}

void slz_put_int16(slz_sink_t *sink, int16_t val)
{
    /* TODO: read C spec. is this really OK? */
    slz_put_uint16(sink, (uint16_t) val);
}

void slz_put_uint32(slz_sink_t *sink, uint32_t val)
{
    /* NB. big-endian. */
    char bytes[] = {val >> 24, (val >> 16) % 256, (val >> 8) % 256, val % 256};
    slz_put_bytes(sink, sizeof bytes, bytes);
}

void slz_put_int32(slz_sink_t *sink, int32_t val)
{
    /* TODO: read C spec. is this really OK? */
    slz_put_uint32(sink, (uint32_t) val);
}

void slz_put_uint64(slz_sink_t *sink, uint64_t val)
{
    /* NB. big-endian. */
    slz_put_uint32(sink, val >> 32);
    slz_put_uint32(sink, val % (((uint64_t)1) << 32));
}

void slz_put_int64(slz_sink_t *sink, int64_t val)
{
    /* TODO: read C spec. is this really OK?  */
    slz_put_uint64(sink, (uint64_t) val);
}


/* Deserialization. */
void slz_get_bytes(slz_src_t *src, size_t len, char *out)
{
    switch (src->type) {
      case SLZ_SRC_FILE: (void) 0;
        FILE *f = src->src.file;
        size_t read = fread(out, 1, len, f);
        if (read != len) {
            /* Error! */
            if (feof(f) || ferror(f)) {
                src->err.state = SLZ_ERRNO;
                src->err.data.saved_errno = errno;
            }
            else
                src->err.state = SLZ_UNKNOWN_ERROR;
            slz_raise(&src->err);
        }
        break;

      default: IMPOSSIBLE;
    }
}

/* TODO: many of these functions are dubiously portable. */
bool slz_get_bool(slz_src_t *src)
{
    char c;
    slz_get_bytes(src, 1, &c);
    return c ? true : false;
}

uint8_t slz_get_uint8(slz_src_t *src)
{
    uint8_t v;
    slz_get_bytes(src, 1, (char*)&v);
    return v;
}

int8_t slz_get_int8(slz_src_t *src) { return (int8_t) slz_get_uint8(src); }

uint16_t slz_get_uint16(slz_src_t *src)
{
    char bytes[sizeof(uint16_t)];
    slz_get_bytes(src, sizeof bytes, bytes);
    return (((uint16_t) bytes[0]) << 8) + (uint16_t) bytes[1];
}

int16_t slz_get_int16(slz_src_t *src) { return (int16_t) slz_get_uint16(src); }

uint32_t slz_get_uint32(slz_src_t *src)
{
    uint32_t hi = slz_get_uint16(src);
    uint32_t lo = slz_get_uint16(src);
    return (hi << 16) + lo;
}

int32_t slz_get_int2(slz_src_t *src) { return (int32_t) slz_get_uint32(src); }

uint64_t slz_get_uint64(slz_src_t *src)
{
    uint64_t hi = slz_get_uint32(src);
    uint64_t lo = slz_get_uint32(src);
    return (hi << 32) + lo;
}

int64_t slz_get_int64(slz_src_t *src) { return (int64_t) slz_get_uint64(src); }


