import assert from "node:assert/strict";
import { existsSync, readFileSync } from "node:fs";
import test from "node:test";

test("theme catalog references checked-in rtheme files", () => {
  const catalog = JSON.parse(readFileSync("themes/index.json", "utf8"));
  assert.ok(catalog.length >= 10);

  for (const theme of catalog) {
    assert.match(theme.id, /^[a-z0-9-]+$/);
    assert.match(theme.file, /^[a-z0-9-]+\.rtheme$/);
    assert.ok(existsSync(`themes/${theme.file}`), theme.file);
  }
});
