// Run with: deno run --allow-read tests/model.test.js
// Model.js has no exports, so it is evaluated here rather than imported.

const source = Deno.readTextFileSync(new URL("../Model.js", import.meta.url))
const Model = new Function(
  source + "; return { parseStatus, podFrom, defaultPod, noiseModeVerb, earDetectionVerb, levelFraction, levelText, podMeta, elideError, availableModes, NOISE_OFF, NOISE_ANC, NOISE_TRANSPARENCY, NOISE_ADAPTIVE, LEVEL_UNKNOWN, NOISE_UNKNOWN, EAR_PAUSE_ONE_OUT, LID_UNKNOWN, MAX_ERROR_CHARS, SETTINGS, NO_CONTROL_ID, HOLD_NONE, RENAME_MAX_BYTES, settingByKey, settingByField, settingVerb, settingValueFrom, settingConfirmed, utf8ByteCount, maskFromModes, modesFromMask, pendingAfter, dropHold, settle, earliestUntilMs, settlePodSettings, withSetting, enqueue, dequeue }"
)()

let failures = 0

function check(name, actual, expected) {
  const ok = JSON.stringify(actual) === JSON.stringify(expected)
  if (!ok) {
    failures++
    console.log("FAIL " + name + "\n  expected " + JSON.stringify(expected) + "\n  got      " + JSON.stringify(actual))
  }
}

// Byte for byte the line a running daemon wrote, counters and all, copied from the box.
const live = '{"adaptive_level_changes_total":0,"adaptive_noise_level":50,"ca_changes_total":0,"case":{"available":true,"charging":false,"level":100},"connect_calls_total":0,"connect_failures_total":0,"connected":true,"conversational_awareness":true,"device_name":"GM’s AirPods Pro","disconnect_calls_total":0,"disconnect_failures_total":0,"ear_detection_behavior":0,"ear_detection_changes_total":0,"forget_calls_total":0,"is_pro_series":true,"left":{"available":true,"charging":false,"in_ear":false,"level":79},"lid_state":2,"model_int":11,"model_name":"AirPods Pro 3","model_number":"A3064","noise_control_changes_total":0,"noise_mode":1,"one_bud_anc_changes_total":0,"one_bud_anc_mode":true,"reconnect_attempts_total":0,"reconnect_failures_total":0,"reopen_calls_total":0,"right":{"available":true,"charging":true,"in_ear":false,"level":100},"schema_version":1,"supports_noise_off":false}'

const good = Model.parseStatus(live)
check("live line parses", good.ok, true)
check("live modelName", good.modelName, "AirPods Pro 3")
check("live deviceName keeps the daemon's apostrophe", good.deviceName, "GM’s AirPods Pro")
check("live left level", good.left.level, 79)
check("live case has no in_ear", good.caseBattery, { level: 100, charging: false })
check("live supportsNoiseOff is honoured", good.supportsNoiseOff, false)
check("live noiseMode", good.noiseMode, 1)

// A fresh daemon omits left, right and case entirely rather than sending available:false.
const fresh = Model.parseStatus('{"connected":false,"noise_mode":-1,"schema_version":1}')
check("fresh line parses", fresh.ok, true)
check("fresh left is the default pod", fresh.left, Model.defaultPod())
check("fresh case is unknown", fresh.caseBattery.level, Model.LEVEL_UNKNOWN)
check("supports_noise_off absent means the model has Off", fresh.supportsNoiseOff, true)

// available:false means the daemon stopped hearing from the pod, so no flag survives.
const gone = Model.podFrom({ available: false, level: 82, charging: true, in_ear: true })
check("unavailable pod reports no level", gone.level, Model.LEVEL_UNKNOWN)
check("unavailable pod reports no charging", gone.charging, false)
check("unavailable pod reports no in_ear", gone.inEar, false)
check("unavailable pod shows no meta", Model.podMeta(gone), "")

// Every failure path returns the full default shape rather than throwing.
const empty = Model.parseStatus("")
check("empty input is not ok", empty.ok, false)
check("empty input names the failure", empty.lastError !== "", true)
check("empty input still has a left pod", empty.left, Model.defaultPod())

