# PC Modbus UI Tool

Simple Windows UI tool for testing the MSP430 Modbus RTU measurement registers.

## Run

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\pc_modbus_ui\modbus_test_gui.ps1
```

## Features

- COM port refresh and selection
- `FC03` / `FC04` read
- Start address and count input
- One-shot read
- Periodic watch mode
- Raw request and response log
- Decoded values for registers `0x0000` to `0x0007`

## Expected Serial Settings

- Baud rate: `9600`
- Data bits: `8`
- Parity: `None`
- Stop bits: `1`

## Implemented Register Decode

- `0x0000` / `0x0001`: `CO_PPM`
- `0x0002`: `VOUT_RAW`
- `0x0003`: `VREF_RAW`
- `0x0004` / `0x0005`: `TEMP_C`
- `0x0006` / `0x0007`: `VS_mV`
