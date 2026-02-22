# oneUpMon — System Monitor

A graphical system resource monitor for the Argon ONE UP laptop, built with PyQt6. It displays live rolling charts for CPU utilization, disk I/O, network I/O, temperatures, and fan speed, updated once per second.

## Requirements

```
PyQt6 (including QtCharts)
gpiozero
RPi.GPIO
smartmontools (smartctl, for drive temperatures)
```

## Running

```bash
python3 oneUpMon.py
```

The application reads its configuration from `/etc/sysmon.ini` at startup.

## Charts

The window displays four chart rows, each showing 60 seconds of history:

| Row | Chart | Notes |
|-----|-------|-------|
| 1 | **CPU Utilization** | One line per logical CPU core, 0–100% |
| 2 | **Disk I/O** | Read and write bytes/s per drive; auto-scales from Bytes/s through KiB/s, MiB/s, GiB/s |
| 3 | **Network I/O** | Read and write bytes/s per interface; same auto-scaling as disk |
| 4 | **Temperature** | CPU (via `gpiozero`) and each monitored drive (via `smartctl`), 20–80 °C |
| 4 (right half) | **Fan Speed** | CPU fan RPM, Raspberry Pi 5 only; optionally includes a case fan if configured |

The disk and network charts auto-scale their Y axis and unit label dynamically. When the peak value exceeds the current scale ceiling, all historical points are rescaled and the chart title updates (e.g., `Disk I/O (MiB/s)`). The scale steps back down when the window clears.

## Design

### Source files

| File | Purpose |
|------|---------|
| `oneUpMon.py` | GUI application — chart layout, timer loop, config loading |
| `systemsupport.py` | Hardware data — CPU load, CPU info/fan, drive stats, network stats |
| `configfile.py` | INI file reader wrapping `configparser`; never raises exceptions |
| `fanspeed.py` | Optional GPIO fan tachometer reader for a case fan |

### Data flow

Every second a `QTimer` fires `MonitorWindow.refresh_metrics()`, which calls into `systemsupport.py` classes and appends new values to each chart:

```
QTimer (1 s)
  └─ refresh_metrics()
       ├─ CPULoad.getPercentages()     → /proc/stat delta
       ├─ CPUInfo.temperature          → gpiozero CPUTemperature
       ├─ CPUInfo.CPUFanSpeed          → /sys/devices/platform/cooling_fan/hwmon/*/fan1_input  (Pi 5 only)
       ├─ GetCaseFanSpeed.RPM          → GPIO interrupt counter  (optional)
       ├─ multiDriveStat.readWriteBytes() → /sys/block/<dev>/stat delta × 512
       ├─ multiDriveStat.driveTemp()   → smartctl -A /dev/<drive>
       └─ NetworkLoad.stats            → /sys/class/net/<iface>/statistics/{rx,tx}_bytes delta
```

All data classes compute deltas between consecutive calls, so each sample represents throughput during the most recent interval, not a cumulative total.

Drive temperature is read by calling `smartctl -A /dev/<drive>` and searching for SMART attributes `194`, `190`, or the `Temperature:` field, in that order.

The CPU fan speed chart only appears on Raspberry Pi 5 / Compute Module 5 (detected by reading `/proc/cpuinfo`).

### Chart classes

- **`RollingChart`** — Fixed Y-axis. Points older than the 60-sample window are trimmed via binary search; the X axis range shifts to follow the newest point.
- **`RollingChartDynamic`** — Extends `RollingChart`. Wraps a `scaleValues` object that tracks the current unit tier. When any series value exceeds the current ceiling after scaling, all existing points are divided by 1024, the tier advances, and the chart title updates. The scale reverts one tier when the on-screen maximum drops below 1.

## Configuration

The configuration file is `/etc/sysmon.ini`. A sample file is provided at `monitor/sysmon.ini`. The file uses standard INI format; sections and keys that are absent are silently ignored and replaced with defaults.

---

### `[drive]` — Drive filtering

Controls which block devices appear in the temperature and performance charts. Device names must match exactly as they appear in `/sys/block/` (e.g., `nvme0n1`, `mmcblk0`, `sda`).

```ini
[drive]
    temp_ignore = mmcblk0, sda
    perf_ignore = mmcblk0
```

| Key | Default | Description |
|-----|---------|-------------|
| `temp_ignore` | *(none)* | Comma-separated list of devices to exclude from the Temperature chart. Useful for devices that don't expose SMART temperature data (e.g., SD cards). |
| `perf_ignore` | *(none)* | Comma-separated list of devices to exclude from the Disk I/O chart. |

---

### `[network]` — Network interface filtering

Controls which network interfaces appear in the Network I/O chart. Interface names must match exactly as listed in `/sys/class/net/`.

```ini
[network]
    device_ignore = lo, eth0
```

| Key | Default | Description |
|-----|---------|-------------|
| `device_ignore` | *(none)* | Comma-separated list of interfaces to exclude. Typically `lo` and any unused physical ports. |

---

### `[cooling]` — Case fan (optional)

Enables monitoring of a case fan connected to a GPIO pin via its tachometer signal. This is separate from the built-in CPU fan (which is always shown on Pi 5). If this section or key is absent, the case fan line is not added to the Fan Speed chart.

```ini
[cooling]
    casefan = 18
```

| Key | Default | Description |
|-----|---------|-------------|
| `casefan` | *(disabled)* | BCM GPIO pin number connected to the fan tachometer. The pin is configured with a pull-up resistor and uses a falling-edge interrupt to calculate RPM. The fan is assumed to generate 2 pulses per revolution (`PULSE = 2`). |

---

### `[smartctl]` — Per-drive smartctl options (optional)

Some drives require extra arguments to `smartctl` to be read correctly (e.g., drives behind a USB-SATA bridge need `-d sat`). Add one entry per drive that needs it.

```ini
[smartctl]
    sda = -d sat
    sdb = -d sat
```

| Key | Default | Description |
|-----|---------|-------------|
| `<device>` | *(none)* | The key is the device name (e.g., `sda`). The value is inserted before `-a /dev/<device>` in the smartctl command when reading that drive's temperature. |

Without a `[smartctl]` entry for a drive, the command used is `smartctl -A /dev/<device>`.
