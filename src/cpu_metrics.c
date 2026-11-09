#include "cpu_metrics.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── detect total CPU count ───────────────────────────────────────────────── */

static int detect_total_cpus(void)
{
    FILE *fp = fopen("/sys/devices/system/cpu/possible", "r");
    if (!fp) {
        DIR *dir = opendir("/sys/devices/system/cpu");
        if (!dir) return 8;
        struct dirent *ent;
        int max_id = -1;
        while ((ent = readdir(dir)) != NULL) {
            int id = -1;
            if (sscanf(ent->d_name, "cpu%d", &id) == 1 && id > max_id)
                max_id = id;
        }
        closedir(dir);
        return (max_id >= 0) ? max_id + 1 : 8;
    }
    int max_id = -1, a, b;
    char sep;
    while (!feof(fp)) {
        if (fscanf(fp, "%d", &a) != 1) break;
        if (a > max_id) max_id = a;
        if (fscanf(fp, "%c", &sep) != 1) break;
        if (sep == '-') {
            if (fscanf(fp, "%d", &b) == 1 && b > max_id) max_id = b;
            if (fscanf(fp, "%c", &sep) != 1) break;
        }
    }
    fclose(fp);
    return (max_id >= 0) ? max_id + 1 : 8;
}

/* ── detect frequency state count ────────────────────────────────────────── */

static int detect_freq_states(int policy_id)
{
    char path[128];
    snprintf(path, sizeof(path),
             "/sys/devices/system/cpu/cpufreq/policy%d/stats/time_in_state",
             policy_id);
    FILE *fp = fopen(path, "r");
    if (!fp) return 0;
    int count = 0;
    unsigned long freq; unsigned long long t;
    while (fscanf(fp, "%lu %llu", &freq, &t) == 2) count++;
    fclose(fp);
    return count;
}

/* ── detect clusters ──────────────────────────────────────────────────────── */

int cpu_detect_clusters(cpu_snap_t *snap)
{
    memset(snap, 0, sizeof(*snap));
    snap->n_total_cpus = detect_total_cpus();

    DIR *dir = opendir("/sys/devices/system/cpu/cpufreq");
    if (!dir) return 0;

    int policy_ids[256], n_policies = 0;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        int pid = -1;
        if (sscanf(ent->d_name, "policy%d", &pid) == 1 && pid >= 0)
            if (n_policies < 256) policy_ids[n_policies++] = pid;
    }
    closedir(dir);
    if (n_policies == 0) return 0;

    for (int i = 0; i < n_policies - 1; i++)
        for (int j = i + 1; j < n_policies; j++)
            if (policy_ids[j] < policy_ids[i]) {
                int tmp = policy_ids[i]; policy_ids[i] = policy_ids[j]; policy_ids[j] = tmp;
            }

    snap->n_clusters = n_policies;
    snap->clusters = calloc((size_t)n_policies, sizeof(cluster_snap_t));
    if (!snap->clusters) { snap->n_clusters = 0; return 0; }

    for (int c = 0; c < n_policies; c++) {
        cluster_snap_t *cl = &snap->clusters[c];
        cl->policy_id = policy_ids[c];

        char path[128];
        snprintf(path, sizeof(path),
                 "/sys/devices/system/cpu/cpufreq/policy%d/related_cpus",
                 cl->policy_id);

        FILE *fp = fopen(path, "r");
        cl->n_cpus = 0;
        if (fp) { int cpu; while (fscanf(fp, "%d", &cpu) == 1) cl->n_cpus++; fclose(fp); }
        if (cl->n_cpus == 0) cl->n_cpus = 1;

        cl->cpu_ids  = calloc((size_t)cl->n_cpus, sizeof(int));
        cl->cpu_stat = calloc((size_t)cl->n_cpus, sizeof(per_cpu_stat_t));

        fp = fopen(path, "r");
        if (fp && cl->cpu_ids) {
            int idx = 0, cpu;
            while (fscanf(fp, "%d", &cpu) == 1 && idx < cl->n_cpus)
                cl->cpu_ids[idx++] = cpu;
            fclose(fp);
        }

        cl->n_freq_states = detect_freq_states(cl->policy_id);
        if (cl->n_freq_states > 0)
            cl->freq_states = calloc((size_t)cl->n_freq_states, sizeof(freq_state_t));
    }
    return n_policies;
}

void cpu_snap_free(cpu_snap_t *snap)
{
    if (!snap->clusters) return;
    for (int c = 0; c < snap->n_clusters; c++) {
        cluster_snap_t *cl = &snap->clusters[c];
        free(cl->cpu_ids); free(cl->cpu_stat); free(cl->freq_states);
    }
    free(snap->clusters);
    memset(snap, 0, sizeof(*snap));
}

