import { spawnSync } from "node:child_process";
import * as fs from "node:fs/promises";
import * as path from "node:path";

const baseDir = path.dirname(import.meta.dirname);
const addonSrc = path.join(baseDir, "build/cobble.node");
const addonDest = path.join(
  baseDir,
  `cobble-${process.platform}-${process.arch}.node`,
);

const spawn = (command: string, args: string[]): void => {
  // Executing Node module shims works on *nix systems without shell, but needs
  // the shell option enabled to work on Windows.
  const child = spawnSync(command, args, {
    cwd: baseDir,
    stdio: "inherit",
    shell: process.platform === "win32",
  });

  if (child.error !== undefined) {
    throw child.error;
  }

  if (child.status !== 0) {
    process.exit(child.status);
  }
};

spawn("cmake", ["--workflow", "--preset", "deps"]);
spawn("cmake", ["--workflow", "--preset", "release"]);
spawn("tsc", ["--project", "./tsconfig.build.json"]);
await fs.copyFile(addonSrc, addonDest);
