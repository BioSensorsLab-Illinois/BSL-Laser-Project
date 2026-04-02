import { useEffect } from 'react'

const HOVER_HELP_SELECTOR = [
  'button',
  '[role="button"]',
  '.help-hint',
  '.nav-link',
  '.segmented__button',
  '.chip',
  '.status-badge',
  '.inline-token',
  '.state-pill',
  '.transport-chip',
  '.gpio-mini-token',
  '.controller-wireless-status-card',
  '.controller-wireless-feedback',
  '.gpio-detail-chip',
  '.gpio-module-visual__chip',
  '.board-hotspot__chip',
  '.hero-kpi',
  '.hero-workflow__step',
  '.hero-facts__item',
  '.connect-step',
  '.field-readback',
].join(', ')

const TOOLTIP_ID = 'global-hover-help-tooltip'
const TOOLTIP_VISIBLE_CLASS = 'is-visible'

const explicitHelpByLabel: Record<string, string> = {
  overview: 'Open the high-level controller summary, connection state, and live bench overview.',
  control: 'Open the bench control workspace for laser, TEC, and modulation staging.',
  'bring-up': 'Open the module bring-up workspace for planning, probing, and guarded tuning.',
  events: 'Open the session log, decoded events, and command history views.',
  firmware: 'Open the firmware loading and browser flashing workspace.',
  tools: 'Open the service tools workspace for GPIO, bus, and utility commands.',
  'mock rig': 'Switch the GUI to the simulated controller transport for host-only testing.',
  'web serial': 'Use the browser Web Serial transport over the ESP32 USB link.',
  wireless: 'Use the controller WebSocket transport over Wi-Fi.',
  'connect board': 'Open the browser serial-port picker and connect to the real controller over USB.',
  'connect wireless': 'Connect this GUI to the controller WebSocket URL over Wi-Fi.',
  disconnect: 'Close the current controller transport and stop live telemetry updates.',
  'start mock rig': 'Start the local mock controller so the GUI can be exercised without hardware.',
  'scan ssids': 'Ask the controller radio to scan nearby 2.4 GHz Wi-Fi networks.',
  'scanning…': 'The controller radio is currently scanning nearby 2.4 GHz Wi-Fi networks.',
  'join existing wi-fi': 'Send the typed SSID and password to the controller so it joins an existing 2.4 GHz Wi-Fi network.',
  'restore bench ap': 'Switch the controller back to its built-in bench access point and default WebSocket address.',
  'use controller url': 'Copy the controller-reported WebSocket URL into the Wireless controller URL field.',
  dark: 'Switch the host console theme to the dark appearance.',
  light: 'Switch the host console theme to the light appearance.',
  rst: 'Physical reset button on the ESP32-S3 board. Tap this to reboot the controller.',
  boot: 'Physical BOOT strap button on the ESP32-S3 board. Hold it during reset to enter ROM download mode for flashing.',
}

function normalizeLabel(text: string): string {
  return text.replace(/\s+/g, ' ').trim().toLowerCase()
}

function normalizeHelpText(text: string | null | undefined): string | null {
  if (text === null || text === undefined) {
    return null
  }

  const normalized = text.replace(/\s+/g, ' ').trim()
  return normalized.length > 0 ? normalized : null
}

function readElementLabel(element: HTMLElement): string {
  const ariaLabel = normalizeHelpText(element.getAttribute('aria-label'))
  if (ariaLabel !== null) {
    return ariaLabel
  }

  const customLabel = normalizeHelpText(element.dataset.hoverHelpLabel)
  if (customLabel !== null) {
    return customLabel
  }

  const text = normalizeHelpText(element.textContent)
  return text ?? ''
}

function firstMeaningfulText(elements: Array<Element | null>): string | null {
  for (const element of elements) {
    if (!(element instanceof HTMLElement)) {
      continue
    }

    const text = normalizeHelpText(element.textContent)
    if (text !== null) {
      return text
    }
  }

  return null
}

function inferHelpHintContext(element: HTMLElement): string | null {
  const fieldLabel = element.closest('.field-label')
  if (fieldLabel instanceof HTMLElement) {
    const labelText = firstMeaningfulText(
      Array.from(fieldLabel.children).filter(
        (child) => !(child instanceof HTMLElement && child.classList.contains('help-hint')),
      ),
    )

    if (labelText !== null) {
      return `More detail about ${labelText}.`
    }
  }

  const label = element.closest('label')
  if (label instanceof HTMLElement) {
    const labelText = firstMeaningfulText([
      label.querySelector('.field-label span'),
      label.querySelector('span'),
      label.querySelector('strong'),
    ])

    if (labelText !== null) {
      return `More detail about ${labelText}.`
    }
  }

  const sectionText = firstMeaningfulText([
    element.closest('.cutout-head')?.querySelector('strong') ?? null,
    element.closest('.panel-cutout')?.querySelector('strong') ?? null,
    element.closest('section')?.querySelector('h2') ?? null,
    element.closest('article')?.querySelector('h2') ?? null,
  ])

  if (sectionText !== null) {
    return `More detail about ${sectionText}.`
  }

  return 'More detail about this control.'
}

