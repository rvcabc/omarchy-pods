---
type: reference
title: The pods echo control-command settings after the notification request
description: How the daemon turns echoes into truthful status keys, and which ids this unit has been seen to echo
tags: [airpods, aap]
status: stable
verified:
  - by: upstream docs (AAP Definitions, opcode 0x0009 marked bidirectional) and the Android app's ControlCommandRepository; captured on this A2698 on 2026-09-06
    at: 2026-09-06
---

# The mechanism

Opcode 0x09 travels both ways. After `04 00 04 00 0f 00 ff ff ff ff ff` the pods send one frame per
setting they hold, and again whenever a setting changes on them. `controlcommandstate.hpp` records
every such frame; `control_ids_seen` in the status file lists the ids, and each setting key is
published from its echo when there is one, else from the value last asked for here. The settings
window marks the second case "not yet echoed", because the protocol never refuses a write.

# What this unit echoes

Captured 2026-09-06 on the AirPods Pro 2 (A2698, firmware string 81.2675000075000000.6877) with the
debug drop-in on. Right after `Request notifications packet written` the pods sent, all 11 bytes:

| CC id | payload | meaning |
|---|---|---|
| 0x0D | 03 | listening mode transparency |
| 0x18 | 00 | hold duration default |
| 0x17 | 00 | press speed default |
| 0x25 | 01 | volume swipe on |
| 0x23 | 00 | swipe speed default |
| 0x1F | 50 50 | tone volume 80, second byte mirrors it |
| 0x24 | 20 03 | call management, encoding still to learn from the iPhone |
| 0x28 | 02 | conversation awareness off |
| 0x26 | 01 | personalized volume on |
| 0x29 | 02 | SSL |
| 0x2C | 02 00 | hearing aid off, not enrolled |
| 0x2F | 00 | HPS gain swipe |
| 0x33 | 00 | hearing assist, neither 01 nor 02, published as null |
| 0x2E | 32 | adaptive level 50 |
| 0x1B | 02 | one-bud ANC off |
| 0x35 | 02 | sleep detection off |
| 0x3E | 02 | uplink EQ bud |

Not echoed on this firmware: 0x34 allow off, 0x1A hold cycle mask, 0x16 hold sides, 0x01 mic mode,
0x0A on-bud ear detection, 0x31 case tone, 0x20 and 0x36 auto-connect. Writes to 0x01, 0x16 and
0x34 went out and were not echoed back either, so those rows stay "not yet echoed" and publish the
persisted wish; whether the pods applied them is a hardware check by ear (mic side, allow-off in
the long-press cycle).

Also seen on the same connect: opcode 0x4E, 0x53 (headphone accommodation, all zero), 0x55, 0x17 (a
TLV accessory descriptor carrying the serials), 0x2E (a connected-devices list with MACs in normal
byte order), 0x08, 0x0C, and 0x0E naming this box as media with the MAC byte-reversed, which is
what handoff compares against. The opcode 0x4B level sequence is still owed: Conversation Awareness
was off.
