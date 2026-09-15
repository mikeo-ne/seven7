import { defineConfig } from 'vitepress'
import { withMermaid } from 'vitepress-plugin-mermaid'

// https://vitepress.dev/reference/site-config
export default withMermaid(
  defineConfig({
    title: 'seven7',
    titleTemplate: 'seven7 · DAW Blueprint',
    description:
      'Design blueprint for seven7 — a next-generation DAW bridging Logic Pro creativity and Pro Tools precision.',
    cleanUrls: true,
    lastUpdated: true,
    appearance: 'dark',

    vite: {
      server: {
        // Allow sandboxed/preview proxy hosts (e.g. *.e2b.app) to reach the dev server.
        allowedHosts: ['.e2b.app'],
      },
    },

    head: [
      ['link', { rel: 'icon', type: 'image/png', href: '/logo.png' }],
      ['meta', { name: 'theme-color', content: '#17181c' }],
    ],

    mermaid: {
      // docs: https://mermaid.js.org/config/setup/modules/mermaidAPI.html#mermaidapi-configuration-defaults
      theme: 'dark',
    },

    themeConfig: {
      logo: '/logo.png',
      siteTitle: 'seven7',

      nav: [
        { text: 'Blueprint', link: '/00-vision-positioning-scope' },
        { text: 'UI / UX', link: '/02-ui-ux-main-window' },
        { text: 'Compatibility', link: '/05-logic-pro-compatibility' },
        {
          text: 'v0.9',
          items: [{ text: 'GitHub Repository', link: 'https://github.com/mikeo-ne/seven7' }],
        },
      ],

      sidebar: [
        {
          text: 'seven7 Blueprint',
          items: [
            { text: 'Overview', link: '/' },
            { text: '00 · Vision, Positioning & Scope', link: '/00-vision-positioning-scope' },
            { text: '01 · System Architecture', link: '/01-system-architecture' },
            { text: '02 · UI/UX & Main Window', link: '/02-ui-ux-main-window' },
            { text: '03 · Mix Engine & Signal Flow', link: '/03-mix-engine-signal-flow' },
            { text: '04 · Editing, Sequencing & Automation', link: '/04-editing-sequencing-automation' },
            { text: '05 · Logic Pro Compatibility', link: '/05-logic-pro-compatibility' },
            { text: '06 · Glossary', link: '/06-glossary' },
          ],
        },
      ],

      outline: { level: 'deep', label: 'On this page' },

      search: { provider: 'local' },

      editLink: {
        pattern: 'https://github.com/mikeo-ne/seven7/edit/arena/01a0a5f6-seven7/docs/:path',
        text: 'Edit this page on GitHub',
      },

      socialLinks: [{ icon: 'github', link: 'https://github.com/mikeo-ne/seven7' }],

      footer: {
        message: 'seven7 — two rooms, one house. Blueprint v0.9, draft for review.',
        copyright: 'Specifications are product design documents; Logic Pro and Pro Tools are trademarks of their respective owners.',
      },
    },
  }),
)