void cpu_metrics_alloc(const cpu_snap_t *snap, cpu_metrics_t *m)
{
    m->n_clusters    = snap->n_clusters;
    m->cluster       = calloc((size_t)snap->n_clusters, sizeof(cluster_metric_t));
    m->cpu_temp      = -1.0f;
    m->cpu_psi_avg10 = -1.0f;
    m->n_total_cpus  = snap->n_total_cpus;
    m->core_util_pct = calloc((size_t)snap->n_total_cpus, sizeof(float));
    m->core_io_pct   = calloc((size_t)snap->n_total_cpus, sizeof(float));
    m->core_irq_pct  = calloc((size_t)snap->n_total_cpus, sizeof(float));
    for (int c = 0; c < snap->n_clusters; c++) {
        const cluster_snap_t *cl = &snap->clusters[c];
        m->cluster[c].policy_id = cl->policy_id;
        if (cl->n_cpus > 0 && cl->cpu_ids) {
            int lo = cl->cpu_ids[0], hi = cl->cpu_ids[0];
            for (int i = 1; i < cl->n_cpus; i++) {
                if (cl->cpu_ids[i] < lo) lo = cl->cpu_ids[i];
                if (cl->cpu_ids[i] > hi) hi = cl->cpu_ids[i];
            }
            m->cluster[c].first_cpu = lo;
            m->cluster[c].last_cpu  = hi;
        } else {
            m->cluster[c].first_cpu = cl->policy_id;
            m->cluster[c].last_cpu  = cl->policy_id;
        }
    }
}

void cpu_metrics_free(cpu_metrics_t *m)
{
    free(m->cluster);
    free(m->core_util_pct);
    free(m->core_io_pct);
    free(m->core_irq_pct);
    memset(m, 0, sizeof(*m));
}

/* ── /proc/stat ───────────────────────────────────────────────────────────── */

static void read_proc_stat_all(per_cpu_stat_t *stats, int n_cpus)
{
    FILE *fp = fopen("/proc/stat", "r");
    if (!fp) return;
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        int idx = -1;
        if (sscanf(line, "cpu%d", &idx) != 1 || idx < 0 || idx >= n_cpus) continue;
        per_cpu_stat_t *s = &stats[idx];
        sscanf(line, "cpu%*d %llu %llu %llu %llu %llu %llu %llu %llu",
               &s->user, &s->nice, &s->system, &s->idle,
               &s->iowait, &s->irq, &s->softirq, &s->steal);
    }
    fclose(fp);
}

/* ── time_in_state ────────────────────────────────────────────────────────── */

static void read_time_in_state(int policy_id, freq_state_t *states, int n_states)
{
    char path[128];
    snprintf(path, sizeof(path),
             "/sys/devices/system/cpu/cpufreq/policy%d/stats/time_in_state",
             policy_id);
    FILE *fp = fopen(path, "r");
    if (!fp) return;
    int i = 0;
    while (i < n_states &&
           fscanf(fp, "%lu %llu", &states[i].freq_khz, &states[i].time) == 2)
        i++;
    fclose(fp);
}

/* ── snapshot ─────────────────────────────────────────────────────────────── */

void cpu_snapshot_begin(cpu_snap_t *snap)
{
    per_cpu_stat_t *all = calloc((size_t)snap->n_total_cpus, sizeof(per_cpu_stat_t));
    if (!all) return;
    read_proc_stat_all(all, snap->n_total_cpus);
    for (int c = 0; c < snap->n_clusters; c++) {
        cluster_snap_t *cl = &snap->clusters[c];
        for (int i = 0; i < cl->n_cpus; i++)
            cl->cpu_stat[i] = all[cl->cpu_ids[i]];
        if (cl->n_freq_states > 0)
            read_time_in_state(cl->policy_id, cl->freq_states, cl->n_freq_states);
    }
    free(all);
}

/* ── temperature ──────────────────────────────────────────────────────────── */

