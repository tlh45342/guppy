#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <ctype.h>

#include "account_util.h"
#include "vfs.h"
#include "vfs_stat.h"

/* ---------------- VFS text helpers ---------------- */

bool account_join_path(char *out, size_t cap, const char *root, const char *suffix)
{
    if (!out || cap == 0 || !root || !suffix) return false;
    size_t rl = strlen(root);
    int slash = (rl > 0 && root[rl - 1] == '/');
    int n = snprintf(out, cap, "%s%s%s", root, slash ? "" : "/", suffix[0]=='/' ? suffix+1 : suffix);
    return n >= 0 && (size_t)n < cap;
}

bool account_vfs_read_all(const char *path, char **out, size_t *len_out)
{
    struct file *f = NULL;
    char *buf = NULL;
    size_t len = 0, cap = 0;

    if (!out) return false;
    *out = NULL;
    if (len_out) *len_out = 0;

    if (vfs_open(path, VFS_O_RDONLY, 0, &f) != 0 || !f) {
        buf = (char*)calloc(1, 1);
        if (!buf) return false;
        *out = buf;
        return true; /* missing account files are treated as empty */
    }

    for (;;) {
        char tmp[4096];
        ssize_t n = vfs_read(f, tmp, sizeof tmp);
        if (n < 0) { vfs_close(f); free(buf); return false; }
        if (n == 0) break;
        if (len + (size_t)n + 1 > cap) {
            size_t nc = cap ? cap * 2 : 4096;
            while (nc < len + (size_t)n + 1) nc *= 2;
            char *nb = (char*)realloc(buf, nc);
            if (!nb) { vfs_close(f); free(buf); return false; }
            buf = nb; cap = nc;
        }
        memcpy(buf + len, tmp, (size_t)n);
        len += (size_t)n;
    }
    if (vfs_close(f) != 0) { free(buf); return false; }
    if (!buf) {
        buf = (char*)malloc(1);
        if (!buf) return false;
    }
    buf[len] = '\0';
    *out = buf;
    if (len_out) *len_out = len;
    return true;
}

bool account_vfs_write_all(const char *path, const char *data, size_t len, uint32_t mode)
{
    struct file *f = NULL;
    if (!path || !data) return false;
    if (vfs_open(path, VFS_O_WRONLY|VFS_O_CREAT|VFS_O_TRUNC, mode, &f) != 0 || !f)
        return false;
    size_t off = 0;
    while (off < len) {
        ssize_t n = vfs_write(f, data + off, len - off);
        if (n <= 0) { vfs_close(f); return false; }
        off += (size_t)n;
    }
    return vfs_close(f) == 0;
}

static bool is_dir(const char *p)
{
    struct g_stat st;
    return vfs_stat(p, &st) == 0 && VFS_S_ISDIR(st.st_mode);
}

bool account_vfs_mkdir_p(const char *path, uint32_t mode)
{
    if (!path || !*path) return false;
    size_t n = strlen(path);
    char *buf = (char*)malloc(n + 1);
    if (!buf) return false;
    memcpy(buf, path, n + 1);

    for (size_t i = 1; i <= n; ++i) {
        if (buf[i] == '/' || buf[i] == '\0') {
            char save = buf[i];
            buf[i] = '\0';
            if (buf[0] && !is_dir(buf) && vfs_mkdir(buf, mode) != 0) {
                free(buf);
                return false;
            }
            buf[i] = save;
        }
    }
    free(buf);
    return true;
}

bool account_valid_name(const char *name)
{
    if (!name || !*name) return false;
    unsigned char c = (unsigned char)name[0];
    if (!(isalpha(c) || c == '_')) return false;
    for (size_t i=1; name[i]; ++i) {
        c = (unsigned char)name[i];
        if (!(isalnum(c) || c=='_' || c=='-')) return false;
    }
    return strlen(name) <= 31;
}

bool account_has_name(const char *text, const char *name)
{
    if (!text || !name) return false;
    size_t nl = strlen(name);
    const char *p = text;
    while (*p) {
        const char *e = strchr(p, '\n');
        size_t ll = e ? (size_t)(e-p) : strlen(p);
        if (ll > nl && !memcmp(p, name, nl) && p[nl] == ':') return true;
        if (!e) break;
        p = e + 1;
    }
    return false;
}

