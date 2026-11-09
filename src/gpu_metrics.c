#include "gpu_metrics.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define KGSL_PATH "/sys/class/kgsl/kgsl-3d0"

/* ── helper ───────────────────────────────────────────────────────────────── */

static long read_long(const char *path)
{
    FILE *fp = fopen(path, "r");
    if (!fp) return -1;
    long v = -1;
    fscanf(fp, "%ld", &v);
    fclose(fp);
    return v;
}

/* ── GPU temperature ──────────────────────────────────────────────────────── */

static float read_gpu_temp(void)
{
    DIR *dir = opendir("/sys/class/thermal");
    if (!dir) return -1.0f;

    struct dirent *ent;
    float max_t = -1.0f;

    while ((ent = readdir(dir)) != NULL) {
        if (strncmp(ent->d_name, "thermal_zone", 12) != 0) continue;

        char *tp;
        if (asprintf(&tp, "/sys/class/thermal/%s/type", ent->d_name) < 0) continue;
        FILE *fp = fopen(tp, "r");
        free(tp);
        if (!fp) continue;
        char type[48] = {0};
        fgets(type, sizeof(type), fp);
        fclose(fp);

        if (strncmp(type, "gpuss", 5) != 0) continue;

        if (asprintf(&tp, "/sys/class/thermal/%s/temp", ent->d_name) < 0) continue;
        fp = fopen(tp, "r");
        free(tp);
        if (!fp) continue;
        int raw = 0;
        fscanf(fp, "%d", &raw);
        fclose(fp);
        float t = raw / 1000.0f;
        if (t > max_t) max_t = t;
    }
    closedir(dir);
    return max_t;
}

/* ── Public API ───────────────────────────────────────────────────────────── */

void gpu_metrics_read(gpu_metrics_t *m)
{
    /*
     * GPU utilisation.
     * gpu_busy_percentage emits "N %" — fscanf with %ld stops at the space.
     */
    long v = read_long(KGSL_PATH "/gpu_busy_percentage");
    m->gpu_util = (v >= 0) ? (float)v : -1.0f;
    if (m->gpu_util < 0) {
        v = read_long(KGSL_PATH "/devfreq/gpu_load");
        if (v >= 0) m->gpu_util = (float)v;
    }

    /*
     * GPU instantaneous frequency.
     * Prefer clock_mhz (already in MHz).  Fall back to devfreq/cur_freq or
     * gpuclk (both in Hz).
     */
    v = read_long(KGSL_PATH "/clock_mhz");
    if (v > 0) {
        m->gpu_mhz = (unsigned long)v;
    } else {
        v = read_long(KGSL_PATH "/devfreq/cur_freq");
        if (v <= 0) v = read_long(KGSL_PATH "/gpuclk");
        m->gpu_mhz = (v > 0) ? (unsigned long)(v / 1000000L) : 0;
    }

    /* GPU temperature */
    m->gpu_temp = read_gpu_temp();

    /* KGSL memory is read by mem_metrics_read(). */
}

/* ── printing ─────────────────────────────────────────────────────────────── */

void gpu_print_header(int level)
{
    if (level == 0)
        /* 4-space gap + 26-char banner = 30 chars  (= data row width).
         * Banner: < + 9= + " gpu " + 10= + > = 26 chars.                  */
        printf("    %s", "<========= gpu ==========>");
    else
        printf("    %9s %8s %7s",
               "util_%", "freq_mhz", "gpu_C");
}

void gpu_print_row(const gpu_metrics_t *m)
{
    /* 4-space gap; util_% in field 9 (was 6) for proper alignment */
    if (m->gpu_util >= 0.0f) printf("    %9.1f", m->gpu_util);
    else                     printf("    %9s",   "N/A");

    printf(" %8lu", m->gpu_mhz);

    if (m->gpu_temp >= 0.0f) printf(" %7.1f", m->gpu_temp);
    else                     printf(" %7s",   "N/A");
}