static float read_cpu_temp(void)
{
    DIR *dir = opendir("/sys/class/thermal");
    if (!dir) return -1.0f;
    struct dirent *ent;
    float max_t = -1.0f;
    while ((ent = readdir(dir)) != NULL) {
        if (strncmp(ent->d_name, "thermal_zone", 12)) continue;
        char *tp;
        if (asprintf(&tp, "/sys/class/thermal/%s/type", ent->d_name) < 0) continue;
        FILE *fp = fopen(tp, "r"); free(tp);
        if (!fp) continue;
        char type[48] = {0};
        if (!fgets(type, sizeof(type), fp)) { fclose(fp); continue; }
        fclose(fp);
        if (strncmp(type, "cpuss", 5)) continue;
        if (asprintf(&tp, "/sys/class/thermal/%s/temp", ent->d_name) < 0) continue;
        fp = fopen(tp, "r"); free(tp);
        if (!fp) continue;
        int raw = 0; if (fscanf(fp, "%d", &raw) != 1) { fclose(fp); continue; }
        fclose(fp);
        float t = raw / 1000.0f;
        if (t > max_t) max_t = t;
    }
    closedir(dir);
    return max_t;
}

/* ── compute ──────────────────────────────────────────────────────────────── */

void cpu_snapshot_end(const cpu_snap_t *prev, cpu_metrics_t *m)
{
    per_cpu_stat_t *all = calloc((size_t)prev->n_total_cpus, sizeof(per_cpu_stat_t));
    if (!all) return;
    read_proc_stat_all(all, prev->n_total_cpus);

    for (int c = 0; c < prev->n_clusters; c++) {
        const cluster_snap_t *cl = &prev->clusters[c];
        cluster_metric_t     *cm = &m->cluster[c];
        cm->policy_id = cl->policy_id;

        unsigned long long d_user=0,d_nice=0,d_sys=0,d_idle=0,
                           d_iow=0,d_irq=0,d_soft=0,d_steal=0;
        for (int i = 0; i < cl->n_cpus; i++) {
            const per_cpu_stat_t *p = &cl->cpu_stat[i];
            const per_cpu_stat_t *n = &all[cl->cpu_ids[i]];
#define D(f) ((n->f)-(p->f))
            d_user+=D(user);d_nice+=D(nice);d_sys+=D(system);d_idle+=D(idle);
            d_iow+=D(iowait);d_irq+=D(irq);d_soft+=D(softirq);d_steal+=D(steal);
#undef D
        }
        unsigned long long tot = d_user+d_nice+d_sys+d_idle+d_iow+d_irq+d_soft+d_steal;
        cm->us_avg = tot ? (float)(d_user+d_nice)*100.f/tot : 0.f;
        cm->io     = tot ? (float)d_iow*100.f/tot : 0.f;
        cm->irq    = tot ? (float)(d_irq+d_soft)*100.f/tot : 0.f;

        char path[128];
        snprintf(path, sizeof(path),
                 "/sys/devices/system/cpu/cpufreq/policy%d/scaling_cur_freq",
                 cl->policy_id);
        FILE *fp = fopen(path, "r");
        unsigned long freq_khz = 0;
        if (fp) { if (fscanf(fp, "%lu", &freq_khz) != 1) freq_khz = 0; fclose(fp); }
        cm->inst_mhz = freq_khz / 1000UL;

        if (cl->n_freq_states > 0) {
            freq_state_t *curr = calloc((size_t)cl->n_freq_states, sizeof(freq_state_t));
            if (curr) {
                read_time_in_state(cl->policy_id, curr, cl->n_freq_states);
                unsigned long long sum_t=0, sum_ft=0;
                for (int i = 0; i < cl->n_freq_states; i++) {
                    unsigned long long dt = curr[i].time - cl->freq_states[i].time;
                    sum_t += dt;
                    sum_ft += dt * (cl->freq_states[i].freq_khz / 1000UL);
                }
                cm->avg_mhz = sum_t ? (unsigned long)(sum_ft/sum_t) : cm->inst_mhz;
                free(curr);
            }
        } else {
            cm->avg_mhz = cm->inst_mhz;
        }
    }

    if (m->core_util_pct) {
        for (int c = 0; c < prev->n_clusters; c++) {
            const cluster_snap_t *cl = &prev->clusters[c];
            for (int i = 0; i < cl->n_cpus; i++) {
                int cpu_id = cl->cpu_ids[i];
                if (cpu_id >= m->n_total_cpus) continue;
                const per_cpu_stat_t *p = &cl->cpu_stat[i];
                const per_cpu_stat_t *n = &all[cpu_id];
                unsigned long long idle  = (n->idle+n->iowait)-(p->idle+p->iowait);
                unsigned long long total = (n->user+n->nice+n->system+n->idle+n->iowait+
                                            n->irq+n->softirq+n->steal)
                                          -(p->user+p->nice+p->system+p->idle+p->iowait+
                                            p->irq+p->softirq+p->steal);
                m->core_util_pct[cpu_id] =
                    total ? (float)(total-idle)*100.f/(float)total : 0.f;
                /* Per-core iowait and irq% computed from same snapshot */
                if (m->core_io_pct) {
                    unsigned long long pc_iow = n->iowait - p->iowait;
                    m->core_io_pct[cpu_id] =
                        total ? (float)pc_iow*100.f/(float)total : 0.f;
                }
                if (m->core_irq_pct) {
                    unsigned long long pc_irq  = n->irq     - p->irq;
                    unsigned long long pc_soft = n->softirq - p->softirq;
                    m->core_irq_pct[cpu_id] =
                        total ? (float)(pc_irq+pc_soft)*100.f/(float)total : 0.f;
                }
            }
        }
    }

    free(all);
    m->cpu_temp = read_cpu_temp();

    /* CPU PSI */
    {
        FILE *fp = fopen("/proc/pressure/cpu", "r");
        if (fp) {
            float v = 0.0f;
            m->cpu_psi_avg10 = (fscanf(fp, "some avg10=%f", &v) == 1) ? v : 0.0f;
            fclose(fp);
        } else {
            m->cpu_psi_avg10 = -1.0f;
        }
    }
}

