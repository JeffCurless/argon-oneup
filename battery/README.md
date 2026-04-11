# oneUpPower — Battery Driver

A Linux kernel module for the Argon ONE UP laptop's battery controller. It
communicates with the CW2217 fuel gauge IC over I2C and integrates with the
kernel's power supply framework, exposing battery state (capacity, status,
charge) and AC power state through the standard sysfs interface at
`/sys/class/power_supply/`. A work queue polls the hardware once per second
and triggers a graceful system shutdown when the charge drops below a
configurable threshold while unplugged.

The driver uses the standard Linux I2C driver model. The kernel binds it to
the hardware automatically at boot through a Device Tree overlay that declares
the battery IC on `i2c1` at address `0x64`. The `./build` script compiles the
overlay and `./install` deploys it; a reboot is required to activate it.

---

## First-time setup

Run the `firsttime` script once to install the required build tools:

```bash
./firsttime
```

It detects the running OS and installs the appropriate packages:

| OS | Packages installed |
|----|--------------------|
| Raspberry Pi OS / Debian | `build-essential`, `linux-headers-rpi-v8`, `dkms` |
| Ubuntu | `build-essential`, `linux-headers-generic`, `dkms` |
| Alpine | `build-base`, `linux-dev` |

`device-tree-compiler` is also required to compile the overlay:

```bash
sudo apt install device-tree-compiler
```

---

## Build

```bash
./build
```

Compiles two artifacts in the `battery/` directory:

| File | Description |
|------|-------------|
| `oneUpPower.ko` | Kernel module, built against the running kernel's headers |
| `argon-oneup-battery.dtbo` | Device Tree overlay binary, compiled from `dts/argon-oneup-battery.dts` |

The module must be built on the target Pi — cross-compiled binaries will have
a `vermagic` mismatch and will be refused by the kernel.

---

## Install

```bash
sudo ./install
```

Installs the module and overlay, then updates the firmware configuration.
Specifically:

1. Copies `oneUpPower.ko` to `/lib/modules/$(uname -r)/kernel/drivers/power/supply/`
2. Adds `oneUpPower` to `/etc/modules` so the module loads automatically on boot
3. Writes the default configuration to `/etc/modprobe.d/oneUpPower.conf`
4. Runs `depmod -a` to update the module dependency database
5. Loads the module immediately with `modprobe`
6. Copies `argon-oneup-battery.dtbo` to `/boot/firmware/overlays/`
7. Appends `dtoverlay=argon-oneup-battery` to `/boot/firmware/config.txt`
   (skipped if the line is already present)

**A reboot is required** for the firmware to merge the overlay into the Device
Tree. After rebooting the kernel will call `probe()` automatically and `BAT0`
and `AC0` will appear in `/sys/class/power_supply/`.

Expected kernel log after `probe()` completes:

```
oneUpPower: Checking battery profile...
oneUpPower: Battery profile is valid.
oneUpPower: Probe successful (v1.0.3)
```

Watch in real time with:

```bash
sudo dmesg -w | grep oneUpPower
```

---

## Remove

```bash
sudo ./remove
```

Fully uninstalls the driver. Specifically:

1. Unloads the module with `rmmod`
2. Removes the `.ko` from `/lib/modules/`
3. Removes the `oneUpPower` entry from `/etc/modules`
4. Removes `/etc/modprobe.d/oneUpPower.conf`
5. Removes `argon-oneup-battery.dtbo` from `/boot/firmware/overlays/`
6. Removes the `dtoverlay=argon-oneup-battery` line from `/boot/firmware/config.txt`

**A reboot is required** for the firmware to stop applying the overlay.

---

## DKMS (optional)

```bash
./setupdkms
```

Installs the driver into DKMS at `/usr/src/oneUpPower-1.0/`. With DKMS the
module is automatically rebuilt whenever the kernel is updated. The Device
Tree overlay does not need to be reinstalled after a kernel update.

---

## Kconfig (in-tree build)

When building the driver as part of the kernel source tree, enable it with:

```
CONFIG_BATTERY_ONEUP=m
```

The `Kconfig` file contains the full entry. The `Makefile` supports both the
in-tree `obj-$(CONFIG_BATTERY_ONEUP)` path and the out-of-tree `obj-m` path
used by DKMS.

---

## Argon40 service conflict

The `argononeupd.service` daemon shipped by Argon40 also attempts to manage
battery reporting. Running both at the same time causes conflicts. Disable it
before using this driver:

```bash
sudo systemctl disable --now argononeupd.service
```

---

## Settings

### `soc_shutdown` — low-battery shutdown threshold

Controls the state-of-charge percentage at which the driver initiates an
automatic shutdown when the system is running on battery power alone. The
default is `5`.

The setting is stored in `/etc/modprobe.d/oneUpPower.conf`:

```
options oneUpPower soc_shutdown=5
```

Edit the file and reload the module to change it persistently, or write
directly to the sysfs parameter file to change it while the module is running:

```bash
echo 10 | sudo tee /sys/module/oneUpPower/parameters/soc_shutdown
```

| Value | Meaning |
|-------|---------|
| `0` | Disable automatic shutdown entirely |
| `1`–`20` | Shut down when battery drops below this percentage and AC is disconnected |

Values outside this range are rejected. A shutdown is triggered only when AC
power is disconnected **and** the reported state of charge falls below the
threshold.

---

## System interfaces

The driver registers two power supply devices with the kernel:

### `BAT0` — battery

Located at `/sys/class/power_supply/BAT0/`. Key attributes:

| Attribute | Description |
|-----------|-------------|
| `capacity` | State of charge, 0–100% |
| `status` | `Charging`, `Discharging`, or `Full` |
| `capacity_level` | Coarse level: `Full`, `High`, `Normal`, `Low`, or `Critical` |
| `charge_full` | Design capacity: 4,800,000 µAh (4,800 mAh) |
| `charge_now` | Estimated current charge in µAh |
| `time_to_empty_avg` | Estimated seconds of runtime remaining |
| `time_to_full_now` | Estimated seconds to full charge |
| `technology` | `Li-ion` |
| `manufacturer` | `Argon40` |
| `model_name` | `oneUp Battery` |

Capacity level thresholds:

| `capacity` | `capacity_level` |
|------------|------------------|
| > 95% | `Full` |
| > 85% | `High` |
| > 75% | `Normal` |
| > 40% | `Low` |
| ≤ 40% | `Critical` |

### `AC0` — AC power

Located at `/sys/class/power_supply/AC0/`. The single attribute `online` is
`1` when the charger is connected and `0` when running on battery.

---

## Diagnostics

Watch driver log messages in real time:

```bash
sudo dmesg -w | grep oneUpPower
```

The driver logs AC connect/disconnect events, capacity changes, and the
shutdown trigger. At load time it prints the version string and confirms
`probe()` succeeded.
