const fs = require("fs");
const path = require("path");

const packageDir = path.dirname(require.resolve("pdf-core-wasm-js/package.json"));
const pdfiumDir = path.join(packageDir, "pdfium");
const pdfiumUrl = pdfiumDir.replaceAll("\\", "/");
const pdfiumAssets = fs.readdirSync(pdfiumDir, { withFileTypes: true })
  .filter(entry => entry.isFile())
  .map(entry => entry.name);

config.set({
  files: [...config.files, ...pdfiumAssets.map(name => ({
    pattern: `${pdfiumUrl}/${name}`,
    included: false,
    served: true,
    watched: false,
  }))],
  proxies: {
    ...config.proxies,
    "/pdfium/": `/absolute${pdfiumUrl}/`,
  },
});
