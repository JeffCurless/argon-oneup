# oneUpPower — Battery Driver

A Linux kernel module for the Argon ONE UP laptop's battery controller. It
communicates with the CW2217 fuel gauge IC over I2C and integrates with the
kernel's power supply framework, exposing battery state (capacity, status,
charge) and AC power state through the standard sysfs interface at
`/sys/class/power_supply/`. A background work queue polls the hardware once
per second and triggers a graceful system shutdown when the charge drops below
a configurable threshold while unplugged.

---

## How probe() is triggered

The driver follows the standard Linux I2C driver model: loading the module
only *registers* it with the I2C subsystem. The driver's `probe()` function —
which initialises the battery IC, registers `BAT0` and `AC0`, and starts the
polling work — is not called until the kernel is told a device exists at
I2C address `0x64` on bus 1. There are two ways to do this:

| Method | How it works | When to use |
|--------|-------------|-------------|
| **`./install` + service** | `./install` writes `oneup-battery 0x64` to the I2C `new_device` sysfs interface, which triggers `probe()` immediately. `oneUpPower.service` repeats this write on every subsequent boot. | Development, DKMS, or any setup where a reboot to apply an overlay is inconvenient |
| **DT overlay** | The compiled overlay is placed in `/boot/firmware/overlays/` and referenced in `config.txt`. The firmware merges it into the Device Tree before boot, and the kernel calls `probe()` automatically. The service is not needed. | Long-term installation; upstream-preferred approach |

Both methods are described below. They are mutually exclusive in normal use —
the service skips the `new_device` write if the device is already present via
the overlay.

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

---

## Build

```bash
./build
```

Compiles `oneUpPower.ko` against the headers for the currently running kernel.
The resulting module must be built on the target Pi — cross-compiled binaries
will have a `vermagic` mismatch and will be refused by the kernel.

---

## Install and load

```bash
./install
```

Installs and starts the driver using the `modprobe` + service path. Specifically:

1. Copies `oneUpPower.ko` to `/lib/modules/$(uname -r)/kernel/drivers/power/supply/`
2. Adds `oneUpPower` to `/etc/modules` so it loads automatically on boot
3. Writes the default configuration to `/etc/modprobe.d/oneUpPower.conf`
4. Runs `depmod -a` to update the module dependency database
5. Loads the module with `modprobe` (reads `soc_shutdown` from the conf file)
6. Writes `oneup-battery 0x64` to `/sys/bus/i2c/devices/i2c-1/new_device`,
   which tells the I2C subsystem a device exists at that address and triggers
   `probe()` immediately (skipped if the DT overlay is already active and the
   device is already present)
7. Installs and enables `oneUpPower.service` so the device is re-instantiated on
   every subsequent boot after the module is loaded from `/etc/modules`

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
./remove
```

Fully uninstalls the driver. Specifically:

1. Disables and stops `oneUpPower.service`
2. Removes `/etc/systemd/system/oneUpPower.service`
3. Writes `0x64` to `/sys/bus/i2c/devices/i2c-1/delete_device` to release the
   I2C device cleanly before unloading (skipped if the device is not present)
4. Unloads the module with `rmmod`
5. Removes the `.ko` from `/lib/modules/`
6. Removes the `oneUpPower` entry from `/etc/modules`
7. Removes `/etc/modprobe.d/oneUpPower.conf`

> **Note:** If the DT overlay was installed separately, `./remove` does not
> touch it. To fully remove the DT overlay path, delete
> `/boot/firmware/overlays/argon-oneup-battery.dtbo`, remove the
> `dtoverlay=argon-oneup-battery` line from `/boot/firmware/config.txt`, and
> reboot.

---

## DT overlay (alternative install path)

The DT overlay is the upstream-preferred binding method. It replaces the
`new_device` sysfs write and the systemd service — the kernel binds the driver
automatically at boot without any userspace assistance.

The overlay source is at `dts/argon-oneup-battery.dts`. It declares a node on
`i2c1` at address `0x64` with `compatible = "argon40,oneup-battery"`, which
matches the driver's `of_device_id` table.

### Compile and install (run on the Pi)

```bash
dtc -I dts -O dtb -o argon-oneup-battery.dtbo dts/argon-oneup-battery.dts
sudo cp argon-oneup-battery.dtbo /boot/firmware/overlays/
```

Add to `/boot/firmware/config.txt`:

```
dtoverlay=argon-oneup-battery
```

Reboot. `probe()` will be called automatically during driver initialisation.

### Relationship to `./install` and `oneUpPower.service`

When the DT overlay is active, `./install` can still be used to copy the
module and set up `/etc/modules` and `/etc/modprobe.d/oneUpPower.conf`. The
`new_device` write in step 6 is skipped automatically because the device is
already present. `oneUpPower.service` is still installed but its `ExecStart`
is also a no-op in this case, so there is no conflict.

---

## DKMS (optional)

```bash
./setupdkms
```

Installs the driver into DKMS at `/usr/src/oneUpPower-1.0/`. With DKMS the
module is automatically rebuilt whenever the kernel is updated. Use this for
long-term installations that receive kernel updates. Device binding (via the
service or DT overlay) is still required separately.

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
