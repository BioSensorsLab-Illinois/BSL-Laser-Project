export interface TecCalibrationPoint {
  tempC: number
  tecVoltageV: number
  wavelengthNm: number
}

const measuredTempVoltagePoints = [
  { tempC: 5.0, tecVoltageV: 0.11 },
  { tempC: 6.0, tecVoltageV: 0.18 },
  { tempC: 7.5, tecVoltageV: 0.221 },
  { tempC: 10.7, tecVoltageV: 0.316 },
  { tempC: 15.5, tecVoltageV: 0.473 },
  { tempC: 20.0, tecVoltageV: 0.606 },
  { tempC: 20.5, tecVoltageV: 0.63 },
  { tempC: 28.2, tecVoltageV: 0.957 },
  { tempC: 29.0, tecVoltageV: 0.985 },
  { tempC: 37.4, tecVoltageV: 1.345 },
  { tempC: 44.7, tecVoltageV: 1.64 },
  { tempC: 46.1, tecVoltageV: 1.7 },
  { tempC: 54.5, tecVoltageV: 2.03 },
  { tempC: 58.7, tecVoltageV: 2.182 },
  { tempC: 65.0, tecVoltageV: 2.429 },
]

function clamp(value: number, min: number, max: number): number {
  if (value < min) {
    return min
  }

  if (value > max) {
    return max
  }

  return value
}

function interpolate(points: readonly { x: number; y: number }[], value: number): number {
  if (points.length === 0) {
    return 0
  }

  if (value <= points[0].x) {
    return points[0].y
  }

  const lastPoint = points[points.length - 1]
  if (value >= lastPoint.x) {
    return lastPoint.y
  }

  for (let index = 1; index < points.length; index += 1) {
    const previous = points[index - 1]
    const current = points[index]

    if (value <= current.x) {
      const span = current.x - previous.x
      const ratio = span === 0 ? 0 : (value - previous.x) / span
      return previous.y + (current.y - previous.y) * ratio
    }
  }

  return lastPoint.y
}

function inferTempFromVoltage(tecVoltageV: number): number {
  const table = measuredTempVoltagePoints
    .map((point) => ({ x: point.tecVoltageV, y: point.tempC }))
    .sort((left, right) => left.x - right.x)

  return interpolate(table, tecVoltageV)
}

// The screenshot provided by the user contains wavelength rows where the
// temperature label is omitted. Those rows are recovered here by inferring
// temperature from the measured TEC voltage using the same calibration table.
export const tecCalibrationPoints: TecCalibrationPoint[] = [
  { tempC: 5.0, tecVoltageV: 0.11, wavelengthNm: 771.2 },
  { tempC: 7.5, tecVoltageV: 0.221, wavelengthNm: 771.8 },
  { tempC: 10.7, tecVoltageV: 0.316, wavelengthNm: 772.8 },
  { tempC: 15.5, tecVoltageV: 0.473, wavelengthNm: 774.8 },
  { tempC: 20.0, tecVoltageV: 0.606, wavelengthNm: 776.8 },
  { tempC: 20.5, tecVoltageV: 0.63, wavelengthNm: 776.9 },
  { tempC: inferTempFromVoltage(0.8), tecVoltageV: 0.8, wavelengthNm: 778.4 },
  { tempC: 28.2, tecVoltageV: 0.957, wavelengthNm: 779.8 },
  { tempC: 29.0, tecVoltageV: 0.985, wavelengthNm: 780.1 },
  { tempC: inferTempFromVoltage(1.224), tecVoltageV: 1.224, wavelengthNm: 780.5 },
  { tempC: 37.4, tecVoltageV: 1.345, wavelengthNm: 781.6 },
  { tempC: inferTempFromVoltage(1.511), tecVoltageV: 1.511, wavelengthNm: 781.6 },
  { tempC: 44.7, tecVoltageV: 1.64, wavelengthNm: 783.0 },
  { tempC: 46.1, tecVoltageV: 1.7, wavelengthNm: 783.1 },
  { tempC: inferTempFromVoltage(1.8), tecVoltageV: 1.8, wavelengthNm: 783.2 },
  { tempC: inferTempFromVoltage(1.9), tecVoltageV: 1.9, wavelengthNm: 784.2 },
  { tempC: 54.5, tecVoltageV: 2.03, wavelengthNm: 784.6 },
  { tempC: 58.7, tecVoltageV: 2.182, wavelengthNm: 786.1 },
  { tempC: inferTempFromVoltage(2.235), tecVoltageV: 2.235, wavelengthNm: 787.4 },
  { tempC: 65.0, tecVoltageV: 2.429, wavelengthNm: 790.0 },
].sort((left, right) => left.tempC - right.tempC)

const wavelengthByTempTable = tecCalibrationPoints.map((point) => ({
  x: point.tempC,
  y: point.wavelengthNm,
}))

const tempByWavelengthTable = tecCalibrationPoints
  .map((point) => ({ x: point.wavelengthNm, y: point.tempC }))
  .sort((left, right) => left.x - right.x)

const voltageByTempTable = tecCalibrationPoints.map((point) => ({
  x: point.tempC,
  y: point.tecVoltageV,
}))

export function clampTecTempC(tempC: number): number {
  return clamp(tempC, tecCalibrationPoints[0].tempC, tecCalibrationPoints[tecCalibrationPoints.length - 1].tempC)
}

export function clampTecVoltageV(tecVoltageV: number): number {
  return clamp(
    tecVoltageV,
    tecCalibrationPoints[0].tecVoltageV,
    tecCalibrationPoints[tecCalibrationPoints.length - 1].tecVoltageV,
  )
}

export function clampTecWavelengthNm(wavelengthNm: number): number {
  const min = tempByWavelengthTable[0].x
  const max = tempByWavelengthTable[tempByWavelengthTable.length - 1].x
  return clamp(wavelengthNm, min, max)
}

export function estimateWavelengthFromTempC(tempC: number): number {
  return interpolate(wavelengthByTempTable, clampTecTempC(tempC))
}

export function estimateTempFromWavelengthNm(wavelengthNm: number): number {
  return interpolate(tempByWavelengthTable, clampTecWavelengthNm(wavelengthNm))
}

export function estimateTempFromTecVoltageV(tecVoltageV: number): number {
  return inferTempFromVoltage(clampTecVoltageV(tecVoltageV))
}

export function estimateTecVoltageFromTempC(tempC: number): number {
  return interpolate(voltageByTempTable, clampTecTempC(tempC))
}

export function estimateTecVoltageFromWavelengthNm(wavelengthNm: number): number {
  return estimateTecVoltageFromTempC(estimateTempFromWavelengthNm(wavelengthNm))
}

export function estimateWavelengthFromTecVoltageV(tecVoltageV: number): number {
  return estimateWavelengthFromTempC(estimateTempFromTecVoltageV(tecVoltageV))
}
