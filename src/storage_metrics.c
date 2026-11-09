#include "storage_metrics.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>

#define SECTOR_BYTES 512ULL

/* ── rootfs block device detection ──────────────────────────────────────── */
/*
 * Convert a partition device path to its parent whole-disk name:
 *   /dev/sda2     → sda     (strip trailing digits)
 *   /dev/mmcblk0p2→ mmcblk0 (strip 'p' + digits)
 *   /dev/nvme0n1p1→ nvme0n1 (strip 'p' + digits)
 *   /dev/sda      → sda     (already a whole disk, no change)
 */
static void partition_to_disk(const char *dev_path, char *dst, int dst_len)
{
    dst[0] = '\0';
    const char *name = strrchr(dev_path, '/');
    name = name ? name + 1 : dev_path;

    char tmp[64];
    strncpy(tmp, name, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';

    if (strncmp(tmp, "mmcblk", 6) == 0) {
        /* mmcblk0p2 → mmcblk0 : find 'p' after the disk-number digits */
        char *p = tmp + 6;
        while (*p >= '0' && *p <= '9') p++;
        *p = '\0';                           /* cut at 'p<partition>' */
    } else if (strncmp(tmp, "nvme", 4) == 0) {
        /* nvme0n1p1 → nvme0n1 : strip 'p<N>' suffix only */
        int n = (int)strlen(tmp);
        while (n > 0 && tmp[n-1] >= '0' && tmp[n-1] <= '9') n--;
        if (n > 0 && tmp[n-1] == 'p') tmp[n-1] = '\0';
    } else {
        /* sd*, vd*, hd*, etc. : strip trailing digits */
        int n = (int)strlen(tmp);
        while (n > 0 && tmp[n-1] >= '0' && tmp[n-1] <= '9') n--;
        tmp[n] = '\0';
    }

    strncpy(dst, tmp, dst_len - 1);
    dst[dst_len - 1] = '\0';
}

/*
 * Find the whole-disk device backing "/" by reading /proc/mounts, then
 * snapshot ONLY that disk in /proc/diskstats.
 *
 * Returns 1 on success (names[0] and snap[0] populated), 0 on failure.
 * On this device "/" is on /dev/sda2, so only "sda" is tracked — not the
 * other UFS LUNs (sdb, sdc, …) which would wrongly inflate the I/O totals.
 */
int storage_snapshot_begin(disk_raw_t snap[QCTOP_MAX_DISKS],
                            char names[QCTOP_MAX_DISKS][32])
{
    /* Step 1: find the whole-disk name from /proc/mounts */
    char root_disk[32] = "";
    FILE *mf = fopen("/proc/mounts", "r");
    if (mf) {
        char dev[64], mnt[64], line[256];
        while (fgets(line, sizeof(line), mf)) {
            if (sscanf(line, "%63s %63s", dev, mnt) != 2) continue;
            if (strcmp(mnt, "/") != 0) continue;
            partition_to_disk(dev, root_disk, sizeof(root_disk));
            break;
        }
        fclose(mf);
    }
    if (root_disk[0] == '\0') return 0;

    /* Step 2: find that single disk entry in /proc/diskstats */
    FILE *fp = fopen("/proc/diskstats", "r");
    if (!fp) return 0;

    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        unsigned int maj, min;
        char name[32];
        unsigned long long rd_ios, rd_merge, rd_sect, rd_ms,
                           wr_ios, wr_merge, wr_sect, wr_ms;
        if (sscanf(line, "%u %u %31s %llu %llu %llu %llu %llu %llu %llu %llu",
                   &maj, &min, name,
                   &rd_ios, &rd_merge, &rd_sect, &rd_ms,
                   &wr_ios, &wr_merge, &wr_sect, &wr_ms) < 11)
            continue;
        if (strcmp(name, root_disk) != 0) continue;

        strncpy(names[0], name, 31);
        snap[0].rd_sectors = rd_sect;
        snap[0].wr_sectors = wr_sect;
        fclose(fp);
        return 1;
    }
    fclose(fp);
    return 0;
}

/* ── Storage controller clock ────────────────────────────────────────────── */
/*
 * Try devfreq entries for UFS, eMMC, and NVMe.
 * For NVMe, also check /sys/class/nvme/nvme<N>/link_speed (PCIe GT/s -> approx MHz).
 */
static unsigned long read_storage_clk_mhz(void)
{
    /* devfreq: matches "ufs", "emmc", "nvme" */
    static const char *patterns[] = { "ufs", "emmc", "nvme", NULL };

    DIR *dir = opendir("/sys/class/devfreq");
    if (dir) {
        struct dirent *ent;
        unsigned long result = 0;
        while ((ent = readdir(dir)) != NULL) {
            const char *n = ent->d_name;
            int match = 0;
            for (int i = 0; patterns[i]; i++)
                if (strstr(n, patterns[i])) { match = 1; break; }
            if (!match) continue;

            char *path;
            if (asprintf(&path, "/sys/class/devfreq/%s/cur_freq", n) < 0) continue;
            FILE *fp = fopen(path, "r");
            free(path);
            if (!fp) continue;
            unsigned long long v = 0;
            fscanf(fp, "%llu", &v);
            fclose(fp);
            unsigned long mhz = (v >= 1000000ULL) ? (unsigned long)(v / 1000000ULL)
                                                   : (unsigned long)v;
            if (mhz > result) result = mhz;
        }
        closedir(dir);
        if (result) return result;
    }

    /* NVMe fallback: read link_speed "8.0 GT/s" → approx 8000 MT/s → ~4000 MHz */
    for (int i = 0; i < 4; i++) {
        char path[64];
        snprintf(path, sizeof(path), "/sys/class/nvme/nvme%d/device/current_link_speed", i);
        FILE *fp = fopen(path, "r");
        if (!fp) {
            snprintf(path, sizeof(path), "/sys/class/nvme/nvme%d/link_speed", i);
            fp = fopen(path, "r");
        }
        if (!fp) continue;
        float gts = 0.0f;
        fscanf(fp, "%f", &gts);
        fclose(fp);
        if (gts > 0.0f)
            return (unsigned long)(gts * 500.0f); /* GT/s × 500 ≈ effective MHz */
    }

    /* MMC host clock fallback */
    FILE *fp = fopen("/sys/class/mmc_host/mmc0/clock", "r");
    if (fp) {
        unsigned long long v = 0;
        fscanf(fp, "%llu", &v);
        fclose(fp);
        if (v > 1000000ULL) return (unsigned long)(v / 1000000ULL);
    }

    return 0;
}

