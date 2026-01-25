# Chat App

ESP-NOW based chat application with channel-based messaging. Devices with the same encryption key can communicate in real-time without requiring a WiFi access point or internet connection.

## Features

- **Channel-based messaging**: Join named channels (e.g. `#general`, `#random`) to organize conversations
- **Broadcast support**: Messages with empty target are visible in all channels
- **Configurable nickname**: Identify yourself with a custom name (max 23 characters)
- **Encryption key**: Optional shared key for private group communication
- **Persistent settings**: Nickname, key, and current chat channel are saved across reboots

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

On first launch (when no settings file exists), the settings panel opens automatically so users can configure their nickname before chatting.

## Settings

Tap the gear icon to configure:

| Setting | Description | Default |
|---------|-------------|---------|
| Nickname | Your display name (max 23 chars) | `Device` |
| Key | Encryption key as 32 hex characters (16 bytes) | All zeros (empty field) |

Settings are stored in `/data/settings/chat.properties`. The encryption key is stored encrypted using AES-256-CBC.

When the key field is left empty, the default all-zeros key is used. All devices using the default key can communicate without configuration.

Changing the encryption key causes ESP-NOW to restart with the new configuration.

## Wire Protocol

Variable-length packed struct broadcast over ESP-NOW (ESP-NOW v2.0):

```text
Offset  Size   Field
------  ----   -----
0       4      header (magic: 0x31544354 "TCT1")
4       1      protocol_version (0x01)
5       24     sender_name (null-terminated, zero-padded)
29      24     target (null-terminated, zero-padded)
53      1-1417 message (null-terminated, variable length)
```

- **Minimum packet**: 54 bytes (header + 1 byte message)
- **Maximum packet**: 1470 bytes (ESP-NOW v2.0 limit)
- **v1.0 compatibility**: Messages < 250 bytes work with ESP-NOW v1.0 devices

Messages with incorrect magic/version or invalid length are silently discarded.

### Target Field Semantics

| Target Value | Meaning |
|-------------|---------|
| `""` (empty) | Broadcast - visible in all channels |
| `#channel` | Channel message - visible only when viewing that channel |
| `username` | Direct message |

## Architecture

```
ChatApp         - App lifecycle, ESP-NOW send/receive, settings management
ChatState       - Message storage (deque, max 100), channel filtering, mutex-protected
ChatView        - LVGL UI: toolbar, message list, input bar, settings/channel panels
ChatProtocol    - Variable-length Message struct, serialize/deserialize (v2.0 support)
ChatSettings    - Properties file load/save with encrypted key storage
```

All files are guarded with `#if defined(CONFIG_SOC_WIFI_SUPPORTED) && !defined(CONFIG_SLAVE_SOC_WIFI_SUPPORTED)` to exclude from P4 builds.

## Message Flow

### Sending

1. User types message and taps Send
2. Message serialized into `Message` struct with current nickname and channel as target
3. Broadcast via ESP-NOW to nearby devices
4. Own message stored and displayed locally

### Receiving

1. ESP-NOW callback fires with raw data
2. Validate: size within valid range (54-1470 bytes), magic and version must match
3. Copy into aligned local struct (avoids unaligned access on embedded platforms)
4. Extract sender name, target, and message as strings
5. Store in message deque
6. Display if target matches current channel or is broadcast (empty)

## Limitations

- Maximum 100 stored messages (oldest discarded when full)
- Nickname: 23 characters max
- Channel name: 23 characters max
- Message text: 1416 characters max (ESP-NOW v2.0)
- No message persistence across app restarts (messages are in-memory only)
- All communication is broadcast; channel filtering is client-side only
- Messages > 250 bytes only received by devices running ESP-NOW v2.0
