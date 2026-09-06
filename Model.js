// No QML imports on purpose, so every function here runs in a plain JS harness.

var NOISE_OFF = 0
var NOISE_ANC = 1
var NOISE_TRANSPARENCY = 2
var NOISE_ADAPTIVE = 3

var LID_OPEN = 0
var LID_CLOSED = 1
var LID_UNKNOWN = 2

var EAR_PAUSE_ONE_OUT = 0
var EAR_PAUSE_BOTH_OUT = 1
var EAR_DISABLED = 2

// Highest schema_version this panel knows how to read.
var SUPPORTED_SCHEMA = 1

// Level the daemon reports when a pod or the case has not been heard from.
var LEVEL_UNKNOWN = -1

// noise_mode before the daemon has identified the device.
var NOISE_UNKNOWN = -1

// How many ear-detection behaviours exist, so callers can cycle without a bare 3.
var EAR_BEHAVIOR_COUNT = 3

// nf-md-check U+F012C, the one candidate glyph that rendered on the box.
var GLYPH_CHECK = "\uDB80\uDD2C"

// Longest error the panel will show inside a row, and the cut that leaves room for the ellipsis.
var MAX_ERROR_CHARS = 140
var ELIDED_ERROR_CHARS = 137

function defaultPod() {
  return { level: LEVEL_UNKNOWN, charging: false, inEar: false, optimizedCharging: false }
}

// Full shape on every path, so the panel never reads undefined off a parse failure.
function defaultStatus() {
  return {
    ok: false,
    lastError: "",
    schemaVersion: 0,
    schemaTooNew: false,
    connected: false,
    deviceName: "",
    modelName: "",
    isProSeries: false,
    isHeadset: false,
    supportsNoiseOff: true,
    supportsNoiseControl: true,
    supportsAdaptive: false,
    supportsConversationalAwareness: false,
    supportsOneBudANC: false,
    noiseMode: NOISE_UNKNOWN,
    adaptiveNoiseLevel: 0,
    oneBudANC: false,
    conversationalAwareness: false,
    earDetectionBehavior: EAR_PAUSE_ONE_OUT,
    lidState: LID_UNKNOWN,
    left: defaultPod(),
    right: defaultPod(),
    caseBattery: { level: LEVEL_UNKNOWN, charging: false },
    headset: { level: LEVEL_UNKNOWN, charging: false },
    firmwareVersion: "",
    hardwareRevision: "",
    serialNumber: "",
    leftSerial: "",
    rightSerial: "",
    controlIdsSeen: [],
    podSettings: {}
  }
}

function intOr(value, fallback) {
  var n = parseInt(value, 10)
  return isFinite(n) ? n : fallback
}

// A daemon older than the capability keys sends nothing, which is not the same as false.
function boolOr(value, fallback) {
  return value === undefined ? fallback : value === true
}

function podFrom(raw) {
  var pod = defaultPod()
  if (!raw || typeof raw !== "object") return pod
  // available:false means the daemon has stopped hearing from this pod, so its charging and in_ear are stale too.
  if (raw.available !== true) return pod
  pod.level = intOr(raw.level, LEVEL_UNKNOWN)
  pod.charging = raw.charging === true
  pod.inEar = raw.in_ear === true
  pod.optimizedCharging = raw.optimized_charging === true
  return pod
}

