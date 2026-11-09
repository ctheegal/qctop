#include "mem_metrics.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── /proc/meminfo ────────────────────────────────────────────────────────── */

static void read_meminfo(mem_metrics_t *m)
{
    FILE *fp = fopen("/proc/meminfo", "r");
    if (!fp) return;

    char  line[256];
    unsigned long val;
    unsigned long anon = 0, huge_anon = 0;
    unsigned long swap_total = 0, swap_free = 0;

    while (fgets(line, sizeof(line), fp)) {
        if      (sscanf(line, "MemAvailable: %lu kB",   &val) == 1) m->avail_kb    = val;
        else if (sscanf(line, "AnonPages: %lu kB",       &val) == 1) anon           = val;
        else if (sscanf(line, "AnonHugePages: %lu kB",   &val) == 1) huge_anon      = val;
        else if (sscanf(line, "VmallocUsed: %lu kB",     &val) == 1) m->vmalloc_kb  = val;
        else if (sscanf(line, "Slab: %lu kB",            &val) == 1) m->slab_kb     = val;
        else if (sscanf(line, "Mapped: %lu kB",          &val) == 1) m->mapped_kb   = val;
        else if (sscanf(line, "SwapTotal: %lu kB",       &val) == 1) swap_total      = val;
        else if (sscanf(line, "SwapFree: %lu kB",        &val) == 1) swap_free       = val;
    }
    fclose(fp);

    m->anon_kb      = anon + huge_anon;
    m->swap_used_kb = (swap_total > swap_free) ? swap_total - swap_free : 0;
}

/* ── /proc/vmstat ─────────────────────────────────────────────────────────── */

static void read_vmstat(vmstat_snap_t *s)
{
    memset(s, 0, sizeof(*s));
    FILE *fp = fopen("/proc/vmstat", "r");
    if (!fp) return;

    char               key[64];
    unsigned long long val;
    unsigned long long alloc_normal = 0, alloc_movable = 0;
    int                have_split   = 0;

    while (fscanf(fp, "%63s %llu", key, &val) == 2) {
        if      (!strcmp(key, "pgpgin"))             s->pgpgin         = val;
        else if (!strcmp(key, "pgpgout"))            s->pgpgout        = val;
        else if (!strcmp(key, "pgfault"))            s->pgfault        = val;
        else if (!strcmp(key, "pswpin"))             s->pswpin         = val;
        else if (!strcmp(key, "pswpout"))            s->pswpout        = val;
        else if (!strcmp(key, "allocstall_normal"))  { alloc_normal    = val; have_split = 1; }
        else if (!strcmp(key, "allocstall_movable")) { alloc_movable   = val; have_split = 1; }
        else if (!strcmp(key, "allocstall"))         s->allocstall     = val;
        else if (!strcmp(key, "pgscan_direct"))      s->pgscan_direct  = val;
        else if (!strncmp(key, "pgscan_direct_", 14)) s->pgscan_direct += val;
    }
    fclose(fp);
    if (have_split) s->allocstall = alloc_normal + alloc_movable;
}

/* ── /proc/pressure/memory ───────────────────────────────────────────────── */

static float read_psi_memory(void)
{
    FILE *fp = fopen("/proc/pressure/memory", "r");
    if (!fp) return 0.0f;
    float avg10 = 0.0f;
    fscanf(fp, "some avg10=%f", &avg10);
    fclose(fp);
    return avg10;
}

/* ── /sys/kernel/debug/dma_buf/bufinfo ───────────────────────────────────── */

static int is_hex_digit(char c)
{
    return (c >= '0' && c <= '9') ||
           (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}

static void read_dmabuf_stats(unsigned long *dma_tot_kb, unsigned long *dma_npu_kb)
{
    *dma_tot_kb = 0; *dma_npu_kb = 0;
    FILE *fp = fopen("/sys/kernel/debug/dma_buf/bufinfo", "r");
    if (!fp) return;
    char line[512];
    unsigned long long cur = 0;
    int has_fastrpc = 0, in_obj = 0;
    unsigned long long tot = 0, fpc = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (is_hex_digit(line[0])) {
            if (in_obj) { tot += cur; if (has_fastrpc) fpc += cur; }
            cur = 0; has_fastrpc = 0;
            sscanf(line, "%llx", &cur); in_obj = 1;
        } else if (in_obj && (line[0]==' '||line[0]=='\t')) {
            const char *p = line;
            while (*p==' '||*p=='\t') p++;
            if (strstr(p, ":fastrpc:")) has_fastrpc = 1;
        }
    }
    if (in_obj) { tot += cur; if (has_fastrpc) fpc += cur; }
    fclose(fp);
    *dma_tot_kb = (unsigned long)(tot / 1024ULL);
    *dma_npu_kb = (unsigned long)(fpc / 1024ULL);
}