const garbage = Model.parseStatus("not json at all")
check("garbage is not ok", garbage.ok, false)
check("garbage is not schemaTooNew", garbage.schemaTooNew, false)

const noVersion = Model.parseStatus('{"connected":true}')
check("a line with no schema_version is not ok", noVersion.ok, false)

const tooNew = Model.parseStatus('{"schema_version":2,"connected":true}')
check("a newer schema is not ok", tooNew.ok, false)
check("a newer schema is flagged", tooNew.schemaTooNew, true)
check("a newer schema reports both versions", tooNew.lastError, "librepods speaks status schema 2, this panel reads 1")

check("null parses to the default shape", Model.parseStatus("null").ok, false)

// Verbs, and the out-of-range guards that keep a bad index off the command line.
check("noise verb for Adaptive", Model.noiseModeVerb(3), "noise:adaptive")
check("noise verb for an unknown mode is empty", Model.noiseModeVerb(Model.NOISE_UNKNOWN), "")
check("noise verb past the end is empty", Model.noiseModeVerb(4), "")
check("ear verb for never pause", Model.earDetectionVerb(2), "ear:off")
check("ear verb past the end is empty", Model.earDetectionVerb(3), "")

// Meter and label edges.
check("an unknown level draws an empty track", Model.levelFraction(Model.LEVEL_UNKNOWN), 0)
check("a level above 100 is clamped", Model.levelFraction(140), 1)
check("a negative level is clamped", Model.levelFraction(-5), 0)
check("an unknown level reads as dashes", Model.levelText(Model.LEVEL_UNKNOWN), "--")

// Errors are elided to one line rather than dumped.
const long = Model.elideError("x\n\n   y".repeat(60))
check("an elided error is one line", long.indexOf("\n"), -1)
check("an elided error fits the row", long.length <= Model.MAX_ERROR_CHARS, true)
check("elideError copes with nothing", Model.elideError(null), "")

// A Max sends one battery under "headset" and no pods at all, so the panel needs the flag to pick a shape.
const max = Model.parseStatus('{"connected":true,"device_name":"AirPods Max","headset":{"available":true,"charging":false,"level":100},"is_headset":true,"model_name":"AirPods Max","noise_mode":1,"schema_version":1}')
check("max line parses", max.ok, true)
check("max is flagged a headset", max.isHeadset, true)
check("max headset level", max.headset, { level: 100, charging: false })
check("max has no pods", max.left.level, Model.LEVEL_UNKNOWN)
check("max has no case", max.caseBattery.level, Model.LEVEL_UNKNOWN)

// Between connect and the first battery packet the daemon sends the flag with no headset object at all.
const maxFresh = Model.parseStatus('{"connected":true,"is_headset":true,"noise_mode":1,"schema_version":1}')
check("max before any battery packet is still a headset", maxFresh.isHeadset, true)
check("max before any battery packet has no level", maxFresh.headset.level, Model.LEVEL_UNKNOWN)

// A daemon too old to send is_headset must keep drawing the pod rows rather than an empty headset row.
check("no is_headset key means earbuds", good.isHeadset, false)
check("no headset key is unknown, not zero", good.headset.level, Model.LEVEL_UNKNOWN)

// The capability keys, which are what stops the panel offering a control the hardware ignores.
const ap4 = Model.parseStatus('{"connected":true,"is_headset":false,"is_pro_series":false,"model_name":"AirPods 4","noise_mode":-1,"schema_version":1,"supports_adaptive":false,"supports_conversational_awareness":false,"supports_noise_control":false,"supports_noise_off":true,"supports_one_bud_anc":false}')
check("plain AirPods 4 has no listening modes", ap4.supportsNoiseControl, false)
check("plain AirPods 4 has no adaptive", ap4.supportsAdaptive, false)

