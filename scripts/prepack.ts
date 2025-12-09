import { spawnSync } from "node:child_process";
import * as fs from "node:fs/promises";
import * as path from "node:path";

const baseDir = path.dirname(import.meta.dirname);
const addonSrc = path.join(baseDir, "build/cobble.node");
const addonDest = path.join(
  baseDir,
  `cobble-${process.platform}-${process.arch}.node`,
);

const spawnOptions = {
  cwd: baseDir,
  stdio: "inherit",
} as const;

spawnSync("cmake", ["--workflow", "--preset", "deps"], spawnOptions);
spawnSync("cmake", ["--workflow", "--preset", "release"], spawnOptions);
spawnSync("tsc", ["--project", "./tsconfig.build.json"], spawnOptions);
await fs.copyFile(addonSrc, addonDest);
