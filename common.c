/*
    Common helper and utility functions

    Copyright (C) 2018        Sebastian Parschauer  <s.parschauer(a)gmx.de>

    This file is part of libscanmem.

    This library is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published
    by the Free Software Foundation; either version 3 of the License, or
    (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with this library.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "common.h"

/* states returned by check_process() */
enum pstate {
    PROC_RUNNING,
    PROC_ERR,  /* error during detection */
    PROC_DEAD,
    PROC_ZOMBIE
};

/*
 * We check if a process is running in /proc directly.
 * Also zombies are detected.
 *
 * Requirements: Linux kernel, mounted /proc
 * Assumption: (pid > 0)  --> Please check your PID before!
 */
static enum pstate check_process(pid_t pid)
{
    FILE *fp = NULL;
    char *line = NULL;
    size_t alloc_len = 0;
    char status = '\0';
    char path_str[128] = "/proc/";
    int pr_len, path_len = sizeof("/proc/") - 1;

    /* append $pid/status and check if file exists */
    pr_len = snprintf(path_str + path_len, sizeof(path_str) - path_len,
                      "%d/status", pid);
    if (pr_len <= 0 || (size_t)path_len + (size_t)pr_len >= sizeof(path_str))
        goto err;
    path_len += pr_len;

    fp = fopen(path_str, "r");
    if (!fp) {
        if (errno != ENOENT)
            goto err;
        else
            return PROC_DEAD;
    }

    /* read process status */
    while (getline(&line, &alloc_len, fp) != -1) {
        if (alloc_len <= sizeof("State:\t"))
            continue;
        if (strncmp(line, "State:\t", sizeof("State:\t") - 1) == 0) {
            status = line[sizeof("State:\t") - 1];
            break;
        }
    }
    if (line)
        free(line);
    fclose(fp);

    if (status < 'A' || status > 'Z')
       goto err;

    /* zombies are not running - parent doesn't wait */
    if (status == 'Z' || status == 'X')
        return PROC_ZOMBIE;
    return PROC_RUNNING;
err:
    return PROC_ERR;
}

bool sm_process_is_dead(pid_t pid)
{
    return (check_process(pid) != PROC_RUNNING);
}

/* The number a cgroup file starts with, or UINT64_MAX ("max", or no file) */
static uint64_t read_cgroup_number(const char *path)
{
    uint64_t n = UINT64_MAX;
    char buf[32];
    FILE *f = fopen(path, "r");

    if (!f)
        return n;
    if (fgets(buf, sizeof(buf), f) && buf[0] >= '0' && buf[0] <= '9')
        n = strtoull(buf, NULL, 10);
    fclose(f);
    return n;
}

/* The lowest memory limit on this process's cgroup and the ones above it:
 * memory.max and memory.high in cgroup v2, memory.limit_in_bytes in v1 (which
 * gives a huge number for none). UINT64_MAX if there is none. */
static uint64_t cgroup_memory_limit(void)
{
    static const char *const v2_files[] = {"memory.max", "memory.high", NULL};
    static const char *const v1_files[] = {"memory.limit_in_bytes", NULL};
    uint64_t limit = UINT64_MAX;
    char line[4096], path[4200];
    FILE *f = fopen("/proc/self/cgroup", "r");

    if (!f)
        return limit;
    while (fgets(line, sizeof(line), f)) {
        /* hierarchy-ID:controllers:path, with no controllers in v2 */
        char *controllers = strchr(line, ':');
        char *dir = controllers ? strchr(controllers + 1, ':') : NULL;
        if (!dir)
            continue;
        *dir++ = '\0';
        controllers++;
        dir[strcspn(dir, "\n")] = '\0';
        if (*dir != '/')
            continue;

        const char *root = NULL;
        const char *const *files = NULL;
        if (*controllers == '\0') {
            root = "/sys/fs/cgroup";
            files = v2_files;
        } else {
            char *save, *c;
            for (c = strtok_r(controllers, ",", &save); c; c = strtok_r(NULL, ",", &save))
                if (strcmp(c, "memory") == 0) {
                    root = "/sys/fs/cgroup/memory";
                    files = v1_files;
                }
        }
        if (!root)
            continue;

        /* the cgroup, and each one above it up to "/" (in a container, the
         * container's own cgroup) */
        for (;;) {
            for (const char *const *file = files; *file; file++) {
                snprintf(path, sizeof(path), "%s%s/%s", root, dir, *file);
                uint64_t n = read_cgroup_number(path);
                if (n < limit)
                    limit = n;
            }
            char *slash = strrchr(dir, '/');
            if (slash == dir && dir[1] == '\0')
                break;
            if (slash == dir)
                dir[1] = '\0';
            else
                *slash = '\0';
        }
    }
    fclose(f);
    return limit;
}

uint64_t util_usable_memory(void)
{
    long pages = sysconf(_SC_PHYS_PAGES), page_size = sysconf(_SC_PAGESIZE);
    if (pages <= 0 || page_size <= 0)
        return 0;

    uint64_t ram = (uint64_t) pages * page_size;
    uint64_t limit = cgroup_memory_limit();
    return limit < ram ? limit : ram;
}
