# Screen Gateway Board Notes

These notes are based on `Netlist_Schematic1_2026-06-03.tel` and `iKERF-C2F 模组手册V1.0.pdf`.

## MCU

Final module: `iKERF-C2F`, also named `XH-C2F` in the module manual.

The schematic/netlist uses an ESP32-C2/ESPC2 footprint as a substitute for the final iKERF-C2F module.

## Power Nets

- `+5V`: `CN1.1`, `H1.4`, regulator input `U3.1/U3.3`
- `3.3V`: regulator output `U3.5`, module power pins `U12.3/U12.8`, `H2.1`
- `GND`: `CN1.2`, `H1.1`, `H2.4`, `H3.2`, module `U12.9`

## UART / Screen Header

Netlist:

- `TXD`: `U12.16`, `H1.2`, `H2.2`
- `RXD`: `U12.15`, `H1.3`, `H2.3`

Module manual:

- `RXD0`: GPIO19, U0RXD
- `TXD0`: GPIO20, U0TXD

Firmware mapping:

- `GATEWAY_TJC_UART_NUM`: `UART_NUM_0`
- `GATEWAY_TJC_TX_GPIO`: `GPIO20`
- `GATEWAY_TJC_RX_GPIO`: `GPIO19`

Connect to the TJC screen as:

| Gateway board | TJC screen |
| --- | --- |
| TXD / GPIO20 | RX |
| RXD / GPIO19 | TX |
| GND | GND |

H1 includes `+5V` and is likely the practical TJC screen connector if the screen needs 5 V power. H2 includes `3.3V` and should only be used for low-current 3.3 V devices.

## Download Mode

Netlist:

- `IO9`: `U12.12`, `R17.1`, `H3.1`
- `R17.2`: `3.3V`
- `H3.2`: `GND`

Module manual:

- IO9 high: normal run
- IO9 low during reset/power-up: flash download mode

So H3 can be used to pull IO9 low for flashing if automatic download does not work.
