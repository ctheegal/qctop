/*
 * qctop – Qualcomm SoC performance profiler
 */

#include "cpu_metrics.h"
#include "gpu_metrics.h"
#include "hw_info.h"
#include "mem_metrics.h"
#include "net_metrics.h"
#include "npu_metrics.h"
#include "storage_metrics.h"
#include "thermal_metrics.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define F_CPU     (1 << 0)
#define F_GPU     (1 << 1)
#define F_MEM     (1 << 2)
#define F_DDR     (1 << 3)
#define F_STORAGE (1 << 4)
#define F_THERMAL (1 << 5)
#define F_NET     (1 << 6)
#define F_NPU     (1 << 7)
#define F_ALL     (F_CPU|F_GPU|F_MEM|F_DDR|F_STORAGE|F_THERMAL|F_NET|F_NPU)

static volatile int g_stop = 0;
static void on_signal(int sig) { (void)sig; g_stop = 1; }

static void sleep_ms(int ms)
{
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s [OPTIONS]\n"
        "\n"
        "Interval / loop:\n"
        "  -i <ms>     Sampling interval in ms (default: 1000, min: 100)\n"
        "  -n <N>      Exit after N samples\n"
        "  -1          Single sample then exit\n"
        "  -l          Loop continuously until Ctrl-C\n"
        "  -h          This help\n"
        "\n"
        "Subsystem filters (default: all):\n"
        "  --cpu       Per-core CPU util%% + avg freq per cluster + cpu misc\n"
        "  --gpu       GPU util%%, freq (MHz), temp\n"
        "  --mem       RAM (MB) + vmstat activity + allocstall + mem_psi%%\n"
        "  --ddr       DDR time-weighted avg frequency (MHz)\n"
        "  --storage   Storage clock, rd/wr MB, avail MB, io_psi%%\n"
        "  --thermal   Per-category thermal (aggregate)\n"
        "  --net       Network total rx_mb / tx_mb\n"
        "  --npu       NSP thermal temperature (nsp_C)\n"
        "\n"
        "Examples:\n"
        "  %s                       All metrics, 1 s interval\n"
        "  %s --cpu --mem -i 500\n"
        "\n", prog, prog, prog);
}

static void print_header(unsigned int             flags,
                         int                      thermal_verbose,
                         int                      net_verbose,
                         const cpu_metrics_t     *cpu_tmpl,
                         const net_metrics_t     *net_tmpl,
                         const thermal_metrics_t *therm,
                         int                      npu_on)
{
    printf("%12s", "");
    if (flags & F_CPU) {
        cpu_print_freq_header(cpu_tmpl, 0);
        cpu_print_cpu_thermal_header(cpu_tmpl, 0);
        cpu_print_cpu_psi_header(cpu_tmpl, 0);
        cpu_print_cores_header(cpu_tmpl, 0);
    }
    if (flags & F_MEM) {
        mem_print_ram_header(0);
        mem_print_activity_header(0);
        mem_print_psi_header(0);
    }
    if (flags & F_DDR)     mem_print_ddr_header(0);
    if (flags & F_GPU)     gpu_print_header(0);
    if (flags & F_STORAGE) storage_print_header(0);
    if (flags & F_NET) {
        if (net_verbose) net_print_header(net_tmpl, 0);
        else             net_print_total_header(0);
    }
    if (flags & F_THERMAL) {
        if (thermal_verbose) thermal_print_zones_header(therm, 0);
        else                 thermal_print_header(therm, 0);
    }
    if ((flags & F_NPU) && npu_on) npu_print_header(0);
    printf("\n");

    printf("%12s", "uptime");
    if (flags & F_CPU) {
        cpu_print_freq_header(cpu_tmpl, 1);
        cpu_print_cpu_thermal_header(cpu_tmpl, 1);
        cpu_print_cpu_psi_header(cpu_tmpl, 1);
        cpu_print_cores_header(cpu_tmpl, 1);
    }
    if (flags & F_MEM) {
        mem_print_ram_header(1);
        mem_print_activity_header(1);
        mem_print_psi_header(1);
    }
    if (flags & F_DDR)     mem_print_ddr_header(1);
    if (flags & F_GPU)     gpu_print_header(1);
    if (flags & F_STORAGE) storage_print_header(1);
    if (flags & F_NET) {
        if (net_verbose) net_print_header(net_tmpl, 1);
        else             net_print_total_header(1);
    }
    if (flags & F_THERMAL) {
        if (thermal_verbose) thermal_print_zones_header(therm, 1);
        else                 thermal_print_header(therm, 1);
    }
    if ((flags & F_NPU) && npu_on) npu_print_header(1);
    printf("\n");
    fflush(stdout);
}

