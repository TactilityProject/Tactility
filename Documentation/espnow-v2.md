# ESP-NOW v2.0 Support & Chat App Enhancements

## Summary

- Add ESP-NOW v2.0 support with version detection and larger payload capability
- Update Chat app to use variable-length messages (up to 1416 characters)
- Maintain backwards compatibility with ESP-NOW v1.0 devices

## ESP-NOW Service Changes

### New API

```cpp
// Get the ESP-NOW protocol version (1 or 2)
uint32_t espnow::getVersion();

// Get max payload size for current version (250 or 1470 bytes)
size_t espnow::getMaxDataLength();

// Constants
constexpr size_t MAX_DATA_LEN_V1 = 250;
constexpr size_t MAX_DATA_LEN_V2 = 1470;
```

### Version Detection

ESP-NOW version is queried on initialization and logged:
```text
I (15620) ESPNOW: espnow [version: 2.0] init
I [EspNowService] ESP-NOW version: 2.0
```

### Files Modified

| File | Change |
|------|--------|
| `Tactility/Include/Tactility/service/espnow/EspNow.h` | Added constants and `getVersion()`, `getMaxDataLength()` |
| `Tactility/Source/service/espnow/EspNow.cpp` | Implemented new functions |
| `Tactility/Private/Tactility/service/espnow/EspNowService.h` | Added `espnowVersion` member |
| `Tactility/Source/service/espnow/EspNowService.cpp` | Query version after init |

## Chat App Changes

### Larger Messages

- Message size increased from **127 to 1416 characters**
- Variable-length packet transmission (only sends actual message length)
- Backwards compatible: messages < 197 chars still work with v1.0 devices

### Wire Protocol

```text
Offset  Size   Field
------  ----   -----
0       4      header (magic: 0x31544354)
4       1      protocol_version (0x01)
5       24     sender_name
29      24     target
53      1-1417 message (variable length)
```

- **Min packet**: 54 bytes
- **Max packet**: 1470 bytes (v2.0 limit)

### Files Modified

| File | Change |
|------|--------|
| `Tactility/Private/Tactility/app/chat/ChatProtocol.h` | `MESSAGE_SIZE` = 1417, added header constants |
| `Tactility/Source/app/chat/ChatProtocol.cpp` | Variable-length serialize/deserialize |
| `Tactility/Source/app/chat/ChatApp.cpp` | Send actual packet size |
| `Tactility/Source/app/chat/ChatView.cpp` | Input field max length = 1416 |
| `Documentation/chat.md` | Updated protocol documentation |

## Compatibility

| Scenario | Result |
|----------|--------|
| v2.0 device sends short message (< 250 bytes) | v1.0 and v2.0 devices receive |
| v2.0 device sends long message (> 250 bytes) | Only v2.0 devices receive |
| v1.0 device sends message | v1.0 and v2.0 devices receive |

## Requirements

- ESP-IDF v5.4 or later (for ESP-NOW v2.0 support)

## Test Plan

- [ ] Verify "ESP-NOW version: 2.0" appears in logs on startup
- [ ] Send short message between two v2.0 devices
- [ ] Send long message (> 200 chars) between two v2.0 devices
- [ ] Verify `espnow::getMaxDataLength()` returns 1470
