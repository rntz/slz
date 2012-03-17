#include "slz.h"

#include <stdlib.h>
#include <string.h>

/* We serialize argv[1..] to the file argv[0]. */
int main(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: %s FILE ARG...\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    char *progname = argv[0];
    char *filename = argv[1];
    FILE *f = fopen(filename, "w");
    if (!f) {
        perror(progname);
        exit(EXIT_FAILURE);
    }

    slz_ctx_t ctx;
    slz_init_with_perror(&ctx, progname);
    if (slz_catch(&ctx)) {
        slz_perror(&ctx, progname);
        exit(EXIT_FAILURE);
    }

    slz_sink_t sink;
    slz_sink_from_file(&ctx, &sink, f);

    /* Serialize to the file. */
    assert (sizeof(int) <= sizeof(int32_t));
    assert (sizeof(size_t) <= sizeof(uint64_t));

    slz_put_int32(&ctx, &sink, (int32_t) (argc - 2));
    for (int i = 2; i < argc; ++i) {
        size_t len = strlen(argv[i]);
        slz_put_uint64(&ctx, &sink, len);
        slz_put_bytes(&ctx, &sink, len, argv[i]);
    }

    fclose(f);
    return 0;
}
