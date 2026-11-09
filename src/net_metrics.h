#pragma once

/*
 * Network interface metrics.
 *
 * Scans all interfaces under /sys/class/net/ except loopback ("lo").
 * For each active interface reports:
 *   - RX bytes and TX bytes (delta since last snapshot → bytes/interval)
 *   - Cumulative totals since boot
 *
 * Up to NET_MAX_IFACES interfaces are tracked.
 * Interfaces are sorted: wlan* first, then rmnet/rndis/usb, then others.
 */

#define NET_MAX_IFACES 6

typedef struct {
    char               name[16];
    unsigned long long rx_bytes;
    unsigned long long tx_bytes;
} net_iface_snap_t;

typedef struct {
    int            n_ifaces;
    net_iface_snap_t iface[NET_MAX_IFACES];
} net_snap_t;

typedef struct {
    char          name[16];
    unsigned long long rx_bytes_total; /* cumulative RX since boot */
    unsigned long long tx_bytes_total; /* cumulative TX since boot */
    unsigned long rx_bytes_delta;      /* RX bytes in last interval */
    unsigned long tx_bytes_delta;      /* TX bytes in last interval */
} net_iface_metric_t;

typedef struct {
    int               n_ifaces;
    net_iface_metric_t iface[NET_MAX_IFACES];
} net_metrics_t;

/* Capture current byte counters for all active interfaces. */
void net_snapshot_begin(net_snap_t *snap);

/* Compute deltas between prev snapshot and now; fill *m. */
void net_snapshot_end(const net_snap_t *prev, net_metrics_t *m);

/* Per-interface columns (--net explicit). No newline. */
void net_print_header(const net_metrics_t *tmpl, int level);
void net_print_row(const net_metrics_t *m);

/* Summed total across all interfaces (default view). No newline. */
void net_print_total_header(int level);
void net_print_total_row(const net_metrics_t *m);
