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

/**
 * Suggested asset name for addon when embedded in a single executable
 * application.
 */
export const addonAsset = "cobble.node";

/**
 * Path to addon binary that is currently loaded. Useful for building a single
 * executable application for the current platform and architecture.
 */
export let addonPath: string;

/**
 * Find addon binary for other platforms and architectures. Useful for building
 * single executable applications for other platforms and architectures.
 */
export const resolveAddonPath = (platform: string, arch: string): string => {
  if (isSea()) {
    throw new Error("Cannot resolve addon path from within SEA");
  }

  let development: boolean;
  try {
    fs.accessSync(path.join(import.meta.dirname, "build/cobble.node"));
    development = true;
  } catch {
    development = false;
  }

  if (development) {
    throw new Error("Cannot resolve addon path during development");
  }

  const addonPath = path.join(
    import.meta.dirname,
    `cobble-${platform}-${arch}.node`,
  );

  try {
    fs.accessSync(addonPath);
    return addonPath;
  } catch {
    throw new Error(`Cannot find addon for ${platform} ${arch}`);
  }
};

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
  const tempAddonPath = path.join(tempDir, `${digest}.node`);

  if (!fs.existsSync(tempAddonPath)) {
    fs.mkdirSync(tempDir, { recursive: true });
    fs.writeFileSync(tempAddonPath, content);
  }

  addon = require(tempAddonPath);
  addonPath = tempAddonPath;
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

// Piece Interface and Validation

/**
 * Piece of image to be used as source or destination when cobbling. Pixel
 * coordinates increase from top to bottom and left to right with the origin at
 * the top left corner of the image.
 */
export interface Piece {
  /** X-coordinate in pixels of top left corner of piece. */
  left: number;
  /** Y-coordinate in pixels of top left corner of piece. */
  top: number;
  /** Width of piece in pixels from top left corner. */
  width: number;
  /** Height of piece in pixels from top left corner. */
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

/**
 * Cobbler decodes, cobbles, and encodes images from data supplied. Extensive
 * numeric checks ensure no invalid operations can be performed for safety.
 * Image sizes are limited to 16,383 by 16,383 pixels for all image formats.
 * ICC profiles are fully supported. Transparency and animated images are not
 * supported.
 */
export class Cobbler {
  #image: Image;
  #buffer: Uint8Array;

  private constructor(image: Image) {
    this.#image = image;
    this.#buffer = new Uint8Array(image.width * image.height * 3);
  }

  /**
   * Asynchronously decode JPEG or WebP image and create a new instance of
   * {@link Cobbler}. A completely black buffer with the same size as the
   * decoded image.
   *
   * @param imageData - Encoded JPEG or WebP image
   * @returns New instance of {@link Cobbler}
   */
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

  /** Width of the decoded image and allocated buffer in pixels. */
  get width(): number {
    return this.#image.width;
  }

  /** Height of the decoded image and allocated buffer in pixels. */
  get height(): number {
    return this.#image.height;
  }

  /**
   * Copy a piece from the decoded image to the allocated buffer. Both pieces
   * must have the same width and height.
   *
   * @param from - Piece to copy from in decoded image
   * @param to - Piece to copy to in allocated buffer
   */
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

  /**
   * Asynchronously encode buffer to high-quality PNG. Since this operation is
   * very slow, usually taking 1 second for typical images, it is done on the
   * libuv threadpool. By default, the threadpool only has 4 threads, so to
   * increase performance, it is recommended to set the environment variable
   * `UV_THREADPOOL_SIZE` to the number of processor cores.
   *
   * @returns Encoded PNG image
   */
  encode(): Promise<Uint8Array> {
    return addon.encodePng({
      width: this.#image.width,
      height: this.#image.height,
      buffer: this.#buffer,
      iccProfile: this.#image.iccProfile,
    });
  }
}
