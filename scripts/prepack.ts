import { spawnSync } from "node:child_process";
import * as fs from "node:fs/promises";
import * as path from "node:path";

const baseDir = path.dirname(import.meta.dirname);
const addonSrc = path.join(baseDir, "build/cobble.node");
const addonDest = path.join(
  baseDir,
  `cobble-${process.platform}-${process.arch}.node`,
);

const spawn = (command: string): void => {
  const child = spawnSync(command, {
    cwd: baseDir,
    stdio: "inherit",
    shell: true,
  });

  if (child.error !== undefined) {
    throw child.error;
  }

  if (child.status !== 0) {
    process.exit(child.status);
  }
};

spawn("cmake --workflow --preset deps");
spawn("cmake --workflow --preset release");
spawn("tsc --project ./tsconfig.build.json");
await fs.copyFile(addonSrc, addonDest);
