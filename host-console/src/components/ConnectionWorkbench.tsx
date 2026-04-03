import { useEffect, useMemo, useState } from 'react'
import { Activity, Cable, Wifi } from 'lucide-react'

import { ProgressMeter } from './ProgressMeter'
import { formatEnumLabel } from '../lib/presentation'
import {
  controllerBenchAp,
  preferredWirelessUrl,
  sortWirelessScanNetworks,
  transportLabel,
  wirelessConnectionStateLabel,
  wirelessModeLabel,
  wirelessSignalLabel,
  wirelessSignalPercent,
} from '../lib/wireless'
import type {
  DeviceSnapshot,
  TransportKind,
  TransportStatus,
  WirelessStatus,
} from '../types'

type WirelessAction = 'scan' | 'join' | 'restore'
type WirelessFeedbackTone = 'steady' | 'warning' | 'critical'

type ConnectionWorkbenchProps = {
  snapshot: DeviceSnapshot
  transportKind: TransportKind
  transportStatus: TransportStatus
  transportDetail: string
  wifiUrl: string
  onSetWifiUrl: (url: string) => void
  onSetTransportKind: (kind: TransportKind) => Promise<void> | void
  onConnect: () => Promise<void> | void
  onDisconnect: () => Promise<void> | void
  onIssueCommandAwaitAck: (
    cmd: string,
    risk: 'read' | 'write' | 'service' | 'firmware',
    note: string,
    args?: Record<string, number | string | boolean>,
    options?: {
      logHistory?: boolean
      timeoutMs?: number
    },
  ) => Promise<{ ok: boolean; note: string }>
}

function connectStepsForTransport(
  kind: TransportKind,
  wireless: WirelessStatus,
): string[] {
  if (kind === 'wifi') {
    if (wireless.mode === 'station' || wireless.stationConfigured) {
      return [
        'Use Web Serial when staging controller network changes. Switching Wi-Fi mode can drop the current wireless socket immediately.',
        wireless.stationConnected
          ? `Controller joined existing Wi-Fi SSID ${wireless.ssid}. Keep the laptop on that same network and connect to ${preferredWirelessUrl(wireless)}.`
          : wireless.stationConfigured
            ? `Controller is configured for existing Wi-Fi SSID ${wireless.stationSsid}. Power it, let DHCP complete, then use the reported controller URL. Only 2.4 GHz Wi-Fi is supported.`
            : 'Enter an SSID and password below to move the controller onto your existing network.',
        'Station mode keeps the laptop on normal internet while the ESP32 publishes the same bench WebSocket protocol.',
        'Only 2.4 GHz Wi-Fi networks are supported by the ESP32 radio. 5 GHz SSIDs will not work.',
        'If station association fails, reconnect over Web Serial and restore the bench AP.',
        'Firmware flashing still requires Web Serial.',
      ]
    }

    return [
      `Power the controller from USB-PD, then join Wi-Fi SSID ${controllerBenchAp.ssid}.`,
      `Use password ${controllerBenchAp.password}. Keep this host GUI running locally on the laptop.`,
      `Choose Wireless and connect to ${controllerBenchAp.wsUrl}.`,
      'Wireless uses the same controller JSON protocol as USB, including deployment gating and logs.',
      'The laptop will usually lose normal internet while joined to the controller AP. That is expected during bench operation.',
      'Firmware flashing still requires Web Serial.',
    ]
  }

  if (kind === 'serial') {
    return [
      'Open in Chrome or Edge.',
      'Plug in the ESP32-S3 and choose the USB serial device.',
      'Enter service mode from Bring-up or Firmware. Do not press RST for service mode.',
      'Firmware can flash a raw app binary over Web Serial; use BOOT + RST only if the browser reset sequence misses the bootloader.',
      'After page refresh or board reboot, the app will try to reopen the last approved serial port automatically.',
      'If you press RST or BOOT + RST, the USB link will drop. Click Connect board after the port comes back.',
    ]
  }

  return [
    'Mock rig auto-connects.',
    'Use Deployment before Control when you need to test runtime gating logic.',
    'Switch to Web Serial only with a terminated bench unit attached.',
  ]
}

function connectionButtonLabel(kind: TransportKind): string {
  if (kind === 'mock') {
    return 'Start mock rig'
  }

  if (kind === 'wifi') {
    return 'Connect wireless'
  }

  return 'Connect board'
}

