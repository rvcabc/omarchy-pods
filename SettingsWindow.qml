import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Quickshell
import Quickshell.Hyprland
import Quickshell.Wayland
import qs.Commons
import qs.Ui
import "Model.js" as Model

// The long tail of AirPods settings, laid out like macOS System Settings > AirPods: same overlay idiom as the
// Logitech plugin (scrim, centered card, Esc or the scrim to leave). Every row reads status.json through the
// service and writes through librepods-ctl; nothing here talks to Bluetooth.
PanelWindow {
  id: root

  property var service: null
  property var anchorItem: null
  property string fontFamily: Style.font.family
  property bool open: false

  readonly property var anchorWindow: anchorItem ? anchorItem.QsWindow.window : null
  // Held by NAME and re-resolved in the binding: a held ShellScreen object goes stale and the window migrates.
  property string targetScreenName: ""
  screen: {
    if (targetScreenName !== "") {
      var screens = Quickshell.screens
      for (var i = 0; i < screens.length; i++) {
        if (screens[i].name === targetScreenName) return screens[i]
      }
    }
    return anchorWindow ? anchorWindow.screen : null
  }

  visible: open
  anchors { top: true; bottom: true; left: true; right: true }
  color: "transparent"
  WlrLayershell.namespace: "omarchy-omapods-settings"
  WlrLayershell.layer: WlrLayer.Overlay
  // Exclusive only while open, or a hidden window would keep the keyboard from the desktop.
  WlrLayershell.keyboardFocus: open ? WlrKeyboardFocus.Exclusive : WlrKeyboardFocus.None
  exclusionMode: ExclusionMode.Ignore

  readonly property color foreground: Color.menu.text
  // Alpha toward the surface rather than Qt.darker, which inverts on light themes.
  readonly property color dim: Util.alpha(foreground, 0.58)
  readonly property color faint: Util.alpha(foreground, 0.38)
  readonly property color urgent: Color.urgent
  readonly property color accent: Color.accent

  readonly property bool hasPods: !!(service && service.hasAirPods)
  readonly property var settings: service ? service.podSettings : ({})
  readonly property var idsSeen: service ? service.controlIdsSeen : []

  function show() {
    targetScreenName = Hyprland.focusedMonitor ? Hyprland.focusedMonitor.name : ""
    if (service) service.refresh()
    open = true
  }
  function hide() { open = false }
  function toggle() { open ? hide() : show() }
  onOpenChanged: if (open) Qt.callLater(function () { keys.forceActiveFocus(); flick.contentY = 0 })

  function has(key) { return settings[key] !== undefined }
  function on(key) { return settings[key] === true }
  function text(key) { return has(key) ? String(settings[key]) : "unknown" }
  // A setting the pods have not echoed this session is shown as what was asked for, and says so.
  function unconfirmed(key) { return has(key) && !Model.settingConfirmed(key, idsSeen) }
  function set(key, value) { if (service) service.setPodSetting(key, value) }

  Rectangle {
    anchors.fill: parent
    color: Color.menu.scrim
  }

  MouseArea {
    anchors.fill: parent
    onClicked: root.hide()
  }

  BorderSurface {
    id: card
    width: Math.min(Style.space(560), root.width - Style.gapsOut * 2)
    height: Math.min(cardColumn.implicitHeight + contentTopInset + contentBottomInset,
                     root.height - Style.gapsOut * 4)
    radius: Style.cornerRadius
    anchors.centerIn: parent
    color: Color.menu.background
    borderSpec: Border.surfaceSpec("menu", "border", Color.menu.border, Math.max(1, Style.space(2)))
    padding: Style.spacing.panelPadding

    MouseArea { anchors.fill: parent; onClicked: {} }

    Item {
      id: keys
      anchors.fill: parent
      anchors.topMargin: card.contentTopInset
      anchors.rightMargin: card.contentRightInset
      anchors.bottomMargin: card.contentBottomInset
      anchors.leftMargin: card.contentLeftInset
      focus: true
      Keys.onPressed: function (event) {
        if (event.key === Qt.Key_Escape) { root.hide(); event.accepted = true }
      }

      ColumnLayout {
        id: cardColumn
        width: parent.width
        height: parent.height
        spacing: Style.space(10)

        RowLayout {
          Layout.fillWidth: true
          spacing: Style.space(10)

          AirPodsIcon {
            iconSize: Style.font.display
            color: root.foreground
            variant: root.service && root.service.isHeadset ? "max" : (root.service && root.service.isProSeries ? "pro" : "buds")
          }
          Text {
            text: "AirPods Settings"
            textFormat: Text.PlainText
            color: root.foreground
            font.family: root.fontFamily
            font.pixelSize: Style.font.title
          }
          Item { Layout.fillWidth: true }
          PanelActionButton {
            iconText: "󰑐"
            tooltipText: "Re-read the status file"
            foreground: root.foreground
            fontFamily: root.fontFamily
            onClicked: if (root.service) root.service.refresh()
          }
          PanelActionButton {
            iconText: "󰅖"
            tooltipText: "Close (Esc)"
            foreground: root.foreground
            fontFamily: root.fontFamily
            onClicked: root.hide()
          }
        }

        Text {
          Layout.fillWidth: true
          visible: !root.hasPods
          text: root.service && root.service.daemonReachable
            ? "The AirPods are not connected. Settings apply once they are."
            : "librepods is not running, so nothing can be read or set."
          textFormat: Text.PlainText
          color: root.dim
          font.family: root.fontFamily
          font.pixelSize: Style.font.bodySmall
          wrapMode: Text.WordWrap
        }

        Flickable {
          id: flick
          Layout.fillWidth: true
          Layout.preferredHeight: Math.min(body.implicitHeight, Style.space(680))
          Layout.fillHeight: true
          Layout.maximumHeight: Math.min(body.implicitHeight, Style.space(680))
          contentWidth: width
          contentHeight: body.implicitHeight
          clip: true
          boundsBehavior: Flickable.StopAtBounds
          flickableDirection: Flickable.VerticalFlick
          interactive: contentHeight > height
          // A hidden AsNeeded scrollbar still eats clicks along the right edge, so it is not interactive.
          ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded; interactive: false }

          Column {
            id: body
            width: flick.width - Style.spaceReal(10)
            spacing: Style.space(12)

            SectionCard {
              width: body.width
              title: "Name"
              Item {
                width: parent.width
                implicitHeight: nameField.implicitHeight + Style.space(4)
                TextField {
                  id: nameField
                  anchors.left: parent.left
                  anchors.right: nameHint.left
                  anchors.rightMargin: Style.space(10)
                  anchors.verticalCenter: parent.verticalCenter
                  foreground: root.foreground
                  accent: root.accent
                  text: root.service ? root.service.deviceName : ""
                  enabled: root.hasPods
                  onAccepted: root.set("device_name", text)
                }
                Text {
                  id: nameHint
                  anchors.right: parent.right
                  anchors.verticalCenter: parent.verticalCenter
                  text: Model.utf8ByteCount(nameField.text) + "/" + Model.RENAME_MAX_BYTES + " bytes, Enter applies"
                  textFormat: Text.PlainText
                  color: Model.utf8ByteCount(nameField.text) > Model.RENAME_MAX_BYTES ? root.urgent : root.faint
                  font.family: root.fontFamily
                  font.pixelSize: Style.font.caption
                }
              }
            }

            SectionCard {
              width: body.width
              title: "Noise control"
              visible: root.service && root.service.supportsNoiseControl
              ChoiceRowRaw {
                width: parent.width
                label: "Listening mode"
                value: root.service ? Model.noiseModeName(root.service.noiseMode) : ""
                onCycle: function (direction) {
                  var modes = root.service.availableModes()
                  if (modes.length === 0) return
                  var at = modes.indexOf(root.service.noiseMode)
                  root.service.setNoiseMode(modes[(at + direction + modes.length) % modes.length])
                }
              }
              SettingToggle { width: parent.width; key: "allow_off"; label: "Allow Off"; caption: "Keep Off in the stem's press-and-hold cycle" }
              Item {
                width: parent.width
                implicitHeight: cycleLabel.implicitHeight + cycleToggles.implicitHeight + Style.space(10)
                visible: root.has("hold_cycle_modes") || root.hasPods
                Text {
                  id: cycleLabel
                  text: "Press and hold cycles through"
                  textFormat: Text.PlainText
                  color: root.foreground
                  font.family: root.fontFamily
                  font.pixelSize: Style.font.body
                }
                Row {
                  id: cycleToggles
                  anchors.top: cycleLabel.bottom
                  anchors.topMargin: Style.space(6)
                  spacing: Style.space(6)
                  Repeater {
                    model: root.service ? root.service.availableModes() : []
                    Rectangle {
                      id: modeChip
                      required property var modelData
                      readonly property int mask: root.has("hold_cycle_modes") ? Number(root.settings.hold_cycle_modes) : 0
                      readonly property bool active: Model.modesFromMask(mask).indexOf(modelData) >= 0
                      width: chipText.implicitWidth + Style.space(18)
                      height: chipText.implicitHeight + Style.space(8)
                      radius: height / 2
                      color: active ? Style.selectedFillFor(root.foreground, root.accent) : "transparent"
                      border.color: active ? Util.alpha(root.accent, 0.9) : root.faint
                      border.width: 1
                      Text {
                        id: chipText
                        anchors.centerIn: parent
                        text: Model.noiseModeName(modeChip.modelData)
                        textFormat: Text.PlainText
                        color: modeChip.active ? root.foreground : root.dim
                        font.family: root.fontFamily
                        font.pixelSize: Style.font.bodySmall
                      }
                      MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                          var modes = Model.modesFromMask(modeChip.mask)
                          var at = modes.indexOf(modeChip.modelData)
                          if (at >= 0) modes.splice(at, 1); else modes.push(modeChip.modelData)
                          // The pods need two modes to cycle between, so the last two chips cannot be turned off.
                          if (modes.length < 2) return
                          root.set("hold_cycle_modes", Model.maskFromModes(modes))
                        }
                      }
                    }
                  }
                }
              }
              HoldSideRow { width: parent.width; key: "hold_left"; label: "Press and hold, left" }
              HoldSideRow { width: parent.width; key: "hold_right"; label: "Press and hold, right" }
              SliderRowRaw {
                width: parent.width
                visible: root.service && root.service.supportsAdaptive
                label: "Adaptive noise level"
                caption: root.service && root.service.noiseMode === Model.NOISE_ADAPTIVE ? "" : "Applies while the listening mode is Adaptive"
                enabled: root.service && root.service.noiseMode === Model.NOISE_ADAPTIVE
                min: 0
                max: 100
                value: root.service ? root.service.adaptiveNoiseLevel : 0
                onCommitted: function (v) { root.service.setAdaptiveNoiseLevel(v) }
              }
            }

            SectionCard {
              width: body.width
              title: "Automatic ear detection"
              SettingToggle { width: parent.width; key: "ear_detection_on_bud"; label: "Detect ears on the buds"; caption: "Off, the pods stop reporting in-ear state entirely" }
              ChoiceRowRaw {
                width: parent.width
                label: "Pause playback"
                value: root.service ? Model.earDetectionName(root.service.earDetectionBehavior) : ""
                onCycle: function (direction) {
                  var next = (root.service.earDetectionBehavior + direction + Model.EAR_BEHAVIOR_COUNT) % Model.EAR_BEHAVIOR_COUNT
                  root.service.setEarDetectionBehavior(next)
                }
              }
            }

            SectionCard {
              width: body.width
              title: "Microphone"
              SettingChoice { width: parent.width; key: "mic_mode"; label: "Microphone"; caption: "Which bud carries the microphone" }
            }

            SectionCard {
              width: body.width
              title: "Volume"
              SettingToggle { width: parent.width; key: "volume_swipe"; label: "Volume control"; caption: "Swipe the stem to change volume" }
              SettingToggle { width: parent.width; key: "personalized_volume"; label: "Personalized Volume"; caption: "Adjusts media volume to the surroundings" }
              SettingSlider { width: parent.width; key: "tone_volume"; label: "Tone volume" }
            }

            SectionCard {
              width: body.width
              title: "Accessibility"
              SettingChoice { width: parent.width; key: "press_speed"; label: "Press speed" }
              SettingChoice { width: parent.width; key: "hold_duration"; label: "Press and hold duration" }
              SettingChoice { width: parent.width; key: "volume_swipe_speed"; label: "Volume swipe speed" }
              ToggleRowRaw {
                width: parent.width
                visible: root.service && root.service.supportsOneBudANC
                label: "Noise cancellation with one AirPod"
                caption: "Keep noise cancellation on with one pod in"
                checked: root.service ? root.service.oneBudANC : false
                onToggled: root.service.setOneBudANC(!root.service.oneBudANC)
              }
            }

            SectionCard {
              width: body.width
              title: "Case and sleep"
              SettingToggle { width: parent.width; key: "case_sounds"; label: "Charging case sounds" }
              SettingToggle { width: parent.width; key: "sleep_detection"; label: "Pause media when falling asleep" }
            }

            SectionCard {
              width: body.width
              title: "Connection"
              SettingToggle { width: parent.width; key: "connect_automatically"; label: "Connect automatically"; caption: "Documented as CC 0x20; confirmed against the iPhone's toggle once captured" }
              SettingToggle { width: parent.width; key: "allow_auto_connect"; label: "Allow automatic connection"; caption: "Documented as CC 0x36" }
              SettingToggle { width: parent.width; key: "audio_follow_on_connect"; label: "Audio follows the pods"; caption: "Make the AirPods the default output when they connect" }
              SettingToggle { width: parent.width; key: "handoff_connect_on_play"; label: "Connect on play"; caption: "Pull the pods off the phone when playback starts here" }
              InfoRow {
                width: parent.width
                label: "Audio source"
                value: root.service ? (root.service.audioSource.type + (root.service.audioSource.otherDevice ? " on another device" : "")) : ""
              }
              InfoRow {
                width: parent.width
                label: "Handoff"
                value: root.service ? (root.service.handoffClaimsTotal + " claims, " + root.service.handoffInterruptionsTotal + " interruptions"
                                      + (root.service.handoffInterrupted ? ", interrupted now" : "")) : ""
              }
            }

            SectionCard {
              width: body.width
              title: "Hearing"
              Text {
                width: parent.width
                text: root.service && root.service.hearingGateReady
                  ? "The adapter identifies as Apple, so the pods accept hearing settings."
                  : "The pods only accept hearing settings from a host that identifies as Apple. To opt in, set DeviceID = bluetooth:004C:0000:0000 in /etc/bluetooth/main.conf and run sudo systemctl restart bluetooth. This applies to every Bluetooth peer of this box and may cause periodic disconnects; remove the line to revert."
                textFormat: Text.PlainText
                color: root.service && root.service.hearingGateReady ? root.dim : root.faint
                font.family: root.fontFamily
                font.pixelSize: Style.font.caption
                wrapMode: Text.WordWrap
              }
              SettingToggle { width: parent.width; key: "hearing_aid"; label: "Hearing Aid"; caption: "Needs an audiogram enrolled from an iPhone"; interactive: root.service && root.service.hearingGateReady }
              SettingToggle { width: parent.width; key: "hearing_assist"; label: "Hearing Assistance"; interactive: root.service && root.service.hearingGateReady }
              SettingToggle { width: parent.width; key: "loud_sound_reduction"; label: "Loud Sound Reduction"; interactive: root.service && root.service.hearingGateReady }
            }

            SectionCard {
              width: body.width
              title: "Notifications"
              SettingToggle { width: parent.width; key: "notifications_enabled"; label: "Desktop notifications" }
              SettingToggle { width: parent.width; key: "notifications_connected"; label: "Battery banner on connect"; caption: "Also when the case opens nearby" }
            }

            SectionCard {
              width: body.width
              title: "About"
              InfoRow { width: parent.width; label: "Model"; value: root.service ? root.service.modelName : "" }
              InfoRow { width: parent.width; label: "Firmware"; value: root.service ? root.service.firmwareVersion : "" }
              InfoRow { width: parent.width; label: "Hardware revision"; value: root.service ? root.service.hardwareRevision : "" }
              InfoRow { width: parent.width; label: "Serial"; value: root.service ? root.service.serialNumber : "" }
              InfoRow { width: parent.width; label: "Left / right serial"; value: root.service ? (root.service.leftSerial + " / " + root.service.rightSerial) : "" }
              InfoRow { width: parent.width; label: "Settings echoed by the pods"; value: String(root.idsSeen.length) }
            }
          }
        }

        Rectangle {
          Layout.fillWidth: true
          visible: !!(root.service && (root.service.actionStatus !== "" || root.service.lastError !== ""))
          readonly property bool isError: !!(root.service && root.service.actionStatus !== "")
          radius: Style.cornerRadius
          color: Util.alpha(isError ? root.urgent : root.foreground, 0.08)
          border.width: Style.normalBorderWidth
          border.color: Util.alpha(isError ? root.urgent : root.foreground, 0.18)
          implicitHeight: settingsStatus.implicitHeight + Style.space(12)

          Text {
            id: settingsStatus
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: Style.space(10)
            anchors.rightMargin: Style.space(10)
            text: root.service ? (root.service.actionStatus !== "" ? root.service.actionStatus : root.service.lastError) : ""
            textFormat: Text.PlainText
            color: parent.isError ? root.urgent : root.dim
            font.family: root.fontFamily
            font.pixelSize: Style.font.bodySmall
            wrapMode: Text.WordWrap
          }
        }
      }
    }
  }

  // --- building blocks ------------------------------------------------------

  // A titled group; children are assigned through data: explicitly so they land inside the surface.
  component SectionCard: Column {
    id: sectionCard
    property string title: ""
    default property alias content: cardInner.data
    spacing: Style.space(6)

    data: [
      Text {
        visible: sectionCard.title !== ""
        width: sectionCard.width
        leftPadding: Style.space(4)
        text: sectionCard.title
        textFormat: Text.PlainText
        color: root.dim
        font.family: root.fontFamily
        font.pixelSize: Style.font.caption
        font.letterSpacing: 1.2
        font.capitalization: Font.AllUppercase
        elide: Text.ElideRight
      },
      Rectangle {
        width: sectionCard.width
        height: cardInner.implicitHeight + Style.space(20)
        radius: Style.cornerRadius
        color: Util.alpha(root.foreground, 0.04)
        border.width: Style.normalBorderWidth
        border.color: Util.alpha(root.foreground, 0.10)

        Column {
          id: cardInner
          anchors.left: parent.left
          anchors.right: parent.right
          anchors.verticalCenter: parent.verticalCenter
          anchors.leftMargin: Style.space(12)
          anchors.rightMargin: Style.space(12)
          spacing: Style.space(6)
        }
      }
    ]
  }

  // Label plus caption on the left; the control on the right is the child.
  component LabelledRow: Item {
    id: labelled
    property string label: ""
    property string caption: ""
    default property alias control: controlSlot.data
    implicitHeight: Math.max(labels.implicitHeight, controlSlot.implicitHeight, Style.space(30)) + Style.space(6)

    Column {
      id: labels
      anchors.verticalCenter: parent.verticalCenter
      width: parent.width * 0.55
      spacing: Style.space(1)
      Text {
        width: parent.width
        text: labelled.label
        textFormat: Text.PlainText
        color: root.foreground
        font.family: root.fontFamily
        font.pixelSize: Style.font.body
        elide: Text.ElideRight
      }
      Text {
        visible: labelled.caption !== ""
        width: parent.width
        text: labelled.caption
        textFormat: Text.PlainText
        color: root.faint
        font.family: root.fontFamily
        font.pixelSize: Style.font.caption
        wrapMode: Text.WordWrap
      }
    }
    Item {
      id: controlSlot
      anchors.right: parent.right
      anchors.verticalCenter: parent.verticalCenter
      implicitWidth: childrenRect.width
      implicitHeight: childrenRect.height
    }
  }

  component ToggleRowRaw: LabelledRow {
    id: toggleRaw
    property bool checked: false
    property bool interactive: true
    signal toggled()
    ToggleSwitch {
      checked: toggleRaw.checked
      interactive: toggleRaw.interactive && root.hasPods
      foreground: root.foreground
      accent: root.accent
      onToggled: toggleRaw.toggled()
    }
  }

  // A boolean daemon setting; absent means the pods never echoed it and nothing asked for it yet.
  component SettingToggle: ToggleRowRaw {
    id: settingToggle
    property string key: ""
    checked: root.on(key)
    caption: {
      if (!root.has(key)) return "Not reported yet"
      if (root.unconfirmed(key)) return "Set here, not yet echoed by the pods"
      return ""
    }
    onToggled: root.set(key, !root.on(key))
  }

  component ChoiceRowRaw: LabelledRow {
    id: choiceRaw
    property string value: ""
    signal cycle(int direction)
    ChoiceCycler {
      text: choiceRaw.value
      onCycle: function (direction) { choiceRaw.cycle(direction) }
    }
  }

  component SettingChoice: ChoiceRowRaw {
    id: settingChoice
    property string key: ""
    value: root.text(key)
    caption: root.unconfirmed(key) ? "Set here, not yet echoed by the pods" : ""
    onCycle: function (direction) {
      var spec = Model.settingByKey(key)
      if (!spec) return
      var choices = spec.choices.split("|")
      var at = choices.indexOf(root.text(key))
      at = at < 0 ? (direction > 0 ? 0 : choices.length - 1) : (at + direction + choices.length) % choices.length
      root.set(key, choices[at])
    }
  }

  // Press and hold per bud: the echo may say siri, but the only thing this box can set is noise control.
  component HoldSideRow: LabelledRow {
    id: holdSide
    property string key: ""
    caption: root.text(key) === "siri" ? "Siri is chosen from the iPhone" : ""
    Row {
      spacing: Style.space(8)
      Text {
        anchors.verticalCenter: parent.verticalCenter
        text: root.text(holdSide.key) === "noise" ? "Noise Control" : root.text(holdSide.key)
        textFormat: Text.PlainText
        color: root.foreground
        font.family: root.fontFamily
        font.pixelSize: Style.font.bodySmall
      }
      Button {
        visible: root.text(holdSide.key) !== "noise"
        text: "Set to Noise Control"
        onClicked: root.set(holdSide.key, "noise")
      }
    }
  }

  component SliderRowRaw: LabelledRow {
    id: sliderRaw
    property real min: 0
    property real max: 100
    property real value: 0
    signal committed(real v)
    Row {
      spacing: Style.space(8)
      HSlider {
        id: track
        anchors.verticalCenter: parent.verticalCenter
        enabled: sliderRaw.enabled && root.hasPods
        opacity: enabled ? 1 : 0.4
        min: sliderRaw.min
        max: sliderRaw.max
        value: sliderRaw.value
        onCommitted: function (v) { sliderRaw.committed(v) }
      }
      Text {
        anchors.verticalCenter: parent.verticalCenter
        width: Style.space(40)
        horizontalAlignment: Text.AlignRight
        text: Math.round(track.shown)
        textFormat: Text.PlainText
        color: root.foreground
        font.family: root.fontFamily
        font.pixelSize: Style.font.bodySmall
      }
    }
  }

  component SettingSlider: SliderRowRaw {
    id: settingSlider
    property string key: ""
    readonly property var spec: Model.settingByKey(key)
    min: spec ? spec.min : 0
    max: spec ? spec.max : 100
    value: root.has(key) ? Number(root.settings[key]) : min
    caption: root.has(key) ? (root.unconfirmed(key) ? "Set here, not yet echoed by the pods" : "") : "Not reported yet"
    onCommitted: function (v) { root.set(key, Math.round(v)) }
  }

  component InfoRow: RowLayout {
    id: infoRow
    property string label: ""
    property string value: ""
    spacing: Style.space(8)
    Text {
      text: infoRow.label
      textFormat: Text.PlainText
      color: root.dim
      font.family: root.fontFamily
      font.pixelSize: Style.font.bodySmall
    }
    Item { Layout.fillWidth: true }
    Text {
      Layout.maximumWidth: infoRow.width * 0.6
      text: infoRow.value === "" ? "unknown" : infoRow.value
      textFormat: Text.PlainText
      color: root.faint
      font.family: root.fontFamily
      font.pixelSize: Style.font.bodySmall
      elide: Text.ElideRight
    }
  }

  // Draggable horizontal slider: local value while dragging, committed(v) on release.
  component HSlider: Item {
    id: slider
    property real min: 0
    property real max: 100
    property real value: 0
    signal committed(real v)

    width: Style.space(150)
    height: Style.space(20)
    property real localValue: -3e38
    readonly property real shown: localValue > -3e38 ? localValue : value
    readonly property real fraction: max > min ? Math.max(0, Math.min(1, (shown - min) / (max - min))) : 0

    Rectangle {
      anchors.verticalCenter: parent.verticalCenter
      width: parent.width
      height: Style.spaceReal(6)
      radius: height / 2
      color: Util.alpha(root.foreground, 0.15)
    }
    Rectangle {
      anchors.verticalCenter: parent.verticalCenter
      width: Math.max(Style.space(3), parent.width * slider.fraction)
      height: Style.spaceReal(6)
      radius: height / 2
      color: root.accent
    }
    Rectangle {
      x: Math.max(0, Math.min(parent.width - width, parent.width * slider.fraction - width / 2))
      anchors.verticalCenter: parent.verticalCenter
      width: Style.space(13)
      height: width
      radius: width / 2
      color: root.foreground
      border.width: Math.max(1, Style.spaceReal(2))
      border.color: root.accent
    }
    MouseArea {
      id: sliderMouse
      anchors.fill: parent
      enabled: slider.enabled
      hoverEnabled: true
      cursorShape: Qt.PointingHandCursor
      function valueAt(x) {
        var fraction = Math.max(0, Math.min(1, x / slider.width))
        return Math.round(slider.min + fraction * (slider.max - slider.min))
      }
      onPressed: function (mouse) { slider.localValue = valueAt(mouse.x) }
      onPositionChanged: function (mouse) { if (pressed) slider.localValue = valueAt(mouse.x) }
      onReleased: function (mouse) {
        slider.committed(valueAt(mouse.x))
        slider.localValue = -3e38
      }
    }
  }

  // Cycles through a closed set of options.
  component ChoiceCycler: Row {
    id: cycler
    property string text: ""
    signal cycle(int direction)
    spacing: Style.space(4)

    PanelActionButton {
      iconText: "󰅁"
      foreground: root.dim
      fontFamily: root.fontFamily
      onClicked: cycler.cycle(-1)
    }
    Rectangle {
      anchors.verticalCenter: parent.verticalCenter
      // Wide enough for "Pause when both are out"; shorter values keep the same minimum so the pills line up.
      width: Math.max(Style.space(114), cyclerText.implicitWidth + Style.space(20))
      height: Style.space(24)
      radius: height / 2
      color: Util.alpha(root.foreground, 0.06)
      border.width: Style.normalBorderWidth
      border.color: Util.alpha(root.foreground, 0.15)

      Text {
        id: cyclerText
        anchors.fill: parent
        anchors.leftMargin: Style.space(8)
        anchors.rightMargin: Style.space(8)
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        text: cycler.text
        textFormat: Text.PlainText
        color: root.foreground
        font.family: root.fontFamily
        font.pixelSize: Style.font.bodySmall
        elide: Text.ElideMiddle
      }
    }
    PanelActionButton {
      iconText: "󰅂"
      foreground: root.dim
      fontFamily: root.fontFamily
      onClicked: cycler.cycle(1)
    }
  }
}
