#include "hw_info.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>
#include <sys/utsname.h>

/* ════════════════════════════════════════════════════════════════════════
 * Helpers
 * ════════════════════════════════════════════════════════════════════════ */

static void read_first_line(const char *path, char *buf, int len)
{
    buf[0] = '\0';
    FILE *fp = fopen(path, "r");
    if (!fp) return;
    if (fgets(buf, len, fp)) {
        char *nl = strchr(buf, '\n');
        if (nl) *nl = '\0';
    }
    fclose(fp);
}

static unsigned long long read_ull(const char *path)
{
    FILE *fp = fopen(path, "r");
    if (!fp) return 0;
    unsigned long long v = 0;
    if (fscanf(fp, "%llu", &v) != 1) v = 0;
    fclose(fp);
    return v;
}

/* ════════════════════════════════════════════════════════════════════════
 * Software info
 * ════════════════════════════════════════════════════════════════════════ */

static void print_software(void)
{
    printf("  Software\n");

    FILE *fp = fopen("/etc/os-release", "r");
    if (fp) {
        char line[256], name[128]="", version[128]="", build_id[128]="";
        while (fgets(line, sizeof(line), fp)) {
            if      (sscanf(line, "NAME=\"%127[^\"]\"",       name)     == 1) {}
            else if (sscanf(line, "VERSION=\"%127[^\"]\"",    version)  == 1) {}
            else if (sscanf(line, "VERSION_ID=\"%127[^\"]\"", version)  == 1) {}
            else if (sscanf(line, "BUILD_ID=%127s",            build_id) == 1) {}
        }
        fclose(fp);
        if (name[0])
            printf("    %-14s: %s%s%s\n", "OS", name,
                   version[0] ? " " : "", version[0] ? version : "");
        if (build_id[0])
            printf("    %-14s: %s\n", "Build", build_id);
    }

    struct utsname u;
    if (uname(&u) == 0)
        printf("    %-14s: %s %s\n", "Kernel", u.sysname, u.release);
}

/* ════════════════════════════════════════════════════════════════════════
 * CPU info
 * ════════════════════════════════════════════════════════════════════════ */

static const struct { unsigned part; const char *name; } arm_parts[] = {
    { 0xD03, "Cortex-A53"  }, { 0xD04, "Cortex-A35"  },
    { 0xD05, "Cortex-A55"  }, { 0xD07, "Cortex-A57"  },
    { 0xD08, "Cortex-A72"  }, { 0xD09, "Cortex-A73"  },
    { 0xD0A, "Cortex-A75"  }, { 0xD0B, "Cortex-A76"  },
    { 0xD0D, "Cortex-A77"  }, { 0xD41, "Cortex-A78"  },
    { 0xD44, "Cortex-X1"   }, { 0xD46, "Cortex-A510" },
    { 0xD47, "Cortex-A710" }, { 0xD48, "Cortex-X2"   },
    { 0xD49, "Cortex-A510" }, { 0xD4B, "Cortex-A78C" },
    { 0xD4D, "Cortex-A510" }, { 0xD4E, "Cortex-X3"   },
    { 0xD80, "Cortex-A520" }, { 0xD84, "Cortex-X4"   },
    { 0xD85, "Cortex-A720" }, { 0xD87, "Cortex-A725" },
    { 0, NULL }
};

static const char *arm_cpu_name(unsigned part)
{
    for (int i = 0; arm_parts[i].name; i++)
        if (arm_parts[i].part == part) return arm_parts[i].name;
    return "unknown";
}

static unsigned cpu_part(int cpu_idx)
{
    FILE *fp = fopen("/proc/cpuinfo", "r");
    if (!fp) return 0;
    char line[256]; int cur = -1; unsigned part = 0;
    while (fgets(line, sizeof(line), fp)) {
        int idx;
        if (sscanf(line, "processor : %d", &idx) == 1) { cur = idx; continue; }
        if (cur == cpu_idx) {
            unsigned v;
            if (sscanf(line, "CPU part : %x", &v) == 1) { part = v; break; }
        }
    }
    fclose(fp);
    return part;
}

