config.ignoreWarnings ??= [];
config.ignoreWarnings.push(warning =>
  warning.module?.resource?.endsWith("rsvpnano-web.import-object.mjs") &&
  warning.message.includes("the request of a dependency is an expression")
);

if (config.devServer) {
  config.devServer.static ??= [{
    directory: require("path").resolve(__dirname, "kotlin"),
    watch: true,
  }];
  config.devServer.hot = true;
  config.devServer.liveReload = true;
  config.devServer.static?.forEach(entry => {
    entry.watch = true;
  });
}
