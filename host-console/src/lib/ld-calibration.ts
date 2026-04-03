export const ldCurrentFullScaleA = 6
export const ldVoltageSpanV = 2.5
export const ldMaxCommandCurrentA = 5
export const ldMaxCommandVoltageV =
  (ldMaxCommandCurrentA / ldCurrentFullScaleA) * ldVoltageSpanV
export const ldDiodeNominalVoltageV = 3.0

export function clampLdCommandCurrentA(value: number): number {
  if (!Number.isFinite(value)) {
    return 0
  }

  return Math.min(ldMaxCommandCurrentA, Math.max(0, value))
}

export function clampLdCommandVoltageV(value: number): number {
  if (!Number.isFinite(value)) {
    return 0
  }

  return Math.min(ldMaxCommandVoltageV, Math.max(0, value))
}

export function estimateLdCurrentFromVoltageV(voltageV: number): number {
  return (clampLdCommandVoltageV(voltageV) / ldVoltageSpanV) * ldCurrentFullScaleA
}

export function estimateLdVoltageFromCurrentA(currentA: number): number {
  return (clampLdCommandCurrentA(currentA) / ldCurrentFullScaleA) * ldVoltageSpanV
}

export function estimateLdTempFromTmoVoltageV(voltageV: number): number {
  const clampedVoltageV = Math.max(0, Math.min(ldVoltageSpanV, voltageV))
  return 192.5576 - 90.104 * clampedVoltageV
}

export function estimateLdDiodeElectricalPowerW(currentA: number): number {
  return Math.max(0, currentA) * ldDiodeNominalVoltageV
}

export function estimateLdSupplyDrawW(currentA: number): number {
  return estimateLdDiodeElectricalPowerW(currentA) / 0.9
}
