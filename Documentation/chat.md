# Chat App

ESP-NOW based chat application with channel-based messaging. Devices with the same encryption key can communicate in real-time without requiring a WiFi access point or internet connection.

## Features

- **Channel-based messaging**: Join named channels (e.g. `#general`, `#random`) to organize conversations
- **Broadcast support**: Messages with empty target are visible in all channels
- **Configurable nickname**: Identify yourself with a custom name (max 23 characters)
- **Unique sender ID**: Each device gets a random 32-bit ID on first launch for future DM support
- **Encryption key**: Optional shared key for private group communication
- **Persistent settings**: Sender ID, nickname, key, and current chat channel are saved across reboots

## Requirements

- ESP32 with WiFi support (not available on ESP32-P4)
- ESP-NOW service enabled

## UI Layout

```text
+------------------------------------------+
| [Back] Chat: #general    [List] [Gear]   |
+------------------------------------------+
|  alice: hello everyone                   |
|  bob: hey alice!                         |
|  You: hi there                           |
|  (scrollable message list)               |
+------------------------------------------+
| [____input textarea____] [Send]          |
+------------------------------------------+
```

- **Toolbar title**: Shows `Chat: <channel>` with the current channel name
- **List icon**: Opens channel selector to switch channels
- **Gear icon**: Opens settings panel (nickname, encryption key)
- **Message list**: Shows messages matching the current channel or broadcast messages
- **Input bar**: Type and send messages to the current channel

## Channel Selector

Tap the list icon to change channels. Enter a channel name (e.g. `#general`, `#team1`) and press OK. The message list refreshes to show only messages matching the new channel.

Messages are sent with the current channel as the target. Only devices viewing the same channel will display the message. Broadcast messages (empty target) appear in all channels.

## First Launch

On first launch (when no settings file exists), the settings panel opens automatically so users can configure their nickname before chatting. A unique sender ID is also generated using the hardware RNG.

## Settings

Tap the gear icon to configure:

| Setting | Description | Default |
|---------|-------------|---------|
| Nickname | Your display name (max 23 chars) | `Device` |
| Key | Encryption key as 32 hex characters (16 bytes) | All zeros (empty field) |

Settings are stored in `/data/settings/chat.properties`. The encryption key is stored encrypted using AES-256-CBC. The sender ID is stored as a decimal number.

When the key field is left empty, the default all-zeros key is used. All devices using the default key can communicate without configuration.

Changing the encryption key causes ESP-NOW to restart with the new configuration.

## Wire Protocol v2

Compact variable-length packets broadcast over ESP-NOW:

### Header (16 bytes)

```text
Offset  Size   Field
------  ----   -----
0       4      magic (0x54435432 "TCT2")
4       2      protocol_version (2)
6       4      from (sender ID, random uint32)
10      4      to (recipient ID, 0 = broadcast/channel)
14      1      payload_type (1 = TextMessage)
15      1      payload_size (length of payload)
```

### Text Message Payload (variable)

```text
[nickname\0][target\0][message bytes]
```

- `nickname`: Null-terminated sender display name (max 23 chars + null)
- `target`: Null-terminated channel (e.g. `#general`) or empty for broadcast (max 23 chars + null)
- `message`: Remaining bytes, NOT null-terminated (length = `payload_size - strlen(nickname) - 1 - strlen(target) - 1`)

**Example calculation:** If nickname is "Alice" (5 chars) and target is "#general" (8 chars):
- Overhead: 5 + 1 + 8 + 1 = 15 bytes
- Max message: 255 - 15 = 240 bytes

### Example

"Alice" sends "Hi!" to #general:
- Header: 16 bytes
- Payload: `Alice\0#general\0Hi!` = 18 bytes
- **Total: 34 bytes**

### Size Limits

| Constraint | Value |
|------------|-------|
| Header size | 16 bytes |
| Max payload (uint8_t) | 255 bytes |
| Max nickname | 23 characters |
| Max channel/target | 23 characters |
| Max message | 200-251 bytes (varies by nickname/target length) |

### Payload Types

| Type | Value | Description |
|------|-------|-------------|
| TextMessage | 1 | Chat message with nickname, target, and text |
| (reserved) | 2+ | Future: Position, Telemetry, etc. |

### Target Field Semantics

| `to` Value | `target` Field | Meaning |
|------------|----------------|---------|
| 0 | `""` (empty) | Broadcast - visible in all channels |
| 0 | `#channel` | Channel message - visible only when viewing that channel |
| non-zero | `nickname` | Direct message (future - requires address discovery protocol) |

Messages with incorrect magic/version or invalid payload are silently discarded.

> **Note:** Direct messaging (non-zero `to`) will require an address discovery mechanism, such as periodic broadcasts announcing nickname→sender_id mappings, before devices can address each other directly.

## Architecture

```
ChatApp         - App lifecycle, ESP-NOW send/receive, settings management
ChatState       - Message storage (deque, max 100), channel filtering, mutex-protected
ChatView        - LVGL UI: toolbar, message list, input bar, settings/channel panels
ChatProtocol    - MessageHeader struct, serialize/deserialize, PayloadType enum
ChatSettings    - Properties file load/save with encrypted key storage, sender ID generation
```

All files are guarded with `#if defined(CONFIG_SOC_WIFI_SUPPORTED) && !defined(CONFIG_SLAVE_SOC_WIFI_SUPPORTED)` to exclude from P4 builds.

## Message Flow

### Sending

1. User types message and taps Send
2. `serializeTextMessage()` builds compact packet with sender ID, nickname, channel, message
3. Broadcast via ESP-NOW to nearby devices
4. Own message stored and displayed locally

### Receiving

1. ESP-NOW callback fires with raw data
2. Validate packet:
   - Minimum size: 18 bytes (16 header + 2 null terminators)
   - Magic bytes: must be `0x54435432` ("TCT2")
   - Protocol version: must be 2
   - Payload size: `header.payload_size` must equal `received_length - 16`
3. Parse null-terminated nickname and target from payload
4. Extract message from remaining bytes (length derived from payload_size)
5. Store in message deque with sender ID
6. Display if target matches current channel or is broadcast (empty)

## Limitations

- Maximum 100 stored messages (oldest discarded when full)
- Nickname: 23 characters max
- Channel name: 23 characters max
- Message text: 200 characters max (UI limit; actual wire limit varies by nickname/target length)
- No message persistence across app restarts (messages are in-memory only)
- All communication is broadcast; channel filtering is client-side only
- Sender ID collisions: 32-bit random IDs have ~50% collision probability at ~77,000 active devices (birthday paradox); no collision detection/resolution implemented

## Security Considerations

The chat protocol relies on ESP-NOW's built-in encryption (when configured) but has additional security limitations:

- **No message authentication**: No MAC/HMAC to verify message integrity or sender authenticity beyond the sender ID
- **No replay protection**: No sequence numbers or timestamps; messages can be replayed
- **Sender ID spoofing**: Any device knowing the encryption key can forge messages with arbitrary sender IDs
- **No forward secrecy**: Compromise of the shared key exposes all past and future messages

These tradeoffs are acceptable for casual local communication but should be understood before using for sensitive applications.
