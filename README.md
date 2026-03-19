# Argon ONE UP

Hardware support code for the **Argon ONE UP laptop** (based on Raspberry Pi 5). Targets 64-bit Raspberry Pi OS.

> [!NOTE]
> This code is supported on 64-bit Raspberry Pi OS only. There is no current plan to support other operating systems.

## Components

### [`battery/`](battery/README.md) — Battery driver

A Linux kernel module (`oneUpPower.ko`) that integrates the laptop's battery controller with the kernel power supply framework. It exposes battery state and AC power status through `/sys/class/power_supply/` and performs a graceful shutdown when the battery drops below a configurable charge threshold while unplugged.

See [battery/README.md](battery/README.md) for build instructions, installation, DKMS setup, and driver settings.

---

### [`monitor/`](monitor/README.md) — System monitor

A graphical system resource monitor (`oneUpMon.py`) built with PyQt6. Displays live rolling charts for CPU utilization, disk I/O, network I/O, temperatures, and fan speed, updated once per second.

Also includes `argon-oneup-sensors.conf`, an `lm-sensors` chip-label configuration for this hardware (install to `/etc/sensors.d/`).

See [monitor/README.md](monitor/README.md) for requirements, configuration, and a description of the charts and data sources.

---

### [`config/`](config/) — Boot configuration snippets

Additions to `/boot/firmware/config.txt` for fan curve tuning and CPU overclocking. Apply manually or review `config/update.sh` for reference.

| File | Purpose |
|------|---------|
| `fansettings_config.txt` | Fan curve parameters (`dtparam=fan_temp*`) |
| `overclock_config.txt` | CPU overclock (`arm_freq=2800`) |

---

### [`fioscript/`](fioscript/) — NVMe benchmark jobs

FIO job files for testing NVMe drive performance across a range of access patterns (sequential and random reads, writes, and mixed workloads at 4K, 8K, 64K, and 512-byte block sizes).

---

### [`archive/`](archive/) — Argon40 reference scripts

Original Python scripts from the Argon40 kickstarter campaign. Kept here as a reference for understanding the hardware behaviour documented during the campaign. **Do not install from this directory** — this code predates the current driver and monitor and is not maintained.

---

## Pictures

Discharging at 88%:
![Discharging](/pictures/PXL_20251007_022637735.jpg)

Charging:
![Charging](/pictures/PXL_20251007_022658734.jpg)
