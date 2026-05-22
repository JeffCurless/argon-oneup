# Known Issues

## Medium Severity

### ~~1. `init_battery_profile`: I2C read error on `REG_CONTROL` not handled~~ *(fixed)*

`i2c_smbus_read_byte_data` on `REG_CONTROL` now checks for a negative return
value and returns the error immediately, preventing a broken bus from silently
falling through to the full profile-reprogram path.

---

### ~~3. `restart_battery_ic`: write return values ignored in retry loop~~ *(fixed)*

Both `CTRL_RESTART` and `CTRL_ACTIVE` writes now check the return value. A
negative errno is logged and returned immediately — a broken bus no longer
burns the full 18-second retry budget before surfacing the real error.

---

### ~~4. Missing `i2c_check_functionality` in probe~~ *(fixed)*

`oneup_battery_probe` now calls `i2c_check_functionality` for
`I2C_FUNC_SMBUS_BYTE_DATA` as its first action and returns `-EOPNOTSUPP`
immediately if the adapter cannot fulfill SMBUS byte-data requests.

---

### ~~5. `bat->status` stale at boot after initial AC read~~ *(fixed)*

`set_power_states(bat)` is now called immediately after the boot-time AC read
in `oneup_battery_probe`, so `bat->status` and `bat->capacity_level` are
consistent with the real AC state before the power supplies are registered.

---

### 6. Hardcoded `VOLTAGE_NOW` and `TEMP` return fake values *(partially fixed)*

`POWER_SUPPLY_PROP_TEMP` and `POWER_SUPPLY_PROP_VOLTAGE_NOW` have been removed
from `power_battery_props[]` and their `case` branches dropped. Userspace now
receives `-EINVAL` (property not supported) rather than plausible-looking but
wrong data.

#### Path forward — real register reads (requires CW2217 datasheet)

The CW2217 exposes cell voltage at registers `0x02` (high) / `0x03` (low) and
die temperature at `0x06` (high) / `0x07` (low). Once the register map and
scaling factors are confirmed, the implementation path is:

1. **Add cached fields to `struct oneup_battery`:**
   ```c
   int voltage_uv;   /* cell voltage in µV */
   int temp_dc;      /* temperature in tenths of °C */
   ```

2. **Add a `check_voltage_temp()` helper** (alongside `check_battery_state`)
   that reads the two register pairs, applies the datasheet scaling, and
   writes the results under `bat->lock`.

3. **Call it from `oneup_battery_work()`** after `check_battery_state()`.

4. **Re-add the properties** to `power_battery_props[]` and their `case`
   branches in `oneup_bat_get_property()`, reading from the cached fields
   under the spinlock.

5. **Propagate changes** — include `voltage_uv` and `temp_dc` in the
   `power_supply_changed(bat->bat_psy)` condition check.

Use `tools/cw2217_probe.py` to dump raw register values from a live system
and cross-check against a known voltage (multimeter on the battery terminals)
and known temperature to reverse-engineer the scaling if the datasheet is
unavailable.

---

## Low Severity

### 7. `sprintf` used in module parameter getter

**Location:** `param_get_soc_shutdown` (line 697)

```c
return sprintf(buffer, "%d", soc_shutdown);
```

The kernel convention for sysfs/param callbacks is `sysfs_emit(buffer, ...)` (kernel 5.10+) or
at minimum `scnprintf`. Plain `sprintf` has no bounds checking on the provided buffer.

---

### 8. `module_param` declarations appear after `module_i2c_driver()`

**Location:** lines 670 vs. 706

`module_i2c_driver` expands to `module_init`/`module_exit`. Placing `module_param` and its
supporting ops struct after this macro is unconventional. Parameter declarations should
appear before the driver registration macro.

---

### 9. `REG_SOCALERT` write clobbers alert threshold bits

**Location:** `init_battery_profile` (line 396)

```c
i2c_smbus_write_byte_data(client, REG_SOCALERT, 0x80);
```

The entire register is written as `0x80` without a read-modify-write. If the CW2217 uses
lower bits of `REG_SOCALERT` for SOC alert thresholds, they are silently zeroed.

---

### 10. `POWER_SUPPLY_PROP_CHARGE_TYPE` always returns `FAST`

**Location:** `oneup_bat_get_property` (line 496)

```c
case POWER_SUPPLY_PROP_CHARGE_TYPE:
    val->intval = POWER_SUPPLY_CHARGE_TYPE_FAST;
```

When the battery is discharging this should return `POWER_SUPPLY_CHARGE_TYPE_NONE`.
Returning `FAST` while discharging misleads power managers and desktop environments.

---

### 11. `TIME_TO_FULL_NOW` / `TIME_TO_EMPTY_AVG` ignore charging state

**Location:** `oneup_bat_get_property` (lines 524–529)

- `TIME_TO_EMPTY_AVG` returns a non-zero value even when AC is connected and the
  battery is full — there is no meaningful time-to-empty while charging.
- `TIME_TO_FULL_NOW` returns a non-zero value even while discharging — there is no
  meaningful time-to-full while draining.

Both should return `0` in the inapplicable direction.
