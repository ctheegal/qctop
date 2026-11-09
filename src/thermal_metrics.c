#include "thermal_metrics.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── category names ───────────────────────────────────────────────────────── */

static const char *cat_names[THERMAL_CAT_COUNT] = {
    "cpu", "gpu", "npu", "soc", "pmic", "other"
};

const char *thermal_cat_name(thermal_cat_t cat)
{
    if (cat >= 0 && cat < THERMAL_CAT_COUNT)
        return cat_names[cat];
    return "?";
}

/* ── classify a zone type string ──────────────────────────────────────────── */

static thermal_cat_t classify(const char *type)
{
    if (strncmp(type, "cpuss", 5) == 0) return THERMAL_CPU;
    if (strncmp(type, "cpu-",  4) == 0) return THERMAL_CPU;
    if (strncmp(type, "cpu",   3) == 0) return THERMAL_CPU;
    if (strncmp(type, "tsens", 5) == 0) return THERMAL_CPU;
    if (strncmp(type, "gpuss", 5) == 0) return THERMAL_GPU;
    if (strncmp(type, "gpu",   3) == 0) return THERMAL_GPU;
    if (strncmp(type, "nsp",  3) == 0)  return THERMAL_NPU;
    if (strncmp(type, "cdsp", 4) == 0)  return THERMAL_NPU;
    if (strncmp(type, "aoss", 4) == 0)  return THERMAL_SOC;
    if (strncmp(type, "soc",  3) == 0)  return THERMAL_SOC;
    if (strncmp(type, "xo",   2) == 0)  return THERMAL_SOC;
    if (strncmp(type, "pm",   2) == 0)  return THERMAL_PMIC;
    return THERMAL_OTHER;
}

/* ── zone name cleaning ───────────────────────────────────────────────────── */
/*
 * Strip "-thermal" or "thermal" suffix, then strip any trailing "-".
 * Examples:
 *   "aoss-2-thermal"    → "aoss-2"
 *   "nsp-0-0-1-thermal" → "nsp-0-0-1"  (trailing "-" also removed)
 *   "cpuss-0-0-thermal" → "cpuss-0-0"
 *   "gpuss-0-thermal"   → "gpuss-0"
 */
static void clean_zone_name(const char *type, char *out, int out_len)
{
    strncpy(out, type, out_len - 1);
    out[out_len - 1] = '\0';

    int len = (int)strlen(out);

    /* Strip "-thermal" suffix (8 chars) */
    if (len >= 8 && strcmp(out + len - 8, "-thermal") == 0) {
        len -= 8; out[len] = '\0';
    }
    /* Strip bare "thermal" suffix (7 chars, zone type = "thermal" itself) */
    else if (len >= 7 && strcmp(out + len - 7, "thermal") == 0) {
        len -= 7; out[len] = '\0';
    }

    /* Strip any remaining trailing "-" characters */
    while (len > 0 && out[len - 1] == '-') out[--len] = '\0';
}

/* ── zone sort comparator (alphabetical by cleaned name) ─────────────────── */

static int compare_zones_alpha(const void *a, const void *b)
{
    const thermal_zone_t *za = (const thermal_zone_t *)a;
    const thermal_zone_t *zb = (const thermal_zone_t *)b;
    char ca[32], cb[32];
    clean_zone_name(za->type, ca, sizeof(ca));
    clean_zone_name(zb->type, cb, sizeof(cb));
    return strcmp(ca, cb);
}

/* ── init: scan all zones ─────────────────────────────────────────────────── */

void thermal_init(thermal_metrics_t *m)
{
    memset(m, 0, sizeof(*m));
    for (int i = 0; i < THERMAL_CAT_COUNT; i++)
        m->max_temp[i] = -1.0f;

    /* Count zones first */
    int n = 0;
    char path[96];
    for (int z = 0; z < 256; z++) {
        snprintf(path, sizeof(path),
                 "/sys/class/thermal/thermal_zone%d/type", z);
        FILE *fp = fopen(path, "r");
        if (!fp) continue;
        fclose(fp);
        n++;
    }

    if (n == 0) return;

    m->zones = calloc((size_t)n, sizeof(thermal_zone_t));
    if (!m->zones) return;

    int idx = 0;
    for (int z = 0; z < 256 && idx < n; z++) {
        snprintf(path, sizeof(path),
                 "/sys/class/thermal/thermal_zone%d/type", z);
        FILE *fp = fopen(path, "r");
        if (!fp) continue;

        thermal_zone_t *tz = &m->zones[idx];
        tz->zone_id = z;
        (void)fgets(tz->type, sizeof(tz->type), fp);
        fclose(fp);

        char *nl = strchr(tz->type, '\n');
        if (nl) *nl = '\0';

        tz->category = classify(tz->type);
        m->has_cat[tz->category] = 1;
        m->zone_count[tz->category]++;
        idx++;
    }
    m->n_zones = idx;

    /* Sort zones alphabetically by cleaned display name */
    if (m->n_zones > 1)
        qsort(m->zones, (size_t)m->n_zones, sizeof(thermal_zone_t),
              compare_zones_alpha);
}

void thermal_free(thermal_metrics_t *m)
{
    free(m->zones);
    memset(m, 0, sizeof(*m));
}

/* ── read: update max per category ───────────────────────────────────────── */

