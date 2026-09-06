import QtQuick
import Quickshell
import Quickshell.Io
import "Model.js" as Model

Item {
  id: root

  property var settings: ({})

  property bool daemonReachable: false
  property bool connected: false
  property string deviceName: ""
  property string modelName: ""
  property bool isProSeries: false
  property bool isHeadset: false
  property bool supportsNoiseOff: true
  property bool supportsNoiseControl: true
  property bool supportsAdaptive: false
  property bool supportsConversationalAwareness: false
  property bool supportsOneBudANC: false
  property int noiseMode: Model.NOISE_UNKNOWN
  property int adaptiveNoiseLevel: 0
  property bool oneBudANC: false
  property bool conversationalAwareness: false
  property int earDetectionBehavior: Model.EAR_PAUSE_ONE_OUT
  property int lidState: Model.LID_UNKNOWN
  property bool schemaUnsupported: false
  property var leftPod: Model.defaultPod()
  property var rightPod: Model.defaultPod()
  property var caseBattery: ({ level: Model.LEVEL_UNKNOWN, charging: false })
  property var headsetBattery: ({ level: Model.LEVEL_UNKNOWN, charging: false })
  property string firmwareVersion: ""
  property string hardwareRevision: ""
  property string serialNumber: ""
  property string leftSerial: ""
  property string rightSerial: ""
  // Every setting the daemon has heard or been asked for, keyed as status.json spells them; absent means neither.
  property var podSettings: ({})
  // Control-command ids the pods echoed this session, as "0x34" strings.
  property var controlIdsSeen: []
  property bool hearingGateReady: false
  property var audioSource: ({ type: "unknown", otherDevice: false })
  property int handoffClaimsTotal: 0
  property int handoffInterruptionsTotal: 0
  property bool handoffInterrupted: false
  property string lastError: ""
  property string actionStatus: ""

  readonly property string ctlPath: String(setting("ctlPath", "") || "librepods-ctl")
  readonly property bool busy: commandProcess.running
  // The daemon publishes here on change, so there is nothing to poll.
  readonly property string statePath: (Quickshell.env("XDG_STATE_HOME")
    || Quickshell.env("HOME") + "/.local/state") + "/librepods/status.json"
  readonly property bool hasAirPods: daemonReachable && connected
  // Battery keeps arriving over BLE while the audio link is down, so it is not gated on connected.
  readonly property bool hasBattery: daemonReachable
    && (isHeadset
      ? headsetBattery.level !== Model.LEVEL_UNKNOWN
      : (leftPod.level !== Model.LEVEL_UNKNOWN
        || rightPod.level !== Model.LEVEL_UNKNOWN
        || caseBattery.level !== Model.LEVEL_UNKNOWN))

  // How long an optimistic value is held before the daemon's own state wins.
  readonly property int settleHoldMs: 4000
  readonly property int actionStatusMs: 2200

  // One hold per field, { value, untilMs }, so a write in flight for one control
  // cannot snap another back and an incoming read cannot undo a click the daemon
  // has not answered yet.
  property var _pending: ({})
  // Verbs waiting for the running librepods-ctl, oldest first.
  property var _queue: []
  property var _inFlight: null

  function setting(name, fallback) {
    var value = settings ? settings[name] : undefined
    return value === undefined || value === null ? fallback : value
  }

  function refresh() {
    stateFile.reload()
  }

  function applyLine(raw) {
    var status = Model.parseStatus(raw)
    if (!status.ok) {
      // A line we cannot read still proves the daemon is running and writing.
      daemonReachable = true
      connected = false
      schemaUnsupported = status.schemaTooNew
      lastError = status.lastError
      return
    }
    daemonReachable = true
    schemaUnsupported = false
    lastError = ""
    applyStatus(status)
  }

  // The daemon removes the file when it stops, so an absent file is a stopped daemon.
  function stateGone() {
    daemonReachable = false
    connected = false
    schemaUnsupported = false
    lastError = ""
  }

  function applyStatus(status) {
    connected = status.connected
    deviceName = status.deviceName
    modelName = status.modelName
    isProSeries = status.isProSeries
    isHeadset = status.isHeadset
    supportsNoiseOff = status.supportsNoiseOff
    supportsNoiseControl = status.supportsNoiseControl
    supportsAdaptive = status.supportsAdaptive
    supportsConversationalAwareness = status.supportsConversationalAwareness
    supportsOneBudANC = status.supportsOneBudANC
    leftPod = status.left
    rightPod = status.right
    caseBattery = status.caseBattery
    headsetBattery = status.headset
    lidState = status.lidState
    firmwareVersion = status.firmwareVersion
    hardwareRevision = status.hardwareRevision
    serialNumber = status.serialNumber
    leftSerial = status.leftSerial
    rightSerial = status.rightSerial
    controlIdsSeen = status.controlIdsSeen
    hearingGateReady = status.hearingGateReady
    audioSource = status.audioSource
    handoffClaimsTotal = status.handoffClaimsTotal
    handoffInterruptionsTotal = status.handoffInterruptionsTotal
    handoffInterrupted = status.handoffInterrupted

    var now = Date.now()
    noiseMode = _settle("noiseMode", status.noiseMode, now)
    adaptiveNoiseLevel = _settle("adaptiveNoiseLevel", status.adaptiveNoiseLevel, now)
    oneBudANC = _settle("oneBudANC", status.oneBudANC, now)
    conversationalAwareness = _settle("conversationalAwareness", status.conversationalAwareness, now)
    earDetectionBehavior = _settle("earDetectionBehavior", status.earDetectionBehavior, now)
    var settled = Model.settlePodSettings(_pending, status.podSettings, now)
    _pending = settled.pending
    podSettings = settled.podSettings
    _armSettleTimer()
  }

  function _settle(field, reported, nowMs) {
    var settled = Model.settle(_pending, field, reported, nowMs)
    _pending = settled.pending
    return settled.value
  }

  // A pod setting lives in the podSettings map; the five listening controls are properties of their own.
  function _show(field, value) {
    var spec = Model.settingByField(field)
    if (spec) podSettings = Model.withSetting(podSettings, spec.key, value)
    else root[field] = value
  }

  function _armSettleTimer() {
    var until = Model.earliestUntilMs(_pending)
    if (until === Model.HOLD_NONE) {
      settleTimer.stop()
      return
    }
    settleTimer.interval = Math.max(0, until - Date.now())
    settleTimer.restart()
  }

  function _send(verb, field, optimistic) {
    if (verb === "") return
    _pending = Model.pendingAfter(_pending, field, optimistic, Date.now(), settleHoldMs)
    _show(field, optimistic)
    _armSettleTimer()
    var item = { verb: verb, field: field, optimistic: optimistic }
    if (commandProcess.running) {
      _queue = Model.enqueue(_queue, item)
      return
    }
    _run(item)
  }

  function _run(item) {
    _inFlight = item
    // The hold restarts as the verb goes out, so a click that waited in the queue keeps its full hold.
    _pending = Model.pendingAfter(_pending, item.field, item.optimistic, Date.now(), settleHoldMs)
    _armSettleTimer()
    commandProcess.command = [ctlPath, item.verb]
    commandProcess.running = true
  }

  // Guards the keyboard and the bar's right click too, not just the panel rows.
  function setNoiseMode(mode) {
    if (availableModes().indexOf(mode) < 0) return
    _send(Model.noiseModeVerb(mode), "noiseMode", mode)
  }

  function availableModes() {
    return Model.availableModes(supportsNoiseControl, supportsNoiseOff, supportsAdaptive)
  }

  function cycleNoiseMode() {
    if (!hasAirPods) return
    var modes = availableModes()
    if (modes.length === 0) return
    var at = modes.indexOf(noiseMode)
    // An unknown current mode has no next one, so start at the head instead of past it.
    setNoiseMode(at < 0 ? modes[0] : modes[(at + 1) % modes.length])
  }

  function setAdaptiveNoiseLevel(level) {
    var clamped = Math.max(0, Math.min(100, Math.round(level)))
    _send("adaptive:" + clamped, "adaptiveNoiseLevel", clamped)
  }

  function setConversationalAwareness(enabled) {
    _send(enabled ? "ca:on" : "ca:off", "conversationalAwareness", enabled)
  }

  function setOneBudANC(enabled) {
    _send(enabled ? "onebud:on" : "onebud:off", "oneBudANC", enabled)
  }

  function setEarDetectionBehavior(behavior) {
    _send(Model.earDetectionVerb(behavior), "earDetectionBehavior", behavior)
  }

  function cycleEarDetection() {
    setEarDetectionBehavior((earDetectionBehavior + 1) % Model.EAR_BEHAVIOR_COUNT)
  }

  // Every daemon setting goes through here; Model.SETTINGS is the spec, and a value it rejects is a caller bug, not a user error.
  function setPodSetting(key, value) {
    var verb = Model.settingVerb(key, value)
    if (verb === "") {
      console.warn("omapods: setPodSetting(" + key + ") rejected " + JSON.stringify(value))
      return
    }
    var spec = Model.settingByKey(key)
    _send(verb, spec.field, Model.settingValueFrom(spec, value))
  }

  Timer {
    // Fires when the earliest hold ends and re-reads, because a verb that changed nothing
    // leaves the daemon's file untouched, so no watch fires to correct the display.
    id: settleTimer
    repeat: false
    onTriggered: root.refresh()
  }

  Timer {
    id: actionStatusTimer
    interval: root.actionStatusMs
    repeat: false
    onTriggered: root.actionStatus = ""
  }

  FileView {
    id: stateFile
    path: root.statePath
    watchChanges: true
    printErrors: false
    // text() is stale inside the change signal, so both paths go through reload.
    onFileChanged: reload()
    onLoaded: root.applyLine(text())
    onLoadFailed: root.stateGone()
  }

  Process {
    id: commandProcess
    running: false
    command: []
    stderr: StdioCollector { id: commandErr; waitForEnd: true }
    onExited: function (exitCode) {
      var done = root._inFlight
      root._inFlight = null
      if (exitCode !== 0) {
        // Only the refused verb's hold goes; the others are still waiting on their own answers.
        root._pending = Model.dropHold(root._pending, done.field)
        root._armSettleTimer()
        root.refresh()
        // Its own field with its own timer, or the next status read wipes it unread.
        root.actionStatus = Model.elideError(commandErr.text || "librepods-ctl rejected the command")
        actionStatusTimer.restart()
      }
      var next = Model.dequeue(root._queue)
      root._queue = next.queue
      if (next.item) root._run(next.item)
    }
  }
}