/* ── /sys/class/kgsl/kgsl/ ───────────────────────────────────────────────── */

static unsigned long read_kgsl_kb(void)
{
    static const char *const pools[] = {
        "/sys/class/kgsl/kgsl/page_alloc",
        "/sys/class/kgsl/kgsl/coherent",
        "/sys/class/kgsl/kgsl/secure", NULL
    };
    unsigned long long total = 0; int found = 0;
    for (int i = 0; pools[i]; i++) {
        FILE *fp = fopen(pools[i], "r"); if (!fp) continue;
        unsigned long long v = 0;
        if (fscanf(fp, "%llu", &v) == 1) { total += v; found = 1; }
        fclose(fp);
    }
    if (found) return (unsigned long)(total / 1024ULL);

    DIR *dir = opendir("/sys/class/kgsl/kgsl/proc");
    if (dir) {
        struct dirent *ent;
        unsigned long long proc_total = 0;
        while ((ent = readdir(dir)) != NULL) {
            if (ent->d_name[0] == '.') continue;
            unsigned long long mapped = 0, unmapped = 0;
            char *path;
            if (asprintf(&path, "/sys/class/kgsl/kgsl/proc/%s/gpumem_mapped", ent->d_name) >= 0) {
                FILE *f = fopen(path, "r");
                if (f) { (void)fscanf(f, "%llu", &mapped); fclose(f); }
                free(path);
            }
            if (asprintf(&path, "/sys/class/kgsl/kgsl/proc/%s/gpumem_unmapped", ent->d_name) >= 0) {
                FILE *f = fopen(path, "r");
                if (f) { (void)fscanf(f, "%llu", &unmapped); fclose(f); }
                free(path);
            }
            proc_total += mapped + unmapped;
        }
        closedir(dir);
        if (proc_total > 0) return (unsigned long)(proc_total / 1024ULL);
    }
    return 0;
}

/* ── /sys/kernel/debug/qcom_stats/ddr_stats ──────────────────────────────── */

static int read_ddr_stats(ddr_state_snap_t *states, int max_states)
{
    FILE *fp = fopen("/sys/kernel/debug/qcom_stats/ddr_stats", "r");
    if (!fp) return 0;
    char line[256]; int n = 0;
    while (n < max_states && fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "DDR Freq ", 9) != 0) continue;
        unsigned long freq = 0; unsigned long long ticks = 0;
        if (sscanf(line, "DDR Freq %luMhz:", &freq) != 1) continue;
        const char *p = strstr(line, "Duration (ticks):");
        if (!p) continue;
        if (sscanf(p, "Duration (ticks):%llu", &ticks) != 1) continue;
        states[n].freq_mhz = freq; states[n].duration = ticks; n++;
    }
    fclose(fp);
    return n;
}

/* ── Public API ───────────────────────────────────────────────────────────── */

void mem_snapshot_begin(mem_snap_t *snap)
{
    snap->ddr.n_states = read_ddr_stats(snap->ddr.states, DDR_MAX_STATES);
    if (snap->ddr.n_states == 0)
        snap->ddr.n_states = read_ddr_stats(snap->ddr.states, DDR_MAX_STATES);
    read_vmstat(&snap->vmstat);
}

/* Read max temp from zones whose type starts with 'prefix' */
static float read_zone_max_temp(const char *prefix)
{
    float max_t = -1.0f;
    DIR *dir = opendir("/sys/class/thermal");
    if (!dir) return max_t;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (strncmp(ent->d_name, "thermal_zone", 12) != 0) continue;
        char path[96];
        snprintf(path, sizeof(path), "/sys/class/thermal/%s/type", ent->d_name);
        FILE *fp = fopen(path, "r");
        if (!fp) continue;
        char type[32] = {0};
        (void)fgets(type, sizeof(type), fp); fclose(fp);
        if (strncmp(type, prefix, strlen(prefix)) != 0) continue;
        snprintf(path, sizeof(path), "/sys/class/thermal/%s/temp", ent->d_name);
        fp = fopen(path, "r");
        if (!fp) continue;
        int raw = 0; (void)fscanf(fp, "%d", &raw); fclose(fp);
        float t = raw / 1000.0f;
        if (t > max_t) max_t = t;
    }
    closedir(dir);
    return max_t;
}

