// Simple initrd creator for mateOS
// Format: [name_len:4][name][size:4][data]... [0:4]
// Usage: mkinitrd output.img file1 file2 ...

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s output.img [files...]\n", argv[0]);
        return 1;
    }

    const char *output = argv[1];
    FILE *out = fopen(output, "wb");
    if (!out) {
        perror("fopen output");
        return 1;
    }

    printf("Creating initrd: %s\n", output);

    // Process each input file
    for (int i = 2; i < argc; i++) {
        const char *filename = argv[i];

        // Get basename (last component of path)
        const char *basename = strrchr(filename, '/');
        basename = basename ? basename + 1 : filename;

        FILE *in = fopen(filename, "rb");
        if (!in) {
            fprintf(stderr, "Warning: Cannot open %s, skipping\n", filename);
            continue;
        }

        // Get file size
        fseek(in, 0, SEEK_END);
        long size = ftell(in);
        fseek(in, 0, SEEK_SET);

        if (size < 0) {
            fprintf(stderr, "Warning: Invalid size for %s, skipping\n", filename);
            fclose(in);
            continue;
        }

        // Write name length
        uint32_t name_len = strlen(basename);
        fwrite(&name_len, 4, 1, out);

        // Write name
        fwrite(basename, 1, name_len, out);

        // Write size
        uint32_t file_size = (uint32_t)size;
        fwrite(&file_size, 4, 1, out);

        // Write data
        uint8_t *data = malloc(file_size);
        if (!data) {
            fprintf(stderr, "Error: Out of memory\n");
            fclose(in);
            fclose(out);
            return 1;
        }

        fread(data, 1, file_size, in);
        fwrite(data, 1, file_size, out);

        printf("  Added: %s (%u bytes)\n", basename, file_size);

        free(data);
        fclose(in);
    }

    // Write EOF marker
    uint32_t eof = 0;
    fwrite(&eof, 4, 1, out);

    fclose(out);
    printf("Initrd created successfully\n");
    return 0;
}
