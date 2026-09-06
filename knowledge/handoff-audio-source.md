---
type: reference
title: Handoff rides the opcode 0x0E audio-source frame and CC 0x06
description: What the pods tell every host about who holds them, and how the daemon claims and releases
tags: [airpods, aap, handoff]
status: stable
verified:
  - by: reading thisisgm/omarchy-pods PR 40 and the upstream Rust aacp.rs; the iPhone matrix is still owed on hardware
    at: 2026-09-06
---

# The frames

`04 00 04 00 0e [len] [6-byte MAC, byte-reversed] [type]` names the host that owns the pods' audio,
type 00 none, 01 call, 02 media. CC 0x06 with 0x01 claims ownership, 0x00 releases it.

# The rules the daemon follows (handoffstate.hpp)

- CLAIM only on a user-originated edge into playing, RELEASE only on a user-originated edge out of
  it. Repeated states (browser tabs registering MPRIS names) and the daemon's own ear-detection
  pause or resume send nothing.
- A frame naming another device as media is an interruption only when it follows a none, when it is
  not within `claimAckTimeoutMs` of our own claim, and when something plays here; a call always is.
- Another device's none while interrupted means reclaim: CLAIM, restore the pods as default only if
  they held it when the interruption began, then resume only the players the daemon paused.
- `handoff:connectonplay` (off by default) also pulls the pods off the phone when playback starts
  here with the link down; on by default it would fight Apple's own switching.

# Still to prove on the pods

The twelve-case matrix in the plan (interruptions, releases, name churn, ear pauses, a user moving
the default away during an interruption) has not been run; `handoff_claims_total` and
`handoff_interruptions_total` in the status file are the counters to read before and after.
