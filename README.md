# qctop — Qualcomm SoC All-in-One Performance Profiler

## Why qctop?

Debugging multimedia workloads with multiple concurrent processes is next to impossible when using separate tools — manually correlating `top`, `htop`, `gpufreq`, `/sys/class/thermal/`, and DDR counters across different terminals is too clumsy and too slow.

Real-world multimedia issues often stem from **3 to 4 simultaneous bottlenecks**, making root cause analysis genuinely challenging. More critically, problems rarely occur in isolation — they manifest as a **chain effect**:

```
memory leak
    → high RAM pressure
        → direct reclaim / OOM-killer activity
            → elevated CPU utilisation
                → thermal throttle
                    → frame drops / poor UX
                        → stability concerns
```

Each stage feeds into the next, and by the time the symptom is visible the root cause is already cold.

**qctop** was developed to troubleshoot exactly this class of multimedia workloads on Qualcomm platforms. It tracks all critical system metrics — CPU, GPU, RAM, DDR, storage, NPU, network, and thermal — **simultaneously, in a single lightweight tool**, with a 1-second sampling interval and negligible CPU overhead.

---

## Features at a glance

| Subsystem | What is measured |
|-----------|-----------------|
| `--cpu` | Per-core util% / iowait% / irq%, per-cluster avg freq, cpu_C temp, cpu_psi% |
| `--gpu` | GPU util%, clock (MHz), GPU temperature |
| `--mem` | RAM snapshot (MB), vmstat activity deltas, allocstall, memory PSI |
| `--ddr` | DDR time-weighted avg freq (MHz), DDR temperature |
| `--storage` | Storage controller freq, read/write MB/interval, avail GB, I/O PSI |
| `--thermal` | Per-category max temp (cpu/gpu/npu/soc/pmic) or full per-zone view |
| `--net` | Network rx_mb / tx_mb per interval |
| `--npu` | NSP (Neural Signal Processor) thermal temperature |

**Device info banner** — printed once at startup: CPU topology (model, GHz), cache sizes, RAM (System RAM, MemTotal, Kernel, carveouts), DDR Fmax, GPU Fmax, storage type/size.

---

## Quick start

```bash
# Build (requires CMake ≥ 3.13 and a C11 compiler)
./scripts/build.sh

# Copy the static binary to the device
adb push build/qctop /data/

# Run — all subsystems, 1 second interval
/data/qctop

# Focus on CPU + GPU + thermal at 500 ms
/data/qctop --cpu --gpu --thermal -i 500

# Memory pressure investigation
/data/qctop --mem --ddr

# 60-second thermal log to file
/data/qctop --thermal -n 60 > thermal_log.txt
```

---

## Command reference

```
Usage: qctop [OPTIONS]

Interval / loop:
  -i <ms>     Sampling interval in ms (default: 1000, min: 100)
  -n <N>      Exit after N samples
  -1          Single snapshot then exit
  -l          Loop continuously until Ctrl-C (default)
  -h          This help

Subsystem filters (default: all subsystems):
  --cpu       Per-core util% / io% / irq% + cluster avg freq + cpu misc
  --gpu       GPU util%, freq (MHz), temp
  --mem       RAM (MB) + vmstat activity + allocstall + memory PSI
  --ddr       DDR time-weighted avg freq (MHz) + DDR temp
  --storage   Storage clock, rd/wr MB, avail GB, io_psi%
  --thermal   Per-category thermal aggregate (cpu/gpu/npu/soc/pmic/other)
  --net       Network total rx_mb / tx_mb per interval
  --npu       NSP thermal temperature (nsp_C)
```

---

## Output layout

```
             <== cpufreq_mhz ==>  <= cpu misc =>  <====== cpu util% ======>  ...
      uptime   avg0-1 avg2-3 avg4-7   cpu_C cpu_psi%   cpu0  io0 irq0  ...
  251161.030     2112   2361   1958    32.4      N/A    0.0  0.0  0.0  ...
```