export function ConnectionWorkbench({
  snapshot,
  transportKind,
  transportStatus,
  transportDetail,
  wifiUrl,
  onSetWifiUrl,
  onSetTransportKind,
  onConnect,
  onDisconnect,
  onIssueCommandAwaitAck,
}: ConnectionWorkbenchProps) {
  const [wirelessStationSsidDraft, setWirelessStationSsidDraft] = useState('')
  const [wirelessStationSsidDraftTouched, setWirelessStationSsidDraftTouched] = useState(false)
  const [wirelessStationPasswordDraft, setWirelessStationPasswordDraft] = useState('')
  const [wirelessReuseSavedPassword, setWirelessReuseSavedPassword] = useState(false)
  const [wirelessActionPending, setWirelessActionPending] = useState<WirelessAction | null>(null)
  const [wirelessActionStartedAtMs, setWirelessActionStartedAtMs] = useState<number | null>(null)
  const [wirelessActionError, setWirelessActionError] = useState<string | null>(null)

  const connected = transportStatus === 'connected'
  const connectSteps = useMemo(
    () => connectStepsForTransport(transportKind, snapshot.wireless),
    [snapshot.wireless, transportKind],
  )
  const scannedWirelessNetworks = useMemo(
    () => sortWirelessScanNetworks(snapshot.wireless.scannedNetworks),
    [snapshot.wireless.scannedNetworks],
  )
  const displayedWirelessStationSsidDraft =
    wirelessStationSsidDraftTouched
      ? wirelessStationSsidDraft
      : snapshot.wireless.stationSsid
  const selectedScannedWirelessNetwork = useMemo(
    () =>
      scannedWirelessNetworks.find(
        (network) => network.ssid === displayedWirelessStationSsidDraft.trim(),
      ) ?? null,
    [displayedWirelessStationSsidDraft, scannedWirelessNetworks],
  )
  const requestedStationSsid = displayedWirelessStationSsidDraft.trim()
  const savedStationSsid = snapshot.wireless.stationSsid.trim()
  const reusingSavedStationSsid =
    requestedStationSsid.length > 0 &&
    savedStationSsid.length > 0 &&
    requestedStationSsid === savedStationSsid
  const canReuseSavedPassword =
    snapshot.wireless.stationConfigured && reusingSavedStationSsid
  const wirelessSignalLevel = useMemo(
    () =>
      snapshot.wireless.mode === 'station'
        ? wirelessSignalPercent(snapshot.wireless.stationRssiDbm)
        : snapshot.wireless.apReady
          ? 100
          : 0,
    [
      snapshot.wireless.apReady,
      snapshot.wireless.mode,
      snapshot.wireless.stationRssiDbm,
    ],
  )
  const wirelessSignalTone = wirelessSignalLevel >= 70
    ? 'steady'
    : wirelessSignalLevel >= 35
      ? 'warning'
      : 'critical'
  const wirelessStatusCards = useMemo(
    () => [
      {
        label: 'Mode',
        value: wirelessModeLabel(snapshot.wireless),
        detail: wirelessConnectionStateLabel(snapshot.wireless),
      },
      {
        label: 'Endpoint',
        value: snapshot.wireless.ipAddress || 'Awaiting IP',
        detail: preferredWirelessUrl(snapshot.wireless),
      },
      {
        label: 'Signal',
        value: wirelessSignalLabel(snapshot.wireless),
        detail:
          snapshot.wireless.mode === 'station'
            ? snapshot.wireless.stationConnected
              ? `${snapshot.wireless.ssid} · channel ${snapshot.wireless.stationChannel || '?'}`
              : snapshot.wireless.lastError || 'No active station link yet'
            : `${snapshot.wireless.ssid} · ${snapshot.wireless.clientCount} client${snapshot.wireless.clientCount === 1 ? '' : 's'}`,
      },
      {
        label: 'Nearby SSIDs',
        value: snapshot.wireless.scanInProgress
          ? 'Scanning…'
          : `${scannedWirelessNetworks.length} found`,
        detail:
          scannedWirelessNetworks.length > 0
            ? scannedWirelessNetworks
              .slice(0, 2)
              .map((network) => network.ssid)
              .join(' · ')
            : 'Run a controller Wi-Fi scan from the active link.',
      },
    ],
    [
      scannedWirelessNetworks,
      snapshot.wireless,
    ],
  )
  const wirelessFeedback = useMemo<{
    tone: WirelessFeedbackTone
    label: string
    detail: string
  }>(() => {
    if (wirelessActionError !== null && wirelessActionError.trim().length > 0) {
      return {
        tone: 'critical',
        label: 'Controller Wi-Fi update failed',
        detail: wirelessActionError,
      }
    }

    if (wirelessActionPending === 'scan' || snapshot.wireless.scanInProgress) {
      return {
        tone: 'warning',
        label: 'Scanning nearby Wi-Fi',
        detail: 'Controller radio is scanning nearby 2.4 GHz Wi-Fi networks. Results will appear below when the scan completes.',
      }
    }

    if (wirelessActionPending === 'join') {
      if (snapshot.wireless.stationConnected) {
        return {
          tone: 'steady',
          label: 'Joined existing Wi-Fi',
          detail: snapshot.wireless.apReady
            ? `Controller joined ${snapshot.wireless.ssid} at ${snapshot.wireless.ipAddress || 'the reported IP address'}. Bench AP stays online for management, so you do not need to abandon ${controllerBenchAp.ssid} to change networks later.`
            : `Controller joined ${snapshot.wireless.ssid} at ${snapshot.wireless.ipAddress || 'the reported IP address'} and is ready for wireless reconnection.`,
        }
      }

      if (snapshot.wireless.stationConnecting) {
        return {
          tone: 'warning',
          label: 'Joining existing Wi-Fi',
          detail: `Controller is associating with ${snapshot.wireless.stationSsid || displayedWirelessStationSsidDraft || 'the requested SSID'}. Only 2.4 GHz Wi-Fi is supported.`,
        }
      }

      return {
        tone: 'warning',
        label: 'Waiting for controller network join',
        detail: `Station credentials were sent. Keep USB connected while the controller joins ${snapshot.wireless.stationSsid || displayedWirelessStationSsidDraft || 'the requested SSID'} and waits for DHCP.`,
      }
    }

    if (wirelessActionPending === 'restore') {
      if (snapshot.wireless.mode === 'softap' && snapshot.wireless.apReady) {
        return {
          tone: 'steady',
          label: 'Bench AP restored',
          detail: `${snapshot.wireless.ssid} is back online at ${preferredWirelessUrl(snapshot.wireless)}.`,
        }
      }

      return {
        tone: 'warning',
        label: 'Restoring bench AP',
        detail: 'Controller is switching back to its bench access point. The wireless socket may drop briefly during the transition.',
      }
    }

    if (snapshot.wireless.mode === 'station' && snapshot.wireless.stationConnected) {
      return {
        tone: 'steady',
        label: 'Existing Wi-Fi linked',
        detail: snapshot.wireless.apReady
          ? `Controller joined ${snapshot.wireless.ssid} at ${snapshot.wireless.ipAddress}. Bench AP ${controllerBenchAp.ssid} stays up for stable management and future network changes.`
          : `Controller joined ${snapshot.wireless.ssid} at ${snapshot.wireless.ipAddress}. Only 2.4 GHz Wi-Fi is supported.`,
      }
    }

    if (snapshot.wireless.mode === 'station' && snapshot.wireless.stationConfigured) {
      return {
        tone: 'warning',
        label: 'Existing Wi-Fi configured',
        detail: `Controller is staged for ${snapshot.wireless.stationSsid || 'the saved SSID'} but is not connected yet. Only 2.4 GHz Wi-Fi is supported.`,
      }
    }

    if (snapshot.wireless.mode === 'softap' && snapshot.wireless.apReady) {
      return {
        tone: 'steady',
        label: 'Bench AP ready',
        detail: `${snapshot.wireless.ssid} is online at ${preferredWirelessUrl(snapshot.wireless)} for local bench access.`,
      }
    }

    return {
      tone: 'warning',
      label: 'Wireless ready for setup',
      detail: 'Use Web Serial to stage controller Wi-Fi changes safely. The ESP32 radio supports 2.4 GHz Wi-Fi only.',
    }
  }, [displayedWirelessStationSsidDraft, snapshot.wireless, wirelessActionError, wirelessActionPending])

  useEffect(() => {
    if (
      snapshot.wireless.mode === 'station' &&
      snapshot.wireless.stationConnected &&
      snapshot.wireless.wsUrl.trim().length > 0 &&
      wifiUrl.trim().length === 0
    ) {
      onSetWifiUrl(snapshot.wireless.wsUrl)
    }
  }, [
    snapshot.wireless.mode,
    snapshot.wireless.stationConnected,
    snapshot.wireless.wsUrl,
    onSetWifiUrl,
    wifiUrl,
  ])

  useEffect(() => {
    if (wirelessActionPending === null) {
      return
    }

    if (
      wirelessActionPending === 'scan' &&
      wirelessActionStartedAtMs !== null &&
      window.performance.now() - wirelessActionStartedAtMs > 1200 &&
      !snapshot.wireless.scanInProgress
    ) {
      setWirelessActionPending(null)
      setWirelessActionStartedAtMs(null)
      return
    }

    if (wirelessActionPending === 'join' && snapshot.wireless.stationConnected) {
      setWirelessActionPending(null)
      setWirelessActionStartedAtMs(null)
      setWirelessActionError(null)
      return
    }

    if (
      wirelessActionPending === 'restore' &&
      snapshot.wireless.mode === 'softap' &&
      snapshot.wireless.apReady
    ) {
      setWirelessActionPending(null)
      setWirelessActionStartedAtMs(null)
      setWirelessActionError(null)
      return
    }

    if (
      wirelessActionPending === 'join' &&
      wirelessActionStartedAtMs !== null &&
      window.performance.now() - wirelessActionStartedAtMs > 1400 &&
      snapshot.wireless.mode === 'station' &&
      !snapshot.wireless.stationConnected &&
      !snapshot.wireless.stationConnecting &&
      snapshot.wireless.lastError.trim().length > 0
    ) {
      setWirelessActionPending(null)
      setWirelessActionStartedAtMs(null)
      setWirelessActionError(snapshot.wireless.lastError)
    }
  }, [
    snapshot.wireless.apReady,
    snapshot.wireless.lastError,
    snapshot.wireless.mode,
    snapshot.wireless.scanInProgress,
    snapshot.wireless.stationConnected,
    snapshot.wireless.stationConnecting,
    wirelessActionPending,
    wirelessActionStartedAtMs,
  ])

  useEffect(() => {
    setWirelessActionPending(null)
    setWirelessActionStartedAtMs(null)
    setWirelessActionError(null)
  }, [transportKind])

  async function handleConfigureControllerStationMode() {
    setWirelessActionError(null)
    const nextStationSsid = displayedWirelessStationSsidDraft.trim()

    if (nextStationSsid.length === 0) {
      setWirelessActionPending(null)
      setWirelessActionStartedAtMs(null)
      setWirelessActionError('Enter the target Wi-Fi SSID before asking the controller to join.')
      return
    }

    setWirelessActionPending('join')
    setWirelessActionStartedAtMs(window.performance.now())
    const args: Record<string, string> = { mode: 'station', ssid: nextStationSsid }

    if (!(wirelessReuseSavedPassword && canReuseSavedPassword)) {
      args.password = wirelessStationPasswordDraft
    }

    const result = await onIssueCommandAwaitAck(
      'configure_wireless',
      'write',
      'Configure the controller to join an existing Wi-Fi network.',
      args,
      { timeoutMs: 5000 },
    )

    if (!result.ok) {
      setWirelessActionPending(null)
      setWirelessActionStartedAtMs(null)
      setWirelessActionError(result.note)
      return
    }

    onSetWifiUrl(controllerBenchAp.wsUrl)

    if (transportKind === 'serial' && transportStatus === 'connected') {
      void onIssueCommandAwaitAck(
        'get_status',
        'read',
        'Refresh controller wireless status after staging station-mode credentials.',
        undefined,
        {
          logHistory: false,
          timeoutMs: 2200,
        },
      )
    }
  }

  async function handleRestoreControllerBenchAp() {
    setWirelessActionError(null)
    setWirelessActionPending('restore')
    setWirelessActionStartedAtMs(window.performance.now())
    const result = await onIssueCommandAwaitAck(
      'configure_wireless',
      'write',
      'Restore the controller bench SoftAP network.',
      { mode: 'softap' },
      { timeoutMs: 5000 },
    )

    if (!result.ok) {
      setWirelessActionPending(null)
      setWirelessActionStartedAtMs(null)
      setWirelessActionError(result.note)
      return
    }

    if (transportKind === 'serial' && transportStatus === 'connected') {
      void onIssueCommandAwaitAck(
        'get_status',
        'read',
        'Refresh controller wireless status after restoring the bench access point.',
        undefined,
        {
          logHistory: false,
          timeoutMs: 2200,
        },
      )
    }
  }

  async function handleScanControllerWirelessNetworks() {
    setWirelessActionError(null)
    setWirelessActionPending('scan')
    setWirelessActionStartedAtMs(window.performance.now())

    const result = await onIssueCommandAwaitAck(
      'scan_wireless_networks',
      'read',
      'Refresh the controller Wi-Fi scan list.',
      undefined,
      { timeoutMs: 8000 },
    )

    if (!result.ok) {
      setWirelessActionPending(null)
      setWirelessActionStartedAtMs(null)
      setWirelessActionError(result.note)
    }
  }

  return (
    <section className="panel-section">
      <div className="panel-section__head">
        <div>
          <p className="eyebrow">Connection</p>
          <h2>Transport and controller Wi-Fi</h2>
        </div>
        <p className="panel-note">
          Transport selection, reconnect actions, controller endpoint management, and Wi-Fi mode changes live here instead of in the shared top shell.
        </p>
      </div>

      <div className="control-banner">
        <div className="control-banner__copy">
          <strong>
            {connected
              ? `${transportLabel(transportKind)} link healthy.`
              : transportStatus === 'connecting'
                ? `Connecting ${transportLabel(transportKind).toLowerCase()} link.`
                : transportStatus === 'error'
                  ? 'Transport needs operator attention.'
                  : 'Select a transport and connect.'}
          </strong>
          <p>{transportDetail}</p>
        </div>
        <div className="status-badges">
          <span className={connected ? 'status-badge is-on' : transportStatus === 'connecting' ? 'status-badge is-warn' : transportStatus === 'error' ? 'status-badge is-critical' : 'status-badge is-off'}>
            <Activity size={14} />
            Link {formatEnumLabel(transportStatus)}
          </span>
          <span className={transportKind === 'wifi' ? 'status-badge is-on' : 'status-badge'}>
            <Wifi size={14} />
            {transportKind === 'wifi' ? wirelessModeLabel(snapshot.wireless) : transportLabel(transportKind)}
          </span>
          <span className={snapshot.wireless.apReady || snapshot.wireless.stationConnected ? 'status-badge is-on' : 'status-badge is-warn'}>
            <Cable size={14} />
            {snapshot.wireless.stationConnected
              ? 'Controller Wi-Fi linked'
              : snapshot.wireless.apReady
                ? 'Bench AP ready'
                : 'Network idle'}
          </span>
        </div>
      </div>

      <div className="connection-layout">
        <article className="panel-cutout control-module">
          <div className="cutout-head">
            <strong>Transport selector</strong>
          </div>
          <div className="transport-mode-picker">
            {(
              [
                ['mock', 'Mock rig', 'Logic-only session'],
                ['serial', 'Web Serial', 'USB, flashing, setup'],
                ['wifi', 'Wireless', 'Bench AP or station'],
              ] as Array<[TransportKind, string, string]>
            ).map(([kind, label, detail]) => (
              <button
                key={kind}
                type="button"
                className={transportKind === kind ? 'transport-mode-button is-active' : 'transport-mode-button'}
                onClick={() => {
                  void onSetTransportKind(kind)
                }}
              >
                <span>{label}</span>
                <small>{detail}</small>
              </button>
            ))}
          </div>

          <div className="button-row">
            <button
              type="button"
              className="action-button is-inline is-accent"
              disabled={connected}
              onClick={() => {
                void onConnect()
              }}
            >
              {connected ? 'Connected' : connectionButtonLabel(transportKind)}
            </button>
            <button
              type="button"
              className="action-button is-inline"
              disabled={transportStatus === 'disconnected'}
              onClick={() => {
                void onDisconnect()
              }}
            >
              Disconnect
            </button>
          </div>

          <p className="panel-note">{transportDetail}</p>

          <div className="connect-steps">
            {connectSteps.map((step, index) => (
              <div key={step} className="connect-step">
                <span>{index + 1}</span>
                <p>{step}</p>
              </div>
            ))}
          </div>
        </article>

        <article className="panel-cutout control-module">
          <div className="cutout-head">
            <strong>Controller Wi-Fi management</strong>
          </div>

          {transportKind === 'mock' ? (
            <p className="panel-note">
              Mock rig sessions do not expose controller Wi-Fi state. Switch to Web Serial or Wireless to manage controller networking.
            </p>
          ) : (
            <div className="field-stack">
              {transportKind === 'wifi' ? (
                <label className="field-block">
                  <span>Wireless controller URL</span>
                  <input
                    type="text"
                    value={wifiUrl}
                    onChange={(event) => onSetWifiUrl(event.target.value)}
                    placeholder={preferredWirelessUrl(snapshot.wireless)}
                  />
                  <small>
                    Controller network: <code>{wirelessModeLabel(snapshot.wireless)}</code>
                    {' '}• endpoint <code>{preferredWirelessUrl(snapshot.wireless)}</code>
                  </small>
                  <small>
                    {snapshot.wireless.mode === 'station'
                      ? snapshot.wireless.stationConnected
                        ? `Joined ${snapshot.wireless.ssid} at ${snapshot.wireless.ipAddress}`
                        : snapshot.wireless.lastError || 'Waiting for station association'
                      : `Bench AP: ${controllerBenchAp.ssid} / ${controllerBenchAp.password}`}
                  </small>
                </label>
              ) : null}

              <div
                className={`controller-wireless-feedback is-${wirelessFeedback.tone}`}
                data-hover-help={wirelessFeedback.detail}
              >
                <div>
                  <span>Wi-Fi transition</span>
                  <strong>{wirelessFeedback.label}</strong>
                  <small>{wirelessFeedback.detail}</small>
                </div>
                <div className={`state-pill is-${wirelessFeedback.tone}`}>
                  <span>{wirelessFeedback.label}</span>
                </div>
              </div>

              <div className="controller-wireless-status-grid">
                {wirelessStatusCards.map((card) => (
                  <div
                    key={card.label}
                    className="controller-wireless-status-card"
                    data-hover-help={`${card.label}. ${card.detail}`}
                  >
                    <span>{card.label}</span>
                    <strong>{card.value}</strong>
                    <small>{card.detail}</small>
                  </div>
                ))}
              </div>

              <div className="controller-wireless-meter">
                <div className="controller-wireless-meter__label">
                  <span>Signal / endpoint confidence</span>
                  <strong>
                    {snapshot.wireless.mode === 'station'
                      ? `${wirelessSignalLevel}%`
                      : snapshot.wireless.apReady
                        ? 'AP ready'
                        : 'AP idle'}
                  </strong>
                </div>
                <ProgressMeter value={wirelessSignalLevel} tone={wirelessSignalTone} compact />
              </div>

              <div className="field-grid field-grid--two">
                {scannedWirelessNetworks.length > 0 ? (
                  <label className="field-block field-block--compact">
                    <span>Nearby SSIDs</span>
                    <select
                      value={
                        selectedScannedWirelessNetwork?.ssid ??
                        (displayedWirelessStationSsidDraft.trim().length > 0 ? '__manual__' : '')
                      }
                      onChange={(event) => {
                        const nextValue = event.target.value
                        if (nextValue === '__manual__' || nextValue === '') {
                          return
                        }
                        setWirelessStationSsidDraftTouched(true)
                        setWirelessStationSsidDraft(nextValue)
                        setWirelessReuseSavedPassword(false)
                      }}
                    >
                      <option value="">Select scanned network</option>
                      {selectedScannedWirelessNetwork === null &&
                      displayedWirelessStationSsidDraft.trim().length > 0 ? (
                        <option value="__manual__">
                          Manual SSID: {displayedWirelessStationSsidDraft.trim()}
                        </option>
                      ) : null}
                      {scannedWirelessNetworks.map((network) => (
                        <option
                          key={`${network.ssid}-${network.channel}`}
                          value={network.ssid}
                        >
                          {network.ssid} · {network.rssiDbm} dBm · ch {network.channel}
                          {network.secure ? '' : ' · open'}
                        </option>
                      ))}
                    </select>
                    <small>
                      {snapshot.wireless.scanInProgress
                        ? 'Controller scan is in progress.'
                        : 'Pick a scanned SSID or type one manually below. Only 2.4 GHz networks are supported.'}
                    </small>
                  </label>
                ) : null}
                <label className="field-block field-block--compact">
                  <span>Existing Wi-Fi SSID</span>
                  <input
                    type="text"
                    value={displayedWirelessStationSsidDraft}
                    onChange={(event) => {
                      setWirelessStationSsidDraftTouched(true)
                      setWirelessStationSsidDraft(event.target.value)
                      setWirelessReuseSavedPassword(false)
                    }}
                    placeholder={snapshot.wireless.stationSsid || 'Lab network'}
                  />
                </label>
                <label className="field-block field-block--compact">
                  <span>Password</span>
                  <input
                    type="text"
                    value={wirelessStationPasswordDraft}
                    onChange={(event) => {
                      setWirelessStationPasswordDraft(event.target.value)
                      if (event.target.value.length > 0) {
                        setWirelessReuseSavedPassword(false)
                      }
                    }}
                    placeholder={
                      canReuseSavedPassword
                        ? 'Enter a replacement password or enable reuse below'
                        : 'Enter Wi-Fi password or leave blank for an open network'
                    }
                  />
                  <small>
                    {wirelessReuseSavedPassword && canReuseSavedPassword
                      ? `Controller will reuse the saved password for ${savedStationSsid}.`
                      : selectedScannedWirelessNetwork === null
                        ? canReuseSavedPassword
                          ? `Blank means open-network join unless you enable saved-password reuse for ${savedStationSsid}.`
                          : 'Password is always visible here so you can confirm exactly what will be sent.'
                        : selectedScannedWirelessNetwork.secure
                          ? `Selected network ${selectedScannedWirelessNetwork.ssid} is secured. Enter its password or explicitly reuse the saved password for ${savedStationSsid}.`
                          : `Selected network ${selectedScannedWirelessNetwork.ssid} is open; password can stay blank.`}
                  </small>
                </label>
              </div>

              {canReuseSavedPassword ? (
                <label className="field-inline-toggle">
                  <input
                    type="checkbox"
                    checked={wirelessReuseSavedPassword}
                    onChange={(event) =>
                      setWirelessReuseSavedPassword(event.target.checked)
                    }
                  />
                  <span>
                    Reuse the controller’s saved password for {savedStationSsid}
                  </span>
                </label>
              ) : null}

              <p className="controller-wireless-note">
                ESP32 Wi-Fi supports 2.4 GHz networks only. 5 GHz SSIDs will not work.
              </p>

              <div className="button-row">
                <button
                  type="button"
                  className="action-button is-inline"
                  disabled={
                    !connected ||
                    snapshot.wireless.scanInProgress ||
                    wirelessActionPending === 'scan'
                  }
                  onClick={() => {
                    void handleScanControllerWirelessNetworks()
                  }}
                >
                  {snapshot.wireless.scanInProgress || wirelessActionPending === 'scan'
                    ? 'Scanning…'
                    : 'Scan SSIDs'}
                </button>
                <button
                  type="button"
                  className="action-button is-inline is-accent"
                  disabled={
                    !connected ||
                    wirelessActionPending === 'join' ||
                    wirelessActionPending === 'restore'
                  }
                  onClick={() => {
                    void handleConfigureControllerStationMode()
                  }}
                >
                  {wirelessActionPending === 'join' ? 'Saving and joining…' : 'Save and join Wi-Fi'}
                </button>
                <button
                  type="button"
                  className="action-button is-inline"
                  disabled={
                    !connected ||
                    wirelessActionPending === 'join' ||
                    wirelessActionPending === 'restore'
                  }
                  onClick={() => {
                    void handleRestoreControllerBenchAp()
                  }}
                >
                  {wirelessActionPending === 'restore' ? 'Restoring…' : 'Restore bench AP'}
                </button>
                <button
                  type="button"
                  className="action-button is-inline"
                  onClick={() => onSetWifiUrl(preferredWirelessUrl(snapshot.wireless))}
                >
                  Use controller URL
                </button>
                {transportKind === 'wifi' ? (
                  <button
                    type="button"
                    className="action-button is-inline"
                    onClick={() => onSetWifiUrl(controllerBenchAp.wsUrl)}
                  >
                    Use bench AP URL
                  </button>
                ) : null}
              </div>

              {!connected ? (
                <p className="controller-wireless-note">
                  Connect to the controller first, then scan or save Wi-Fi settings. If the saved station endpoint is stale, point the host back to <code>{controllerBenchAp.wsUrl}</code> and reconnect over the bench AP.
                </p>
              ) : null}
            </div>
          )}
        </article>
      </div>
    </section>
  )
}