static void print_cpu(void)
{
    printf("  CPU\n");

    DIR *dir = opendir("/sys/devices/system/cpu/cpufreq");
    if (!dir) { printf("    (cpufreq not available)\n"); return; }

    int pids[64], np = 0;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL && np < 64) {
        int pid = -1;
        if (sscanf(ent->d_name, "policy%d", &pid) == 1 && pid >= 0)
            pids[np++] = pid;
    }
    closedir(dir);

    for (int i = 0; i < np-1; i++)
        for (int j = i+1; j < np; j++)
            if (pids[j] < pids[i]) { int t=pids[i]; pids[i]=pids[j]; pids[j]=t; }

    for (int c = 0; c < np; c++) {
        char path[128];
        snprintf(path, sizeof(path),
                 "/sys/devices/system/cpu/cpufreq/policy%d/related_cpus", pids[c]);

        int first_cpu = pids[c], last_cpu = pids[c], n_cpus = 0;
        { FILE *fp = fopen(path, "r");
          if (fp) {
              if (fscanf(fp, "%d", &first_cpu) != 1)
                  first_cpu = pids[c];
              fclose(fp);
          } }
        { FILE *fp = fopen(path, "r"); int x;
          if (fp) { while (fscanf(fp, "%d", &x)==1){ n_cpus++; last_cpu=x; } fclose(fp); } }

        snprintf(path, sizeof(path),
                 "/sys/devices/system/cpu/cpufreq/policy%d/cpuinfo_max_freq", pids[c]);
        unsigned long long max_khz = read_ull(path);

        unsigned part = cpu_part(first_cpu);
        const char *model = arm_cpu_name(part);

        /* Use %-16s for the key — same field width as Memory/GPU/Storage sections
         * so colons align across all sections.                                  */
        char key[24];
        if (first_cpu == last_cpu)
            snprintf(key, sizeof(key), "CPU %d", first_cpu);
        else
            snprintf(key, sizeof(key), "CPUs %d-%d", first_cpu, last_cpu);
        printf("    %-16s: %d \xc3\x97 %-14s @ %.2f GHz\n",
               key, n_cpus, model, max_khz / 1e6);
    }

    /* Cache: show ONLY if sysfs "size" file is readable for at least one index.
     * If no size file exists, skip the "Cache" header entirely.              */
    {
        int any_size = 0;
        char path[128], sz[16];
        for (int ci = 0; ci < 8 && !any_size; ci++) {
            snprintf(path, sizeof(path),
                     "/sys/devices/system/cpu/cpu0/cache/index%d/size", ci);
            read_first_line(path, sz, sizeof(sz));
            if (sz[0]) any_size = 1;
        }
        if (any_size) {
            printf("    Cache\n");
            for (int cidx = 0; cidx < 8; cidx++) {
                char val[32], type[32]={0}, shared[64]={0}, sz2[16]={0};

                snprintf(path, sizeof(path),
                         "/sys/devices/system/cpu/cpu0/cache/index%d/level", cidx);
                read_first_line(path, val, sizeof(val));
                if (!val[0]) break;
                int level = atoi(val);

                snprintf(path, sizeof(path),
                         "/sys/devices/system/cpu/cpu0/cache/index%d/type", cidx);
                read_first_line(path, type, sizeof(type));

                snprintf(path, sizeof(path),
                         "/sys/devices/system/cpu/cpu0/cache/index%d/shared_cpu_list", cidx);
                read_first_line(path, shared, sizeof(shared));
                int is_shared = (strchr(shared,'-')||strchr(shared,',')) &&
                                !(shared[0]=='0' && shared[1]=='\0');

                snprintf(path, sizeof(path),
                         "/sys/devices/system/cpu/cpu0/cache/index%d/size", cidx);
                read_first_line(path, sz2, sizeof(sz2));

                if (sz2[0])
                    printf("      L%d %-12s: %s  (%s)\n",
                           level, type, sz2, is_shared ? "shared" : "per-core");
            }
        }
    }
}

