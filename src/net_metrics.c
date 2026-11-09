#include "net_metrics.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── interface priority ───────────────────────────────────────────────────── */
/*
 * Lower number = higher priority (sorted first in output).
 *   0  wlan<N>
 *   1  rmnet<N>    (cellular data on Qualcomm)
 *   2  rndis<N>, usb<N>  (USB tethering)
 *   3  eth<N>, enp<N>    (Ethernet)
 *   4  everything else
 * loopback "lo" is always skipped.
 */
static int iface_priority(const char *name)
{
    if (strncmp(name, "wlan",  4) == 0) return 0;
    if (strncmp(name, "rmnet", 5) == 0) return 1;
    if (strncmp(name, "rndis", 5) == 0) return 2;
    if (strncmp(name, "usb",   3) == 0) return 2;
    if (strncmp(name, "eth",   3) == 0) return 3;
    if (strncmp(name, "enp",   3) == 0) return 3;
    return 4;
}

/* ── read one counter ─────────────────────────────────────────────────────── */

static unsigned long long read_counter(const char *iface, const char *stat)
{
    char *path;
    if (asprintf(&path, "/sys/class/net/%s/statistics/%s", iface, stat) < 0)
        return 0;
    FILE *fp = fopen(path, "r");
    free(path);
    if (!fp) return 0;
    unsigned long long v = 0;
    fscanf(fp, "%llu", &v);
    fclose(fp);
    return v;
}

/* ── check if interface is up / has traffic ───────────────────────────────── */

static int iface_is_active(const char *name)
{
    /* Skip loopback */
    if (strcmp(name, "lo") == 0) return 0;

    /* Check operstate */
    char *path;
    if (asprintf(&path, "/sys/class/net/%s/operstate", name) < 0) goto check_traffic;
    FILE *fp = fopen(path, "r");
    free(path);
    if (fp) {
        char state[16] = {0};
        fgets(state, sizeof(state), fp);
        fclose(fp);
        /* Accept "up" or "unknown" (some virtual/cellular ifaces stay unknown) */
        if (strncmp(state, "up",      2) == 0) return 1;
        if (strncmp(state, "unknown", 7) == 0) return 1;
        /* "down" – only include if it has ever had traffic */
    }

check_traffic:;
    /* Include if it has ever received or sent something */
    unsigned long long rx = read_counter(name, "rx_bytes");
    unsigned long long tx = read_counter(name, "tx_bytes");
    return (rx > 0 || tx > 0);
}

/* ── scan all interfaces ──────────────────────────────────────────────────── */

static int scan_ifaces(net_iface_snap_t out[NET_MAX_IFACES])
{
    /* Collect candidates */
    char candidates[NET_MAX_IFACES * 2][16];
    int  n_cand = 0;

    DIR *dir = opendir("/sys/class/net");
    if (!dir) return 0;

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL &&
           n_cand < (int)(sizeof(candidates) / sizeof(candidates[0]))) {
        if (!iface_is_active(ent->d_name)) continue;
        strncpy(candidates[n_cand], ent->d_name, 15);
        n_cand++;
    }
    closedir(dir);

    /* Sort by priority */
    for (int i = 0; i < n_cand - 1; i++)
        for (int j = i + 1; j < n_cand; j++)
            if (iface_priority(candidates[j]) < iface_priority(candidates[i])) {
                char tmp[16];
                memcpy(tmp,            candidates[i], 16);
                memcpy(candidates[i],  candidates[j], 16);
                memcpy(candidates[j],  tmp,            16);
            }

    int n = (n_cand < NET_MAX_IFACES) ? n_cand : NET_MAX_IFACES;
    for (int i = 0; i < n; i++) {
        strncpy(out[i].name, candidates[i], 15);
        out[i].rx_bytes = read_counter(candidates[i], "rx_bytes");
        out[i].tx_bytes = read_counter(candidates[i], "tx_bytes");
    }
    return n;
}

/* ── Public API ───────────────────────────────────────────────────────────── */

void net_snapshot_begin(net_snap_t *snap)
{
    snap->n_ifaces = scan_ifaces(snap->iface);
}

void net_snapshot_end(const net_snap_t *prev, net_metrics_t *m)
{
    /* Take a fresh read */
    net_iface_snap_t curr[NET_MAX_IFACES];
    int n_curr = scan_ifaces(curr);

    /*
     * Match interfaces by name between prev and curr.
     * Use prev's interface list as the authoritative order so columns stay
     * stable across samples even if a new interface appears mid-run.
     */
    m->n_ifaces = prev->n_ifaces;

    for (int i = 0; i < prev->n_ifaces; i++) {
        net_iface_metric_t *mi = &m->iface[i];
        strncpy(mi->name, prev->iface[i].name, 15);

        /* Find matching entry in curr */
        unsigned long long rx_now = 0, tx_now = 0;
        for (int j = 0; j < n_curr; j++) {
            if (strcmp(curr[j].name, prev->iface[i].name) == 0) {
                rx_now = curr[j].rx_bytes;
                tx_now = curr[j].tx_bytes;
                break;
            }
        }

        mi->rx_bytes_total = rx_now;
        mi->tx_bytes_total = tx_now;
        mi->rx_bytes_delta = (rx_now >= prev->iface[i].rx_bytes) ?
                             (unsigned long)(rx_now - prev->iface[i].rx_bytes) : 0;
        mi->tx_bytes_delta = (tx_now >= prev->iface[i].tx_bytes) ?
                             (unsigned long)(tx_now - prev->iface[i].tx_bytes) : 0;
    }
}

/* ── printing ─────────────────────────────────────────────────────────────── */

void net_print_header(const net_metrics_t *tmpl, int level)
{
    if (level == 0) {
        /* Group label spanning 2 cols × n_ifaces */
        for (int i = 0; i < tmpl->n_ifaces; i++)
            printf(" %-19s", tmpl->iface[i].name);
    } else {
        for (int i = 0; i < tmpl->n_ifaces; i++)
            printf(" %9s %9s", "rx(B)", "tx(B)");
    }
}

void net_print_row(const net_metrics_t *m)
{
    for (int i = 0; i < m->n_ifaces; i++)
        printf(" %9lu %9lu",
               m->iface[i].rx_bytes_delta,
               m->iface[i].tx_bytes_delta);
}

/* ── Total across all interfaces (default view) ───────────────────────────── */

void net_print_total_header(int level)
{
    if (level == 0)
        /* 4-space gap + "<= network io (mb)>" = 4+19 = 23 chars = data width. */
        printf("    %s", "<= network io (mb)>");
    else
        printf("    %9s %9s", "rx_mb", "tx_mb");
}

void net_print_total_row(const net_metrics_t *m)
{
    unsigned long total_rx = 0, total_tx = 0;
    for (int i = 0; i < m->n_ifaces; i++) {
        total_rx += m->iface[i].rx_bytes_delta;
        total_tx += m->iface[i].tx_bytes_delta;
    }
    /* 4-space gap; convert bytes to MB */
    printf("    %9.2f %9.2f",
           (double)total_rx / (1024.0 * 1024.0),
           (double)total_tx / (1024.0 * 1024.0));
}