const ap4anc = Model.parseStatus('{"connected":true,"is_headset":false,"is_pro_series":false,"model_name":"AirPods 4","noise_mode":1,"schema_version":1,"supports_adaptive":true,"supports_conversational_awareness":true,"supports_noise_control":true,"supports_noise_off":true,"supports_one_bud_anc":true}')
check("AirPods 4 with ANC hardware gets adaptive despite not being Pro", ap4anc.supportsAdaptive, true)
check("AirPods 4 with ANC hardware gets conversation awareness", ap4anc.supportsConversationalAwareness, true)
check("AirPods 4 with ANC hardware gets one-bud ANC", ap4anc.supportsOneBudANC, true)

const max2 = Model.parseStatus('{"connected":true,"is_headset":true,"is_pro_series":false,"model_name":"AirPods Max 2","noise_mode":1,"schema_version":1,"supports_adaptive":true,"supports_conversational_awareness":true,"supports_noise_control":true,"supports_noise_off":true,"supports_one_bud_anc":false}')
check("Max 2 has adaptive", max2.supportsAdaptive, true)
check("Max 2 has no second bud to keep ANC on", max2.supportsOneBudANC, false)

// A daemon older than the capability keys falls back to the Pro flag, which is what it used to gate on.
check("older daemon, Pro device, keeps adaptive", good.supportsAdaptive, true)
check("older daemon, Pro device, keeps one-bud ANC", good.supportsOneBudANC, true)
check("older daemon assumes listening modes exist", good.supportsNoiseControl, true)
const oldPlain = Model.parseStatus('{"connected":true,"is_pro_series":false,"noise_mode":1,"schema_version":1}')
check("older daemon, non-Pro device, hides adaptive", oldPlain.supportsAdaptive, false)
check("a false capability key is not treated as absent", ap4.supportsNoiseControl, false)

// An AirPods Pro 1 is the case the capability keys exist for: Pro, and no Adaptive.
const pro1 = Model.parseStatus('{"connected":true,"is_headset":false,"is_pro_series":true,"model_name":"AirPods Pro","noise_mode":1,"schema_version":1,"supports_adaptive":false,"supports_conversational_awareness":false,"supports_noise_control":true,"supports_noise_off":true,"supports_one_bud_anc":true}')
check("a Pro with an explicit false keeps the key, not the Pro flag", pro1.supportsAdaptive, false)
check("the same Pro still gets conversation awareness taken away", pro1.supportsConversationalAwareness, false)

// Three booleans rather than a status object, the shape Service.qml calls it with.
function modesFor(status) {
  return Model.availableModes(status.supportsNoiseControl, status.supportsNoiseOff, status.supportsAdaptive)
}
check("no listening modes at all on a plain AirPods 4", modesFor(ap4), [])
check("a Pro 1 gets Off, Transparency and ANC but no Adaptive", modesFor(pro1),
  [Model.NOISE_OFF, Model.NOISE_TRANSPARENCY, Model.NOISE_ANC])
check("a Pro 3 loses Off and keeps Adaptive", modesFor(good),
  [Model.NOISE_TRANSPARENCY, Model.NOISE_ADAPTIVE, Model.NOISE_ANC])
check("a Max 2 gets all four", modesFor(max2),
  [Model.NOISE_OFF, Model.NOISE_TRANSPARENCY, Model.NOISE_ADAPTIVE, Model.NOISE_ANC])


// The settings table: every row names its verb, and the verb text is built from the table rather than typed per call site.
for (const spec of Model.SETTINGS) {
  if (spec.kind === "bool") check(spec.key + " on", Model.settingVerb(spec.key, true), spec.verb + ":on")
  if (spec.kind === "bool") check(spec.key + " off", Model.settingVerb(spec.key, false), spec.verb + ":off")
  if (spec.kind === "bool") check(spec.key + " rejects a number", Model.settingVerb(spec.key, 1), "")
  check(spec.key + " is found by key", Model.settingByKey(spec.key), spec)
  check(spec.key + " is found by field", Model.settingByField(spec.field), spec)
}
check("the table has every daemon setting", Model.SETTINGS.length, 25)
check("an unknown key is not found", Model.settingByKey("volume"), null)
check("an unknown key makes no verb", Model.settingVerb("volume", 50), "")

