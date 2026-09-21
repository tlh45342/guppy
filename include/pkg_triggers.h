#pragma once

int pkg_triggers_register_interests(const char *package, const char *target_root);
int pkg_trigger_activate_noawait(const char *target_root, const char *trigger_name);
int pkg_triggers_activate_package(const char *package, const char *target_root);
int pkg_triggers_list(const char *target_root);
int pkg_triggers_process(const char *target_root);
