import Foundation

extension DeviceSnapshot {
    /// Overlays a `live_telemetry` patch onto a live snapshot. Only sub-blocks
    /// named in `presentKeys` are merged; every other sub-block is preserved
    /// from `base`. Within a present sub-block, only fields actually in the
    /// raw JSON override the base values — absent fields keep the prior live
    /// state. Without per-field handling, a partial sub-block (e.g. `tec` with
    /// only `tempC` sent) would clobber every other tec field with 0.
    /// Mirrors `host-console/src/lib/live-telemetry.ts` spread-merge semantics.
    public static func overlay(
        base: DeviceSnapshot,
        patch: DeviceSnapshot,
        presentKeys: Set<String>,
        rawPayload: Data? = nil
    ) -> DeviceSnapshot {
        var merged = base
        let raw = rawPayload.flatMap { try? JSONSerialization.jsonObject(with: $0) as? [String: Any] }

        if presentKeys.contains("session") { merged.session = patch.session }
        if presentKeys.contains("rails"),   let r = raw?["rails"]   as? [String: Any] { merged.rails   = merged.rails.overlay(with: r)   } else if presentKeys.contains("rails")   { merged.rails   = patch.rails   }
        if presentKeys.contains("imu"),     let r = raw?["imu"]     as? [String: Any] { merged.imu     = merged.imu.overlay(with: r)     } else if presentKeys.contains("imu")     { merged.imu     = patch.imu     }
        if presentKeys.contains("tof"),     let r = raw?["tof"]     as? [String: Any] { merged.tof     = merged.tof.overlay(with: r)     } else if presentKeys.contains("tof")     { merged.tof     = patch.tof     }
        if presentKeys.contains("laser"),   let r = raw?["laser"]   as? [String: Any] { merged.laser   = merged.laser.overlay(with: r)   } else if presentKeys.contains("laser")   { merged.laser   = patch.laser   }
        if presentKeys.contains("tec"),     let r = raw?["tec"]     as? [String: Any] { merged.tec     = merged.tec.overlay(with: r)     } else if presentKeys.contains("tec")     { merged.tec     = patch.tec     }
        if presentKeys.contains("pd"),      let r = raw?["pd"]      as? [String: Any] { merged.pd      = merged.pd.overlay(with: r)      } else if presentKeys.contains("pd")      { merged.pd      = patch.pd      }
        if presentKeys.contains("bench"),   let r = raw?["bench"]   as? [String: Any] { merged.bench   = merged.bench.overlay(with: r)   } else if presentKeys.contains("bench")   { merged.bench   = patch.bench   }
        if presentKeys.contains("safety"),  let r = raw?["safety"]  as? [String: Any] { merged.safety  = merged.safety.overlay(with: r)  } else if presentKeys.contains("safety")  { merged.safety  = patch.safety  }
        if presentKeys.contains("fault"),   let r = raw?["fault"]   as? [String: Any] { merged.fault   = merged.fault.overlay(with: r)   } else if presentKeys.contains("fault")   { merged.fault   = patch.fault   }
        if presentKeys.contains("deployment"), let r = raw?["deployment"] as? [String: Any] { merged.deployment = merged.deployment.overlay(with: r) } else if presentKeys.contains("deployment") { merged.deployment = patch.deployment }
        return merged
    }
}

// MARK: - Per-field overlays
//
// Each sub-block's `overlay(with:)` checks for the raw JSON key and only
// overrides if actually present. This is the Swift mirror of the JS
// `{ ...base, ...patch }` spread pattern that host-console uses.

extension RailState {
    public func overlay(with patch: [String: Any]) -> RailState {
        var c = self
        if let v = patch["enabled"] as? Bool { c.enabled = v }
        if let v = patch["pgood"]   as? Bool { c.pgood   = v }
        return c
    }
}

extension RailsBlock {
    public func overlay(with patch: [String: Any]) -> RailsBlock {
        var c = self
        if let r = patch["ld"]  as? [String: Any] { c.ld  = c.ld.overlay(with: r)  }
        if let r = patch["tec"] as? [String: Any] { c.tec = c.tec.overlay(with: r) }
        return c
    }
}

extension ImuStatus {
    public func overlay(with patch: [String: Any]) -> ImuStatus {
        var c = self
        if let v = patch["valid"] as? Bool { c.valid = v }
        if let v = patch["fresh"] as? Bool { c.fresh = v }
        return c
    }
}

extension TofStatus {
    public func overlay(with patch: [String: Any]) -> TofStatus {
        var c = self
        if let v = patch["valid"]     as? Bool   { c.valid     = v }
        if let v = patch["fresh"]     as? Bool   { c.fresh     = v }
        if let v = patch["distanceM"] as? Double { c.distanceM = v }
        return c
    }
}