// The whole of $XDG_STATE_HOME/librepods/status.json, one line, keys sorted by QJsonObject rather than by insert order:
// {"adaptive_level_changes_total":0,"adaptive_noise_level":50,"ca_changes_total":0,
//  "case":{"available":true,"charging":false,"level":100},"connect_calls_total":0,
//  "connect_failures_total":0,"connected":true,"conversational_awareness":true,
//  "device_name":"GM’s AirPods Pro","disconnect_calls_total":0,
//  "disconnect_failures_total":0,"ear_detection_behavior":0,
//  "ear_detection_changes_total":0,"forget_calls_total":0,"is_pro_series":true,
//  "left":{"available":true,"charging":false,"in_ear":false,"level":79},
//  "lid_state":2,"model_int":11,"model_name":"AirPods Pro 3","model_number":"A3064",
//  "noise_control_changes_total":0,"noise_mode":1,"one_bud_anc_changes_total":0,
//  "one_bud_anc_mode":true,"reconnect_attempts_total":0,"reconnect_failures_total":0,
//  "reopen_calls_total":0,
//  "right":{"available":true,"charging":true,"in_ear":false,"level":100},
//  "schema_version":1,"supports_adaptive":true,"supports_conversational_awareness":true,
//  "supports_noise_control":true,"supports_noise_off":false,"supports_one_bud_anc":true}
// A headset (AirPods Max) instead sends "is_headset":true and "headset":{"available":true,"charging":false,"level":100}, with no pods and no case.
// A daemon with the settings verbs also sends "optimized_charging":false inside left, right and case, the identity strings
// "firmware_version":"7E93","hardware_revision":"1.0.0","serial_number":"SCRUBBED","left_serial":"SCRUBBED","right_serial":"SCRUBBED",
// the echo list "control_ids_seen":["0x0D","0x1A","0x34"], and one key per setting it has heard or been asked for, such as
// "allow_off":true,"custom_eq":{"enabled":true,"high":50,"low":50,"mid":50},"hold_cycle_modes":7,"hold_left":"noise","mic_mode":"auto","tone_volume":40.
function parseStatus(raw) {
  var status = defaultStatus()
  var text = String(raw || "").trim()
  if (text === "") {
    status.lastError = "The librepods status file is empty"
    return status
  }

  var parsed
  try {
    parsed = JSON.parse(text)
  } catch (e) {
    status.lastError = "Could not read the librepods status file"
    return status
  }
  if (!parsed || typeof parsed !== "object" || parsed.schema_version === undefined) {
    status.lastError = "The librepods status file carried no schema_version"
    return status
  }

  status.schemaVersion = intOr(parsed.schema_version, 0)
  if (status.schemaVersion > SUPPORTED_SCHEMA) {
    // Newer daemon: report the version rather than draw fields we may be misreading.
    status.schemaTooNew = true
    status.lastError = "librepods speaks status schema " + status.schemaVersion + ", this panel reads " + SUPPORTED_SCHEMA
    return status
  }

  status.ok = true
  status.connected = parsed.connected === true
  status.deviceName = String(parsed.device_name || "")
  status.modelName = String(parsed.model_name || "")
  status.isProSeries = parsed.is_pro_series === true
  // Older daemons send neither key, so this stays false and the pod rows keep drawing.
  status.isHeadset = parsed.is_headset === true
  // Older daemons do not send this, and every model before the Pro 3 had Off.
  status.supportsNoiseOff = parsed.supports_noise_off !== false
  // A daemon without these keys falls back to what the panel used to gate on.
  status.supportsNoiseControl = boolOr(parsed.supports_noise_control, true)
  status.supportsAdaptive = boolOr(parsed.supports_adaptive, status.isProSeries)
  status.supportsConversationalAwareness = boolOr(parsed.supports_conversational_awareness, status.isProSeries)
  status.supportsOneBudANC = boolOr(parsed.supports_one_bud_anc, status.isProSeries)
  status.noiseMode = intOr(parsed.noise_mode, NOISE_UNKNOWN)
  status.adaptiveNoiseLevel = intOr(parsed.adaptive_noise_level, 0)
  status.oneBudANC = parsed.one_bud_anc_mode === true
  status.conversationalAwareness = parsed.conversational_awareness === true
  status.earDetectionBehavior = intOr(parsed.ear_detection_behavior, EAR_PAUSE_ONE_OUT)
  status.lidState = intOr(parsed.lid_state, LID_UNKNOWN)
  status.left = podFrom(parsed.left)
  status.right = podFrom(parsed.right)
  var caseRaw = podFrom(parsed["case"])
  status.caseBattery = { level: caseRaw.level, charging: caseRaw.charging }
  var headsetRaw = podFrom(parsed.headset)
  status.headset = { level: headsetRaw.level, charging: headsetRaw.charging }
  status.firmwareVersion = String(parsed.firmware_version || "")
  status.hardwareRevision = String(parsed.hardware_revision || "")
  status.serialNumber = String(parsed.serial_number || "")
  status.leftSerial = String(parsed.left_serial || "")
  status.rightSerial = String(parsed.right_serial || "")
  status.controlIdsSeen = Array.isArray(parsed.control_ids_seen) ? parsed.control_ids_seen : []
  status.podSettings = podSettingsFrom(parsed)
  return status
}

