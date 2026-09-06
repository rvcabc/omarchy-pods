---
type: reference
title: The pods accept hearing settings only from a host that identifies as Apple
description: What the DeviceID gate covers, how the daemon detects it, and what changing it costs
tags: [airpods, bluez, hearing]
status: stable
verified:
  - by: bluetoothctl show on beast (Modalias usb:v1D6Bp0246d0557) and the upstream README's vendor-id table
    at: 2026-09-06
---

# The gate

Hearing Aid (CC 0x2C), Hearing Assistance (CC 0x33), Loud Sound Reduction and the transparency
customisation (both ATT) are ignored by the pods unless the host's Device ID record names Apple,
vendor 0x004C. BlueZ publishes the stock `usb:v1D6Bp0246d0557` (Linux Foundation, BlueZ, 5.87)
until `/etc/bluetooth/main.conf` carries `DeviceID = bluetooth:004C:0000:0000` and bluetoothd is
restarted. Custom EQ, stem configuration and head tracking are not gated.

# How the daemon knows

`BluetoothMonitor::adapterModalias()` reads `org.bluez.Adapter1.Modalias` at start and on every
control-link connect; `hearing_gate_ready` is true when it starts with `bluetooth:v004C`. Every
hearing verb answers `error: hearing features need DeviceID = ...` while it is false, so nothing
is sent that the pods would silently drop. The daemon never edits `main.conf`.

# What it costs

The identity is adapter-wide, so every Bluetooth peer sees an Apple host. Upstream reports periodic
A2DP disconnects while spoofed. The bond is not touched, but the pods may cache the old identity and
need a re-pair; the Magic Cloud Keys are requested again on the first connect afterwards, so the
BLE battery path heals itself. Remove the line and restart bluetooth to revert.