/* ── Filesystem usage ─────────────────────────────────────────────────────── */
/*
 * POSIX: f_bavail counts blocks in units of f_frsize (fundamental block size),
 * not f_bsize (preferred I/O size).  On ext4 both are 4096 so the result is
 * identical, but f_frsize is the semantically correct multiplier.
 *
 * The value matches `df` "Avail" column: both use f_bavail (blocks available
 * to non-privileged users).  `df -h` rounds to the nearest integer GiB for
 * values ≥ 10 GiB, so e.g. 96.6 GiB appears as "97G" while qctop shows the
 * exact 98928 MiB — the values are consistent, just at different precision.
 */
static unsigned long long read_fs_avail_mb(void)
{
    struct statvfs st;
    if (statvfs("/", &st) != 0) return 0;
    return ((unsigned long long)st.f_bavail * st.f_frsize) / (1024ULL * 1024ULL);
}

/* ── Public API ───────────────────────────────────────────────────────────── */

void storage_snapshot_end(const disk_raw_t  prev[QCTOP_MAX_DISKS],
                          int               n_disks,
                          int               interval_ms,
                          storage_metrics_t *m)
{
    (void)interval_ms;

    /* Read the AFTER snapshot, then compute Δ(curr - prev) per disk.
     * Using deltas ensures rd_mb / wr_mb show MB transferred *this interval*
     * rather than cumulative totals since boot.                              */
    disk_raw_t curr[QCTOP_MAX_DISKS];
    char       curr_names[QCTOP_MAX_DISKS][32];
    storage_snapshot_begin(curr, curr_names);

    unsigned long long delta_rd = 0, delta_wr = 0;
    for (int i = 0; i < n_disks && i < QCTOP_MAX_DISKS; i++) {
        if (curr[i].rd_sectors >= prev[i].rd_sectors)
            delta_rd += curr[i].rd_sectors - prev[i].rd_sectors;
        if (curr[i].wr_sectors >= prev[i].wr_sectors)
            delta_wr += curr[i].wr_sectors - prev[i].wr_sectors;
    }

    m->rd_mb       = (unsigned long)((delta_rd * SECTOR_BYTES) / (1024ULL * 1024ULL));
    m->wr_mb       = (unsigned long)((delta_wr * SECTOR_BYTES) / (1024ULL * 1024ULL));
    m->eva_clk_mhz = read_storage_clk_mhz();
    m->avail_mb    = read_fs_avail_mb();

    /* I/O PSI: /proc/pressure/io "some avg10"; -1 if PSI not compiled in */
    {
        FILE *fp = fopen("/proc/pressure/io", "r");
        if (fp) {
            float v = 0.0f;
            if (fscanf(fp, "some avg10=%f", &v) == 1)
                m->io_psi_avg10 = v;
            else
                m->io_psi_avg10 = 0.0f;
            fclose(fp);
        } else {
            m->io_psi_avg10 = -1.0f;  /* PSI unavailable */
        }
    }
}

/* ── printing ─────────────────────────────────────────────────────────────── */

void storage_print_header(int level)
{
    if (level == 0) {
        /* Banner 51 chars = data total (1+9)+(1+8)+(1+8)+(1+13)+(1+9) = 51.
         * With 4-space gap prefix: total = 55 chars = data row width.     */
        char buf[64];
        int  label_len = 7, inner = 51-2, fill = inner-label_len-2;
        int  left = fill/2, right = fill-left, pos = 0;
        buf[pos++] = '<';
        for (int i = 0; i < left;  i++) buf[pos++] = '=';
        buf[pos++] = ' ';
        for (int i = 0; i < label_len; i++) buf[pos++] = "storage"[i];
        buf[pos++] = ' ';
        for (int i = 0; i < right; i++) buf[pos++] = '=';
        buf[pos++] = '>'; buf[pos] = '\0';
        printf("    %s", buf);      /* 4-space gap */
    } else {
        printf("    %9s %8s %8s %13s %9s",
               "freq_mhz", "rd_mb", "wr_mb", "avail_mb", "io_psi%");
    }
}

void storage_print_row(const storage_metrics_t *m)
{
    printf("    %9lu %8lu %8lu %13llu",   /* 4-space gap */
           m->eva_clk_mhz, m->rd_mb, m->wr_mb, m->avail_mb);
    if (m->io_psi_avg10 >= 0.0f)
        printf(" %9.2f", m->io_psi_avg10);
    else
        printf(" %9s", "N/A");
}
