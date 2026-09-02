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

Add `-D LORA_NO_DEBUG` to compile the debug port out.

## 4. Build

```sh
pio run -e mkrwan1310
```

## 5. Upload

```sh
pio run -e mkrwan1310 -t upload
```

Add `--upload-port /dev/ttyACM0` if more than one board is attached.

## 6. Watch the debug output

Wire a USB-TTL adapter to the debug port (pins 13/14 on the MKR WAN 1310), then:

```sh
pio device monitor --port /dev/ttyUSB0 -b 115200
```

Expected at boot:

```
lora modem up
  radio      SF7/BW125k/CR4-5 @ 915 MHz
  sync word  0x2B
  tx power   17 dBm
  ldro       off
  payload    200 B max, 317.7 ms on air
  dwell      ok, 252 B fits the 400 ms budget
```

Stop and re-flash if the `dwell` line says `ILLEGAL`: lower
`LORA_SPREADING_FACTOR`, raise `LORA_BANDWIDTH_HZ`, or lower
`LORA_MAX_PAYLOAD_BYTES` to the byte count the line names.

A counter line follows every 30 s:

```
stats host_rx=0 crc=0 cobs=0 len=0 oversize=0 resync=0 tx_queued=0 tx_dropped=0 air_oversize=0 host_tx_dropped=0
```

## 7. Connect the host

The framed port is the board's USB serial device, at 115200 8N1, no flow
control. Nothing but frames may be written to it.

```sh
ls /dev/ttyACM*
```

Point the host at that device. For `amiga_ros2_comms`:

```sh
ros2 launch amiga_ros2_comms lora_bridge.launch.py \
    serial_port:=/dev/ttyACM0 \
    rx_link_stats:=header \
    max_payload_bytes:=200
```

`max_payload_bytes` must match `LORA_MAX_PAYLOAD_BYTES`, and `rx_link_stats`
must be `header`.

## 8. Check the link

With two boards flashed and powered, publish a payload on one host and watch it
arrive on the other:

```sh
ros2 topic pub --once /lora/tx amiga_interfaces/msg/LoRaFrame '{data: [1,2,3]}'
ros2 topic echo /lora/rx
```

`host_rx` rises in the sender's stats line; `data`, `rssi` and `snr` appear on
the receiver's `/lora/rx`.
