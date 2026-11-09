#pragma once

/*
 * Storage metrics:
 *   eva_clk_mhz – eMMC/UFS storage controller clock (MHz)
 *                 /sys/class/devfreq/ entry with "ufs" or "emmc"
 *   rd_mb / wr_mb – cumulative MB read/written since boot (/proc/diskstats)
 *   avail_mb     – root filesystem available space (statvfs "/")
 *
 * Note: there is no time-in-state (trans_stat) data available for the UFS/eMMC
 * devfreq node on Qualcomm kernels, so an interval-weighted average frequency
 * cannot be computed.  Only the instantaneous devfreq clock is reported.
 */

#define QCTOP_MAX_DISKS 8

typedef struct {
    unsigned long long rd_sectors;
    unsigned long long wr_sectors;
} disk_raw_t;

typedef struct {
    unsigned long      eva_clk_mhz;  /* eMMC/UFS controller clock (MHz)        */
    unsigned long      rd_mb;        /* MB read  this interval (/proc/diskstats) */
    unsigned long      wr_mb;        /* MB written this interval                */
    unsigned long long avail_mb;     /* root-FS available (MB)                  */
    float              io_psi_avg10; /* /proc/pressure/io "some avg10" %        */
                                     /* -1.0 = PSI unavailable (CONFIG_PSI=n)   */
} storage_metrics_t;

int  storage_snapshot_begin(disk_raw_t snap[QCTOP_MAX_DISKS],
                             char names[QCTOP_MAX_DISKS][32]);

void storage_snapshot_end(const disk_raw_t  prev[QCTOP_MAX_DISKS],
                          int               n_disks,
                          int               interval_ms,
                          storage_metrics_t *m);

/* io_psi% is printed as the 5th column within storage_print_header/row */
void storage_print_header(int level);
void storage_print_row(const storage_metrics_t *m);
