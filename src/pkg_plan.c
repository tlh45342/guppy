#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pkg_plan.h"
#include "pkg_repo.h"
#include "pkg_state.h"
#include "deb_dep.h"
#include "deb_version.h"
#include "app_printf.h"
#include "logfile.h"
#include "pkg_scripts.h"

#define MAX_NODES 512
#define MAX_EDGES 4096

typedef struct node {
    const pkg_repo_entry_t *e;
    int installed;
    int visiting, done;
    int index, low, onstack;
    int component;
    char via[128];
} node_t;

typedef struct edge { int a, b; } edge_t;

typedef struct component {
    int members[MAX_NODES];
    int count;
    int emitted;
} component_t;

typedef struct plan {
    node_t n[MAX_NODES];
    int nn;
    edge_t e[MAX_EDGES];
    int ne;
    int valid;
    char conflict[1024];
    int next_index;
    int stack[MAX_NODES], sp;
    component_t comp[MAX_NODES];
    int ncomp;
} plan_t;

static int node_for(plan_t *p, const pkg_repo_entry_t *e)
{
    int i;
    for (i = 0; i < p->nn; i++)
        if (!strcmp(p->n[i].e->package, e->package)) return i;
    if (p->nn >= MAX_NODES) return -1;
    i = p->nn++;
    memset(&p->n[i], 0, sizeof p->n[i]);
    p->n[i].e = e;
    p->n[i].index = -1;
    p->n[i].component = -1;
    return i;
}
static void add_edge(plan_t*p,int a,int b){if(a>=0&&b>=0&&p->ne<MAX_EDGES){p->e[p->ne].a=a;p->e[p->ne].b=b;p->ne++;}}
static const pkg_repo_entry_t*choose_alt(const deb_dep_group_t*g,const char*root,
                                              pkg_state_entry_t*installed_out,int*is_inst,
                                              const deb_dep_alt_t **chosen_alt){
    unsigned j;
    if(is_inst)*is_inst=0;
    if(chosen_alt)*chosen_alt=NULL;
    for(j=0;j<g->alt_count;j++){
        const deb_dep_alt_t*a=&g->alt[j]; pkg_state_entry_t st;
        if(pkg_state_lookup(root,a->name,&st)==0 && deb_version_satisfies(st.version,a->op,a->version)){
            const pkg_repo_entry_t*e=pkg_repo_find_best(a->name,a->op,a->version);
            if(installed_out)*installed_out=st;
            if(chosen_alt)*chosen_alt=a;
            if(is_inst)*is_inst=e?1:2; /* 2: satisfied by target, absent from repo */
            return e;
        }
        {
            const pkg_repo_entry_t*e=pkg_repo_find_best(a->name,a->op,a->version);
            if(e){if(chosen_alt)*chosen_alt=a;return e;}
        }
        {
            const pkg_repo_entry_t*e=pkg_repo_find_provider(a->name);
            if(e){
                pkg_state_entry_t pst;
                if(chosen_alt)*chosen_alt=a;
                /* A virtual dependency such as base-files -> awk is already
                 * satisfied when the selected provider package is present at
                 * the same version on the target.  Previously the planner
                 * rediscovered the provider but forgot to mark its node as
                 * installed, causing needless re-unpack on every request. */
                if(pkg_state_lookup(root,e->package,&pst)==0 &&
                   deb_version_compare(pst.version,e->version)==0){
                    if(installed_out)*installed_out=pst;
                    if(is_inst)*is_inst=1;
                }
                return e;
            }
        }
    }
    return NULL;
}
static int expand(plan_t*p,int ni,const char*root){
    node_t*n=&p->n[ni]; deb_dep_list_t *list; char err[128]; int which,rc=0;
    if(n->done)return 0;
    if(n->visiting)return 0;

    /*
     * deb_dep_list_t is intentionally generous (128 groups x 8 alternatives)
     * and is therefore far too large for a recursive planner stack frame,
     * especially on Windows where the default executable stack is commonly
     * about 1 MiB.  Keep the dependency parse workspace on the heap.
     */
    list=(deb_dep_list_t*)malloc(sizeof *list);
    if(!list){
        p->valid=0;
        snprintf(p->conflict,sizeof p->conflict,"out of memory while expanding %s",n->e->package);
        return -1;
    }

    n->visiting=1;
    for(which=0;which<2;which++){
        const char*txt=which?n->e->depends:n->e->pre_depends; unsigned i;
        if(!txt[0])continue;
        if(deb_dep_parse(txt,list,err,sizeof err)){
            p->valid=0;snprintf(p->conflict,sizeof p->conflict,"cannot parse dependencies of %s: %s",n->e->package,err);rc=-1;goto out;
        }
        for(i=0;i<list->group_count;i++){
            pkg_state_entry_t st; int isinst=0; const deb_dep_alt_t*alt=NULL;
            const pkg_repo_entry_t*d=choose_alt(&list->group[i],root,&st,&isinst,&alt); int di,j;
            if(isinst==2) continue; /* already satisfied by target package not present in this repo */
            if(!d){
                unsigned aj; int existing=-1; const deb_dep_alt_t *bad=NULL;
                for(aj=0;aj<list->group[i].alt_count && existing<0;aj++){
                    int x;
                    for(x=0;x<p->nn;x++) if(!strcmp(p->n[x].e->package,list->group[i].alt[aj].name)){
                        existing=x; bad=&list->group[i].alt[aj]; break;
                    }
                }
                p->valid=0;
                if(existing>=0 && bad && bad->op[0])
                    snprintf(p->conflict,sizeof p->conflict,
                             "selected %s = %s but %s requires %s %s %s",
                             p->n[existing].e->package,p->n[existing].e->version,n->e->package,
                             bad->name,bad->op,bad->version);
                else
                    snprintf(p->conflict,sizeof p->conflict,"%s requires an unsatisfied dependency group",n->e->package);
                rc=-1;goto out;
            }
            for(j=0;j<p->nn;j++) if(!strcmp(p->n[j].e->package,d->package)) break;
            if(j<p->nn && alt && !deb_version_satisfies(p->n[j].e->version,alt->op,alt->version)){
                p->valid=0;
                snprintf(p->conflict,sizeof p->conflict,
                         "selected %s = %s but %s requires %s %s %s",
                         p->n[j].e->package,p->n[j].e->version,n->e->package,
                         alt->name,alt->op[0]?alt->op:"(any)",alt->version);
                rc=-1;goto out;
            }
            di=node_for(p,d);
            if(di<0){p->valid=0;snprintf(p->conflict,sizeof p->conflict,"dependency graph exceeds %d packages",MAX_NODES);rc=-1;goto out;}
            if(isinst)p->n[di].installed=1;
            add_edge(p,ni,di);
            if(expand(p,di,root)){rc=-1;goto out;}
        }
    }
    n->done=1;
out:
    n->visiting=0;
    free(list);
    return rc;
}
static void tarjan(plan_t *p, int v)
{
    int i;
    p->n[v].index = p->n[v].low = p->next_index++;
    p->stack[p->sp++] = v;
    p->n[v].onstack = 1;

    for (i = 0; i < p->ne; i++) {
        if (p->e[i].a == v) {
            int w = p->e[i].b;
            if (p->n[w].index < 0) {
                tarjan(p, w);
                if (p->n[w].low < p->n[v].low) p->n[v].low = p->n[w].low;
            } else if (p->n[w].onstack && p->n[w].index < p->n[v].low) {
                p->n[v].low = p->n[w].index;
            }
        }
    }

    if (p->n[v].low == p->n[v].index) {
        component_t *c;
        int w;
        if (p->ncomp >= MAX_NODES) return;
        c = &p->comp[p->ncomp];
        memset(c, 0, sizeof *c);
        do {
            w = p->stack[--p->sp];
            p->n[w].onstack = 0;
            p->n[w].component = p->ncomp;
            c->members[c->count++] = w;
        } while (w != v);
        p->ncomp++;
    }
}

