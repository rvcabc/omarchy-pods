---
type: reference
title: The pods echo control-command settings after the notification request
description: How the daemon turns echoes into truthful status keys, and which ids this unit has been seen to echo
tags: [airpods, aap]
status: draft
verified:
  - by: upstream docs (AAP Definitions, opcode 0x0009 marked bidirectional) and the Android app's ControlCommandRepository; the capture on this A2698 is still owed
    at: 2026-09-06
---

# The mechanism

Opcode 0x09 travels both ways. After `04 00 04 00 0f 00 ff ff ff ff ff` the pods send one frame per
setting they hold, and again whenever a setting changes on them. `controlcommandstate.hpp` records
every such frame; `control_ids_seen` in the status file lists the ids, and each setting key is
published from its echo when there is one, else from the value last asked for here. The settings
window marks the second case "not yet echoed", because the protocol never refuses a write.

# What this unit echoes

Not captured yet: the pods were in their case for the whole session that built this. The recorder
needs one connect with the debug drop-in on; the ids and their widths belong here once seen, along
with the opcode 0x4B level sequence of one spoken sentence (whether 0x03 arrives before a 6, 8 or 9).
