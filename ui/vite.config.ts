import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'

// seven7 UI build.
//  - `npm run dev`   → Vite dev server; /api/* is proxied to s7bridge (the C++ engine) on :8787
//  - `npm run build` → static bundle in dist/, embedded into the desktop app by CMake
//                      (apps/seven7) and served from JUCE's WebBrowserComponent resource provider.
export default defineConfig({
  plugins: [react()],
  base: './',
  server: {
    host: '0.0.0.0',
    port: 5174,
    strictPort: true,
    allowedHosts: ['.e2b.app', 'localhost'],
    proxy: {
      '/api': { target: 'http://127.0.0.1:8787', changeOrigin: true },
    },
  },
  build: {
    outDir: 'dist',
    target: 'es2020',
    sourcemap: false,
    // One JS + one CSS file keeps the JUCE resource provider simple.
    rollupOptions: { output: { manualChunks: undefined } },
  },
})
