#pragma once

/*
 * GPU (Adreno/KGSL) metrics.
 *
 * Sources:
 *   GPU util / freq : /sys/class/kgsl/kgsl-3d0/
 *   GPU temperature : /sys/class/thermal/ zone type "gpuss*"
 *
 * Note: devfreq/trans_stat on this Qualcomm kernel records only transition
 * counts and cumulative time in a from→to matrix format.  When the GPU is
 * lightly used, all counters are zero.  A meaningful interval-weighted
 * average frequency cannot be reliably derived, so only the instantaneous
 * frequency is reported.
 *
 * KGSL memory is reported in the memory subsystem (--mem), not here.
 */

typedef struct {
    float         gpu_util;   /* GPU busy %          (-1 = N/A) */
    unsigned long gpu_mhz;    /* GPU inst. freq MHz  ( 0 = N/A) */
    float         gpu_temp;   /* hottest gpuss zone  (-1 = N/A) */
} gpu_metrics_t;

void gpu_metrics_read(gpu_metrics_t *m);

/* level 0 = group header, level 1 = column names.  No newline. */
void gpu_print_header(int level);

/* Print data values.  No newline. */
void gpu_print_row(const gpu_metrics_t *m);