static void one_sample(unsigned int       flags,
                       int                thermal_verbose,
                       int                net_verbose,
                       cpu_snap_t        *cpu_snap,
                       disk_raw_t         disk_snap[QCTOP_MAX_DISKS],
                       char               disk_names[QCTOP_MAX_DISKS][32],
                       int               *n_disks,
                       net_snap_t        *net_snap,
                       thermal_metrics_t *therm,
                       int                interval_ms,
                       int                npu_on)
{
    cpu_snapshot_begin(cpu_snap);
    mem_snap_t mem_snap = {0};
    if (flags & (F_MEM | F_DDR)) mem_snapshot_begin(&mem_snap);
    if (flags & F_STORAGE) *n_disks = storage_snapshot_begin(disk_snap, disk_names);
    if (flags & F_NET)     net_snapshot_begin(net_snap);

    sleep_ms(interval_ms);

    cpu_metrics_t     cpu  = {0};
    gpu_metrics_t     gpu  = {0};
    mem_metrics_t     mem  = {0};
    storage_metrics_t stor = {0};
    net_metrics_t     net  = {0};
    npu_metrics_t     npu  = {0};

    cpu_metrics_alloc(cpu_snap, &cpu);
    cpu_snapshot_end(cpu_snap, &cpu);

    if (flags & F_GPU)             gpu_metrics_read(&gpu);
    if (flags & (F_MEM | F_DDR))   mem_metrics_read(&mem_snap, &mem);
    if (flags & F_STORAGE)         storage_snapshot_end(disk_snap, *n_disks,
                                                         interval_ms, &stor);
    if (flags & F_NET)             net_snapshot_end(net_snap, &net);
    if (flags & F_THERMAL)         thermal_read(therm);
    if ((flags & F_NPU) && npu_on) npu_metrics_read(&npu);

    struct timespec boot_ts;
    clock_gettime(CLOCK_BOOTTIME, &boot_ts);
    printf("%12.3f",
           (double)boot_ts.tv_sec + (double)boot_ts.tv_nsec / 1e9);

    if (flags & F_CPU) {
        cpu_print_freq_row(&cpu);
        cpu_print_cpu_thermal_row(&cpu);
        cpu_print_cpu_psi_row(&cpu);
        cpu_print_cores_row(&cpu);
    }
    if (flags & F_MEM) {
        mem_print_ram_row(&mem);
        mem_print_activity_row(&mem);
        mem_print_psi_row(&mem);
    }
    if (flags & F_DDR)     mem_print_ddr_row(&mem);
    if (flags & F_GPU)     gpu_print_row(&gpu);
    if (flags & F_STORAGE) storage_print_row(&stor);
    if (flags & F_NET) {
        if (net_verbose) net_print_row(&net);
        else             net_print_total_row(&net);
    }
    if (flags & F_THERMAL) {
        if (thermal_verbose) thermal_print_zones_row(therm);
        else                 thermal_print_row(therm);
    }
    if ((flags & F_NPU) && npu_on) npu_print_row(&npu);

    printf("\n");
    fflush(stdout);
    cpu_metrics_free(&cpu);
}

int main(int argc, char *argv[])
{
    int          interval_ms     = 1000;
    int          max_count       = 0;
    unsigned int flags           = 0;
    int          thermal_verbose = 0;
    int          net_verbose     = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            usage(argv[0]); return 0;
        } else if (!strcmp(argv[i], "-1")) {
            max_count = 1;
        } else if (!strcmp(argv[i], "-l")) {
            max_count = 0;
        } else if (!strcmp(argv[i], "-i") && i + 1 < argc) {
            interval_ms = atoi(argv[++i]);
            if (interval_ms < 100) {
                interval_ms = 100;
                fprintf(stderr, "Warning: interval clamped to 100 ms\n");
            }
        } else if (!strcmp(argv[i], "-n") && i + 1 < argc) {
            max_count = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--cpu"))     flags |= F_CPU;
        else if (!strcmp(argv[i], "--gpu"))        flags |= F_GPU;
        else if (!strcmp(argv[i], "--mem"))        flags |= F_MEM;
        else if (!strcmp(argv[i], "--ddr"))        flags |= F_DDR;
        else if (!strcmp(argv[i], "--storage"))    flags |= F_STORAGE;
        else if (!strcmp(argv[i], "--thermal")) { flags |= F_THERMAL; thermal_verbose = 1; }
        else if (!strcmp(argv[i], "--net"))      { flags |= F_NET; net_verbose = 1; }
        else if (!strcmp(argv[i], "--npu"))        flags |= F_NPU;
        else {
            fprintf(stderr, "Unknown option: %s\n\n", argv[i]);
            usage(argv[0]); return 1;
        }
    }

    if (flags == 0) flags = F_ALL;

    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);

    /* ── Print hardware/software information banner ── */
    print_hw_info();

    int npu_on = (flags & F_NPU) ? npu_init() : 0;

    thermal_metrics_t therm = {0};
    if (flags & F_THERMAL) thermal_init(&therm);

    cpu_snap_t cpu_snap = {0};
    cpu_detect_clusters(&cpu_snap);

    cpu_metrics_t cpu_tmpl = {0};
    cpu_metrics_alloc(&cpu_snap, &cpu_tmpl);

    net_snap_t net_snap = {0};
    if (flags & F_NET) net_snapshot_begin(&net_snap);

    net_metrics_t net_tmpl = {0};
    net_tmpl.n_ifaces = net_snap.n_ifaces;
    for (int i = 0; i < net_snap.n_ifaces; i++)
        strncpy(net_tmpl.iface[i].name, net_snap.iface[i].name, 15);

    disk_raw_t disk_snap[QCTOP_MAX_DISKS];
    char       disk_names[QCTOP_MAX_DISKS][32];
    int        n_disks = 0;

    print_header(flags, thermal_verbose, net_verbose,
                 &cpu_tmpl, &net_tmpl, &therm, npu_on);

    int count = 0;
    while (!g_stop && (max_count == 0 || count < max_count)) {
        one_sample(flags, thermal_verbose, net_verbose,
                   &cpu_snap, disk_snap, disk_names, &n_disks,
                   &net_snap, &therm, interval_ms, npu_on);
        count++;
    }

    cpu_metrics_free(&cpu_tmpl);
    cpu_snap_free(&cpu_snap);
    if (flags & F_THERMAL) thermal_free(&therm);
    npu_shutdown();
    return 0;
}
