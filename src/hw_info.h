#pragma once
/*
 * hw_info – print device hardware and software summary before metrics loop.
 *
 * Sources used:
 *   CPU model    : /proc/cpuinfo  (implementer + part → name lookup)
 *   CPU freq     : /sys/devices/system/cpu/cpufreq/policy<N>/cpuinfo_max_freq
 *   CPU cache    : /sys/devices/system/cpu/cpu0/cache/index<N>/{level,type,size}
 *   OS release   : /etc/os-release
 *   Kernel       : /proc/version
 *   MemTotal     : /proc/meminfo
 *   System RAM   : /proc/iomem  (sum of "System RAM" regions)
 *   NHLOS        : Total_DRAM - System_RAM  (firmware/modem/TrustZone)
 *   Kernel+carv. : System_RAM  - MemTotal
 *   DDR Fmax     : /sys/class/devfreq/ available_frequencies (max value)
 *   DDR type     : Fmax > 2133 MHz → LPDDR5, else LPDDR4
 *   GPU          : /sys/class/kgsl/kgsl-3d0/ + devfreq
 *   Storage      : /proc/partitions + /sys/class/block/
 */
void print_hw_info(void);
