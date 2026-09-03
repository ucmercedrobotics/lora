# LoRa Modem

Firmware that presents a LoRa radio to a host as framed opaque bytes over a UART.

## 1. Requirements

- Python 3.6+
- A board with an SX127x-family radio (default environment: Arduino MKR WAN 1310)
- A USB-to-TTL adapter, if you want the debug output

## 2. Install PlatformIO

```sh
pip install platformio
```

First build downloads the toolchain and libraries into `~/.platformio` (~300 MB).

## 3. Configure

Defaults are in `src/config.h`. Override any of them with `build_flags` in
`platformio.ini` — every device on the link must be built with matching values
for the first six:

| flag | default |
| --- | --- |
| `LORA_FREQUENCY_HZ` | `915000000L` |
| `LORA_SPREADING_FACTOR` | `7` |
| `LORA_BANDWIDTH_HZ` | `125000L` |
| `LORA_CODING_RATE` | `5` |
| `LORA_PREAMBLE_SYMBOLS` | `8` |
| `LORA_SYNC_WORD` | `0x2B` |
| `LORA_TX_POWER_DBM` | `17` |
| `LORA_MAX_PAYLOAD_BYTES` | `200` |
| `LORA_HOST_PORT` | `Serial` |
| `LORA_DEBUG_PORT` | `Serial1` |
| `LORA_HOST_BAUD` | `115200` |
| `LORA_TX_QUEUE_DEPTH` | `6` |
| `LORA_STATS_PERIOD_MS` | `30000UL` |

```ini
[env:mkrwan1310]
platform = atmelsam
board = mkrwan1310
build_flags =
    -D LORA_SYNC_WORD=0x4F
    -D LORA_SPREADING_FACTOR=9
```

On a board other than the MKR WAN, add the radio's pins:

```ini
    -D LORA_SS_PIN=10 -D LORA_RESET_PIN=9 -D LORA_DIO0_PIN=2
```

## 4. Build

```sh
pio run -e mkrwan1310
```

## 5. Upload

```sh
pio run -e mkrwan1310 -t upload
```

Add `--upload-port <usb_port>` if more than one board is attached.