/* ════════════════════════════════════════════════════════════════════════
 * Memory info
 * ════════════════════════════════════════════════════════════════════════ */

static void print_memory(void)
{
    printf("  Memory\n");

    unsigned long mem_total_kb = 0;
    FILE *fp = fopen("/proc/meminfo", "r");
    if (fp) {
        char line[128]; unsigned long v;
        while (fgets(line, sizeof(line), fp))
            if (sscanf(line, "MemTotal: %lu kB", &v) == 1) mem_total_kb = v;
        fclose(fp);
    }

    unsigned long long sys_ram_kb = 0;
    fp = fopen("/proc/iomem", "r");
    if (fp) {
        char line[256];
        while (fgets(line, sizeof(line), fp)) {
            if (!strstr(line, "System RAM")) continue;
            unsigned long long s=0, e=0;
            if (sscanf(line, " %llx-%llx", &s, &e) == 2)
                sys_ram_kb += (e - s + 1) / 1024ULL;
        }
        fclose(fp);
    }

    unsigned long long kernel_kb = 0;
    fp = fopen("/proc/iomem", "r");
    if (fp) {
        char line[256];
        while (fgets(line, sizeof(line), fp)) {
            if (!strstr(line, "Kernel ")) continue;
            unsigned long long s=0, e=0;
            if (sscanf(line, " %llx-%llx", &s, &e) == 2)
                kernel_kb += (e - s + 1) / 1024ULL;
        }
        fclose(fp);
    }

    /* Primary: scan all "DDR Freq XXXMhz:" lines in ddr_stats and take max */
    unsigned long ddr_fmax_mhz = 0;
    {
        FILE *ff = fopen("/sys/kernel/debug/qcom_stats/ddr_stats", "r");
        if (ff) {
            char line[256];
            while (fgets(line, sizeof(line), ff)) {
                if (strncmp(line, "DDR Freq ", 9) != 0) continue;
                unsigned long freq = 0;
                if (sscanf(line, "DDR Freq %luMhz:", &freq) == 1)
                    if (freq > ddr_fmax_mhz) ddr_fmax_mhz = freq;
            }
            fclose(ff);
        }
    }
    /* Fallback: devfreq available_frequencies */
    if (!ddr_fmax_mhz) {
        DIR *dir = opendir("/sys/class/devfreq");
        if (dir) {
            struct dirent *ent;
            while ((ent = readdir(dir)) != NULL) {
                const char *n = ent->d_name;
                if (!strstr(n,"ddr") && !strstr(n,"mc")) continue;
                if (strstr(n,"bw")) continue;
                char path[512];
                int written = snprintf(path, sizeof(path),
                                       "/sys/class/devfreq/%s/available_frequencies", n);
                if (written < 0 || written >= (int)sizeof(path)) continue;
                FILE *ff = fopen(path, "r");
                if (!ff) continue;
                unsigned long long v;
                while (fscanf(ff, "%llu", &v) == 1) {
                    unsigned long mhz = (v>=1000000ULL)?(unsigned long)(v/1000000ULL):(unsigned long)v;
                    if (mhz > ddr_fmax_mhz) ddr_fmax_mhz = mhz;
                }
                fclose(ff);
            }
            closedir(dir);
        }
    }

    double mem_total_gb = mem_total_kb / (1024.0*1024.0);
    double sys_ram_gb   = sys_ram_kb   / (1024.0*1024.0);
    double kernel_mb    = kernel_kb / 1024.0;
    double carveout_mb  = (sys_ram_kb - mem_total_kb) / 1024.0 - kernel_mb;

    /* Format value+unit into a fixed-width string so "(" aligns across all lines */
    char sys_ram_s[16], memtotal_s[16], kernel_s[16], carveout_s[16];
    snprintf(sys_ram_s,  sizeof(sys_ram_s),  "%.2f GB",  sys_ram_gb);
    snprintf(memtotal_s, sizeof(memtotal_s), "%.2f GB",  mem_total_gb);
    snprintf(kernel_s,   sizeof(kernel_s),   "%.0f MB",  kernel_mb);
    snprintf(carveout_s, sizeof(carveout_s), "%.0f MB",  carveout_mb > 0 ? carveout_mb : 0.0);

    printf("    %-16s: %-10s (sum of /proc/iomem System RAM)\n", "System RAM",    sys_ram_s);
    printf("    %-16s: %-10s (Linux-managed)\n",                 "MemTotal",      memtotal_s);
    printf("    %-16s: %-10s (Kernel code+data)\n",              "Kernel static", kernel_s);
    printf("    %-16s: %-10s (System RAM \xe2\x88\x92 MemTotal \xe2\x88\x92 Kernel)\n",
           "Carveouts", carveout_s);

    if (ddr_fmax_mhz > 0)
        printf("    %-16s: Fmax = %lu MHz\n", "DDR", ddr_fmax_mhz);
}

