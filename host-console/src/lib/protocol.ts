import type { CommandTemplate } from '../types'

export const commandTemplates: CommandTemplate[] = [
  {
    id: 'get-status',
    label: 'Refresh status',
    command: 'get_status',
    risk: 'read',
    description: 'Poll the controller for a fresh full snapshot.',
  },
  {
    id: 'get-faults',
    label: 'Read faults',
    command: 'get_faults',
    risk: 'read',
    description: 'Fetch active and historical fault information.',
  },
  {
    id: 'get-bringup-profile',
    label: 'Read bring-up profile',
    command: 'get_bringup_profile',
    risk: 'read',
    description: 'Fetch the current service bring-up profile and module expectations.',
  },
  {
    id: 'clear-faults',
    label: 'Clear faults',
    command: 'clear_faults',
    risk: 'write',
    description: 'Requests a fault clear without bypassing firmware recovery rules.',
  },
  {
    id: 'enable-alignment',
    label: 'Enable green laser',
    command: 'enable_alignment',
    risk: 'service',
    description: 'Bench-only green alignment laser request routed through all interlocks.',
  },
  {
    id: 'disable-alignment',
    label: 'Disable green laser',
    command: 'disable_alignment',
    risk: 'write',
    description: 'Clear any host-requested green alignment laser output.',
  },
  {
    id: 'reboot',
    label: 'Reboot controller',
    command: 'reboot',
    risk: 'service',
    description: 'Controlled reboot after dropping outputs safe.',
  },
  {
    id: 'enter-service',
    label: 'Enter service mode',
    command: 'enter_service_mode',
    risk: 'service',
    description: 'Protected manufacturing / bench-only mode.',
  },
  {
    id: 'exit-service',
    label: 'Exit service mode',
    command: 'exit_service_mode',
    risk: 'service',
    description: 'Return from service mode to normal safe operation.',
  },
]
