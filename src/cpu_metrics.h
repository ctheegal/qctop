#pragma once

/*
 * Per-cluster CPU metrics – fully dynamic sizing.
 *
 * All limits detected at runtime from sysfs.
 */

#include <stddef.h>

typedef struct {
    unsigned long long user, nice, system, idle,
                       iowait, irq, softirq, steal;
} per_cpu_stat_t;

typedef struct {
    unsigned long      freq_khz;
    unsigned long long time;
} freq_state_t;

typedef struct {
    int             policy_id;
    int             n_cpus;
    int            *cpu_ids;
    per_cpu_stat_t *cpu_stat;
    freq_state_t   *freq_states;
    int             n_freq_states;
} cluster_snap_t;

typedef struct {
    int             n_clusters;
    int             n_total_cpus;
    cluster_snap_t *clusters;
} cpu_snap_t;

typedef struct {
    int           policy_id;
    int           first_cpu;   /* lowest CPU index in this cluster  */
    int           last_cpu;    /* highest CPU index in this cluster */
    float         us_avg;
    float         io;
    float         irq;
    unsigned long inst_mhz;
    unsigned long avg_mhz;
} cluster_metric_t;

typedef struct {
    /* Per-cluster aggregated metrics (default columnar output) */
    int               n_clusters;
    cluster_metric_t *cluster;       /* heap [n_clusters]    */
    float             cpu_temp;

    /* Per-core breakdown (for --cpu detailed view) */
    int               n_total_cpus;
    float            *core_util_pct; /* heap [n_total_cpus]  – (user+nice+sys+irq+io)/total */
    float            *core_io_pct;   /* heap [n_total_cpus]  – iowait/total                */
    float            *core_irq_pct;  /* heap [n_total_cpus]  – (irq+softirq)/total         */

    /* CPU PSI – /proc/pressure/cpu "some avg10" %
     * -1.0 = PSI unavailable (CONFIG_PSI not enabled in kernel)       */
    float             cpu_psi_avg10;
} cpu_metrics_t;

int  cpu_detect_clusters(cpu_snap_t *snap);
void cpu_snap_free(cpu_snap_t *snap);
void cpu_metrics_alloc(const cpu_snap_t *snap, cpu_metrics_t *m);
void cpu_metrics_free(cpu_metrics_t *m);
void cpu_snapshot_begin(cpu_snap_t *snap);
void cpu_snapshot_end(const cpu_snap_t *prev, cpu_metrics_t *m);

/* ── Printing ── */
/* Aggregated per-cluster (default view) */
void cpu_print_group_header(const cpu_metrics_t *m, int level);
void cpu_print_row(const cpu_metrics_t *m);

/* Detailed per-core utilization (--cpu view) */
void cpu_print_cores_header(const cpu_metrics_t *m, int level);
void cpu_print_cores_row(const cpu_metrics_t *m);

/* Per-cluster frequency only */
void cpu_print_freq_header(const cpu_metrics_t *m, int level);
void cpu_print_freq_row(const cpu_metrics_t *m);

/* Thermal column for --cpu view (separate <thermal> sub-section) */
void cpu_print_cpu_thermal_header(const cpu_metrics_t *m, int level);
void cpu_print_cpu_thermal_row(const cpu_metrics_t *m);

/* CPU PSI column for --cpu view */
void cpu_print_cpu_psi_header(const cpu_metrics_t *m, int level);
void cpu_print_cpu_psi_row(const cpu_metrics_t *m);