/* ════════════════════════════════════════════════════════════════════════
 * GPU info
 * ════════════════════════════════════════════════════════════════════════ */

static void print_gpu(void)
{
    printf("  GPU\n");

    char gpu_name[64] = "";
    char path[128];
    snprintf(path, sizeof(path), "/sys/class/kgsl/kgsl-3d0/gpu_model");
    read_first_line(path, gpu_name, sizeof(gpu_name));
    if (!gpu_name[0]) {
        snprintf(path, sizeof(path),
                 "/sys/bus/platform/devices/3d00000.gpu/of_node/compatible");
        read_first_line(path, gpu_name, sizeof(gpu_name));
    }
    if (!gpu_name[0]) snprintf(gpu_name, sizeof(gpu_name), "Adreno (Qualcomm)");

    unsigned long gpu_fmax_mhz = 0;
    DIR *dir = opendir("/sys/class/devfreq");
    if (dir) {
        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL) {
            if (!strstr(ent->d_name,"kgsl") && !strstr(ent->d_name,"gpu")) continue;
            int written = snprintf(path, sizeof(path),
                                   "/sys/class/devfreq/%s/available_frequencies", ent->d_name);
            if (written < 0 || written >= (int)sizeof(path)) continue;
            FILE *ff = fopen(path, "r");
            if (!ff) continue;
            unsigned long long v;
            while (fscanf(ff, "%llu", &v) == 1) {
                unsigned long mhz = (v>=1000000ULL)?(unsigned long)(v/1000000ULL):(unsigned long)v;
                if (mhz > gpu_fmax_mhz) gpu_fmax_mhz = mhz;
            }
            fclose(ff);
        }
        closedir(dir);
    }
    if (!gpu_fmax_mhz) {
        unsigned long long v = read_ull("/sys/class/kgsl/kgsl-3d0/clock_mhz");
        if (v) gpu_fmax_mhz = (unsigned long)v;
    }

    if (gpu_fmax_mhz) printf("    %-16s: Fmax = %lu MHz\n", gpu_name, gpu_fmax_mhz);
    else              printf("    %-16s: (freq unavailable)\n", gpu_name);
}

/* ════════════════════════════════════════════════════════════════════════
 * Storage info
 * ════════════════════════════════════════════════════════════════════════ */

