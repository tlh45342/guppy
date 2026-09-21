#pragma once

/* Read-only maintainer-script inventory and signal scan. */
int pkg_scripts_inspect(const char *path);

/* Print one maintainer script verbatim with line numbers plus a read-only
 * interesting-line index.  The script is NEVER executed. */
int pkg_script_show(const char *path, const char *script_name);
