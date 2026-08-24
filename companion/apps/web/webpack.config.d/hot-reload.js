if (config.devServer) {
  config.devServer.static ??= [{
    directory: require("path").resolve(__dirname, "kotlin"),
    watch: true,
  }];
  config.devServer.hot = true;
  config.devServer.liveReload = true;
  config.devServer.static.push({
    directory: require("path").resolve(__dirname, "../../../../build/firmware"),
    publicPath: "/firmware",
    watch: true,
  });
}