void mem_metrics_read(const mem_snap_t *prev, mem_metrics_t *m)
{
    *m = (mem_metrics_t){0};
    m->ddr_temp = -1.0f;
    read_meminfo(m);

    static unsigned long last_ddr_avg = 0;
    if (prev && prev->ddr.n_states > 0) {
        ddr_state_snap_t curr[DDR_MAX_STATES];
        int n = read_ddr_stats(curr, DDR_MAX_STATES);
        if (n == 0) n = read_ddr_stats(curr, DDR_MAX_STATES);
        if (n > 0) {
            unsigned long long sum_t = 0, sum_ft = 0;
            for (int i = 0; i < prev->ddr.n_states; i++) {
                for (int j = 0; j < n; j++) {
                    if (curr[j].freq_mhz != prev->ddr.states[i].freq_mhz) continue;
                    unsigned long long dt = curr[j].duration - prev->ddr.states[i].duration;
                    sum_t  += dt;
                    sum_ft += dt * (unsigned long long)prev->ddr.states[i].freq_mhz;
                    break;
                }
            }
            if (sum_t > 0) last_ddr_avg = (unsigned long)(sum_ft / sum_t);
        }
        m->ddr_avg_mhz = last_ddr_avg;
    }

    if (prev) {
        vmstat_snap_t curr_vs;
        read_vmstat(&curr_vs);
#define DELTA(f) ((long long)(curr_vs.f) - (long long)(prev->vmstat.f))
        m->d_pagein        = DELTA(pgpgin);
        m->d_pageout       = DELTA(pgpgout);
        m->d_pgfault       = DELTA(pgfault);
        m->d_swpin         = DELTA(pswpin);
        m->d_swpout        = DELTA(pswpout);
        m->d_alloc_stall   = DELTA(allocstall);
        m->d_pgscan_direct = DELTA(pgscan_direct);
#undef DELTA
    }

    m->psi_some_avg10 = read_psi_memory();
    m->ddr_temp       = read_zone_max_temp("ddrss");
    read_dmabuf_stats(&m->dma_tot_kb, &m->dma_npu_kb);
    m->kgsl_kb = read_kgsl_kb();
}

/* ── printing helpers ─────────────────────────────────────────────────────── */

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

/* KB→MB conversion helper */
static unsigned long kb_to_mb(unsigned long kb) { return kb / 1024UL; }

/* ──────────────────────────────────────────────────────────────────────────
 * Static memory section (4-space gap from CPU, values in MB)
 *
 * Columns: avail anon vmalloc slab mapped swap dma_total dma_npu kgsl
 * Field widths (MB values are ~10× smaller than KB):
 *   avail(8) anon(6) vmalloc(8) slab(6) mapped(6) swap(6) dma_total(10) dma_npu(8) kgsl(6)
 * Row total = (1+8)+(1+6)+(1+8)+(1+6)+(1+6)+(1+6)+(1+10)+(1+8)+(1+6) = 72 chars
 * Banner: 73 chars (1-char overhang), label "memory_mb"
 * ─────────────────────────────────────────────────────────────────────────*/
void mem_print_ram_header(int level)
{
    if (level == 0) {
        char buf[128];
        make_section_banner(buf, sizeof(buf), 72, "memory_mb");
        printf("    %s", buf);   /* 4-space gap before major section */
    } else {
        printf("    %8s %6s %8s %6s %6s %6s %10s %8s %6s",
               "avail", "anon", "vmalloc", "slab",
               "mapped", "swap", "dma_total", "dma_npu", "kgsl");
    }
}

void mem_print_ram_row(const mem_metrics_t *m)
{
    printf("    %8lu %6lu %8lu %6lu %6lu %6lu %10lu %8lu %6lu",
           kb_to_mb(m->avail_kb),   kb_to_mb(m->anon_kb),
           kb_to_mb(m->vmalloc_kb), kb_to_mb(m->slab_kb),
           kb_to_mb(m->mapped_kb),  kb_to_mb(m->swap_used_kb),
           kb_to_mb(m->dma_tot_kb), kb_to_mb(m->dma_npu_kb),
           kb_to_mb(m->kgsl_kb));
}

