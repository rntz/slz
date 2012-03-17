#include <slz.h>

#include <stdlib.h>
#include <string.h>

/* We serialize argv[1..] to the file argv[0]. */
int main(int argc, char **argv)
{
    if (argc < 1) {             /* never happens */
        printf("Usage: %s ARG...\n\n", argv[0]);
        printf("  Serializes ARG... to standard output.\n");
        exit(EXIT_FAILURE);
    }

    char *progname = argv[0];

    slz_ctx_t ctx;
    slz_init_with_perror(&ctx, progname);
    if (slz_catch(&ctx)) {
        slz_perror(&ctx, progname);
        exit(EXIT_FAILURE);
    }

    slz_sink_t sink;
    slz_sink_from_file(&ctx, &sink, stdout);

    /* Serialize to the file. */
    assert (sizeof(int) <= sizeof(int32_t));
    assert (sizeof(size_t) <= sizeof(uint64_t));

    slz_put_int32(&ctx, &sink, (int32_t) (argc - 1));
    for (int i = 1; i < argc; ++i) {
        size_t len = strlen(argv[i]);
        slz_put_uint64(&ctx, &sink, len);
        slz_put_bytes(&ctx, &sink, len, argv[i]);
    }

    return 0;
}