static void print_storage(void)
{
    printf("  Storage\n");

    /* Find first whole-disk from /proc/partitions */
    char disk_dev[64] = "";
    FILE *fp = fopen("/proc/partitions", "r");
    if (fp) {
        char line[256];
        if (!fgets(line, sizeof(line), fp)) { fclose(fp); return; } /* skip header */
        if (!fgets(line, sizeof(line), fp)) { fclose(fp); return; }
        while (fgets(line, sizeof(line), fp)) {
            unsigned maj, min; unsigned long long blks; char name[64];
            if (sscanf(line, "%u %u %llu %63s", &maj, &min, &blks, name) != 4) continue;
            if (blks < 1000000) continue;
            int is_whole = 0;
            if (strncmp(name,"sd",2)==0 || strncmp(name,"vd",2)==0 ||
                strncmp(name,"hd",2)==0) {
                int n = (int)strlen(name);
                is_whole = (name[n-1]<'0' || name[n-1]>'9');
            } else if (strncmp(name,"mmcblk",6)==0) {
                is_whole = !strchr(name+6,'p');
            } else if (strncmp(name,"nvme",4)==0) {
                const char *p = strstr(name,"n");
                is_whole = p && !strchr(p+1,'p');
            }
            if (is_whole) { snprintf(disk_dev, sizeof(disk_dev), "%s", name); break; }
        }
        fclose(fp);
    }
    if (!disk_dev[0]) { printf("    (not detected)\n"); return; }

    char path[256];
    snprintf(path, sizeof(path), "/sys/class/block/%s/size", disk_dev);
    unsigned long long sectors = read_ull(path);
    double total_gb = (double)sectors * 512.0 / (1024.0*1024.0*1024.0);

    struct statvfs st;
    double avail_gb = 0.0;
    if (statvfs("/", &st) == 0)
        avail_gb = (double)st.f_bavail * (double)st.f_frsize / (1024.0*1024.0*1024.0);

    unsigned long stor_fmax_mhz = 0;
    DIR *dir = opendir("/sys/class/devfreq");
    if (dir) {
        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL) {
            const char *n = ent->d_name;
            if (!strstr(n,"ufs") && !strstr(n,"emmc") && !strstr(n,"mmc")) continue;
            if (strlen(n) >= 218) continue;
            char devfreq_path[256];
            int written = snprintf(devfreq_path, sizeof(devfreq_path),
                                   "/sys/class/devfreq/%s/available_frequencies", n);
            if (written < 0 || written >= (int)sizeof(devfreq_path)) continue;
            FILE *ff = fopen(devfreq_path, "r");
            if (!ff) continue;
            unsigned long long v;
            while (fscanf(ff, "%llu", &v) == 1) {
                unsigned long mhz = (v>=1000000ULL)?(unsigned long)(v/1000000ULL):(unsigned long)v;
                if (mhz > stor_fmax_mhz) stor_fmax_mhz = mhz;
            }
            fclose(ff);
        }
        closedir(dir);
    }

    const char *stor_type = strstr(disk_dev,"mmcblk") ? "eMMC/SD"
                          : strstr(disk_dev,"nvme")   ? "NVMe" : "UFS";

    printf("    %-16s: total=%.0f GB  avail=%.1f GB  (/dev/%s)\n",
           stor_type, total_gb, avail_gb, disk_dev);
    if (stor_fmax_mhz)
        printf("    %-16s: Fmax=%lu MHz\n", "Storage clock", stor_fmax_mhz);
}

/* ════════════════════════════════════════════════════════════════════════
 * Public entry point
 * ════════════════════════════════════════════════════════════════════════ */

void print_hw_info(void)
{
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════╗\n");
    printf("║              qctop  –  Device Information                ║\n");
    printf("╠══════════════════════════════════════════════════════════╣\n");
    print_software();
    printf("╠══════════════════════════════════════════════════════════╣\n");
    print_cpu();
    printf("╠══════════════════════════════════════════════════════════╣\n");
    print_memory();
    printf("╠══════════════════════════════════════════════════════════╣\n");
    print_gpu();
    printf("╠══════════════════════════════════════════════════════════╣\n");
    print_storage();
    printf("╚══════════════════════════════════════════════════════════╝\n");
    printf("\n");
}