static void build_components(plan_t *p)
{
    int i;
    p->next_index = 0;
    p->sp = 0;
    p->ncomp = 0;
    for (i = 0; i < p->nn; i++) {
        p->n[i].index = -1;
        p->n[i].low = 0;
        p->n[i].onstack = 0;
        p->n[i].component = -1;
    }
    for (i = 0; i < p->nn; i++)
        if (p->n[i].index < 0) tarjan(p, i);
}

static void print_cycles(const plan_t *p)
{
    int ci, k;
    int any = 0;
    for (ci = 0; ci < p->ncomp; ci++) {
        const component_t *c = &p->comp[ci];
        if (c->count <= 1) continue;
        app_printf("  [");
        for (k = 0; k < c->count; k++) {
            if (k) app_printf(", ");
            app_printf("%s", p->n[c->members[k]].e->package);
        }
        app_printf("]\n");
        any = 1;
    }
    if (!any) app_printf("  (none)\n");
}

/*
 * Edges point from a package to a dependency.  Emit dependency components
 * recursively before the component that needs them.  Because cycles have
 * already been collapsed into SCCs, this is a deterministic post-order walk
 * over the condensation DAG and is suitable as a future unpack order.
 */
static void emit_component(plan_t *p, int ci, int *step)
{
    component_t *c;
    int mi, ei;
    if (ci < 0 || ci >= p->ncomp) return;
    c = &p->comp[ci];
    if (c->emitted) return;
    c->emitted = 1;

    for (mi = 0; mi < c->count; mi++) {
        int n = c->members[mi];
        for (ei = 0; ei < p->ne; ei++) {
            if (p->e[ei].a == n) {
                int depc = p->n[p->e[ei].b].component;
                if (depc != ci) emit_component(p, depc, step);
            }
        }
    }

    if (c->count > 1) {
        int k;
        app_printf("  %d. group:\n", (*step)++);
        for (k = 0; k < c->count; k++) {
            const node_t *n = &p->n[c->members[k]];
            app_printf("       %-31s %-31s %s\n",
                       n->e->package, n->e->version,
                       n->installed ? "already present" : "unpack");
        }
    } else {
        const node_t *n = &p->n[c->members[0]];
        app_printf("  %d. %-31s %-31s %s\n",
                   (*step)++, n->e->package, n->e->version,
                   n->installed ? "already present" : "unpack");
    }
}

