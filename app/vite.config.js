import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'
import viteCompression from 'vite-plugin-compression'

export default defineConfig({
  base: './',
  plugins: [
    react(),
    viteCompression({ algorithm: 'gzip', deleteOriginFile: true, threshold: 0 }),
  ],
  build: {
    outDir: '../data',      // pasta que o PlatformIO envia para a ESP32
    emptyOutDir: true,
    sourcemap: false,
    rollupOptions: {
      output: {
        entryFileNames: 'assets/app.js',
        chunkFileNames: 'assets/[name].js',
        assetFileNames: 'assets/[name][extname]',
      },
    },
  },
})