// The one list both the cycle and the panel rows are built from, in the order the panel draws them.
function availableModes(hasNoiseControl, hasOff, hasAdaptive) {
  // AirPods 1, 2, 3 and the plain AirPods 4 have no listening modes at all.
  if (!hasNoiseControl) return []
  var modes = []
  if (hasOff) modes.push(NOISE_OFF)
  modes.push(NOISE_TRANSPARENCY)
  if (hasAdaptive) modes.push(NOISE_ADAPTIVE)
  modes.push(NOISE_ANC)
  return modes
}

function noiseModeName(mode) {
  if (mode === NOISE_OFF) return "Off"
  if (mode === NOISE_ANC) return "Noise Cancellation"
  if (mode === NOISE_TRANSPARENCY) return "Transparency"
  if (mode === NOISE_ADAPTIVE) return "Adaptive"
  return "Unknown"
}

// The four AAP verbs, indexed by mode, in the order librepods-ctl accepts them.
function noiseModeVerb(mode) {
  var verbs = ["noise:off", "noise:anc", "noise:transparency", "noise:adaptive"]
  if (mode < 0 || mode >= verbs.length) return ""
  return verbs[mode]
}

function earDetectionVerb(behavior) {
  var verbs = ["ear:one", "ear:both", "ear:off"]
  if (behavior < 0 || behavior >= verbs.length) return ""
  return verbs[behavior]
}

function earDetectionName(behavior) {
  if (behavior === EAR_PAUSE_ONE_OUT) return "Pause when one is out"
  if (behavior === EAR_PAUSE_BOTH_OUT) return "Pause when both are out"
  if (behavior === EAR_DISABLED) return "Never pause"
  return "Unknown"
}

function levelText(level) {
  return level === LEVEL_UNKNOWN ? "--" : String(level) + "%"
}

// 0 to 1 for the meter; an unknown level draws an empty track rather than a full one.
function levelFraction(level) {
  if (level === LEVEL_UNKNOWN) return 0
  return Math.max(0, Math.min(100, level)) / 100
}

function lidText(lidState) {
  if (lidState === LID_OPEN) return "Open"
  if (lidState === LID_CLOSED) return "Closed"
  return ""
}

function podMeta(pod) {
  if (pod.charging) return "Charging"
  if (pod.inEar) return "In ear"
  return ""
}

// Collapse librepods-ctl's stderr into one line the panel can show inside a row.
function elideError(text) {
  var value = String(text || "").replace(/\s+/g, " ").trim()
  return value.length > MAX_ERROR_CHARS ? value.substring(0, ELIDED_ERROR_CHARS) + "…" : value
}

// Every setting the daemon exposes, one row each: key is the status.json key, verb the librepods-ctl family,
// id the control command the pods echo it back on ("" for rename and eq, which are not control commands).
var NO_CONTROL_ID = ""
var TONE_VOLUME_MIN = 0
var TONE_VOLUME_MAX = 100
var EQ_BAND_MIN = 0
var EQ_BAND_MAX = 100
// One bit per listening mode, so the mask runs from Off alone to all four.
var HOLD_CYCLE_MASK_MIN = 1
var HOLD_CYCLE_MASK_MAX = 15
// The rename frame carries the length in one byte and the pods cap it at 32, counted in UTF-8 bytes.
var RENAME_MIN_BYTES = 1
var RENAME_MAX_BYTES = 32