static void print_unpack_plan(plan_t *p, int root_node)
{
    int i, step = 1;
    for (i = 0; i < p->ncomp; i++) p->comp[i].emitted = 0;
    if (root_node >= 0 && root_node < p->nn)
        emit_component(p, p->n[root_node].component, &step);

    /* Defensive: include any graph node not reached from the requested root. */
    for (i = 0; i < p->ncomp; i++)
        if (!p->comp[i].emitted) emit_component(p, i, &step);
}


static int build_plan(plan_t *p, const char *package, const char *root, int *root_node)
{
    const pkg_repo_entry_t *rootpkg;
    pkg_state_entry_t st;
    int ri;
    memset(p, 0, sizeof *p); p->valid = 1;
    rootpkg = pkg_repo_find_best(package, NULL, NULL);
    if (!rootpkg) { p->valid=0; snprintf(p->conflict,sizeof p->conflict,"requested package '%s' not found",package); if(root_node)*root_node=-1; return 1; }
    ri=node_for(p,rootpkg);
    if(ri<0){p->valid=0;snprintf(p->conflict,sizeof p->conflict,"dependency graph exceeds %d packages",MAX_NODES);if(root_node)*root_node=-1;return 1;}
    if(pkg_state_lookup(root,package,&st)==0 && deb_version_compare(st.version,rootpkg->version)==0) p->n[ri].installed=1;
    (void)expand(p,ri,root); build_components(p); if(root_node)*root_node=ri; return p->valid?0:1;
}
static int entry_path(const pkg_repo_entry_t *e,char*out,size_t outsz){size_t n;if(!e||!out||!outsz)return 1;n=strlen(e->repo_root);return (n&&e->repo_root[n-1]=='/')?snprintf(out,outsz,"%s%s",e->repo_root,e->filename)>=(int)outsz:snprintf(out,outsz,"%s/%s",e->repo_root,e->filename)>=(int)outsz;}
static int apply_component(plan_t*p,int ci,const char*root,pkg_plan_install_cb cb,void*opaque,int*done,int*skip){component_t*c;int mi,ei;if(ci<0||ci>=p->ncomp)return 0;c=&p->comp[ci];if(c->emitted)return 0;c->emitted=1;for(mi=0;mi<c->count;mi++){int n=c->members[mi];for(ei=0;ei<p->ne;ei++)if(p->e[ei].a==n){int dc=p->n[p->e[ei].b].component;if(dc!=ci&&apply_component(p,dc,root,cb,opaque,done,skip))return 1;}}if(c->count>1)app_printf("pkg: unpacking dependency cycle (%d packages)\n",c->count);for(mi=0;mi<c->count;mi++){node_t*n=&p->n[c->members[mi]];char path[1400];if(n->installed){app_printf("pkg: already present: %s %s\n",n->e->package,n->e->version);if(skip)(*skip)++;continue;}if(entry_path(n->e,path,sizeof path)){guppy_eprintf("pkg: repository path too long for %s\n",n->e->package);return 1;}app_printf("pkg: unpack %s %s\n",n->e->package,n->e->version);if(cb(path,root,opaque)){guppy_eprintf("pkg: automatic install stopped at %s %s\n",n->e->package,n->e->version);return 1;}n->installed=1;if(done)(*done)++;}return 0;}