check("mic choice", Model.settingVerb("mic_mode", "left"), "mic:left")
check("mic rejects an unknown choice", Model.settingVerb("mic_mode", "both"), "")
check("mic rejects a number", Model.settingVerb("mic_mode", 2), "")
check("swipe speed choice", Model.settingVerb("volume_swipe_speed", "longest"), "swipespeed:longest")
check("press speed choice", Model.settingVerb("press_speed", "slowest"), "pressspeed:slowest")
check("hold duration choice", Model.settingVerb("hold_duration", "shortest"), "holdduration:shortest")

check("tone level", Model.settingVerb("tone_volume", 40), "tone:40")
check("tone at the floor", Model.settingVerb("tone_volume", 0), "tone:0")
check("tone at the ceiling", Model.settingVerb("tone_volume", 100), "tone:100")
check("tone over the ceiling", Model.settingVerb("tone_volume", 101), "")
check("tone below the floor", Model.settingVerb("tone_volume", -1), "")
check("tone rejects a fraction", Model.settingVerb("tone_volume", 40.5), "")
check("tone rejects a numeric string", Model.settingVerb("tone_volume", "40"), "")

check("hold modes mask", Model.settingVerb("hold_cycle_modes", 7), "holdmodes:7")
check("hold modes all four", Model.settingVerb("hold_cycle_modes", 15), "holdmodes:15")
check("hold modes rejects an empty cycle", Model.settingVerb("hold_cycle_modes", 0), "")
check("hold modes rejects a fifth bit", Model.settingVerb("hold_cycle_modes", 16), "")

check("hold left side", Model.settingVerb("hold_left", "noise"), "hold:left:noise")
check("hold right side", Model.settingVerb("hold_right", "siri"), "hold:right:siri")
check("hold off", Model.settingVerb("hold_left", "off"), "hold:left:off")
check("hold rejects an unknown action", Model.settingVerb("hold_left", "camera"), "")

check("rename", Model.settingVerb("device_name", "My Pods"), "rename:My Pods")
check("rename keeps a colon", Model.settingVerb("device_name", "Pods: Bryce"), "rename:Pods: Bryce")
check("rename rejects empty", Model.settingVerb("device_name", ""), "")
check("rename rejects a non-string", Model.settingVerb("device_name", 42), "")
check("rename at 32 ascii bytes", Model.settingVerb("device_name", "a".repeat(32)), "rename:" + "a".repeat(32))
check("rename over 32 ascii bytes", Model.settingVerb("device_name", "a".repeat(33)), "")
// Seventeen two-byte characters is 34 bytes, so the byte limit trips where a character count would not.
check("rename counts bytes, not characters", Model.settingVerb("device_name", "é".repeat(17)), "")
check("rename at 32 bytes of two-byte characters", Model.settingVerb("device_name", "é".repeat(16)), "rename:" + "é".repeat(16))
check("the curly apostrophe is three bytes", Model.utf8ByteCount("GM’s AirPods Pro"), 18)
check("an emoji is four bytes", Model.utf8ByteCount("\u{1F3A7}"), 4)

check("eq on", Model.settingVerb("custom_eq", { enabled: true, low: 50, mid: 50, high: 50 }), "eq:on:50:50:50")
check("eq off keeps the bands", Model.settingVerb("custom_eq", { enabled: false, low: 0, mid: 100, high: 30 }), "eq:off:0:100:30")
check("eq rejects a band over 100", Model.settingVerb("custom_eq", { enabled: true, low: 101, mid: 50, high: 50 }), "")
check("eq rejects a missing band", Model.settingVerb("custom_eq", { enabled: true, low: 50, mid: 50 }), "")
check("eq rejects a non-boolean enabled", Model.settingVerb("custom_eq", { enabled: 1, low: 50, mid: 50, high: 50 }), "")
check("eq rejects null", Model.settingVerb("custom_eq", null), "")

