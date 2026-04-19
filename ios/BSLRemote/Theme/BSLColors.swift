import SwiftUI

/// Design tokens for the BSLRemote redesign. Mirrors the palette in
/// `bsl-laser-system/project/BSL Laser Controller.html:396-433`.
enum BSL {

    // Brand (UIUC BioSensors Lab)
    static let orange     = Color(red: 1.00, green: 0.372, blue: 0.019)   // #FF5F05
    static let orangeDeep = Color(red: 0.85, green: 0.309, blue: 0.000)   // #D94F00
    static let orangeSoft = Color(red: 1.00, green: 0.913, blue: 0.862)   // #FFE9DC
    static let navy       = Color(red: 0.074, green: 0.160, blue: 0.294)  // #13294B
    static let navyDeep   = Color(red: 0.039, green: 0.090, blue: 0.188)  // #0A1730
    static let navySoft   = Color(red: 0.890, green: 0.909, blue: 0.941)  // #E3E8F0

    // Semantic
    static let ok          = Color(red: 0.090, green: 0.698, blue: 0.415) // #17B26A
    static let okSoft      = Color(red: 0.862, green: 0.980, blue: 0.901) // #DCFAE6
    static let caution     = Color(red: 0.960, green: 0.647, blue: 0.141) // #F5A524
    static let cautionSoft = Color(red: 0.996, green: 0.956, blue: 0.831) // #FEF4D4
    static let warn        = Color(red: 0.850, green: 0.176, blue: 0.125) // #D92D20
    static let warnSoft    = Color(red: 0.996, green: 0.894, blue: 0.886) // #FEE4E2

    // NIR (near-infrared indicator)
    static let nir     = Color(red: 0.545, green: 0.172, blue: 0.960)     // #8B2CF5
    static let nirSoft = Color(red: 0.933, green: 0.882, blue: 1.000)     // #EEE1FF

    // Neutral palette — light theme
    enum Light {
        static let bg       = Color(red: 0.980, green: 0.980, blue: 0.980)    // #FAFAFA
        static let surface  = Color.white
        static let border   = Color(red: 0.074, green: 0.160, blue: 0.294, opacity: 0.08)
        static let borderStrong = Color(red: 0.074, green: 0.160, blue: 0.294, opacity: 0.16)
        static let ink      = Color(red: 0.058, green: 0.090, blue: 0.164)    // #0F172A
        static let muted    = Color(red: 0.058, green: 0.090, blue: 0.164, opacity: 0.55)
        static let dim      = Color(red: 0.058, green: 0.090, blue: 0.164, opacity: 0.35)
    }

    // Neutral palette — dark / OR theme
    enum Dark {
        static let bg       = Color(red: 0.043, green: 0.058, blue: 0.086)    // #0B0F16
        static let surface  = Color(red: 0.078, green: 0.101, blue: 0.141)    // #141A24
        static let surface2 = Color(red: 0.106, green: 0.133, blue: 0.188)    // #1B2230
        static let border   = Color.white.opacity(0.08)
        static let borderStrong = Color.white.opacity(0.16)
        static let ink      = Color(red: 0.960, green: 0.968, blue: 0.980)    // #F5F7FA
        static let muted    = Color(red: 0.960, green: 0.968, blue: 0.980, opacity: 0.60)
        static let dim      = Color(red: 0.960, green: 0.968, blue: 0.980, opacity: 0.35)
    }
}
