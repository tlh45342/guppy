#include "use.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

typedef struct {
    char  letter;        // 'a'..'z'
    char  path[512];
    bool  ro;
    bool  in_use;
} Dev;

static Dev g_devs[26];
static char g_selected = 0; // 0 = none

static int idx(char letter) { letter = (char)tolower((unsigned char)letter); return (letter<'a'||letter>'z')?-1:(letter-'a'); }

bool use_add(char letter, const char *path, bool ro){
    int i = idx(letter); if (i<0) return false;
    snprintf(g_devs[i].path, sizeof g_devs[i].path, "%s", path);
    g_devs[i].letter = (char)tolower((unsigned char)letter);
    g_devs[i].ro = ro; g_devs[i].in_use = true;
    if (!g_selected) g_selected = g_devs[i].letter;
    return true;
}
bool use_rm(char letter){ int i=idx(letter); if(i<0) return false; g_devs[i].in_use=false; if(g_selected==g_devs[i].letter) g_selected=0; return true; }
bool use_select(char letter){ int i=idx(letter); if(i<0||!g_devs[i].in_use) return false; g_selected=g_devs[i].letter; return true; }
bool use_get(char letter, const char **out_path){ int i=idx(letter); if(i<0||!g_devs[i].in_use) return false; *out_path=g_devs[i].path; return true; }
bool use_get_selected(char *out_letter, const char **out_path){ if(!g_selected) return false; return use_get(g_selected,out_path) && ((*out_letter=g_selected),1); }
void use_list(void){
    printf("Devices:\n");
    for(int i=0;i<26;i++) if(g_devs[i].in_use) printf("  /dev/%c  %s%s\n", g_devs[i].letter, g_devs[i].path, g_selected==g_devs[i].letter?"  [selected]":"");
}
bool resolve_image_or_dev(const char *arg, const char **out_path){
    if(!arg||!*arg) return false;
    if (strncmp(arg,"/dev/",5)==0 && arg[5] && !arg[6]) return use_get(arg[5], out_path);
    if (strlen(arg)==1 && isalpha((unsigned char)arg[0])) return use_get(arg[0], out_path);
    *out_path = arg; return true; // raw path
}