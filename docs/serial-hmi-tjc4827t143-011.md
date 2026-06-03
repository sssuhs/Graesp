# TJC4827T143_011 serial HMI debug page

This note records the first GraEsp serial-screen mapping for the TJC4827T143_011 HMI.

## Wiring

Use the firmware defaults in `firmware/main/include/app_config.h`:

| ESP32-S3 | TJC screen |
| --- | --- |
| GPIO16 TX | RX |
| GPIO21 RX | TX |
| GND | GND |

The screen uses TTL UART. Power the screen according to the screen module requirements.

## UART

- Baud: `9600`
- Data bits: `8`
- Parity: none
- Stop bits: `1`
- Command ending: `0xFF 0xFF 0xFF`

## TJC page and controls

Create one page named `main`.

Text controls:

| Control name | Meaning |
| --- | --- |
| `t_device` | device id |
| `t_state` | alarm state |
| `t_rise` | temperature rise |
| `t_current` | estimated current |
| `t_prob` | overload probability |
| `t_battery` | battery percent |
| `t_ntc1` | NTC1 temperature |
| `t_ntc2` | NTC2 temperature |
| `t_ambient` | ambient temperature |
| `t_rate` | heating rate |
| `t_fault` | self-test and fault mask |

Progress/bar controls:

| Control name | Meaning |
| --- | --- |
| `j_prob` | overload probability, 0-100 |
| `j_battery` | battery percent, 0-100 |

## Firmware output

The firmware sends commands like:

```text
main.t_state.txt="正常" FF FF FF
main.t_rise.txt="2.35 C" FF FF FF
main.j_prob.val=36 FF FF FF
```

If the page or control names are different, update the names in `firmware/main/src/serial_hmi.c` or change the TJC project to match this table.
