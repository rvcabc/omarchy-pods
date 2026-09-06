#pragma once

#include <QList>

#include "enums.h"

// Only for a model where supportsNoiseControl(model) holds: ANC and Transparency are unconditional below, so the list is never empty.
namespace OpenPods
{
// Apple's long-press order, Off then ANC then Transparency then Adaptive; hasOff is supportsNoiseOff(model) and hasAdaptive is supportsAdaptiveAudio(model).
inline QList<int> availableModes(bool hasOff, bool hasAdaptive)
{
    using AirpodsTrayApp::Enums::NoiseControlMode;
    QList<int> modes;
    if (hasOff) {
        modes.append(static_cast<int>(NoiseControlMode::Off));
    }
    modes.append(static_cast<int>(NoiseControlMode::NoiseCancellation));
    modes.append(static_cast<int>(NoiseControlMode::Transparency));
    if (hasAdaptive) {
        modes.append(static_cast<int>(NoiseControlMode::Adaptive));
    }
    return modes;
}

// A current the model cannot hold (-1 before the first status, or a lacked mode) restarts at the head, which is also where walking forward from any lacked mode would land.
inline int nextNoiseMode(int current, bool hasOff, bool hasAdaptive)
{
    const QList<int> modes = availableModes(hasOff, hasAdaptive);
    const qsizetype index = modes.indexOf(current);
    if (index < 0) {
        return modes.first();
    }
    return modes.at((index + 1) % modes.size());
}
}
