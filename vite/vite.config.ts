import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'
import svgr from "vite-plugin-svgr"

// https://vite.dev/config/
export default defineConfig({
  build: {
    chunkSizeWarningLimit: 2000,
    rollupOptions: {
      input: {
        main: 'index.html',
        t3k_callback: 't3k_response.html',  // your alternate page
      }
    }
  },
  plugins: [react(),svgr()],
  server: {
    proxy: {
      '/pipedal': {
        target: process.env.PIPEDAL_SERVER ?? 'http://localhost:8080',
        ws: true,
        changeOrigin: !!process.env.PIPEDAL_SERVER,
      },
      '/resources': {
        target: process.env.PIPEDAL_SERVER ?? 'http://localhost:8080',
        changeOrigin: !!process.env.PIPEDAL_SERVER,
      },
      '^/var/.*': {
        target: process.env.PIPEDAL_SERVER ?? 'http://localhost:8080',
        changeOrigin: !!process.env.PIPEDAL_SERVER,
      },
    }
}
})
