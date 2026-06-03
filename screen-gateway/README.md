# GraEsp Screen Gateway

This ESP-IDF project is for the ESP32-C2 board connected to the TJC4827T143_011 serial HMI screen.

System role:

```text
GraEsp measuring terminal -> WiFi -> screen gateway board -> UART -> TJC screen
```

The measuring terminal should keep measuring and publishing telemetry. This gateway project should receive that telemetry and translate it into TJC UART commands.

## Planned Modules

- WiFi station connection
- UDP telemetry receiver, local same-WiFi mode first
- MQTT telemetry subscriber, optional cloud/cross-network mode later
- TJC UART output
- Screen page/control mapping

## Reference Files

Put datasheets, TJC editor screenshots, page-control tables, exported `.tft` files, and protocol notes in `references/`.

Current board notes are recorded in `references/board-notes.md`.
