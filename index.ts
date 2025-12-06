import { createHash } from "node:crypto";
import * as fs from "node:fs";
import { createRequire } from "node:module";
import * as os from "node:os";
import * as path from "node:path";
import process from "node:process";
import { getRawAsset, isSea } from "node:sea";

// Native Addon Loader and Interfaces

interface Image {
  width: number;
  height: number;
  buffer: Uint8Array;
  iccProfile: Uint8Array;
}

interface Addon {
  decodeJpeg(buffer: Uint8Array): Promise<Image>;
  decodeWebP(buffer: Uint8Array): Promise<Image>;
  encodePng(image: Image): Promise<Uint8Array>;
}

// These are just here to make creating single executable applications easier.
export const addonAsset = "cobble.node";
export let addonPath: string;

// import.meta.filename can only be used from ESM. As a result, when bundling
// for single executable applications, which must be CommonJS, the user must
// configure their bundler to replace it with __filename.
const require = createRequire(import.meta.filename);

let addon: Addon;
// For single executable applications, attempt to write addon to disk and load
// it. It would be nice if it could be loaded from memory, but dlopen needs a
// filesystem to work.
if (isSea()) {
  const content = new Uint8Array(getRawAsset(addonAsset));
  const digest = createHash("sha256").update(content).digest("hex");

  const tempDir = path.join(os.tmpdir(), "seal");
  const addonPath = path.join(tempDir, `${digest}.node`);

  if (!fs.existsSync(addonPath)) {
    fs.mkdirSync(tempDir, { recursive: true });
    fs.writeFileSync(addonPath, content);
  }

  addon = require(addonPath);
}
// Normal loading is just simple require calls. Try to load the current
// development build, then fall back to the production build.
else {
  let rawAddonPath: string;

  try {
    rawAddonPath = "./build/cobble.node";
    addon = require(rawAddonPath);
  } catch {
    rawAddonPath = `./cobble-${process.platform}-${process.arch}.node`;
    addon = require(rawAddonPath);
  }

  addonPath = path.join(import.meta.dirname, rawAddonPath);
}

// Piece Interface and Validaiton

export interface Piece {
  left: number;
  top: number;
  width: number;
  height: number;
}

const checkPiece = (piece: Piece, image: Image): void => {
  if (
    !Number.isInteger(piece.left) ||
    !Number.isInteger(piece.top) ||
    !Number.isInteger(piece.width) ||
    !Number.isInteger(piece.height)
  ) {
    throw new Error("Piece offsets and dimensions must be integers");
  }

  if (piece.left < 0 || piece.top < 0) {
    throw new Error("Piece offsets must not be negative");
  }

  if (piece.width <= 0 || piece.height <= 0) {
    throw new Error("Piece dimensions must be positive");
  }

  if (
    piece.left + piece.width > image.width ||
    piece.top + piece.height > image.height
  ) {
    throw new Error("Piece must not extend outside image");
  }
};

// Image Cobbler

export class Cobbler {
  #image: Image;
  #buffer: Uint8Array;

  private constructor(image: Image) {
    this.#image = image;
    this.#buffer = new Uint8Array(image.width * image.height * 3);
  }

  static async decode(imageData: Uint8Array): Promise<Cobbler> {
    // ff d8 ff are the magic bytes for JPEG images.
    if (imageData[0] === 0xff && imageData[1] == 0xd8 && imageData[2] == 0xff) {
      return new Cobbler(await addon.decodeJpeg(imageData));
    }
    // WebP images start with RIFF in ASCII as a marker for the container, then
    // the size of the contents, and then WEBP in ASCII to signify it is WebP.
    else if (
      imageData[0] === 0x52 &&
      imageData[1] === 0x49 &&
      imageData[2] === 0x46 &&
      imageData[3] === 0x46 &&
      imageData[8] === 0x57 &&
      imageData[9] === 0x45 &&
      imageData[10] === 0x42 &&
      imageData[11] === 0x50
    ) {
      return new Cobbler(await addon.decodeWebP(imageData));
    } else {
      throw new Error("Only JPEG and WebP decoding are supported");
    }
  }

  get width(): number {
    return this.#image.width;
  }

  get height(): number {
    return this.#image.height;
  }

  cobble(from: Piece, to: Piece): void {
    checkPiece(from, this.#image);
    checkPiece(to, this.#image);

    if (from.width != to.width || from.height != to.height) {
      throw new Error("Pieces must have same dimensions");
    }

    for (let row = 0; row < from.height; row++) {
      const fromRow = from.top + row;
      const fromStart = fromRow * this.#image.width + from.left;
      const fromEnd = fromStart + from.width;

      const toRow = to.top + row;
      const toStart = toRow * this.#image.width + to.left;

      this.#buffer.set(
        this.#image.buffer.subarray(fromStart * 3, fromEnd * 3),
        toStart * 3,
      );
    }
  }

  encode(): Promise<Uint8Array> {
    return addon.encodePng({
      width: this.#image.width,
      height: this.#image.height,
      buffer: this.#buffer,
      iccProfile: this.#image.iccProfile,
    });
  }
}