int account_next_uid(const char *text, uint32_t start_uid, uint32_t *uid_out)
{
    uint32_t max = start_uid ? start_uid-1u : 999u;
    const char *p = text ? text : "";
    while (*p) {
        const char *c1 = strchr(p, ':');
        if (!c1) break;
        const char *c2 = strchr(c1+1, ':');
        if (!c2) break;
        const char *c3 = strchr(c2+1, ':');
        if (!c3) break;
        char num[24];
        size_t n=(size_t)(c3-(c2+1));
        if (n < sizeof num) {
            memcpy(num,c2+1,n); num[n]='\0';
            char *end=NULL;
            unsigned long v=strtoul(num,&end,10);
            if (end && *end=='\0' && v<=UINT32_MAX && (uint32_t)v>=start_uid && (uint32_t)v>max)
                max=(uint32_t)v;
        }
        const char *e=strchr(p,'\n');
        if (!e) break;
        p=e+1;
    }
    if (max == UINT32_MAX) return -1;
    *uid_out=max+1u;
    return 0;
}

bool account_append_line(const char *old_text, const char *line, char **out_text)
{
    size_t a = old_text ? strlen(old_text) : 0;
    size_t b = strlen(line);
    int need_nl = a && old_text[a-1] != '\n';
    char *o=(char*)malloc(a + (need_nl?1:0) + b + 2);
    if (!o) return false;
    size_t pos=0;
    if (a) { memcpy(o,old_text,a); pos=a; }
    if (need_nl) o[pos++]='\n';
    memcpy(o+pos,line,b); pos+=b;
    if (!b || line[b-1] != '\n') o[pos++]='\n';
    o[pos]='\0';
    *out_text=o;
    return true;
}

bool account_replace_named_line(const char *old_text, const char *name,
                                const char *newline, char **out_text)
{
    size_t cap = strlen(old_text ? old_text : "") + strlen(newline) + 64;
    char *out=(char*)malloc(cap);
    if (!out) return false;
    size_t pos=0, nl=strlen(name);
    bool replaced=false;
    const char *p=old_text ? old_text : "";

    while (*p) {
        const char *e=strchr(p,'\n');
        size_t ll=e ? (size_t)(e-p) : strlen(p);
        bool hit = ll>nl && !memcmp(p,name,nl) && p[nl]==':';
        const char *src = hit ? newline : p;
        size_t sl = hit ? strlen(newline) : ll;
        if (pos+sl+2>cap) {
            cap=(pos+sl+2)*2;
            char *nb=(char*)realloc(out,cap);
            if (!nb) { free(out); return false; }
            out=nb;
        }
        memcpy(out+pos,src,sl); pos+=sl;
        out[pos++]='\n';
        if (hit) replaced=true;
        if (!e) break;
        p=e+1;
    }
    if (!replaced) {
        size_t sl=strlen(newline);
        if (pos+sl+2>cap) {
            cap=pos+sl+2;
            char *nb=(char*)realloc(out,cap);
            if (!nb) { free(out); return false; }
            out=nb;
        }
        memcpy(out+pos,newline,sl); pos+=sl;
        out[pos++]='\n';
    }
    out[pos]='\0';
    *out_text=out;
    return true;
}

/* ---------------- SHA-256 + SHA256-crypt ---------------- */

typedef struct {
    uint32_t h[8];
    uint64_t total;
    uint8_t block[64];
    size_t used;
} sha256_ctx;

static uint32_t rotr32(uint32_t x,unsigned n){ return (x>>n)|(x<<(32u-n)); }
static uint32_t be32(const uint8_t*p){ return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3]; }
static void putbe32(uint8_t*p,uint32_t v){ p[0]=v>>24;p[1]=v>>16;p[2]=v>>8;p[3]=v; }
static const uint32_t K[64]={
0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u};
static void tr(sha256_ctx*c,const uint8_t*b){
    uint32_t w[64]; for(int i=0;i<16;i++)w[i]=be32(b+4*i);
    for(int i=16;i<64;i++){uint32_t s0=rotr32(w[i-15],7)^rotr32(w[i-15],18)^(w[i-15]>>3);uint32_t s1=rotr32(w[i-2],17)^rotr32(w[i-2],19)^(w[i-2]>>10);w[i]=w[i-16]+s0+w[i-7]+s1;}
    uint32_t a=c->h[0],bb=c->h[1],cc=c->h[2],d=c->h[3],e=c->h[4],f=c->h[5],g=c->h[6],h=c->h[7];
    for(int i=0;i<64;i++){uint32_t S1=rotr32(e,6)^rotr32(e,11)^rotr32(e,25);uint32_t ch=(e&f)^((~e)&g);uint32_t t1=h+S1+ch+K[i]+w[i];uint32_t S0=rotr32(a,2)^rotr32(a,13)^rotr32(a,22);uint32_t maj=(a&bb)^(a&cc)^(bb&cc);uint32_t t2=S0+maj;h=g;g=f;f=e;e=d+t1;d=cc;cc=bb;bb=a;a=t1+t2;}
    c->h[0]+=a;c->h[1]+=bb;c->h[2]+=cc;c->h[3]+=d;c->h[4]+=e;c->h[5]+=f;c->h[6]+=g;c->h[7]+=h;
}
static void si(sha256_ctx*c){static const uint32_t iv[8]={0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u};memcpy(c->h,iv,sizeof iv);c->total=0;c->used=0;}
static void su(sha256_ctx*c,const void*d_,size_t n){const uint8_t*d=d_;c->total+=n;while(n){size_t k=64-c->used;if(k>n)k=n;memcpy(c->block+c->used,d,k);c->used+=k;d+=k;n-=k;if(c->used==64){tr(c,c->block);c->used=0;}}}
static void sf(sha256_ctx*c,uint8_t out[32]){uint64_t bits=c->total*8;c->block[c->used++]=0x80;if(c->used>56){memset(c->block+c->used,0,64-c->used);tr(c,c->block);c->used=0;}memset(c->block+c->used,0,56-c->used);for(int i=0;i<8;i++)c->block[63-i]=(uint8_t)(bits>>(8*i));tr(c,c->block);for(int i=0;i<8;i++)putbe32(out+4*i,c->h[i]);}

