---
type: reference
title: Loud Sound Reduction lives on ATT, which Qt cannot reach by PSM
description: Why attclient.cpp is a POSIX socket and not a QBluetoothSocket
tags: [airpods, att, qt]
status: stable
verified:
  - by: reading qtconnectivity's qbluetoothsocket_bluezdbus.cpp and the upstream Python tool
    at: 2026-09-06
---

# The measurement

`QBluetoothSocket::connectToService(address, port)` exists, but Qt picks its D-Bus backend whenever
bluetoothd is 5.46 or newer, and that backend answers a port with `ServiceNotFoundError`
("Connecting to port is not supported via Bluez DBus"). ATT on BR/EDR is PSM 31 with no SDP record,
so no UUID reaches it. That is why upstream shipped `hearing-aid-adjustments.py` as a separate
script and why the Rust rewrite removed ATT.

# What the daemon does

`attclient.cpp` opens `socket(AF_BLUETOOTH, SOCK_SEQPACKET, BTPROTO_L2CAP)` and connects to
`sockaddr_l2 { l2_psm = htobs(31), l2_bdaddr = pods, l2_bdaddr_type = BDADDR_BREDR }`, non-blocking,
wrapped in a QSocketNotifier. No capability is needed and the unit's
`RestrictAddressFamilies=AF_UNIX AF_BLUETOOTH AF_NETLINK` already allows it. One request is
outstanding at a time and answers within `Att::requestTimeoutMs`. Handles: transparency 0x18, loud
sound reduction 0x1B, hearing aid 0x2A (`attpdu.hpp`). The LSR CCCD does not work, so its state is
read back after each write rather than notified.
