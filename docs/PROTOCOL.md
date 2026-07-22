# BLE Protocol

## Device

```text
Name: NslWa
```

## Service

```text
12345678-1234-1234-1234-123456789aaa
```

## Characteristic

```text
12345678-1234-1234-1234-123456789aab
```

Properties:

- Write
- Write without response

## Time Packet

The companion app writes a UTF-8 text packet:

```text
H:M:S:WD:D:MO:Y
```

Fields:

| Field | Meaning | Example |
| --- | --- | --- |
| `H` | Hour, 24-hour clock | `14` |
| `M` | Minute | `35` |
| `S` | Second | `22` |
| `WD` | Weekday, Sunday = 0 | `3` |
| `D` | Day of month | `22` |
| `MO` | Month, January = 1 | `7` |
| `Y` | Full year | `2026` |

Example packet:

```text
14:35:22:3:22:7:2026
```