// The mask round trips through the same NOISE_* constants the mode list uses.
check("mask of Off, ANC and Transparency", Model.maskFromModes([Model.NOISE_OFF, Model.NOISE_ANC, Model.NOISE_TRANSPARENCY]), 7)
check("mask of all four", Model.maskFromModes([Model.NOISE_OFF, Model.NOISE_ANC, Model.NOISE_TRANSPARENCY, Model.NOISE_ADAPTIVE]), 15)
check("mask of Adaptive alone", Model.maskFromModes([Model.NOISE_ADAPTIVE]), 8)
check("mask of nothing", Model.maskFromModes([]), 0)
check("mask ignores an unknown mode", Model.maskFromModes([Model.NOISE_ANC, 7]), 2)
check("modes from 7", Model.modesFromMask(7), [Model.NOISE_OFF, Model.NOISE_ANC, Model.NOISE_TRANSPARENCY])
check("modes from 15", Model.modesFromMask(15), [Model.NOISE_OFF, Model.NOISE_ANC, Model.NOISE_TRANSPARENCY, Model.NOISE_ADAPTIVE])
check("modes come back in mode order", Model.modesFromMask(Model.maskFromModes([Model.NOISE_ADAPTIVE, Model.NOISE_OFF])), [Model.NOISE_OFF, Model.NOISE_ADAPTIVE])
check("modes from a fifth bit ignore it", Model.modesFromMask(16 | 2), [Model.NOISE_ANC])
for (let mask = 1; mask <= 15; mask++) {
  check("mask " + mask + " round trips", Model.maskFromModes(Model.modesFromMask(mask)), mask)
}

// Holds: the optimistic value stands until the daemon agrees or the hold expires, and each key settles on its own.
const holdMs = 4000
const clicked = 1000
const held = Model.pendingAfter({}, "noiseMode", 2, clicked, holdMs)
check("a hold records the value and its end", held, { noiseMode: { value: 2, untilMs: 5000 } })
check("pendingAfter leaves its input alone", Model.pendingAfter(held, "oneBudANC", true, clicked, holdMs) !== held, true)
check("a disagreeing report is held off", Model.settle(held, "noiseMode", 1, 2000), { value: 2, pending: held })
check("an agreeing report ends the hold", Model.settle(held, "noiseMode", 2, 2000), { value: 2, pending: {} })
check("an expired hold lets the report through", Model.settle(held, "noiseMode", 1, 5000), { value: 1, pending: {} })
check("one ms before expiry still holds", Model.settle(held, "noiseMode", 1, 4999).value, 2)
check("a field with no hold passes straight through", Model.settle(held, "oneBudANC", true, 2000), { value: true, pending: held })
check("a hold hides an absent report", Model.settle(held, "noiseMode", undefined, 2000).value, 2)
check("settle leaves its input alone", Model.settle(held, "noiseMode", 2, 2000).pending !== held, true)

const two = Model.pendingAfter(held, "oneBudANC", true, 3000, holdMs)
check("settling one key keeps the other", Model.settle(two, "noiseMode", 2, 3500).pending, { oneBudANC: { value: true, untilMs: 7000 } })
check("expiring one key keeps the other", Model.settle(two, "noiseMode", 1, 5000).pending, { oneBudANC: { value: true, untilMs: 7000 } })
check("dropHold removes only its key", Model.dropHold(two, "oneBudANC"), held)
check("dropHold of an unheld key changes nothing", Model.dropHold(two, "lidState"), two)
check("the earliest hold is the first to end", Model.earliestUntilMs(two), 5000)
check("no hold means nothing to wait for", Model.earliestUntilMs({}), Model.HOLD_NONE)

// Object values (the eq bands) settle by content, since two builds of the same bands are never the same reference.
const eqValue = { enabled: true, low: 50, mid: 50, high: 50 }
const eqHeld = Model.pendingAfter({}, "customEq", eqValue, clicked, holdMs)
check("an equal object ends the hold", Model.settle(eqHeld, "customEq", { enabled: true, low: 50, mid: 50, high: 50 }, 2000).pending, {})
check("a different object is held off", Model.settle(eqHeld, "customEq", { enabled: true, low: 50, mid: 50, high: 51 }, 2000).value, eqValue)

