#pragma once

/*
 * Thermal zone monitoring.
 *
 * Scans /sys/class/thermal/thermal_zone* once at init time, classifies
 * each zone by its "type" string into one of the categories below, then
 * reads all zones every sample and reports the maximum temperature per
 * category.
 *
 * Classification rules (first match wins):
 *   CPU   – type starts with "cpuss", "cpu-", "cpu"
 *   GPU   – type starts with "gpuss", "gpu"
 *   NPU   – type starts with "nsp"
 *   SoC   – type starts with "aoss", "soc"
 *   PMIC  – type starts with "pm" (pm8350, pmk8350, etc.)
 *   Other – everything else
 *
 * Only categories that have at least one zone are printed.
 */

typedef enum {
    THERMAL_CPU   = 0,
    THERMAL_GPU   = 1,
    THERMAL_NPU   = 2,
    THERMAL_SOC   = 3,
    THERMAL_PMIC  = 4,
    THERMAL_OTHER = 5,
    THERMAL_CAT_COUNT
} thermal_cat_t;

/* One discovered thermal zone */
typedef struct {
    int           zone_id;
    char          type[48];
    thermal_cat_t category;
} thermal_zone_t;

typedef struct {
    /* Discovered zones (heap-allocated at init) */
    int            n_zones;
    thermal_zone_t *zones;          /* [n_zones]  heap */

    /* Which categories have ≥1 zone */
    int            has_cat[THERMAL_CAT_COUNT];

    /* Number of zones per category */
    int            zone_count[THERMAL_CAT_COUNT];

    /* Max temperature per category this sample (°C, -1 = N/A) */
    float          max_temp[THERMAL_CAT_COUNT];
} thermal_metrics_t;

/*
 * thermal_init() – scan /sys/class/thermal/, allocate zone list.
 * Call once at startup.  Call thermal_free() at exit.
 */
void thermal_init(thermal_metrics_t *m);

/* Free heap memory. */
void thermal_free(thermal_metrics_t *m);

/* Read all zone temperatures, update max_temp[]. */
void thermal_read(thermal_metrics_t *m);

/* level 0 = group header, level 1 = column names.  No newline. */
void thermal_print_header(const thermal_metrics_t *m, int level);

/* Print max_temp values for active categories.  No newline. */
void thermal_print_row(const thermal_metrics_t *m);

/* --thermal: one column per individual zone, labelled by type name. */
void thermal_print_zones_header(const thermal_metrics_t *m, int level);
void thermal_print_zones_row(const thermal_metrics_t *m);

/* Human-readable category name. */
const char *thermal_cat_name(thermal_cat_t cat);
