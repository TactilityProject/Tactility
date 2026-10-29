# Meshtastic backend

[Meshtastic](https://meshtastic.org) protocol backend for the mesh service (`mesh-module`). It registers itself with
`mesh_backend_register()` as `meshtastic`. It has a GPL-3.0 license, and none of its headers are part of the app SDK.

Supported:

- The LongFast, LongModerate, LongSlow, MediumFast, MediumSlow, ShortFast, ShortSlow and ShortTurbo modem presets (sync word 0x2B), in the frequency slot that the primary channel's
  name selects within the region's band, like Meshtastic does.
- Channels: the primary channel is preconfigured as "LongFast" with the default key. More channels can be added with
  `mesh_set_channel()`, with a 1-byte PSK index, or a 16 or 32-byte key.
- Text messages on channels, and direct messages to and from nodes. Direct messages request an acknowledgement and
  received ones are acknowledged.
- NodeInfo: sent at start and every 3 hours, received NodeInfo provides node names.

The node number is random and stored in the backend's data directory. The node doesn't rebroadcast packets for
others (like the CLIENT_MUTE role).

Not implemented: public key (PKI) encryption of direct messages, positions and telemetry.
Direct messages are encrypted with the primary channel's key, which current firmware still accepts.
