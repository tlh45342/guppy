// src/cmd_use.c

#include <stdio.h>
#include <string.h>

#include "devmap.h"


int cmd_use(int argc, char **argv) {
    if (argc == 1) {        // list
        devmap_list();
        return 0;
    }
    if (argc == 4 && strcmp(argv[1], "-i") == 0) {
        if (!devmap_add(argv[3], argv[2])) {
            fprintf(stderr, "use: failed to map %s -> %s\n", argv[3], argv[2]);
            return 2;
        }
        printf("Mapped %s -> %s\n", argv[3], argv[2]);
        return 0;
    }
    fprintf(stderr, "use -i <image> <dev> | use\n");
    return 2;
}
