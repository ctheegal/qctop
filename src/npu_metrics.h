#pragma once
/*
 * NPU metrics – NSP thermal temperature only.
 * Performance counter metrics (Q6 util, HVX, HMX, clock) are not
 * available without a vendor DSP library; only the hardware temperature
 * is reported via /sys/class/thermal/ zones matching "nsp*".
 */

typedef struct {
    int   available;   /* always 0 – no perf counters without DSP library */
    float temp_c;      /* max NSP thermal zone temp (-1 if unavailable)   */
    /* fields below are zero-filled; kept for ABI compatibility */
    float         q6_util_pct;
    float         hvx_util_pct;
    float         hmx_util_pct;
    unsigned long q6_clk_hz;
    char          device_name[64];
} npu_metrics_t;

int  npu_init(void);
void npu_metrics_read(npu_metrics_t *m);
void npu_shutdown(void);
void npu_print_header(int level);
void npu_print_row(const npu_metrics_t *m);
