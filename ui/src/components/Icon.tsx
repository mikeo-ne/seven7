// seven7 UI — inline SVG icon set (16×16 grid). No font dependency, so the toolbar
// renders identically inside WKWebView, WebView2 and WebKitGTK.

const PATHS: Record<string, React.ReactNode> = {
  pointer: <path d="M4 2l9 8-4 .6 2.4 4.3-1.8 1L7.2 11.6 4 14.5z" fill="currentColor" />,
  smart: (
    <>
      <path d="M8 1.5l1.6 4.4L14 7.5l-4.4 1.6L8 13.5 6.4 9.1 2 7.5l4.4-1.6z" fill="currentColor" />
      <circle cx="13" cy="13" r="1.6" fill="currentColor" />
    </>
  ),
  grabber: (
    <path
      d="M6 14.5c-1.5 0-2.4-.8-3.2-2.3L1.6 9.6c-.3-.6 0-1.2.6-1.4.5-.2 1 0 1.3.5l.9 1.5V4.3c0-.6.5-1 1-1s1 .4 1 1v4h.4V2.8c0-.6.5-1 1-1s1 .4 1 1v5.5h.4V3.5c0-.6.5-1 1-1s1 .4 1 1v4.8h.4V5.2c0-.6.5-1 1-1s1 .4 1 1v5.3c0 2.3-1.7 4-4 4z"
      fill="currentColor"
    />
  ),
  selector: <path d="M5 2h6v1.5H8.8v9H11V14H5v-1.5h2.2v-9H5z" fill="currentColor" />,
  marquee: <path d="M2 2h3v1.5H3.5V5H2zm9 0h3v3h-1.5V3.5H11zM2 11h1.5v1.5H5V14H2zm10.5 0H14v3h-3v-1.5h1.5zM6.5 2h3v1.5h-3zm0 10.5h3V14h-3zM2 6.5h1.5v3H2zm10.5 0H14v3h-1.5z" fill="currentColor" />,
  pencil: <path d="M11.3 1.6l3.1 3.1-8.6 8.6L2 14l.7-3.8zM3.9 10.8l-.3 1.6 1.6-.3 6.4-6.4-1.3-1.3z" fill="currentColor" />,
  scissors: (
    <>
      <circle cx="4.5" cy="4" r="2" fill="none" stroke="currentColor" strokeWidth="1.5" />
      <circle cx="4.5" cy="12" r="2" fill="none" stroke="currentColor" strokeWidth="1.5" />
      <path d="M6 5.3L14 12M6 10.7L14 4" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" />
    </>
  ),
  eraser: <path d="M9.6 2.2l4.2 4.2-6.8 6.8H3.6L1.8 11.4 9.6 2.2zM4.2 12.2h2.1l1.9-1.9-2.6-2.6-2.5 2.9z" fill="currentColor" />,
  zoom: (
    <>
      <circle cx="6.5" cy="6.5" r="4.5" fill="none" stroke="currentColor" strokeWidth="1.6" />
      <path d="M10 10l4 4" stroke="currentColor" strokeWidth="1.8" strokeLinecap="round" />
      <path d="M4.5 6.5h4M6.5 4.5v4" stroke="currentColor" strokeWidth="1.2" strokeLinecap="round" />
    </>
  ),
  play: <path d="M4 2.5l9 5.5-9 5.5z" fill="currentColor" />,
  pause: <path d="M4 3h3v10H4zm5 0h3v10H9z" fill="currentColor" />,
  stop: <rect x="3.5" y="3.5" width="9" height="9" rx="1" fill="currentColor" />,
  record: <circle cx="8" cy="8" r="4.5" fill="currentColor" />,
  rtz: <path d="M3 3h1.8v10H3zm10 0v10L5.8 8z" fill="currentColor" />,
  rew: <path d="M8 3v10L1.5 8zm7 0v10L8.5 8z" fill="currentColor" />,
  ffw: <path d="M1 3l6.5 5L1 13zm7 0l6.5 5L8 13z" fill="currentColor" />,
  loop: (
    <>
      <path d="M3 8a5 5 0 0 1 8.5-3.6" fill="none" stroke="currentColor" strokeWidth="1.6" strokeLinecap="round" />
      <path d="M13 8a5 5 0 0 1-8.5 3.6" fill="none" stroke="currentColor" strokeWidth="1.6" strokeLinecap="round" />
      <path d="M11.8 1.5v3.2H8.6M4.2 14.5v-3.2h3.2" fill="none" stroke="currentColor" strokeWidth="1.6" strokeLinecap="round" strokeLinejoin="round" />
    </>
  ),
  metronome: <path d="M6 1.5h4l2.5 12H3.5zM7 3l-1.7 9h5.4L9 3zm5.2 1.2l1.3.7-3.2 6-1.3-.7z" fill="currentColor" />,
  undo: <path d="M6 3L1.5 7 6 11V8.2h4a3 3 0 0 1 0 6H6v1.6h4a4.6 4.6 0 0 0 0-9.2H6z" fill="currentColor" />,
  redo: <path d="M10 3l4.5 4L10 11V8.2H6a3 3 0 0 0 0 6h4v1.6H6a4.6 4.6 0 0 1 0-9.2h4z" fill="currentColor" />,
  inspector: <path d="M2 2h12v12H2zm1.5 1.5v9h3v-9zm4.5 0v9h4.5v-9z" fill="currentColor" />,
  library: <path d="M2 2h12v12H2zm1.5 1.5v9h9v-9zM5 5h6v1.5H5zm0 3h6v1.5H5z" fill="currentColor" />,
  editor: <path d="M2 2h12v12H2zm1.5 1.5v5h9v-5zM5 10h6v1.5H5z" fill="currentColor" />,
  mixer: <path d="M3 2h1.6v5H6v1.6H4.6V14H3V8.6H1.6V7H3zm4.2 0h1.6v9h1.4v1.6H8.8V14H7.2v-1.4H5.8V11h1.4zm4.2 0H13v3h1.4v1.6H13V14h-1.6V6.6H10V5h1.4z" fill="currentColor" />,
  automation: <path d="M1.5 12l4-6 3 4 2.5-7 3.5 9" fill="none" stroke="currentColor" strokeWidth="1.6" strokeLinecap="round" strokeLinejoin="round" />,
  list: <path d="M2 3h12v1.6H2zm0 4.2h12v1.6H2zm0 4.2h12V13H2z" fill="currentColor" />,
  focus: (
    <>
      <circle cx="8" cy="8" r="5.5" fill="none" stroke="currentColor" strokeWidth="1.5" />
      <circle cx="8" cy="8" r="2" fill="currentColor" />
    </>
  ),
  nudgeL: <path d="M10.5 2.5L5 8l5.5 5.5z" fill="currentColor" />,
  nudgeR: <path d="M5.5 2.5L11 8l-5.5 5.5z" fill="currentColor" />,
  close: <path d="M3.5 3.5l9 9m0-9l-9 9" stroke="currentColor" strokeWidth="1.8" strokeLinecap="round" />,
  add: <path d="M7.2 2.5h1.6v4.7h4.7v1.6H8.8v4.7H7.2V8.8H2.5V7.2h4.7z" fill="currentColor" />,
  marker: <path d="M3 1.5h1.6v13H3zM4.6 2.5h8.4l-2.5 3 2.5 3H4.6z" fill="currentColor" />,
  audio: <path d="M1.5 7h1.5v2H1.5zm2.5-3h1.5v8H4zm2.5 1.5H8v5H6.5zM9 2h1.5v12H9zm2.5 3H13v6h-1.5zm2.5 1.5h1v3h-1z" fill="currentColor" />,
  midi: <path d="M2 3h12v10H2zm1.5 1.5v7h9v-7zM5 6h1.5v4H5zm2.5 0H9v4H7.5zm2.5 0h1.5v4H10z" fill="currentColor" />,
}

export function Icon({ name, size = 14, className }: { name: keyof typeof PATHS | string; size?: number; className?: string }) {
  return (
    <svg width={size} height={size} viewBox="0 0 16 16" className={className} aria-hidden="true" focusable="false" style={{ display: 'block', flexShrink: 0 }}>
      {PATHS[name] ?? <circle cx="8" cy="8" r="3" fill="currentColor" />}
    </svg>
  )
}
