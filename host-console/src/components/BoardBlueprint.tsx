type BoardBlueprintProps = {
  mode?: 'idle' | 'flash' | 'inspect'
}

export function BoardBlueprint({ mode = 'idle' }: BoardBlueprintProps) {
  const accent = mode === 'flash' ? '#2b6ef2' : '#186a4b'
  const accentSoft = mode === 'flash' ? '#dfe9ff' : '#deece4'

  return (
    <div className="board-blueprint">
      <svg
        viewBox="0 0 1180 524"
        role="img"
        aria-label="Vector board schematic showing calibrated BOOT and RST button locations."
      >
        <rect x="40" y="54" width="760" height="252" rx="10" fill="#ffffff" stroke="#28332d" strokeWidth="2.5" />
        <rect x="84" y="94" width="186" height="118" rx="8" fill="#eff3ee" stroke="#c6d0c7" />
        <rect x="304" y="88" width="232" height="132" rx="8" fill="#16211d" />
        <rect x="568" y="94" width="104" height="116" rx="8" fill="#f8faf7" stroke="#c6d0c7" />
        <rect x="702" y="106" width="64" height="88" rx="6" fill="#f8faf7" stroke="#c6d0c7" />
        <rect x="98" y="230" width="612" height="44" rx="6" fill="#f8faf7" stroke="#c6d0c7" />

        <rect x="778" y="96" width="18" height="40" rx="4" fill="#202925" />
        <rect x="778" y="152" width="18" height="40" rx="4" fill="#202925" />

        <circle cx="789" cy="116" r="18" fill="#ffffff" stroke={accent} strokeWidth="3" />
        <circle cx="789" cy="172" r="18" fill="#ffffff" stroke="#28332d" strokeWidth="3" />

        <path d="M771 116h-16m68 0h-16m-18-34v16m0 36v16" stroke={accent} strokeWidth="1.8" strokeDasharray="3 4" />
        <path d="M771 172h-16m68 0h-16m-18-34v16m0 36v16" stroke="#5d6d63" strokeWidth="1.8" strokeDasharray="3 4" />

        <line x1="809" y1="116" x2="950" y2="116" stroke={accent} strokeWidth="2.5" />
        <line x1="809" y1="172" x2="950" y2="172" stroke="#28332d" strokeWidth="2.5" />

        <rect x="948" y="84" width="186" height="58" rx="6" fill={accentSoft} stroke={accent} />
        <rect x="948" y="146" width="186" height="58" rx="6" fill="#f1f4ef" stroke="#a7b5ab" />

        <text x="966" y="108" fill={accent} fontSize="22" fontWeight="700">RST</text>
        <text x="966" y="131" fill="#486154" fontSize="16">upper-right tactile switch</text>
        <text x="966" y="170" fill="#28332d" fontSize="22" fontWeight="700">BOOT</text>
        <text x="966" y="193" fill="#5d6d63" fontSize="16">lower-right tactile switch</text>

        <text x="96" y="120" fill="#28332d" fontSize="18" fontWeight="700">USB-C + serial edge</text>
        <text x="96" y="145" fill="#5d6d63" fontSize="14">programming, console, and native flash path</text>
        <text x="96" y="252" fill="#28332d" fontSize="15" fontWeight="700">Board reference for manual loader entry</text>
        <text x="96" y="272" fill="#5d6d63" fontSize="13">Right-edge button pair shown at scale for BOOT / RST access.</text>

        <line x1="76" y1="316" x2="804" y2="316" stroke="#9aa99f" strokeWidth="1.2" />
        <line x1="76" y1="322" x2="76" y2="310" stroke="#9aa99f" strokeWidth="1.2" />
        <line x1="804" y1="322" x2="804" y2="310" stroke="#9aa99f" strokeWidth="1.2" />
        <text x="392" y="338" fill="#5d6d63" fontSize="12">button cluster anchored to board edge geometry</text>

        <rect x="58" y="360" width="1064" height="112" rx="8" fill="#ffffff" stroke="#c6d0c7" />
        <text x="84" y="392" fill="#28332d" fontSize="18" fontWeight="700">Button cluster zoom</text>
        <text x="84" y="414" fill="#5d6d63" fontSize="14">Use this reference if the full-board view is too small at a constrained window width.</text>

        <rect x="88" y="430" width="100" height="18" rx="4" fill={accentSoft} stroke={accent} />
        <rect x="88" y="454" width="100" height="18" rx="4" fill="#f1f4ef" stroke="#a7b5ab" />
        <text x="120" y="443" fill={accent} fontSize="15" fontWeight="700">RST</text>
        <text x="112" y="467" fill="#28332d" fontSize="15" fontWeight="700">BOOT</text>

        <circle cx="318" cy="443" r="22" fill="#ffffff" stroke={accent} strokeWidth="3" />
        <circle cx="318" cy="467" r="22" fill="#ffffff" stroke="#28332d" strokeWidth="3" />
        <line x1="188" y1="439" x2="292" y2="439" stroke={accent} strokeWidth="2.4" />
        <line x1="188" y1="463" x2="292" y2="463" stroke="#28332d" strokeWidth="2.4" />

        <text x="366" y="440" fill={accent} fontSize="18" fontWeight="700">RST</text>
        <text x="366" y="460" fill="#486154" fontSize="15">upper-right tactile switch</text>
        <text x="366" y="486" fill="#28332d" fontSize="18" fontWeight="700">BOOT</text>
        <text x="366" y="506" fill="#5d6d63" fontSize="15">lower-right tactile switch</text>

        <text x="730" y="440" fill="#28332d" fontSize="16" fontWeight="700">Loader entry</text>
        <text x="730" y="462" fill="#5d6d63" fontSize="15">1. Hold BOOT.</text>
        <text x="730" y="482" fill="#5d6d63" fontSize="15">2. Tap RST.</text>
        <text x="730" y="502" fill="#5d6d63" fontSize="15">3. Release BOOT after the serial loader appears.</text>
      </svg>
    </div>
  )
}