function inferHoverHelp(element: HTMLElement): string | null {
  const explicitDataHelp = normalizeHelpText(element.dataset.hoverHelp)
  if (explicitDataHelp !== null) {
    return explicitDataHelp
  }

  const explicitTitle = normalizeHelpText(element.getAttribute('title'))
  if (explicitTitle !== null) {
    return explicitTitle
  }

  if (element.matches('.help-hint')) {
    return inferHelpHintContext(element)
  }

  const label = readElementLabel(element)
  if (label.length === 0) {
    return null
  }

  const explicitHelp = explicitHelpByLabel[normalizeLabel(label)]
  if (explicitHelp !== undefined) {
    return explicitHelp
  }

  if (element.matches('.controller-wireless-status-card')) {
    const detail = normalizeHelpText(element.querySelector('small')?.textContent)
    return detail !== null ? `${label}. ${detail}` : `Wireless status card: ${label}.`
  }

  if (element.matches('.controller-wireless-feedback')) {
    const detail = normalizeHelpText(element.querySelector('small')?.textContent)
    return detail !== null ? `Wi-Fi transition: ${detail}` : 'Wi-Fi transition status.'
  }

  if (element.matches('.nav-link')) {
    const detail = normalizeHelpText(element.querySelector('small')?.textContent)
    return detail !== null ? `${label}. ${detail}` : `Navigate to ${label}.`
  }

  if (element.matches('.hero-kpi, .hero-facts__item')) {
    const detail = normalizeHelpText(element.querySelector('small')?.textContent)
    return detail !== null ? `${label}. ${detail}` : label
  }

  if (element.matches('.hero-workflow__step, .connect-step')) {
    return label
  }

  if (
    element.matches(
      '.status-badge, .inline-token, .state-pill, .transport-chip, .gpio-mini-token, .chip, .gpio-detail-chip, .gpio-module-visual__chip, .board-hotspot__chip',
    )
  ) {
    return `Status tag: ${label}.`
  }

  if (element.matches('.field-readback')) {
    return `Live readback: ${label}.`
  }

  if (element.matches('button, [role="button"], .segmented__button')) {
    return `Action button: ${label}.`
  }

  return label
}

function applyHoverHelp(root: HTMLElement): void {
  const elements = [
    ...(root.matches(HOVER_HELP_SELECTOR) ? [root] : []),
    ...Array.from(root.querySelectorAll<HTMLElement>(HOVER_HELP_SELECTOR)),
  ]

  elements.forEach((element) => {
    const currentTitle = normalizeHelpText(element.getAttribute('title')) ?? ''
    const currentAutoTitle = normalizeHelpText(element.dataset.autoHoverHelp) ?? ''

    if (
      currentTitle.length > 0 &&
      currentTitle !== currentAutoTitle &&
      element.dataset.hoverHelp === undefined
    ) {
      element.dataset.hoverHelpResolved = currentTitle
      return
    }

    const nextTitle = inferHoverHelp(element)

    if (nextTitle === null) {
      if (currentAutoTitle.length > 0) {
        element.removeAttribute('title')
        delete element.dataset.autoHoverHelp
      }
      delete element.dataset.hoverHelpResolved
      return
    }

    element.setAttribute('title', nextTitle)
    element.dataset.autoHoverHelp = nextTitle
    element.dataset.hoverHelpResolved = nextTitle

    if (!element.hasAttribute('aria-label') && element.matches('.help-hint')) {
      element.setAttribute('aria-label', nextTitle)
    }
  })
}

function ensureTooltipElement(): HTMLDivElement {
  const existing = document.getElementById(TOOLTIP_ID)
  if (existing instanceof HTMLDivElement) {
    return existing
  }

  const tooltip = document.createElement('div')
  tooltip.id = TOOLTIP_ID
  tooltip.className = 'global-hover-help-tooltip'
  tooltip.setAttribute('role', 'tooltip')
  tooltip.setAttribute('aria-hidden', 'true')
  document.body.appendChild(tooltip)
  return tooltip
}