extension LaserStatus {
    public func overlay(with patch: [String: Any]) -> LaserStatus {
        var c = self
        if let v = patch["nirEnabled"]         as? Bool   { c.nirEnabled         = v }
        if let v = patch["alignmentEnabled"]   as? Bool   { c.alignmentEnabled   = v }
        if let v = patch["measuredCurrentA"]   as? Double { c.measuredCurrentA   = v }
        if let v = patch["commandedCurrentA"]  as? Double { c.commandedCurrentA  = v }
        if let v = patch["loopGood"]           as? Bool   { c.loopGood           = v }
        if let v = patch["driverTempC"]        as? Double { c.driverTempC        = v }
        if let v = patch["driverStandby"]      as? Bool   { c.driverStandby      = v }
        return c
    }
}

extension TecStatus {
    public func overlay(with patch: [String: Any]) -> TecStatus {
        var c = self
        if let v = patch["targetTempC"]               as? Double { c.targetTempC               = v }
        if let v = patch["targetLambdaNm"]            as? Double { c.targetLambdaNm            = v }
        if let v = patch["actualLambdaNm"]            as? Double { c.actualLambdaNm            = v }
        if let v = patch["tempGood"]                  as? Bool   { c.tempGood                  = v }
        if let v = patch["tempC"]                     as? Double { c.tempC                     = v }
        if let v = patch["currentA"]                  as? Double { c.currentA                  = v }
        if let v = patch["voltageV"]                  as? Double { c.voltageV                  = v }
        if let v = patch["settlingSecondsRemaining"]  as? Int    { c.settlingSecondsRemaining  = v }
        return c
    }
}

extension PdStatus {
    public func overlay(with patch: [String: Any]) -> PdStatus {
        var c = self
        if let v = patch["contractValid"]    as? Bool   { c.contractValid    = v }
        if let v = patch["negotiatedPowerW"] as? Double { c.negotiatedPowerW = v }
        if let v = patch["sourceVoltageV"]   as? Double { c.sourceVoltageV   = v }
        if let v = patch["sourceCurrentA"]   as? Double { c.sourceCurrentA   = v }
        return c
    }
}

extension BenchControlStatus {
    public func overlay(with patch: [String: Any]) -> BenchControlStatus {
        var c = self
        if let v = patch["requestedNirEnabled"]      as? Bool   { c.requestedNirEnabled      = v }
        if let v = patch["requestedCurrentA"]        as? Double { c.requestedCurrentA        = v }
        if let v = patch["requestedLedEnabled"]      as? Bool   { c.requestedLedEnabled      = v }
        if let v = patch["requestedLedDutyCyclePct"] as? Int    { c.requestedLedDutyCyclePct = v }
        if let raw = patch["appliedLedOwner"] as? String,
           let owner = AppliedLedOwner(rawValue: raw) { c.appliedLedOwner = owner }
        if let v = patch["appliedLedPinHigh"]        as? Bool   { c.appliedLedPinHigh        = v }
        if let v = patch["illuminationEnabled"]      as? Bool   { c.illuminationEnabled      = v }
        if let v = patch["illuminationDutyCyclePct"] as? Int    { c.illuminationDutyCyclePct = v }
        if let r = patch["hostControlReadiness"]     as? [String: Any] {
            c.hostControlReadiness = c.hostControlReadiness.overlay(with: r)
        }
        if let r = patch["usbDebugMock"] as? [String: Any] {
            c.usbDebugMock = c.usbDebugMock.overlay(with: r)
        }
        return c
    }
}

extension HostControlReadiness {
    public func overlay(with patch: [String: Any]) -> HostControlReadiness {
        var c = self
        if let raw = patch["nirBlockedReason"] as? String,
           let r = NirBlockedReason(rawValue: raw) { c.nirBlockedReason = r }
        if let raw = patch["ledBlockedReason"] as? String,
           let r = LedBlockedReason(rawValue: raw) { c.ledBlockedReason = r }
        if let raw = patch["sbdnState"] as? String,
           let s = SbdnState(rawValue: raw) { c.sbdnState = s }
        return c
    }
}

extension UsbDebugMockStatus {
    public func overlay(with patch: [String: Any]) -> UsbDebugMockStatus {
        var c = self
        if let v = patch["active"]             as? Bool   { c.active             = v }
        if let v = patch["pdConflictLatched"]  as? Bool   { c.pdConflictLatched  = v }
        if let v = patch["lastDisableReason"]  as? String { c.lastDisableReason  = v }
        return c
    }
}

