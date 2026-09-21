#pragma once
#include "pkg_repo.h"
typedef int (*pkg_plan_install_cb)(const char *package_path, const char *target_root, void *opaque);
int pkg_plan_run(const char *package, const char *target_root);
int pkg_plan_apply(const char *package, const char *target_root, pkg_plan_install_cb install_cb, void *opaque);
int pkg_plan_audit(const char *package, const char *target_root);
