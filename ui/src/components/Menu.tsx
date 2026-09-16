import { useEffect, useRef, useState } from 'react'

export interface MenuItem {
  label: string
  shortcut?: string
  disabled?: boolean
  separator?: boolean
  onSelect?: () => void
}

/** Minimal dropdown menu button (toolbar "File ▾" etc.). Closes on outside click / Escape. */
export function MenuButton({ label, items, className = '', title }: { label: React.ReactNode; items: MenuItem[]; className?: string; title?: string }) {
  const [open, setOpen] = useState(false)
  const ref = useRef<HTMLDivElement>(null)
  useEffect(() => {
    if (!open) return
    const onDown = (e: MouseEvent) => {
      if (!ref.current?.contains(e.target as Node)) setOpen(false)
    }
    const onKey = (e: KeyboardEvent) => {
      if (e.key === 'Escape') setOpen(false)
    }
    window.addEventListener('mousedown', onDown)
    window.addEventListener('keydown', onKey)
    return () => {
      window.removeEventListener('mousedown', onDown)
      window.removeEventListener('keydown', onKey)
    }
  }, [open])
  return (
    <div className="menu-anchor" ref={ref}>
      <button className={`btn ${open ? 'on' : ''} ${className}`} title={title} onClick={() => setOpen((o) => !o)} aria-haspopup="menu" aria-expanded={open}>
        {label} <span className="caret">▾</span>
      </button>
      {open && (
        <div className="menu" role="menu">
          {items.map((it, i) =>
            it.separator ? (
              <div key={i} className="sep" />
            ) : (
              <button
                key={i}
                role="menuitem"
                disabled={it.disabled}
                onClick={() => {
                  setOpen(false)
                  it.onSelect?.()
                }}
              >
                <span>{it.label}</span>
                {it.shortcut && <span className="sc">{it.shortcut}</span>}
              </button>
            ),
          )}
        </div>
      )}
    </div>
  )
}