/* ── printing ─────────────────────────────────────────────────────────────── */

static void make_section_banner(char *buf, int buf_size, int width,
                                const char *label)
{
    int label_len = (int)strlen(label);
    int inner = width - 2;
    int fill  = inner - label_len - 2;
    if (fill < 0) fill = 0;
    int left = fill / 2, right = fill - left;
    int pos = 0;
    if (pos < buf_size-1) buf[pos++] = '<';
    for (int i = 0; i < left  && pos < buf_size-1; i++) buf[pos++] = '=';
    if (pos < buf_size-1) buf[pos++] = ' ';
    for (int i = 0; i < label_len && pos < buf_size-1; i++) buf[pos++] = label[i];
    if (pos < buf_size-1) buf[pos++] = ' ';
    for (int i = 0; i < right && pos < buf_size-1; i++) buf[pos++] = '=';
    if (pos < buf_size-1) buf[pos++] = '>';
    buf[pos] = '\0';
}

/* ──────────────────────────────────────────────────────────────────────────
 * Section 1: cpu util%
 *   Each core shows 3 sub-columns: cpuX (5), ioX (5), irqX (5) = 15 chars
 *   Banner: n_cores×15-1 chars (1 trailing space for breathing room)
 *   Label: "cpu util%"
 * ─────────────────────────────────────────────────────────────────────────*/
void cpu_print_cores_header(const cpu_metrics_t *m, int level)
{
    int w = m->n_total_cpus * 15;   /* 3 sub-cols × 5 chars each */
    if (level == 0) {
        char buf[512];
        make_section_banner(buf, sizeof(buf), w - 1, "cpu util%");
        printf("  %-*s", w, buf);   /* 2-space gap from cpu-misc */
    } else {
        printf("  ");               /* 2-space gap from cpu-misc */
        char lbl[16];
        for (int i = 0; i < m->n_total_cpus; i++) {
            snprintf(lbl, sizeof(lbl), "cpu%d", i);
            printf(" %4s", lbl);
        }
        for (int i = 0; i < m->n_total_cpus; i++) {
            snprintf(lbl, sizeof(lbl), "io%d", i);
            printf(" %4s", lbl);
        }
        for (int i = 0; i < m->n_total_cpus; i++) {
            snprintf(lbl, sizeof(lbl), "irq%d", i);
            printf(" %4s", lbl);
        }
    }
}

void cpu_print_cores_row(const cpu_metrics_t *m)
{
    printf("  ");                   /* 2-space gap from cpu-misc */
    for (int i = 0; i < m->n_total_cpus; i++)
        printf(" %4.1f", m->core_util_pct ? m->core_util_pct[i] : 0.f);
    for (int i = 0; i < m->n_total_cpus; i++)
        printf(" %4.1f", m->core_io_pct  ? m->core_io_pct[i]  : 0.f);
    for (int i = 0; i < m->n_total_cpus; i++)
        printf(" %4.1f", m->core_irq_pct ? m->core_irq_pct[i] : 0.f);
}

/* ──────────────────────────────────────────────────────────────────────────
 * Section 2: cpu freq_mhz  (2-space gap from util%)
 *   Shows avg per cluster only; 7 chars each: " %6lu" / " %6s"
 *   Banner: make_section_banner(n_clusters×7, "cpu freq_mhz")
 * ─────────────────────────────────────────────────────────────────────────*/
/* Compute per-cluster freq column width from actual avg label lengths.
 * e.g. "avg0-1"=6 → col_w=6;  "avg10-19"=8 → col_w=8             */
