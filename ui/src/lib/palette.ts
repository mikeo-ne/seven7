// seven7 UI — 24-hue track palette (UIW §1.1). Regions inherit at 78% saturation.

export const TRACK_PALETTE: readonly string[] = [
  '#FF6B6B', '#FF8E53', '#FFA940', '#FFC53D', '#F5E663', '#BAE637',
  '#73D13D', '#36CFC9', '#5CDBD3', '#40A9FF', '#5B9DFF', '#597EF7',
  '#7B61FF', '#9254DE', '#B37FEB', '#F759AB', '#FF85C0', '#FF7A90',
  '#D4A373', '#B08968', '#8FB996', '#7FB3D5', '#A3A8C9', '#9AA0A6',
]

export function trackColor(index: number): string {
  const n = TRACK_PALETTE.length
  return TRACK_PALETTE[((index % n) + n) % n]
}

/** Region fill: track hue at reduced saturation/alpha; solid header strip stays full hue. */
export function regionFill(index: number, alpha = 0.55): string {
  const hex = trackColor(index)
  const r = parseInt(hex.slice(1, 3), 16)
  const g = parseInt(hex.slice(3, 5), 16)
  const b = parseInt(hex.slice(5, 7), 16)
  // desaturate 22% toward luminance (≈ "78% saturation")
  const l = 0.2126 * r + 0.7152 * g + 0.0722 * b
  const mix = (c: number) => Math.round(c * 0.78 + l * 0.22)
  return `rgba(${mix(r)}, ${mix(g)}, ${mix(b)}, ${alpha})`
}

export const KIND_GLYPH: Record<string, string> = {
  audio: '◉',
  instrument: '♪',
  aux: '⤷',
  bus: '⊕',
  vca: 'V',
  master: 'M',
}
