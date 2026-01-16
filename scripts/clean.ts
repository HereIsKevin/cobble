import * as fs from "node:fs/promises";
import * as path from "node:path";

const baseDir = path.dirname(import.meta.dirname);
const buildDir = path.join(baseDir, "build");
const distFiles = [
  "index.js",
  "index.js.map",
  "index.d.ts",
  "index.d.ts.map",
  ...(await fs.readdir(baseDir)).filter(
    (entry) => entry.startsWith("cobble-") && entry.endsWith(".node"),
  ),
].map((file) => path.join(baseDir, file));

await Promise.all([
  fs.rm(buildDir, { force: true, recursive: true }),
  Promise.all(distFiles.map((file) => fs.rm(file, { force: true }))),
]);