**Gap rules:**
- 1 space between columns within a section
- 2 spaces between related sub-sections (cpufreq ↔ cpu-misc ↔ cpu-util%)
- 4 spaces between major sections (cpu ↔ memory ↔ ddr ↔ gpu ↔ storage ↔ net ↔ thermal)

---

## Building

### Native build (on the device or a Linux host)

```bash
./scripts/build.sh                    # Release, static binary
./scripts/build.sh Debug              # Debug build
./scripts/build.sh Release "" OFF     # Release, dynamically linked
```

The binary is placed at `build/qctop`.

### Cross-compile (from x86_64 host for aarch64 device)

```bash
# Install a cross toolchain, e.g.:
#   sudo apt install gcc-aarch64-linux-gnu

./scripts/build.sh Release path/to/aarch64-toolchain.cmake
```

### Package for distribution

```bash
./scripts/build.sh && ./scripts/package.sh
# Produces: qctop-aarch64.tar.gz
```

---

## Key data sources

| Metric | Source |
|--------|--------|
| CPU util% / io% / irq% | `/proc/stat` (delta between snapshots) |
| CPU cluster avg freq | `/sys/devices/system/cpu/cpufreq/policy*/stats/time_in_state` |
| CPU / GPU temp | `/sys/class/thermal/thermal_zone*/` |
| DDR avg freq | `/sys/kernel/debug/qcom_stats/ddr_stats` (time-weighted Δ) |
| DDR temp | `ddrss*` thermal zones |
| GPU util / clock | `/sys/class/kgsl/kgsl-3d0/` |
| RAM fields | `/proc/meminfo` |
| vmstat deltas | `/proc/vmstat` (pgpgin, pgfault, allocstall, pgscan_direct, …) |
| Memory PSI | `/proc/pressure/memory` `some avg10` |
| Storage I/O | `/proc/diskstats` (delta on rootfs disk) |
| Network | `/sys/class/net/*/statistics/` |
| NPU temp | `nsp*` thermal zones |
| DMA-buf | `/sys/kernel/debug/dma_buf/bufinfo` (requires root + debugfs) |
| KGSL memory | `/sys/class/kgsl/kgsl/` |

---

## Memory pressure signals

| Column | Source | Non-zero means |
|--------|--------|----------------|
| `pgscan` | `Δpgscan_direct` | App threads paid direct-reclaim cost now |
| `astall` | `Δallocstall_*` | Page allocator stalled — near-OOM |
| `mem_psi%` | `/proc/pressure/memory` `some avg10` | ≥1 task blocked for memory (0–100%) |

---

## Build requirements

| Tool | Required | Notes |
|------|----------|-------|
| CMake ≥ 3.13 | **Yes** | |
| C11 compiler | **Yes** | `gcc` or `clang` |
| `libc` static | Only for `-DSTATIC_BUILD=ON` | `musl-tools` or `libc6-dev` |

No external runtime libraries are required. The binary is fully self-contained when built with the default `STATIC_BUILD=ON`.

---

## Source layout

```
qctop/
├── CMakeLists.txt              Build system
├── README.md                   This file
├── scripts/
│   ├── build.sh                Build script
│   └── package.sh              Creates distributable tarball
└── src/
    ├── main.c                  Argument parsing, main loop
    ├── cpu_metrics.[ch]        CPU util%, freq, thermal, PSI
    ├── gpu_metrics.[ch]        GPU util, clock, temp
    ├── mem_metrics.[ch]        RAM, DDR, vmstat, PSI
    ├── storage_metrics.[ch]    Storage clock, I/O, filesystem
    ├── net_metrics.[ch]        Network RX/TX
    ├── thermal_metrics.[ch]    Thermal zones (aggregate + per-zone)
    ├── npu_metrics.[ch]        NSP thermal temperature
    └── hw_info.[ch]            Device info banner (printed once at startup)
