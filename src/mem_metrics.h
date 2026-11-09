#pragma once

/*
 * Memory + DDR + Activity + PSI metrics.
 *
 * ── Sources & calculation methods ────────────────────────────────────────────
 *
 * STATIC fields (snapshot at sample-end from /proc/meminfo):
 *   avail_kb     = MemAvailable
 *   anon_kb      = AnonPages + AnonHugePages   (total anonymous RAM)
 *   vmalloc_kb   = VmallocUsed
 *   slab_kb      = Slab  (= SReclaimable + SUnreclaim)
 *   mapped_kb    = Mapped
 *   swap_used_kb = SwapTotal - SwapFree
 *
 * DDR field (time-weighted avg over interval):
 *   ddr_avg_mhz  = Σ(freq_i × Δduration_i) / Σ(Δduration_i)
 *                  source: /sys/kernel/debug/qcom_stats/ddr_stats
 *                  Duration (ticks) is XO-clock; units cancel in ratio.
 *
 * DELTA fields (Δ = after-sleep minus before-sleep, from /proc/vmstat):
 *   d_pagein     = Δpgpgin          pages read from block device
 *   d_pageout    = Δpgpgout         pages written to block device
 *   d_pgfault    = Δpgfault         total page faults (minor + major)
 *   d_swpin      = Δpswpin          pages swapped in from swap device
 *   d_swpout     = Δpswpout         pages swapped out to swap device
 *   d_alloc_stall= Δ(allocstall_normal + allocstall_movable)
 *                  times the page allocator stalled waiting for reclaim
 *   d_pgscan_direct = Δpgscan_direct
 *                  pages scanned during direct (synchronous) reclaim;
 *                  non-zero means application threads paid the reclaim cost
 *
 * PSI field (instantaneous 10-second EMA from /proc/pressure/memory):
 *   psi_some_avg10 = "some avg10" value
 *                    % of time ≥1 task was blocked waiting for memory
 *                    0 = no pressure; >20 = significant; >50 = severe
 *
 * Note: dma_buf and kgsl fields require debugfs + root; 0 if unavailable.
 * ─────────────────────────────────────────────────────────────────────────────
 */

/* ── DDR time-in-state snapshot ──────────────────────────────────────────── */

#define DDR_MAX_STATES 16

typedef struct {
    unsigned long      freq_mhz;
    unsigned long long duration;  /* cumulative Duration (ticks) from ddr_stats */
} ddr_state_snap_t;

typedef struct {
    ddr_state_snap_t states[DDR_MAX_STATES];
    int              n_states;    /* 0 = ddr_stats unavailable */
} ddr_snap_t;

/* ── vmstat counters snapshot ─────────────────────────────────────────────── */

typedef struct {
    unsigned long long pgpgin;
    unsigned long long pgpgout;
    unsigned long long pgfault;
    unsigned long long pswpin;
    unsigned long long pswpout;
    unsigned long long allocstall;    /* allocstall_normal + allocstall_movable */
    unsigned long long pgscan_direct;
} vmstat_snap_t;

/* ── Combined before-sleep snapshot ──────────────────────────────────────── */

typedef struct {
    ddr_snap_t    ddr;
    vmstat_snap_t vmstat;
} mem_snap_t;

/* ── Metrics struct ───────────────────────────────────────────────────────── */

typedef struct {
    /* DDR clock – time-weighted avg from ddr_stats delta; 0 if unavailable */
    unsigned long ddr_avg_mhz;
    /* DDR temperature – max of ddrss thermal zones; -1 if unavailable      */
    float         ddr_temp;

    /* Static memory (KB) – /proc/meminfo snapshot at sample-end */
    unsigned long avail_kb;
    unsigned long anon_kb;        /* AnonPages + AnonHugePages               */
    unsigned long vmalloc_kb;     /* VmallocUsed                             */
    unsigned long slab_kb;        /* Slab (SReclaimable + SUnreclaim)        */
    unsigned long mapped_kb;      /* Mapped                                  */
    unsigned long swap_used_kb;   /* SwapTotal - SwapFree                    */
    unsigned long dma_tot_kb;     /* total DMA-buf size                      */
    unsigned long dma_npu_kb;     /* NPU (fastrpc) DMA-buf                   */
    unsigned long kgsl_kb;        /* GPU physical memory                     */

    /* Activity deltas – /proc/vmstat Δ over the sampling interval           */
    long long d_pagein;           /* Δpgpgin           (pages)               */
    long long d_pageout;          /* Δpgpgout          (pages)               */
    long long d_pgfault;          /* Δpgfault          (faults)              */
    long long d_swpin;            /* Δpswpin           (pages)               */
    long long d_swpout;           /* Δpswpout          (pages)               */
    long long d_alloc_stall;      /* Δallocstall_*     (stall events)        */
    long long d_pgscan_direct;    /* Δpgscan_direct    (pages scanned)       */

    /* Memory pressure – /proc/pressure/memory "some avg10"                  */
    float psi_some_avg10;         /* % time ≥1 task waited for memory        */
} mem_metrics_t;

/* ── Public API ───────────────────────────────────────────────────────────── */

/* Call before the sampling sleep to capture DDR state + vmstat baseline.   */
void mem_snapshot_begin(mem_snap_t *snap);

/* Call after the sleep; computes DDR avg, vmstat deltas, reads meminfo+PSI. */
void mem_metrics_read(const mem_snap_t *prev, mem_metrics_t *m);

/* ── Print functions ──────────────────────────────────────────────────────── */

/* Full combined header/row (default all-flags view) */
void mem_print_header(int level);
void mem_print_row(const mem_metrics_t *m);

/* Static memory section: avail anon vmalloc slab mapped swap dma_tot dma_npu kgsl */
void mem_print_ram_header(int level);
void mem_print_ram_row(const mem_metrics_t *m);

/* DDR avg-freq section (--ddr) */
void mem_print_ddr_header(int level);
void mem_print_ddr_row(const mem_metrics_t *m);

/* Activity (delta) section: pg-in pg-out pgflt swp-in swp-out astall pgscan */
void mem_print_activity_header(int level);
void mem_print_activity_row(const mem_metrics_t *m);

/* PSI section: psi% */
void mem_print_psi_header(int level);
void mem_print_psi_row(const mem_metrics_t *m);
