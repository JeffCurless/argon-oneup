# Known Issues

## Medium Severity

### 1. `init_battery_profile`: I2C read error on `REG_CONTROL` not handled

**Location:** `init_battery_profile` (line 336)

```c
control = i2c_smbus_read_byte_data(client, REG_CONTROL);
if (control == 0) {
```

`i2c_smbus_read_byte_data` returns a negative error code on failure. A negative
value is not zero, so any I2C error causes `profile_ok` to stay false and the
function to fall through to the full profile-reprogram path — writing to the IC
over a broken bus. The error should be checked and returned explicitly:

```c
if (control < 0)
    return control;
```

---

### 3. `restart_battery_ic`: write return values ignored in retry loop

**Location:** `restart_battery_ic` (lines 288–291)

```c
i2c_smbus_write_byte_data(client, REG_CONTROL, CTRL_RESTART);
msleep(500);
i2c_smbus_write_byte_data(client, REG_CONTROL, CTRL_ACTIVE);
```

Both writes are fire-and-forget. Bus errors go unreported and the loop burns the
full 18-second retry budget (3 attempts × 6 seconds each) before returning
`-ETIMEDOUT`, with no indication of the real cause.

---

### 4. Missing `i2c_check_functionality` in probe

**Location:** `oneup_battery_probe` (before first I2C call)

The driver never verifies the adapter supports `I2C_FUNC_SMBUS_BYTE_DATA` before
issuing SMBUS calls. Standard Linux I2C driver practice:

```c
if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA))
    return -EOPNOTSUPP;
```

Without this, the driver binds to adapters that can't fulfill its requests and
the first I2C call returns an obscure error.

---

### 5. `bat->status` stale at boot after initial AC read

**Location:** `oneup_battery_probe` (lines 611–615)

After the boot-time AC read that corrects `bat->ac_online`, `bat->status` is
never updated from its initial `POWER_SUPPLY_STATUS_DISCHARGING` value. If the
charger is connected at boot, there is a brief window before the first workqueue
fires where the AC supply reports `ONLINE=1` but the battery supply reports
`DISCHARGING`. Fix: call `set_power_states(bat)` after the initial read.

---

### 6. Hardcoded `VOLTAGE_NOW` and `TEMP` return fake values

**Location:** `oneup_bat_get_property` (lines 530–534)

```c
case POWER_SUPPLY_PROP_TEMP:
    val->intval = 300;           /* always 30.0 °C */
case POWER_SUPPLY_PROP_VOLTAGE_NOW:
    val->intval = (4200 * 1000); /* always 4.2 V */
```

The CW2217 exposes actual cell voltage (registers `0x02`/`0x03`) and temperature
(`0x06`/`0x07`). Returning hardcoded values misleads userspace tools (upower,
desktop battery widgets). Either read the real registers in the poll loop and
cache the results, or remove both properties from `power_battery_props[]` so
callers receive `-EINVAL` rather than wrong data.

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
