import { defineConfig } from 'vitest/config'
import react from '@vitejs/plugin-react'

// Unit tests: pure libs (units, palette, snap).
// Integration tests (S7_BRIDGE=http://127.0.0.1:8787): mount the real app against a running s7bridge.
export default defineConfig({
  plugins: [react()],
  test: {
    environment: 'happy-dom',
    include: ['src/**/*.test.{ts,tsx}'],
    testTimeout: 20000,
  },
})