// The settings map settles per key: a held key the daemon has not published yet shows, then goes absent when the hold ends.
const allowHeld = Model.pendingAfter({}, "allowOff", true, clicked, holdMs)
check("a held setting shows before the daemon publishes it", Model.settlePodSettings(allowHeld, {}, 2000).podSettings, { allow_off: true })
check("a held setting keeps its hold while unpublished", Model.settlePodSettings(allowHeld, {}, 2000).pending, allowHeld)
check("an expired unpublished setting stays absent", Model.settlePodSettings(allowHeld, {}, 5000), { podSettings: {}, pending: {} })
check("a published match ends the hold", Model.settlePodSettings(allowHeld, { allow_off: true, mic_mode: "auto" }, 2000), { podSettings: { allow_off: true, mic_mode: "auto" }, pending: {} })
check("a published mismatch is held off", Model.settlePodSettings(allowHeld, { allow_off: false }, 2000).podSettings, { allow_off: true })
check("withSetting copies", Model.withSetting({ mic_mode: "auto" }, "allow_off", true), { mic_mode: "auto", allow_off: true })

// The queue is first in, first out, and a repeat for a waiting field replaces it in place.
const a = { verb: "ca:on", field: "conversationalAwareness", optimistic: true }
const b = { verb: "onebud:off", field: "oneBudANC", optimistic: false }
const c = { verb: "adaptive:60", field: "adaptiveNoiseLevel", optimistic: 60 }
let queue = Model.enqueue(Model.enqueue(Model.enqueue([], a), b), c)
check("three queued in order", queue, [a, b, c])
let popped = Model.dequeue(queue)
check("first out is the first in", popped.item, a)
popped = Model.dequeue(popped.queue)
check("second out is the second in", popped.item, b)
popped = Model.dequeue(popped.queue)
check("third out is the third in", popped.item, c)
check("the queue is then empty", popped.queue, [])
check("dequeue on empty yields nothing", Model.dequeue([]), { item: null, queue: [] })
const c2 = { verb: "adaptive:70", field: "adaptiveNoiseLevel", optimistic: 70 }
check("a repeat for a waiting field replaces it in place", Model.enqueue(Model.enqueue([a, c], b), c2), [a, c2, b])
check("enqueue leaves its input alone", queue, [a, b, c])

// A setting is confirmed once the pods have echoed its control command; rename and eq have none to wait for.
check("allow_off echoed", Model.settingConfirmed("allow_off", ["0x0D", "0x34"]), true)
check("allow_off not yet echoed", Model.settingConfirmed("allow_off", ["0x0D"]), false)
check("allow_off with nothing echoed", Model.settingConfirmed("allow_off", []), false)
check("both hold sides share one command", Model.settingConfirmed("hold_right", ["0x16"]), true)
check("rename needs no echo", Model.settingConfirmed("device_name", []), true)
check("eq needs no echo", Model.settingConfirmed("custom_eq", []), true)
check("an unknown key is never confirmed", Model.settingConfirmed("volume", ["0x34"]), false)
check("rows without an id are the non-CC verbs and the daemon switches", Model.SETTINGS.filter(s => s.id === Model.NO_CONTROL_ID).map(s => s.key), ["device_name", "custom_eq", "notifications_enabled", "notifications_connected", "audio_follow_on_connect", "handoff_connect_on_play", "loud_sound_reduction"])

// The new status keys, present: one line with every kind of setting, the echo list and the identity strings.
const withSettings = Model.parseStatus('{"allow_off":true,"case":{"available":true,"charging":true,"level":100,"optimized_charging":true},"connected":true,"control_ids_seen":["0x0D","0x1A","0x34"],"custom_eq":{"enabled":true,"high":50,"low":40,"mid":45},"device_name":"My Pods","firmware_version":"7E93","hardware_revision":"1.0.0","hold_cycle_modes":7,"hold_left":"noise","hold_right":"siri","left":{"available":true,"charging":false,"in_ear":true,"level":79,"optimized_charging":true},"left_serial":"LEFTSERIAL","mic_mode":"auto","right":{"available":true,"charging":true,"in_ear":false,"level":100,"optimized_charging":false},"right_serial":"RIGHTSERIAL","schema_version":1,"serial_number":"CASESERIAL","tone_volume":40}')
check("settings line parses", withSettings.ok, true)
check("podSettings carries exactly the keys sent", withSettings.podSettings,
  { allow_off: true, hold_cycle_modes: 7, hold_left: "noise", hold_right: "siri", mic_mode: "auto", tone_volume: 40, device_name: "My Pods", custom_eq: { enabled: true, low: 40, mid: 45, high: 50 } })
