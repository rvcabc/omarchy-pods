---
type: reference
title: WirePlumber's default sink is a selection stack, and moved streams get pinned
description: Why the daemon sets the default sink once per connect and never moves sink inputs
tags: [pipewire, wireplumber, audio]
status: stable
verified:
  - by: /usr/share/wireplumber/wireplumber.conf lines 944 and 1004 and ~/.local/state/wireplumber/default-nodes on beast
    at: 2026-09-06
---

# Two facts

`linking.follow-default-target = true`: a stream with no explicit target follows a default-sink
change, so setting the default is enough to move playback. `node.stream.restore-target = true`: a
stream moved by `pactl move-sink-input` (which `omarchy-audio-output-set-default` does) gets a
per-application target persisted in `stream-properties`, and that application returns to that sink
whenever it exists.

# What the daemon does

`default-nodes` is a most-recent-selection stack, so the pods only become default on connect while
they were the last output chosen. With `follow:on` (default) the daemon calls `setDefaultSink` on
the pods' sink once per control-link session, on the first profile activation, and again when it
reclaims them after a handoff interruption they held. It never moves sink inputs, so nothing gets
pinned; a stream the user pinned elsewhere stays where it is, as it does in the stock Audio panel.