/* ──────────────────────────────────────────────────────────────────────────
 * Activity (delta) section (2-space gap from memory)
 *
 * Columns: pg-in pg-out pgflt swp-in swp-out allocstall pgscan mem_psi%
 * Field widths:
 *   pg-in(7) pg-out(7) pgflt(7) swp-in(7) swp-out(7) allocstall(10) pgscan(7) mem_psi%(8)
 * Row total = 2 + (1+7)×5 + (1+10) + (1+7) + (1+8) = 2+40+11+8+9 = 70 chars
 * Banner: make_section_banner(70, "mem-activity")
 * ─────────────────────────────────────────────────────────────────────────*/
void mem_print_activity_header(int level)
{
    /* Data columns total = 5×(1+7) + (1+10) + (1+7) + (1+8) = 68 chars.
     * 4-space gap prefix; banner = 68 chars to exactly span data columns. */
    if (level == 0) {
        char buf[128];
        make_section_banner(buf, sizeof(buf), 67, "mem-activity");
        printf("    %s", buf);   /* 4-space gap */
    } else {
        printf("    %7s %7s %7s %7s %7s %10s %7s %8s",
               "pg-in", "pg-out", "pgflt",
               "swp-in", "swp-out", "allocstall", "pgscan", "mem_psi%");
    }
}

void mem_print_activity_row(const mem_metrics_t *m)
{
    printf("    %7lld %7lld %7lld %7lld %7lld %10lld %7lld %8.2f",
           m->d_pagein, m->d_pageout, m->d_pgfault,
           m->d_swpin,  m->d_swpout,
           m->d_alloc_stall, m->d_pgscan_direct,
           m->psi_some_avg10);
}

/* No-op (mem_psi% merged into activity row) */
void mem_print_psi_header(int level) { (void)level; }
void mem_print_psi_row(const mem_metrics_t *m) { (void)m; }

/* ──────────────────────────────────────────────────────────────────────────
 * DDR section (4-space gap, minor banner)
 * Section width = 1+11 = 12 chars for " %11s" / " %11lu"
 * Banner: printf("    %-12s", "<== ddr ==>") left-justified in 12
 * ─────────────────────────────────────────────────────────────────────────*/
void mem_print_ddr_header(int level)
{
    /* Section = 4-gap + avg_mhz(11) + ddr_C(8) = 23 chars.
     * Banner spans all 19 chars of the data section (without the gap). */
    if (level == 0) {
        char buf[32];
        make_section_banner(buf, sizeof(buf), 19, "ddr");
        printf("    %s", buf);   /* 4-gap + 19-char banner = 23 chars */
    } else {
        printf("    %11s %7s", "avg_mhz", "ddr_C");
    }
}

void mem_print_ddr_row(const mem_metrics_t *m)
{
    printf("    %11lu", m->ddr_avg_mhz);
    if (m->ddr_temp >= 0.0f) printf(" %7.1f", m->ddr_temp);
    else                     printf(" %7s",   "N/A");
}

/* ── Combined (default all-flags view) ───────────────────────────────────── */

void mem_print_header(int level)
{
    if (level == 0) {
        char buf[32];
        make_section_banner(buf, sizeof(buf), 12, "ddr");
        printf("%s  ", buf);
        char buf2[128];
        make_section_banner(buf2, sizeof(buf2), 73, "memory_mb");
        printf("%s", buf2);
    } else {
        printf(" %11s  %8s %6s %8s %6s %6s %6s %10s %8s %6s",
               "avg_mhz",
               "avail", "anon", "vmalloc", "slab",
               "mapped", "swap", "dma_total", "dma_npu", "kgsl");
    }
}

void mem_print_row(const mem_metrics_t *m)
{
    printf(" %11lu  %8lu %6lu %8lu %6lu %6lu %6lu %10lu %8lu %6lu",
           m->ddr_avg_mhz,
           kb_to_mb(m->avail_kb), kb_to_mb(m->anon_kb),
           kb_to_mb(m->vmalloc_kb), kb_to_mb(m->slab_kb),
           kb_to_mb(m->mapped_kb), kb_to_mb(m->swap_used_kb),
           kb_to_mb(m->dma_tot_kb), kb_to_mb(m->dma_npu_kb),
           kb_to_mb(m->kgsl_kb));
}