static const char b64t[]="./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
static void b64_24(char **dst,uint8_t b2,uint8_t b1,uint8_t b0,int n){
    uint32_t w=((uint32_t)b2<<16)|((uint32_t)b1<<8)|b0;
    while(n--){*(*dst)++=b64t[w&0x3f];w>>=6;}
}

bool account_sha256_crypt(const char *password, const char *salt_in,
                          char *out, size_t out_cap)
{
    if(!password||!salt_in||!out||out_cap<64)return false;
    char salt[17]; size_t sl=0;
    while(salt_in[sl] && salt_in[sl]!='$' && sl<16){salt[sl]=salt_in[sl];sl++;}
    salt[sl]='\0';
    size_t pl=strlen(password);
    uint8_t A[32],B[32],DP[32],DS[32],C[32];
    sha256_ctx c;

    si(&c); su(&c,password,pl); su(&c,salt,sl);
    sha256_ctx bc; si(&bc); su(&bc,password,pl); su(&bc,salt,sl); su(&bc,password,pl); sf(&bc,B);
    for(size_t n=pl;n>32;n-=32)su(&c,B,32);
    su(&c,B,pl%32);
    for(size_t n=pl;n>0;n>>=1){ if(n&1) su(&c,B,32); else su(&c,password,pl); }
    sf(&c,A);

    si(&c); for(size_t i=0;i<pl;i++)su(&c,password,pl); sf(&c,DP);
    uint8_t *P=(uint8_t*)malloc(pl?pl:1); if(!P)return false;
    for(size_t i=0;i<pl;i++)P[i]=DP[i%32];

    si(&c); for(unsigned i=0;i<16u+(unsigned)A[0];i++)su(&c,salt,sl); sf(&c,DS);
    uint8_t *S=(uint8_t*)malloc(sl?sl:1); if(!S){free(P);return false;}
    for(size_t i=0;i<sl;i++)S[i]=DS[i%32];

    memcpy(C,A,32);
    for(unsigned i=0;i<5000;i++){
        si(&c);
        if(i&1) su(&c,P,pl); else su(&c,C,32);
        if(i%3) su(&c,S,sl);
        if(i%7) su(&c,P,pl);
        if(i&1) su(&c,C,32); else su(&c,P,pl);
        sf(&c,C);
    }
    free(P); free(S);

    char enc[44]; char *q=enc;
    b64_24(&q,C[0],C[10],C[20],4);
    b64_24(&q,C[21],C[1],C[11],4);
    b64_24(&q,C[12],C[22],C[2],4);
    b64_24(&q,C[3],C[13],C[23],4);
    b64_24(&q,C[24],C[4],C[14],4);
    b64_24(&q,C[15],C[25],C[5],4);
    b64_24(&q,C[6],C[16],C[26],4);
    b64_24(&q,C[27],C[7],C[17],4);
    b64_24(&q,C[18],C[28],C[8],4);
    b64_24(&q,C[9],C[19],C[29],4);
    b64_24(&q,0,C[31],C[30],3);
    *q='\0';

    int n=snprintf(out,out_cap,"$5$%s$%s",salt,enc);
    return n>0 && (size_t)n<out_cap;
}