// side is set only on the two Sides rows, which share one control command and differ in the verb's middle segment.
var SETTINGS = [
  { key: "allow_off", field: "allowOff", verb: "allowoff", kind: "bool", choices: "", min: 0, max: 0, label: "Allow Off", id: "0x34" },
  { key: "hold_cycle_modes", field: "holdCycleModes", verb: "holdmodes", kind: "mask", choices: "", min: HOLD_CYCLE_MASK_MIN, max: HOLD_CYCLE_MASK_MAX, label: "Press and hold cycles through", id: "0x1A" },
  { key: "hold_left", field: "holdLeft", verb: "hold", kind: "sides", side: "left", choices: "noise|siri|off", min: 0, max: 0, label: "Press and hold, left", id: "0x16" },
  { key: "hold_right", field: "holdRight", verb: "hold", kind: "sides", side: "right", choices: "noise|siri|off", min: 0, max: 0, label: "Press and hold, right", id: "0x16" },
  { key: "mic_mode", field: "micMode", verb: "mic", kind: "choice", choices: "auto|right|left", min: 0, max: 0, label: "Microphone", id: "0x01" },
  { key: "ear_detection_on_bud", field: "earDetectionOnBud", verb: "eardetect", kind: "bool", choices: "", min: 0, max: 0, label: "Automatic ear detection", id: "0x0A" },
  { key: "volume_swipe", field: "volumeSwipe", verb: "swipe", kind: "bool", choices: "", min: 0, max: 0, label: "Volume swipe", id: "0x25" },
  { key: "volume_swipe_speed", field: "volumeSwipeSpeed", verb: "swipespeed", kind: "choice", choices: "default|longer|longest", min: 0, max: 0, label: "Volume swipe speed", id: "0x23" },
  { key: "personalized_volume", field: "personalizedVolume", verb: "pvol", kind: "bool", choices: "", min: 0, max: 0, label: "Personalized volume", id: "0x26" },
  { key: "tone_volume", field: "toneVolume", verb: "tone", kind: "level", choices: "", min: TONE_VOLUME_MIN, max: TONE_VOLUME_MAX, label: "Tone volume", id: "0x1F" },
  { key: "press_speed", field: "pressSpeed", verb: "pressspeed", kind: "choice", choices: "default|slower|slowest", min: 0, max: 0, label: "Press speed", id: "0x17" },
  { key: "hold_duration", field: "holdDuration", verb: "holdduration", kind: "choice", choices: "default|shorter|shortest", min: 0, max: 0, label: "Press and hold duration", id: "0x18" },
  { key: "case_sounds", field: "caseSounds", verb: "casetone", kind: "bool", choices: "", min: 0, max: 0, label: "Case sounds", id: "0x31" },
  { key: "sleep_detection", field: "sleepDetection", verb: "sleep", kind: "bool", choices: "", min: 0, max: 0, label: "Sleep detection", id: "0x35" },
  { key: "connect_automatically", field: "connectAutomatically", verb: "autoconnect", kind: "bool", choices: "", min: 0, max: 0, label: "Connect automatically", id: "0x20" },
  { key: "allow_auto_connect", field: "allowAutoConnect", verb: "allowautoconnect", kind: "bool", choices: "", min: 0, max: 0, label: "Allow automatic connection", id: "0x36" },
  // Held under deviceName in the pending map; the top-level deviceName property is the daemon's live name and is never held.
  { key: "device_name", field: "deviceName", verb: "rename", kind: "text", choices: "", min: RENAME_MIN_BYTES, max: RENAME_MAX_BYTES, label: "Name", id: NO_CONTROL_ID },
  { key: "custom_eq", field: "customEq", verb: "eq", kind: "eq", choices: "", min: EQ_BAND_MIN, max: EQ_BAND_MAX, label: "Custom EQ", id: NO_CONTROL_ID }
]