check("control ids seen", withSettings.controlIdsSeen, ["0x0D", "0x1A", "0x34"])
check("firmware version", withSettings.firmwareVersion, "7E93")
check("hardware revision", withSettings.hardwareRevision, "1.0.0")
check("serial number", withSettings.serialNumber, "CASESERIAL")
check("left serial", withSettings.leftSerial, "LEFTSERIAL")
check("right serial", withSettings.rightSerial, "RIGHTSERIAL")
check("left pod optimized charging", withSettings.left.optimizedCharging, true)
check("right pod not optimized charging", withSettings.right.optimizedCharging, false)
check("a bool setting sent as a string is not true", Model.parseStatus('{"schema_version":1,"allow_off":"true"}').podSettings, { allow_off: false })
check("an eq reported with the bands in daemon order compares equal to a click", Model.settle(eqHeld, "customEq", Model.parseStatus('{"schema_version":1,"custom_eq":{"enabled":true,"high":50,"low":50,"mid":50}}').podSettings.custom_eq, 2000).pending, {})

// Daemon-side switches share the table, and their verbs already carry the sub-verb.
check("notify verb", Model.settingVerb("notifications_enabled", false), "notify:off")
check("notify connected verb", Model.settingVerb("notifications_connected", true), "notify:connected:on")
check("follow verb", Model.settingVerb("audio_follow_on_connect", true), "follow:on")
check("handoff verb", Model.settingVerb("handoff_connect_on_play", false), "handoff:connectonplay:off")
check("hearing aid verb", Model.settingVerb("hearing_aid", true), "hearingaid:on")
check("daemon switches count as confirmed", Model.settingConfirmed("audio_follow_on_connect", []), true)
check("hearing aid waits for its echo", Model.settingConfirmed("hearing_aid", []), false)

const handoff = Model.parseStatus('{"schema_version":1,"hearing_gate_ready":true,"audio_source":{"type":"media","other_device":true},"handoff_claims_total":3,"handoff_interruptions_total":1,"handoff_interrupted":true,"audio_follow_on_connect":false}')
check("hearing gate parses", handoff.hearingGateReady, true)
check("audio source parses", handoff.audioSource, { type: "media", otherDevice: true })
check("handoff counters parse", [handoff.handoffClaimsTotal, handoff.handoffInterruptionsTotal, handoff.handoffInterrupted], [3, 1, true])
check("follow switch lands in podSettings", handoff.podSettings.audio_follow_on_connect, false)
check("optimized charging leads the pod meta", Model.podMeta({ level: 80, charging: true, inEar: false, optimizedCharging: true }), "Optimized charging")

// The same keys absent: nothing is invented.
// device_name predates the settings table, so the rename row is the one setting an older daemon reports.
check("an older daemon reports only the name", good.podSettings, { device_name: "GM’s AirPods Pro" })
check("an older daemon has no echo list", good.controlIdsSeen, [])
check("an older daemon has no firmware version", good.firmwareVersion, "")
check("an older daemon has no serials", [good.serialNumber, good.leftSerial, good.rightSerial], ["", "", ""])
check("an older daemon has no hearing gate", good.hearingGateReady, false)
check("an older daemon has an unknown audio source", good.audioSource, { type: "unknown", otherDevice: false })
check("an older daemon's pods are not optimized charging", good.left.optimizedCharging, false)
check("the default pod is not optimized charging", Model.defaultPod().optimizedCharging, false)
check("an unavailable pod is not optimized charging", Model.podFrom({ available: false, optimized_charging: true }).optimizedCharging, false)
check("device_name alone is a setting too", Model.parseStatus('{"schema_version":1,"device_name":"GM’s AirPods Pro"}').podSettings, { device_name: "GM’s AirPods Pro" })

if (failures > 0) {
  console.log(failures + " failed")
  Deno.exit(1)
}
console.log("model.test.js: all checks passed")
