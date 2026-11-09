/*
 * npu_metrics.c
 * Reports NSP (Neural Signal Processor) hardware temperature.
 * Full NPU performance counters (Q6 utilisation, HVX, HMX, clock) require
 * a Qualcomm vendor DSP library which is not included here.
 */

#include "npu_metrics.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>

/* ── NSP thermal zones ────────────────────────────────────────────────────── */

/* Scan /sys/class/thermal/ for zones whose type starts with "nsp" and
 * return the maximum temperature across all of them.                        */
static float nsp_max_temp(void)
{
    float max_t = -1.0f;
    DIR *dir = opendir("/sys/class/thermal");
    if (!dir) return max_t;

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (strncmp(ent->d_name, "thermal_zone", 12) != 0) continue;

        char path[96];
        snprintf(path, sizeof(path),
                 "/sys/class/thermal/%s/type", ent->d_name);
        FILE *fp = fopen(path, "r");
        if (!fp) continue;
        char type[32] = {0};
        (void)fgets(type, sizeof(type), fp); fclose(fp);
        if (strncmp(type, "nsp", 3) != 0) continue;

        snprintf(path, sizeof(path),
                 "/sys/class/thermal/%s/temp", ent->d_name);
        fp = fopen(path, "r");
        if (!fp) continue;
        int raw = 0; (void)fscanf(fp, "%d", &raw); fclose(fp);
        float t = raw / 1000.0f;
        if (t > max_t) max_t = t;
    }
    closedir(dir);
    return max_t;
}

/* ── Public API ───────────────────────────────────────────────────────────── */

int  npu_init(void)     { return 1; }  /* always succeeds – nsp thermal via sysfs */
void npu_shutdown(void) { }

void npu_metrics_read(npu_metrics_t *m)
{
    memset(m, 0, sizeof(*m));
    m->available = 0;
    m->temp_c    = nsp_max_temp();
}

/* ── printing ─────────────────────────────────────────────────────────────── */

void npu_print_header(int level)
{
    /* Section = 4-gap + 7-char field = 11 chars */
    if (level == 0)
        printf("    %-7s", "<nsp>");
    else
        printf("    %7s", "nsp_C");
}

void npu_print_row(const npu_metrics_t *m)
{
    if (m->temp_c >= 0.0f) printf("    %7.1f", m->temp_c);
    else                    printf("    %7s",   "N/A");
}
