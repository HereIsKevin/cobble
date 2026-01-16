import { spawnSync } from "node:child_process";
import * as fs from "node:fs/promises";
import * as path from "node:path";
import process from "node:process";
import { parseArgs } from "node:util";

let {
  values: { "no-test": noTest, platform, arch },
} = parseArgs({
  options: {
    "no-test": { type: "boolean" },
    platform: { type: "string" },
    arch: { type: "string" },
  },
});

const baseDir = path.dirname(import.meta.dirname);
const addonSrc = path.join(baseDir, "build/cobble.node");
const addonDest = path.join(
  baseDir,
  `cobble-${platform ?? process.platform}-${arch ?? process.arch}.node`,
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

let configureCommand = "cmake --preset deps";

if (noTest) {
  configureCommand += " -D TEST_DEPS=FALSE";
}

if (platform === undefined && arch !== undefined) {
  platform = process.platform;
}

if (platform !== undefined) {
  let cmakePlatform: string;
  switch (platform) {
    case "win32":
      cmakePlatform = "Windows";
      break;
    case "darwin":
      cmakePlatform = "Darwin";
      break;
    case "linux":
      cmakePlatform = "Linux";
      break;
    default:
      throw new Error(
        "Cross-compiling is only supported for win32, darwin, and linux",
      );
  }

  configureCommand += ` -D "CMAKE_SYSTEM_NAME=${cmakePlatform}"`;
}

if (arch !== undefined) {
  let cmakeArch: string;
  if (arch === "x64") {
    switch (platform) {
      case "win32":
        cmakeArch = "AMD64";
        break;
      case "darwin":
        cmakeArch = "x86_64";
        break;
      case "linux":
        cmakeArch = "x86_64";
        break;
      default:
        throw new Error(
          "Cross-compiling is only supported for win32, darwin, and linux",
        );
    }
  } else if (arch === "arm64") {
    switch (platform) {
      case "win32":
        cmakeArch = "ARM64";
        break;
      case "darwin":
        cmakeArch = "arm64";
        break;
      case "linux":
        cmakeArch = "aarch64";
        break;
      default:
        throw new Error(
          "Cross-compiling is only supported for win32, darwin, and linux",
        );
    }
  } else {
    throw new Error("Cross-compiling is only supported for x64 and arm64");
  }

  configureCommand += ` -D "CMAKE_SYSTEM_PROCESSOR=${cmakeArch}"`;
}

spawn(configureCommand);
spawn("cmake --build --preset deps");

spawn("cmake --workflow --preset release");
spawn("tsc --project ./tsconfig.build.json");
await fs.copyFile(addonSrc, addonDest);
