// src/cmd_pwd.c

#include <stdio.h>

#include "cmds.h"
#include "cwd.h" 
#include "mnttab.h"
#include "devmap.h"

int cmd_pwd(int argc, char **argv) {
    (void)argc; (void)argv;
	
    const char *p = cwd_get();
    puts(p);
    
	// Helpful context: what backs this directory?
    const MountEntry *m = mnttab_find_by_mpoint(p);
    if (m) {
        const char *img = devmap_resolve(m->dev);
        printf("# on %s part=%d fstype=%s -> %s\n",
               m->dev, m->part_index,
               m->fstype[0] ? m->fstype : "-",
               img ? img : "-");
    }
    return 0;
}