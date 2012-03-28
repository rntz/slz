#include <slz.h>

#include <stdlib.h>
#include <string.h>

void *xmalloc(size_t size)
{
    void *p = malloc(size);
    if (p) return p;
    perror("malloc");
    exit(EXIT_FAILURE);
}

/* We deserialize an array of strings from stdin. */
int main(int argc, char **argv)
{
    if (argc < 1) {        /* never happens */
        printf("usage: %s < FILE\n"
               "       %s FILE\n\n", argv[0], argv[0]);
        printf("  Deserializes an array of strings from FILE.\n");
        exit(EXIT_FAILURE);
    }

    char *progname = argv[0];
    FILE *f = stdin;
    if (argc > 1) {
        f = fopen(argv[1], "r");
        if (!f) {
            perror("fopen");
            exit(EXIT_FAILURE);
        }
    }

    slz_ctx_t ctx;
    slz_init_with_perror(&ctx, progname);
    if (slz_catch(&ctx)) {
        slz_perror(&ctx, progname);
        exit(EXIT_FAILURE);
    }

    slz_src_t src;
    slz_src_from_file(&ctx, &src, f);
    slz_expect_magic(&ctx, &src);

    /* Deserialize from file. */
    int nstrs = (int) slz_get_int32(&ctx, &src);
    char **strs = xmalloc(nstrs * sizeof(char*));

    for (int i = 0; i < nstrs; ++i) {
        size_t len = slz_get_uint64(&ctx, &src);
        char *str = strs[i] = xmalloc(len + 1);
        slz_get_bytes(&ctx, &src, len, str);
        str[len] = 0;           /* null-terminate */
    }

    fclose(f);

    /* Print results. */
    printf("num strs: %d\n", nstrs);
    for (int i = 0; i < nstrs; ++i)
        printf("%s\n", strs[i]);

    return 0;
}