function settingByKey(key) {
  for (var i = 0; i < SETTINGS.length; i++) {
    if (SETTINGS[i].key === key) return SETTINGS[i]
  }
  return null
}

function settingByField(field) {
  for (var i = 0; i < SETTINGS.length; i++) {
    if (SETTINGS[i].field === field) return SETTINGS[i]
  }
  return null
}

function isIntegerIn(value, min, max) {
  return typeof value === "number" && isFinite(value) && Math.floor(value) === value && value >= min && value <= max
}

function hasChoice(spec, value) {
  return typeof value === "string" && spec.choices.split("|").indexOf(value) >= 0
}

// Sample input: "GM’s AirPods Pro" counts 18, the curly apostrophe being three bytes; a surrogate pair counts four.
function utf8ByteCount(text) {
  var bytes = 0
  for (var i = 0; i < text.length; i++) {
    var code = text.charCodeAt(i)
    if (code < 0x80) bytes += 1
    else if (code < 0x800) bytes += 2
    else if (code >= 0xD800 && code <= 0xDBFF) { bytes += 4; i++ }
    else bytes += 3
  }
  return bytes
}

function boolVerb(spec, value) {
  return spec.verb + ":" + (value ? "on" : "off")
}

// The exact librepods-ctl argument for one setting, or "" when the value is outside what the daemon's verb table accepts.
function settingVerb(key, value) {
  var spec = settingByKey(key)
  if (!spec) return ""
  switch (spec.kind) {
  case "bool":
    return value === true || value === false ? boolVerb(spec, value) : ""
  case "choice":
    return hasChoice(spec, value) ? spec.verb + ":" + value : ""
  case "sides":
    return hasChoice(spec, value) ? spec.verb + ":" + spec.side + ":" + value : ""
  case "level":
  case "mask":
    return isIntegerIn(value, spec.min, spec.max) ? spec.verb + ":" + value : ""
  case "text":
    if (typeof value !== "string") return ""
    var bytes = utf8ByteCount(value)
    return bytes >= spec.min && bytes <= spec.max ? spec.verb + ":" + value : ""
  case "eq":
    if (!value || typeof value !== "object" || (value.enabled !== true && value.enabled !== false)) return ""
    if (!isIntegerIn(value.low, spec.min, spec.max) || !isIntegerIn(value.mid, spec.min, spec.max)
        || !isIntegerIn(value.high, spec.min, spec.max)) return ""
    return boolVerb(spec, value.enabled) + ":" + value.low + ":" + value.mid + ":" + value.high
  }
  return ""
}

// Sample input: true for a bool row, "noise" for hold_left, 40 for tone_volume, 7 for hold_cycle_modes,
// {"enabled":true,"high":50,"low":50,"mid":50} for custom_eq; the eq keys are rebuilt in one fixed order so settle can compare by JSON text.
function settingValueFrom(spec, raw) {
  switch (spec.kind) {
  case "bool":
    return raw === true
  case "choice":
  case "sides":
  case "text":
    return String(raw)
  case "level":
  case "mask":
    return intOr(raw, spec.min)
  case "eq":
    var eq = raw && typeof raw === "object" ? raw : {}
    return { enabled: eq.enabled === true, low: intOr(eq.low, spec.min), mid: intOr(eq.mid, spec.min), high: intOr(eq.high, spec.min) }
  }
  return raw
}

// Only the keys the daemon sent, so a setting it has never heard or been asked for stays absent rather than invented.
function podSettingsFrom(parsed) {
  var settings = {}
  for (var i = 0; i < SETTINGS.length; i++) {
    var spec = SETTINGS[i]
    if (parsed[spec.key] !== undefined) settings[spec.key] = settingValueFrom(spec, parsed[spec.key])
  }
  return settings
}

