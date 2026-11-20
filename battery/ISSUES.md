# Known Issues

## Low Severity

### Probe can block for up to ~2 s if the fuel gauge never becomes ready

`restart_battery_ic()` (only called when the battery profile has to be
reprogrammed) retries 3 times; each attempt sleeps 2 × 100 ms around the
control-register writes plus up to 5 × 100 ms polling `REG_ICSTATE`, for a
worst case of about 2.1 s. On healthy hardware the first poll succeeds; the
worst case only occurs with a dead or absent IC. Probe continues with a
warning if the profile cannot be loaded.

### `temp` accuracy is unverified

`TEMP` is read from register `0x06` using the CW2217 formula
(−40 °C + 0.5 °C/LSB). On the reference unit it has been observed to read a
constant 25.0 °C, which may mean the pack's NTC is not wired to the IC (the
CW2217 then reports a default). Because `HEALTH` reports `Overheat` above
55 °C and `Cold` below 0 °C from this value, it should be cross-checked with a
thermometer. Use `tools/cw2217_probe.py` to watch the raw register.

### Time estimates use integer SOC

`time_to_empty_avg` / `time_to_full_now` compute remaining charge from the
integer `capacity`, not the 1/256 % fractional SOC that `charge_now` uses, so
the estimate moves in 1 % steps.

## Resolved

- **`VOLTAGE_NOW` / `TEMP` returned fake values** — both now read the CW2217
  (`0x02`–`0x03` at 312.5 µV/LSB × 3 cells, and `0x06`), along with
  `CURRENT_NOW`, `POWER_NOW`, `CYCLE_COUNT` and SOH-based `CHARGE_FULL`.
- **`SERIAL_NUMBER` reported the kernel release string** — the property has
  been removed (the CW2217 has no readable serial number).