function findHoverHelpTarget(start: Element | null): HTMLElement | null {
  let element = start instanceof HTMLElement ? start : start?.parentElement ?? null

  while (element !== null) {
    if (element.matches(HOVER_HELP_SELECTOR)) {
      const resolved = normalizeHelpText(element.dataset.hoverHelpResolved) ?? inferHoverHelp(element)
      if (resolved !== null) {
        return element
      }
    }

    element = element.parentElement
  }

  return null
}

function hideTooltip(tooltip: HTMLDivElement): void {
  tooltip.classList.remove(TOOLTIP_VISIBLE_CLASS)
  tooltip.setAttribute('aria-hidden', 'true')
}

function positionTooltip(
  tooltip: HTMLDivElement,
  anchorX: number,
  anchorY: number,
): void {
  const margin = 14
  const offsetX = 16
  const offsetY = 20

  let left = anchorX + offsetX
  let top = anchorY + offsetY

  const width = tooltip.offsetWidth
  const height = tooltip.offsetHeight

  if (left + width > window.innerWidth - margin) {
    left = Math.max(margin, window.innerWidth - width - margin)
  }

  if (top + height > window.innerHeight - margin) {
    top = Math.max(margin, anchorY - height - offsetY)
  }

  tooltip.style.left = `${left}px`
  tooltip.style.top = `${top}px`
}

function showTooltipForPoint(
  tooltip: HTMLDivElement,
  target: HTMLElement,
  clientX: number,
  clientY: number,
): void {
  const text = normalizeHelpText(target.dataset.hoverHelpResolved) ?? inferHoverHelp(target)
  if (text === null) {
    hideTooltip(tooltip)
    return
  }

  if (tooltip.textContent !== text) {
    tooltip.textContent = text
  }

  tooltip.classList.add(TOOLTIP_VISIBLE_CLASS)
  tooltip.setAttribute('aria-hidden', 'false')
  positionTooltip(tooltip, clientX, clientY)
}

function showTooltipForFocus(
  tooltip: HTMLDivElement,
  target: HTMLElement,
): void {
  const rect = target.getBoundingClientRect()
  showTooltipForPoint(
    tooltip,
    target,
    rect.left + Math.min(rect.width / 2, 120),
    rect.bottom,
  )
}

export function useGlobalHoverHelp(): void {
  useEffect(() => {
    const root = document.getElementById('root')
    if (root === null) {
      return
    }

    const tooltip = ensureTooltipElement()
    let rafId = 0

    const scheduleApply = () => {
      if (rafId !== 0) {
        window.cancelAnimationFrame(rafId)
      }
      rafId = window.requestAnimationFrame(() => {
        rafId = 0
        applyHoverHelp(root)
      })
    }

    const handlePointerMove = (event: MouseEvent) => {
      const target = findHoverHelpTarget(
        document.elementFromPoint(event.clientX, event.clientY),
      )

      if (target === null) {
        hideTooltip(tooltip)
        return
      }

      showTooltipForPoint(tooltip, target, event.clientX, event.clientY)
    }

    const handlePointerLeave = () => {
      hideTooltip(tooltip)
    }

    const handleFocusIn = (event: FocusEvent) => {
      const target = findHoverHelpTarget(event.target as Element | null)
      if (target === null) {
        hideTooltip(tooltip)
        return
      }

      showTooltipForFocus(tooltip, target)
    }

    const handleFocusOut = () => {
      hideTooltip(tooltip)
    }

    scheduleApply()

    const observer = new MutationObserver(() => {
      scheduleApply()
    })

    observer.observe(root, {
      subtree: true,
      childList: true,
      characterData: true,
      attributes: true,
      attributeFilter: [
        'class',
        'disabled',
        'aria-label',
        'title',
        'data-hover-help',
        'data-hover-help-label',
      ],
    })

    document.addEventListener('mousemove', handlePointerMove, { passive: true })
    document.addEventListener('focusin', handleFocusIn)
    document.addEventListener('focusout', handleFocusOut)
    document.addEventListener('scroll', handlePointerLeave, true)
    window.addEventListener('resize', handlePointerLeave)
    window.addEventListener('blur', handlePointerLeave)

    return () => {
      if (rafId !== 0) {
        window.cancelAnimationFrame(rafId)
      }

      observer.disconnect()
      document.removeEventListener('mousemove', handlePointerMove)
      document.removeEventListener('focusin', handleFocusIn)
      document.removeEventListener('focusout', handleFocusOut)
      document.removeEventListener('scroll', handlePointerLeave, true)
      window.removeEventListener('resize', handlePointerLeave)
      window.removeEventListener('blur', handlePointerLeave)
      hideTooltip(tooltip)
    }
  }, [])
}