int pkg_plan_run(const char *package, const char *root)
{
    plan_t p; int ri,i; const pkg_repo_entry_t *rootpkg;
    if(build_plan(&p,package,root,&ri)!=0 && ri<0){app_printf("Plan valid: NO\nConflict: %s\n",p.conflict);return 1;}
    rootpkg=(ri>=0)?p.n[ri].e:NULL;
    if(rootpkg)app_printf("Requested:\n  %s %s\n\n",rootpkg->package,rootpkg->version);
    app_printf("Already present:\n"); {int any=0;for(i=0;i<p.nn;i++)if(p.n[i].installed){app_printf("  %s %s\n",p.n[i].e->package,p.n[i].e->version);any=1;}if(!any)app_printf("  (none)\n");}
    app_printf("\nRequired:\n"); {int any=0;for(i=0;i<p.nn;i++)if(!p.n[i].installed&&i!=ri){app_printf("  %s %s\n",p.n[i].e->package,p.n[i].e->version);any=1;}if(!any)app_printf("  (none)\n");}
    app_printf("\nCyclic groups:\n"); print_cycles(&p);
    if(p.valid){app_printf("\nUnpack plan:\n");print_unpack_plan(&p,ri);}else app_printf("\nUnpack plan:\n  (not available: plan is invalid)\n");
    app_printf("\nPlan valid: %s\n",p.valid?"YES":"NO");if(!p.valid)app_printf("Conflict: %s\n",p.conflict);app_printf("Packages in closure: %d\n",p.nn);return p.valid?0:1;
}
int pkg_plan_apply(const char *package,const char *root,pkg_plan_install_cb cb,void*opaque)
{
    plan_t p;int ri,i,done=0,skip=0;if(!cb)return 1;
    if(build_plan(&p,package,root,&ri)!=0){guppy_eprintf("pkg: cannot install '%s': dependency plan is invalid\n",package);if(p.conflict[0])guppy_eprintf("pkg: conflict: %s\n",p.conflict);return 1;}
    app_printf("pkg: dependency plan valid: %d packages in closure\n",p.nn);for(i=0;i<p.ncomp;i++)p.comp[i].emitted=0;
    if(ri>=0&&apply_component(&p,p.n[ri].component,root,cb,opaque,&done,&skip))return 1;
    for(i=0;i<p.ncomp;i++)if(!p.comp[i].emitted&&apply_component(&p,i,root,cb,opaque,&done,&skip))return 1;
    app_printf("pkg: automatic unpack complete: %d unpacked, %d already present\n",done,skip);app_printf("pkg: maintainer scripts were NOT executed; packages remain unpacked\n");return 0;
}

static int audit_component(plan_t *p, int ci, int *inspected, int *failures)
{
    component_t *c;
    int mi, ei;

    if (ci < 0 || ci >= p->ncomp) return 0;
    c = &p->comp[ci];
    if (c->emitted) return 0;
    c->emitted = 1;

    /* Audit in the same dependency-before-dependent order used by unpack. */
    for (mi = 0; mi < c->count; mi++) {
        int n = c->members[mi];
        for (ei = 0; ei < p->ne; ei++) {
            if (p->e[ei].a == n) {
                int dc = p->n[p->e[ei].b].component;
                if (dc != ci) audit_component(p, dc, inspected, failures);
            }
        }
    }

    if (c->count > 1)
        app_printf("\nDependency cycle: %d packages\n", c->count);

    for (mi = 0; mi < c->count; mi++) {
        node_t *n = &p->n[c->members[mi]];
        char path[1400];
        int rc;

        app_printf("\n============================================================\n");
        app_printf("Package audit: %s %s%s\n",
                   n->e->package, n->e->version,
                   n->installed ? " (already present)" : "");
        app_printf("============================================================\n");

        if (entry_path(n->e, path, sizeof path)) {
            guppy_eprintf("pkg audit: repository path too long for %s\n",
                          n->e->package);
            (*failures)++;
            continue;
        }

        rc = pkg_scripts_inspect(path);
        (*inspected)++;
        if (rc != 0) {
            guppy_eprintf("pkg audit: script scan failed for %s (rc=%d)\n",
                          n->e->package, rc);
            (*failures)++;
        }
    }

    return 0;
}

int pkg_plan_audit(const char *package, const char *root)
{
    plan_t p;
    int ri, i;
    int inspected = 0, failures = 0;

    if (build_plan(&p, package, root, &ri) != 0) {
        guppy_eprintf("pkg audit: cannot audit '%s': dependency plan is invalid\n",
                      package);
        if (p.conflict[0])
            guppy_eprintf("pkg audit: conflict: %s\n", p.conflict);
        return 1;
    }

    app_printf("Maintainer-script closure audit\n");
    app_printf("Requested: %s %s\n",
               p.n[ri].e->package, p.n[ri].e->version);
    app_printf("Packages in closure: %d\n", p.nn);
    app_printf("Mode: READ-ONLY\n");
    app_printf("Execution: DISABLED\n");

    for (i = 0; i < p.ncomp; i++)
        p.comp[i].emitted = 0;

    if (ri >= 0)
        audit_component(&p, p.n[ri].component, &inspected, &failures);

    for (i = 0; i < p.ncomp; i++)
        if (!p.comp[i].emitted)
            audit_component(&p, i, &inspected, &failures);

    app_printf("\n============================================================\n");
    app_printf("Closure audit complete\n");
    app_printf("Packages inspected: %d\n", inspected);
    app_printf("Scan failures: %d\n", failures);
    app_printf("Execution: DISABLED\n");

    return failures ? 1 : 0;
}
