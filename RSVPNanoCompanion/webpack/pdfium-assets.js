const fs = require("fs");
const path = require("path");

module.exports = config => {
  class PdfiumAssetsPlugin {
    apply(compiler) {
      compiler.hooks.thisCompilation.tap("PdfiumAssetsPlugin", compilation => {
        compilation.hooks.processAssets.tap(
          {
            name: "PdfiumAssetsPlugin",
            stage: compiler.webpack.Compilation.PROCESS_ASSETS_STAGE_ADDITIONAL,
          },
          () => {
            const packageDir = path.dirname(require.resolve("pdf-core-wasm-js/package.json", { paths: [compiler.context] }));
            const resources = path.join(packageDir, "pdfium");
            const assets = fs.readdirSync(resources, { withFileTypes: true })
              .filter(entry => entry.isFile())
              .map(entry => entry.name);
            for (const name of assets) {
              compilation.emitAsset(
                `pdfium/${name}`,
                new compiler.webpack.sources.RawSource(
                  fs.readFileSync(path.join(resources, name)),
                ),
              );
            }
          },
        );
      });
    }
  }

  config.plugins.push(new PdfiumAssetsPlugin());
};