void thermal_read(thermal_metrics_t *m)
{
    for (int i = 0; i < THERMAL_CAT_COUNT; i++)
        m->max_temp[i] = -1.0f;

    char path[96];
    for (int i = 0; i < m->n_zones; i++) {
        thermal_zone_t *tz = &m->zones[i];

        snprintf(path, sizeof(path),
                 "/sys/class/thermal/thermal_zone%d/temp", tz->zone_id);
        FILE *fp = fopen(path, "r");
        if (!fp) continue;

        int raw = 0;
        (void)fscanf(fp, "%d", &raw);  /* (void) – warn_unused_result */
        fclose(fp);

        float temp = (float)raw / 1000.0f;
        if (temp > m->max_temp[tz->category])
            m->max_temp[tz->category] = temp;
    }
}

/* ── printing ─────────────────────────────────────────────────────────────── */

/* Build a <===== label =====> banner of exactly `width` chars.             */
static void make_section_banner(char *buf, int buf_size, int width,
                                const char *label)
{
    int label_len = (int)strlen(label);
    int inner = width - 2;
    int fill  = inner - label_len - 2;
    if (fill < 0) fill = 0;
    int left = fill / 2, right = fill - left;
    int pos = 0;
    if (pos < buf_size - 1) buf[pos++] = '<';
    for (int i = 0; i < left  && pos < buf_size - 1; i++) buf[pos++] = '=';
    if (pos < buf_size - 1) buf[pos++] = ' ';
    for (int i = 0; i < label_len && pos < buf_size - 1; i++)
        buf[pos++] = label[i];
    if (pos < buf_size - 1) buf[pos++] = ' ';
    for (int i = 0; i < right && pos < buf_size - 1; i++) buf[pos++] = '=';
    if (pos < buf_size - 1) buf[pos++] = '>';
    buf[pos] = '\0';
}

void thermal_print_header(const thermal_metrics_t *m, int level)
{
    if (level == 0) {
        int active = 0;
        for (int i = 0; i < THERMAL_CAT_COUNT; i++)
            if (m->has_cat[i]) active++;
        /* 6-space gap; banner spans exactly active×8 chars (data row width) */
        char buf[256];
        make_section_banner(buf, sizeof(buf), active * 8, "thermal(C)");
        printf("      %s", buf);
    } else {
        printf("      ");         /* 6-space gap */
        for (int i = 0; i < THERMAL_CAT_COUNT; i++) {
            if (!m->has_cat[i]) continue;
            char col[16];
            snprintf(col, sizeof(col), "%s(%d)",
                     cat_names[i], m->zone_count[i]);
            printf(" %7s", col);  /* 8 chars per category */
        }
    }
}

void thermal_print_row(const thermal_metrics_t *m)
{
    printf("      ");             /* 6-space gap */
    for (int i = 0; i < THERMAL_CAT_COUNT; i++) {
        if (!m->has_cat[i]) continue;
        if (m->max_temp[i] >= 0.0f)
            printf(" %7.1f", m->max_temp[i]);
        else
            printf(" %7s", "N/A");
    }
}

/* ── Individual zone view (--thermal) ────────────────────────────────────── */

static float read_zone_temp_by_id(int zone_id)
{
    char path[64];
    snprintf(path, sizeof(path),
             "/sys/class/thermal/thermal_zone%d/temp", zone_id);
    FILE *fp = fopen(path, "r");
    if (!fp) return -1.0f;
    int raw = 0;
    (void)fscanf(fp, "%d", &raw);  /* (void) – warn_unused_result */
    fclose(fp);
    return (float)raw / 1000.0f;
}

/* Compute the column width for per-zone display.
 * col_w = max(cleaned_name_length) + 2  (2-space min padding).
 * Data values like "100.0" (5 chars) will always fit within col_w.        */
static int zones_col_w(const thermal_metrics_t *m)
{
    int max_len = 5;   /* minimum: "N/A" + some padding; or "100.0" */
    for (int i = 0; i < m->n_zones; i++) {
        char name[32];
        clean_zone_name(m->zones[i].type, name, sizeof(name));
        int len = (int)strlen(name);
        if (len > max_len) max_len = len;
    }
    return max_len;       /* leading space from " %*s" format = 1-space gap */
}

void thermal_print_zones_header(const thermal_metrics_t *m, int level)
{
    if (level == 0) {
        /* Banner spans n_zones × (1+col_w) chars = data row width.
         * Width +1 compensates for "°" being 2 UTF-8 bytes but 1 display char. */
        int col_w = zones_col_w(m);
        char buf[2048];
        make_section_banner(buf, sizeof(buf), m->n_zones * (1 + col_w) + 1,
                            "thermal_zones \xc2\xb0" "C");
        printf("%s", buf);
    } else {
        int col_w = zones_col_w(m);
        for (int i = 0; i < m->n_zones; i++) {
            char name[32];
            clean_zone_name(m->zones[i].type, name, sizeof(name));
            printf(" %*s", col_w, name);   /* right-align in col_w */
        }
    }
}

void thermal_print_zones_row(const thermal_metrics_t *m)
{
    int col_w = zones_col_w(m);
    for (int i = 0; i < m->n_zones; i++) {
        float t = read_zone_temp_by_id(m->zones[i].zone_id);
        if (t >= 0.0f)
            printf(" %*.1f", col_w, t);    /* same col_w as header */
        else
            printf(" %*s",  col_w, "N/A");
    }
}
