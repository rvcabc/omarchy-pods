#ifndef MEDIACONTROLLER_H
#define MEDIACONTROLLER_H

#include <QElapsedTimer>
#include <QObject>
#include "pulseaudiocontroller.h"
#include "../conversationlevel.hpp"

class QProcess;
class EarDetection;
class PlayerStatusWatcher;
class QDBusInterface;
class QTimer;

class MediaController : public QObject
{
  Q_OBJECT
public:
  enum MediaState
  {
    Playing,
    Paused,
    Stopped
  };
  Q_ENUM(MediaState)
  // Who caused a playback transition: the daemon's own pause and resume must not read as the user's.
  enum MediaOrigin
  {
    User,
    Daemon
  };
  Q_ENUM(MediaOrigin)
  enum EarDetectionBehavior
  {
    PauseWhenOneRemoved,
    PauseWhenBothRemoved,
    Disabled
  };
  Q_ENUM(EarDetectionBehavior)

  explicit MediaController(QObject *parent = nullptr);
  ~MediaController();

  void handleEarDetection(EarDetection*);
  void followMediaChanges();
  bool isActiveOutputDeviceAirPods();
  void handleConversationalAwareness(const QByteArray &data);
  void activateA2dpProfileWithRetry(const QString &macAddress);
  void cancelPendingA2dpActivation();
  void removeAudioOutputDevice();
  void setConnectedDeviceMacAddress(const QString &macAddress);
  bool isA2dpProfileAvailable();
  QString getPreferredA2dpProfile();
  QString getActiveProfile();
  bool restartWirePlumber();

  void setEarDetectionBehavior(EarDetectionBehavior behavior);
  // Audio follows the pods once per control-link session; the disconnect finalizer starts the next one.
  void setFollowOnConnect(bool follow) { m_followOnConnect = follow; }
  bool followOnConnect() const { return m_followOnConnect; }
  void startFollowSession() { m_defaultSinkClaimedThisSession = false; }
  inline EarDetectionBehavior getEarDetectionBehavior() const { return earDetectionBehavior; }

  void play();
  void pause();
  MediaState getCurrentMediaState() const;
  // Another device took the pods: remember where audio was, so the reclaim restores only a default the pods held.
  void rememberDefaultSinkForInterruption();
  void reclaimDefaultSinkAfterInterruption();

Q_SIGNALS:
  void mediaStateChanged(MediaState state, MediaOrigin origin);

private:
  MediaState mediaStateFromPlayerctlOutput(const QString &output) const;
  QString getAudioDeviceName();
  QStringList getPlayingMediaPlayers();
  // Only the retry chain calls this, so the one-restart flag below cannot be read outside a chain.
  bool activateA2dpProfile();
  void attemptA2dpActivation(const QString &macAddress, quint64 generation, int attempt, int unanswered = 0);
  void applyConversationDecision(const OpenPods::Conversation::LevelTracker::Decision &decision);
  void duckForConversation();
  void restoreAfterConversation();

  QStringList pausedByAppServices;
  int initialVolume = -1;
  QString connectedDeviceMacAddress;
  EarDetectionBehavior earDetectionBehavior = PauseWhenOneRemoved;
  QString m_deviceOutputName;
  PlayerStatusWatcher *playerStatusWatcher = nullptr;
  PulseAudioController *m_pulseAudio = nullptr;
  QString m_cachedA2dpProfile;
  quint64 m_earDetectionGeneration = 0;
  bool m_earOutPending = false;
  // A queued retry compares its captured generation against this, so a superseded chain stops.
  quint64 m_a2dpRetryGeneration = 0;
  bool m_wirePlumberRestartedThisChain = false;
  // The level stream has no guaranteed restore frame, so the tracker bounds a duck with this timer.
  OpenPods::Conversation::LevelTracker m_conversation;
  QTimer *m_conversationRestoreTimer = nullptr;
  bool m_followOnConnect = true;
  bool m_defaultSinkClaimedThisSession = false;
  // The next profile activation makes the pods default again after an interruption the user did not move away from.
  bool m_reclaimDefaultSinkPending = false;
  QString m_defaultSinkAtInterruption;
  // Set by the daemon's own pause() and play(); the matching MPRIS report within the window reads as Daemon origin.
  void markSelfInitiated(MediaState expected);
  MediaState m_selfInitiatedState = Stopped;
  QElapsedTimer m_selfInitiatedTimer;
};

#endif // MEDIACONTROLLER_H
