/* 2026-04-17 (Uncodixfy polish): framer-motion removed — the two
 * pulsing `motion.div` hotspots used continuous scale + opacity
 * animations, and the step-list used scroll-triggered slide-ins.
 * Both violate "no transform animations ... simple opacity/color
 * changes" per the spec. Replaced with static static hotspot chips
 * and static step list; a subtle CSS opacity-only pulse is
 * retained for the RST/BOOT chips so the guide still draws the
 * eye without a bouncy motion. */
import { Cable, LoaderCircle, RotateCcw } from 'lucide-react'

import boardLayout from '../assets/esp32-board-layout-horizontal.png'
import type { TransportKind } from '../types'

type FirmwareLoaderGuideProps = {
  connected: boolean
  supportsFirmwareTransfer: boolean
  transportKind: TransportKind
}

const flashingSteps = [
  {
    index: '01',
    title: 'Load the firmware image',
    detail:
      'Load a raw .bin first. Browser flashing currently treats a raw image as the app binary at 0x10000.',
  },
  {
    index: '02',
    title: 'Connect the board over Web Serial',
    detail:
      'The Firmware page can inspect the image, run preflight, and use the same approved serial port for flashing.',
  },
  {
    index: '03',
    title: 'Start the flash from this tab',
    detail:
      'Click Flash over Web Serial. The browser flasher will try the ESP32-S3 reset sequence automatically first.',
  },
  {
    index: '04',
    title: 'Use BOOT + RST only if needed',
    detail:
      'If auto-reset does not catch download mode, hold BOOT, tap RST once, then release BOOT when the flasher reports it has connected.',
  },
]

export function FirmwareLoaderGuide({
  connected,
  supportsFirmwareTransfer,
  transportKind,
}: FirmwareLoaderGuideProps) {
  const statusLabel =
    transportKind === 'mock'
      ? 'Mock transfer available'
      : supportsFirmwareTransfer
        ? 'Browser flash available'
        : connected
          ? 'Live board connected'
          : 'Connect board for live prep'

  const statusTone =
    transportKind === 'mock'
      ? 'connected'
      : supportsFirmwareTransfer
        ? 'connected'
        : connected
          ? 'connecting'
          : 'disconnected'

  const transportNote =
    transportKind === 'mock'
      ? 'The mock transport runs the full update sequence so you can rehearse the workflow safely before touching hardware.'
      : 'The live Web Serial path can now flash a raw app binary. BOOT plus RST is the fallback if the browser reset sequence does not catch the ESP32-S3 bootloader.'

  return (
    <section className="firmware-guide">
      <div className="panel-section__head">
        <div>
          <p className="eyebrow">Board assist</p>
          <h2>Load firmware with the right boot sequence</h2>
        </div>
        <p className="panel-note">
          Board image is rotated to fit. On the right edge of this view, the upper button is `RST` and the lower button is `BOOT`.
        </p>
      </div>

      <div className="firmware-guide__status">
        <span className={`transport-chip is-${statusTone}`}>
          <Cable size={14} />
          {statusLabel}
        </span>
        <p className="inline-help">{transportNote}</p>
      </div>

      <div className="firmware-guide__grid">
        <article className="panel-cutout firmware-guide__visual">
          <div className="cutout-head">
            <RotateCcw size={16} />
            <strong>Board button map</strong>
          </div>

          <div className="board-visual">
            <img
              src={boardLayout}
              alt="Rotated board layout with the upper right button labelled RST and the lower right button labelled BOOT."
              loading="lazy"
              decoding="async"
            />

            <div className="board-hotspot is-reset">
              <span className="board-hotspot__pulse" />
              <span className="board-hotspot__chip">RST</span>
            </div>

            <div className="board-hotspot is-boot">
              <span className="board-hotspot__pulse" />
              <span className="board-hotspot__chip">BOOT</span>
            </div>
          </div>

          <div className="board-caption">
            <div>
              <strong>Rotated for a compact guide</strong>
              <span>Right edge buttons: upper is reset, lower is boot/download.</span>
            </div>
          </div>
        </article>

        <article className="panel-cutout firmware-guide__flow">
          <div className="cutout-head">
            <LoaderCircle size={16} />
            <strong>Web flash flow</strong>
          </div>

          <div className="flash-step-list">
            {flashingSteps.map((step) => (
              <div key={step.index} className="flash-step">
                <div className="flash-step__index">{step.index}</div>
                <div className="flash-step__body">
                  <strong>{step.title}</strong>
                  <p>{step.detail}</p>
                </div>
              </div>
            ))}
          </div>

          <div className="firmware-guide__callout">
            <strong>Reset disconnect is normal</strong>
            <p>
              When you tap <code>RST</code>, the USB serial link drops. That is expected.
              The app will try to reconnect after flashing when the USB port comes back.
            </p>
          </div>

          <div className="firmware-guide__notes">
            <div className="inline-token">
              <span>Prepare first, flash second</span>
            </div>
            <div className="inline-token">
              <span>Outputs stay firmware-gated</span>
            </div>
            <div className="inline-token">
              <span>Use the mock rig to rehearse safely</span>
            </div>
          </div>
        </article>
      </div>
    </section>
  )
}