extension SafetyStatus {
    public func overlay(with patch: [String: Any]) -> SafetyStatus {
        var c = self
        if let v = patch["allowAlignment"]       as? Bool   { c.allowAlignment       = v }
        if let v = patch["allowNir"]             as? Bool   { c.allowNir             = v }
        if let v = patch["horizonBlocked"]       as? Bool   { c.horizonBlocked       = v }
        if let v = patch["distanceBlocked"]      as? Bool   { c.distanceBlocked      = v }
        if let v = patch["lambdaDriftBlocked"]   as? Bool   { c.lambdaDriftBlocked   = v }
        if let v = patch["tecTempAdcBlocked"]    as? Bool   { c.tecTempAdcBlocked    = v }
        if let v = patch["horizonThresholdDeg"]  as? Double { c.horizonThresholdDeg  = v }
        if let v = patch["horizonHysteresisDeg"] as? Double { c.horizonHysteresisDeg = v }
        if let v = patch["tofMinRangeM"]         as? Double { c.tofMinRangeM         = v }
        if let v = patch["tofMaxRangeM"]         as? Double { c.tofMaxRangeM         = v }
        if let v = patch["tofHysteresisM"]       as? Double { c.tofHysteresisM       = v }
        if let v = patch["imuStaleMs"]           as? Int    { c.imuStaleMs           = v }
        if let v = patch["tofStaleMs"]           as? Int    { c.tofStaleMs           = v }
        if let v = patch["railGoodTimeoutMs"]    as? Int    { c.railGoodTimeoutMs    = v }
        if let v = patch["lambdaDriftLimitNm"]   as? Double { c.lambdaDriftLimitNm   = v }
        if let v = patch["lambdaDriftHysteresisNm"] as? Double { c.lambdaDriftHysteresisNm = v }
        if let v = patch["lambdaDriftHoldMs"]    as? Int    { c.lambdaDriftHoldMs    = v }
        if let v = patch["ldOvertempLimitC"]     as? Double { c.ldOvertempLimitC     = v }
        if let v = patch["tecTempAdcTripV"]      as? Double { c.tecTempAdcTripV      = v }
        if let v = patch["tecTempAdcHysteresisV"] as? Double { c.tecTempAdcHysteresisV = v }
        if let v = patch["tecTempAdcHoldMs"]     as? Int    { c.tecTempAdcHoldMs     = v }
        if let v = patch["tecMinCommandC"]       as? Double { c.tecMinCommandC       = v }
        if let v = patch["tecMaxCommandC"]       as? Double { c.tecMaxCommandC       = v }
        if let v = patch["tecReadyToleranceC"]   as? Double { c.tecReadyToleranceC   = v }
        if let v = patch["maxLaserCurrentA"]     as? Double { c.maxLaserCurrentA     = v }
        if let v = patch["offCurrentThresholdA"] as? Double { c.offCurrentThresholdA = v }
        if let v = patch["maxTofLedDutyCyclePct"] as? Int   { c.maxTofLedDutyCyclePct = v }
        if let v = patch["lioVoltageOffsetV"]    as? Double { c.lioVoltageOffsetV    = v }
        if let v = patch["actualLambdaNm"]       as? Double { c.actualLambdaNm       = v }
        if let v = patch["targetLambdaNm"]       as? Double { c.targetLambdaNm       = v }
        if let v = patch["lambdaDriftNm"]        as? Double { c.lambdaDriftNm        = v }
        if let v = patch["tempAdcVoltageV"]      as? Double { c.tempAdcVoltageV      = v }
        // interlocks block is atomic on the wire; replace if present.
        if let r = patch["interlocks"] as? [String: Any],
           let data = try? JSONSerialization.data(withJSONObject: r),
           let mask = try? JSONDecoder().decode(InterlockEnableMask.self, from: data) {
            c.interlocks = mask
        }
        return c
    }
}

extension FaultSummary {
    public func overlay(with patch: [String: Any]) -> FaultSummary {
        var c = self
        if let v = patch["latched"]       as? Bool   { c.latched       = v }
        if let v = patch["activeCode"]    as? String { c.activeCode    = v }
        if let v = patch["activeClass"]   as? String { c.activeClass   = v }
        if let v = patch["latchedCode"]   as? String { c.latchedCode   = v }
        if let v = patch["latchedClass"]  as? String { c.latchedClass  = v }
        if let v = patch["activeReason"]  as? String { c.activeReason  = v }
        if let v = patch["latchedReason"] as? String { c.latchedReason = v }
        if let v = patch["activeCount"]   as? Int    { c.activeCount   = v }
        if let v = patch["tripCounter"]   as? Int    { c.tripCounter   = v }
        if let v = patch["lastFaultAtIso"] as? String { c.lastFaultAtIso = v }
        // triggerDiag is atomic on the wire; replace if present.
        if let r = patch["triggerDiag"] as? [String: Any],
           let data = try? JSONSerialization.data(withJSONObject: r),
           let d = try? JSONDecoder().decode(FaultTriggerDiag.self, from: data) {
            c.triggerDiag = d
        }
        return c
    }
}

extension DeploymentSnapshot {
    public func overlay(with patch: [String: Any]) -> DeploymentSnapshot {
        var c = self
        if let v = patch["active"]           as? Bool   { c.active           = v }
        if let v = patch["running"]          as? Bool   { c.running          = v }
        if let v = patch["ready"]            as? Bool   { c.ready            = v }
        if let v = patch["readyIdle"]        as? Bool   { c.readyIdle        = v }
        if let raw = patch["phase"] as? String,
           let p = DeploymentPhase(rawValue: raw) { c.phase = p }
        if let v = patch["maxLaserCurrentA"] as? Double { c.maxLaserCurrentA = v }
        if let v = patch["targetLambdaNm"]   as? Double { c.targetLambdaNm   = v }
        if let v = patch["targetTempC"]      as? Double { c.targetTempC      = v }
        return c
    }
}
