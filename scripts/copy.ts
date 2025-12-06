import * as fs from "node:fs/promises";
import * as path from "node:path";

const baseDir = path.dirname(import.meta.dirname);
const addonSrc = path.join(baseDir, "build/cobble.node");
const addonDest = path.join(
  baseDir,
  `cobble-${process.platform}-${process.arch}.node`,
);

await fs.copyFile(addonSrc, addonDest);