// rename and eq are not control commands, so the pods never echo them and there is nothing to wait for.
function settingConfirmed(key, controlIdsSeen) {
  var spec = settingByKey(key)
  if (!spec) return false
  if (spec.id === NO_CONTROL_ID) return true
  return controlIdsSeen.indexOf(spec.id) >= 0
}

// The hold_cycle_modes mask carries one bit per listening mode: Off 1, ANC 2, Transparency 4, Adaptive 8.
var HOLD_CYCLE_MODES = [NOISE_OFF, NOISE_ANC, NOISE_TRANSPARENCY, NOISE_ADAPTIVE]

function holdCycleBit(mode) {
  return 1 << mode
}

function maskFromModes(modes) {
  var mask = 0
  for (var i = 0; i < HOLD_CYCLE_MODES.length; i++) {
    if (modes.indexOf(HOLD_CYCLE_MODES[i]) >= 0) mask |= holdCycleBit(HOLD_CYCLE_MODES[i])
  }
  return mask
}

// Sample input: 7 is Off, ANC and Transparency; 15 is all four; the modes come back in mode order, not in the order they were set.
function modesFromMask(mask) {
  var modes = []
  for (var i = 0; i < HOLD_CYCLE_MODES.length; i++) {
    if (mask & holdCycleBit(HOLD_CYCLE_MODES[i])) modes.push(HOLD_CYCLE_MODES[i])
  }
  return modes
}

// Optimistic holds, one per field as { value, untilMs }, and the verb queue. Pure and copy-on-write so the
// Deno harness covers them and Service.qml can assign the results straight into its var properties.
var HOLD_NONE = -1

function copyOf(map) {
  var copy = {}
  for (var key in map) copy[key] = map[key]
  return copy
}

function pendingAfter(pending, field, value, nowMs, holdMs) {
  var next = copyOf(pending)
  next[field] = { value: value, untilMs: nowMs + holdMs }
  return next
}

function dropHold(pending, field) {
  var next = copyOf(pending)
  delete next[field]
  return next
}

// Objects (the eq bands) compare by their JSON text, which is why settingValueFrom fixes their key order.
function sameValue(a, b) {
  if (a !== null && typeof a === "object") return JSON.stringify(a) === JSON.stringify(b)
  return a === b
}

// A hold ends when the daemon reports the held value or the clock passes untilMs; from then on the daemon's value wins.
function settle(pending, field, reported, nowMs) {
  var hold = pending[field]
  if (hold === undefined) return { value: reported, pending: pending }
  if (sameValue(reported, hold.value) || nowMs >= hold.untilMs) return { value: reported, pending: dropHold(pending, field) }
  return { value: hold.value, pending: pending }
}

// When the next hold ends, or HOLD_NONE while nothing is held, so one timer can sleep until it matters.
function earliestUntilMs(pending) {
  var earliest = HOLD_NONE
  for (var field in pending) {
    if (earliest === HOLD_NONE || pending[field].untilMs < earliest) earliest = pending[field].untilMs
  }
  return earliest
}

// Every setting at once: a key the daemon has not published shows its held value until the hold ends, then stays absent.
function settlePodSettings(pending, reported, nowMs) {
  var settings = {}
  for (var i = 0; i < SETTINGS.length; i++) {
    var spec = SETTINGS[i]
    var settled = settle(pending, spec.field, reported[spec.key], nowMs)
    pending = settled.pending
    if (settled.value !== undefined) settings[spec.key] = settled.value
  }
  return { podSettings: settings, pending: pending }
}

function withSetting(settings, key, value) {
  var next = copyOf(settings)
  next[key] = value
  return next
}

// Oldest first; a repeat for a field already waiting replaces it in place, which is what arrow-key repeat on a slider produces.
function enqueue(queue, item) {
  var next = queue.slice()
  for (var i = 0; i < next.length; i++) {
    if (next[i].field === item.field) {
      next[i] = item
      return next
    }
  }
  next.push(item)
  return next
}

function dequeue(queue) {
  return { item: queue.length > 0 ? queue[0] : null, queue: queue.slice(1) }
}