static int freq_col_w(const cpu_metrics_t *m)
{
    int col_w = 5;   /* minimum */
    for (int c = 0; c < m->n_clusters; c++) {
        int fc = m->cluster[c].first_cpu, lc = m->cluster[c].last_cpu;
        int len = (fc == lc) ? snprintf(NULL, 0, "avg%d",   fc)
                             : snprintf(NULL, 0, "avg%d-%d", fc, lc);
        if (len > col_w) col_w = len;
    }
    return col_w;
}

void cpu_print_freq_header(const cpu_metrics_t *m, int level)
{
    int cw = freq_col_w(m);
    int w  = m->n_clusters * (1 + cw);   /* (1 space + cw data) per cluster */
    if (level == 0) {
        char buf[256];
        make_section_banner(buf, sizeof(buf), w, "cpufreq_mhz");
        printf("%s", buf);          /* no prefix – first section, banner = w = data width */
    } else {
        for (int c = 0; c < m->n_clusters; c++) {
            char lbl[16];
            int fc = m->cluster[c].first_cpu, lc = m->cluster[c].last_cpu;
            if (fc == lc) snprintf(lbl, sizeof(lbl), "avg%d",    fc);
            else          snprintf(lbl, sizeof(lbl), "avg%d-%d",  fc, lc);
            printf(" %*s", cw, lbl);
        }
    }
}

void cpu_print_freq_row(const cpu_metrics_t *m)
{
    int cw = freq_col_w(m);
    for (int c = 0; c < m->n_clusters; c++)
        printf(" %*lu", cw, m->cluster[c].avg_mhz);
}

/* ──────────────────────────────────────────────────────────────────────────
 * Section 3: cpu misc  (2-space gap from freq)
 *   cpu_C (7 chars: " %6.1f") + cpu_psi% (9 chars: " %8.2f")
 *   Banner: make_section_banner(16, "cpu misc") = "<== cpu misc ==>"
 *   Total: 2-gap + 16-banner = 18 chars for level 0
 *          2-gap + 7 + 9     = 18 chars for level 1 / data
 * ─────────────────────────────────────────────────────────────────────────*/
void cpu_print_cpu_thermal_header(const cpu_metrics_t *m, int level)
{
    (void)m;
    if (level == 0) {
        char buf[32];
        make_section_banner(buf, sizeof(buf), 15, "cpu misc");
        printf("  %s", buf);   /* 2-space gap + 15-char banner = 17 = data width */
    } else {
        printf("  %6s %8s", "cpu_C", "cpu_psi%");
    }
}

void cpu_print_cpu_thermal_row(const cpu_metrics_t *m)
{
    if (m->cpu_temp >= 0.0f) printf("  %6.1f", m->cpu_temp);
    else                     printf("  %6s",   "N/A");

    if (m->cpu_psi_avg10 >= 0.0f) printf(" %8.2f", m->cpu_psi_avg10);
    else                           printf(" %8s",   "N/A");
}

/* No-ops (merged into cpu_print_cpu_thermal_*) */
void cpu_print_cpu_psi_header(const cpu_metrics_t *m, int level) { (void)m;(void)level; }
void cpu_print_cpu_psi_row(const cpu_metrics_t *m) { (void)m; }

/* Aggregated view (default) – kept for compatibility */
void cpu_print_group_header(const cpu_metrics_t *m, int level)
{
    if (level == 0) {
        int util_w = m->n_clusters*(9+5+6);
        int freq_w = m->n_clusters*(7+7+7);
        printf("%-*s", util_w, "<--cpu_util-->");
        printf("%-*s", freq_w, "<--cpu_freq-->");
    } else {
        for (int c = 0; c < m->n_clusters; c++) {
            int id = m->cluster[c].policy_id;
            printf(" %7s%d %4s%d %5s%d","us_avg",id,"io",id,"irq",id);
        }
        for (int c = 0; c < m->n_clusters; c++) {
            int id = m->cluster[c].policy_id;
            printf(" %6s%d %6s%d %6s%d","inst",id,"avg",id,"efec",id);
        }
    }
}

void cpu_print_row(const cpu_metrics_t *m)
{
    for (int c = 0; c < m->n_clusters; c++)
        printf(" %8.0f %4.0f %5.0f",
               m->cluster[c].us_avg, m->cluster[c].io, m->cluster[c].irq);
    for (int c = 0; c < m->n_clusters; c++)
        printf(" %6lu %6lu %6s",
               m->cluster[c].inst_mhz, m->cluster[c].avg_mhz, "NA");
}