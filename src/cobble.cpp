#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <napi.h>
#include <png.h>
#include <turbojpeg.h>
#include <webp/decode.h>
#include <webp/demux.h>

// Make sure std::size_t is at least a 64 bits. Otherwise, PNG size calculations
// can overflow on very large images despite artificial limit of 2^16-1 pixels
// for both width and height.
static_assert(sizeof(std::size_t) >= 8, "std::size_t must be at least 64 bits");

// JPEG handle wrapper with automatic destruction.
class JpegHandle {
  TJINIT type;
  tjhandle handle;

public:
  JpegHandle(TJINIT type) : type(type), handle(tj3Init(type)) {}

  ~JpegHandle() {
    if (handle != nullptr) {
      tj3Destroy(handle);
    }
  }

  tjhandle get() {
    return handle;
  }

  operator tjhandle() {
    return handle;
  }

  const char* getError() {
    char* message = tj3GetErrorStr(handle);
    if (message == nullptr) {
      switch (type) {
        case TJINIT_COMPRESS:
          return "JPEG encoding failed";
        case TJINIT_DECOMPRESS:
          return "JPEG decoding failed";
        case TJINIT_TRANSFORM:
          return "JPEG transform failed";
      }
    }

    return message;
  }
};

// JPEG data buffer.
struct JpegData {
  std::uint8_t* buffer = nullptr;
  std::size_t size = 0;

  ~JpegData() {
    if (buffer != nullptr) {
      tj3Free(buffer);
    }
  }
};

// WebP demuxer wrapper with automatic destruction.
class WebPDemuxerWrapper {
  WebPData data;
  WebPDemuxer* demux;

public:
  WebPDemuxerWrapper(std::uint8_t* buffer, std::size_t size) :
    data({.bytes = buffer, .size = size}),
    demux(WebPDemux(&data)) {}

  ~WebPDemuxerWrapper() {
    if (demux != nullptr) {
      WebPDemuxDelete(demux);
    }
  }

  WebPDemuxer* get() {
    return demux;
  }

  operator WebPDemuxer*() {
    return demux;
  }
};

// WebP demux chunk iterator with automatic release.
struct WebPChunkIteratorWrapper : public WebPChunkIterator {
  ~WebPChunkIteratorWrapper() {
    WebPDemuxReleaseChunkIterator(this);
  }
};

// WebP demux iterator with automatic release.
struct WebPIteratorWrapper : public WebPIterator {
  ~WebPIteratorWrapper() {
    WebPDemuxReleaseIterator(this);
  }
};

// Maximum dimensions are is restricted to 2^16-1 for both width and height.
// This is because encodePng should only ever be used for encoding decoded JPEG
// and WebP images and 2^16-1 is the maximum for each dimension of a JPEG. WebP
// images have an even lower maximum of 2^14-1. Note that the maximums for PNG
// are actually much higher at 2^31-1.
constexpr std::uint32_t PNG_MAX_WIDTH = (2 << 15) - 1;
constexpr std::uint32_t PNG_MAX_HEIGHT = (2 << 15) - 1;

// PNG error handler that throws JavaScript exceptions.
void pngErrorHandler(png_struct* png, const char* message) {
  napi_env env = static_cast<napi_env>(png_get_error_ptr(png));
  throw Napi::Error::New(env, message == nullptr ? "PNG encoding failed" : message);
}

// PNG writer wrapper that also manages other related stuff.
class PngWriter {
  png_struct* png;
  png_info* info;

public:
  PngWriter(napi_env env) {
    png = png_create_write_struct(
      PNG_LIBPNG_VER_STRING,
      env,
      pngErrorHandler,
      pngErrorHandler
    );
    if (png == nullptr) {
      throw Napi::Error::New(env, "PNG encoder initialization failed");
    }

    info = png_create_info_struct(png);
    if (info == nullptr) {
      png_destroy_write_struct(&png, nullptr);
      throw Napi::Error::New(env, "PNG encoder initialization failed");
    }
  }

  ~PngWriter() {
    png_destroy_write_struct(&png, &info);
  }

  png_struct* get() {
    return png;
  }

  operator png_struct*() {
    return png;
  }

  png_info* getInfo() {
    return info;
  }
};

// PNG data buffer.
struct PngData {
  std::uint8_t* buffer = nullptr;
  std::size_t size = 0;
  std::size_t capacity = 0;

  PngData(int capacity) : capacity(capacity) {
    buffer = new std::uint8_t[capacity];
  }

  ~PngData() {
    delete buffer;
  }
};

// PNG zlib stream worst case compression estimation.
constexpr std::size_t pngZlibStreamSize(std::size_t size) {
  // PNG zlib streams have a worst case compression of 2 byte compression
  // headers and flags + uncompressed data size + 5 bytes for each 32 kilobyte
  // chunk of uncompressed data + 4 byte checksum. However, I don't really trust
  // this estimation, so it is not actually used here.
  // std::size_t kb = 2 << 9;
  // std::size_t headerAndFlags = 2;
  // std::size_t chunks = (size + (32 * kb - 1)) / (32 * kb);
  // std::size_t checksum = 4;
  // return headerAndFlags + size + (5 * chunks) + checksum;

  // This is based on deflateBound from zlib.
  return size + ((size + 7) >> 3) + ((size + 63) >> 6) + 5 + 6;
}

// PNG chunks need have 4 byte length + 4 byte chunk type + data size + 4 byte
// CRC at the end.
constexpr std::size_t pngChunkSize(std::size_t size) {
  std::size_t length = 4;
  std::size_t chunkType = 4;
  std::size_t crc = 4;
  return length + chunkType + size + crc;
}

// PNG ICC profile chunks have a worst case compression of 93 byte header + zlib
// stream size.
constexpr std::size_t pngIccProfileChunkSize(std::size_t size) {
  return pngChunkSize(81 + pngZlibStreamSize(size));
}

// PNG images have a worst case compression of 8 byte signature + 25 byte image
// header chunk + (12 + zlib stream size) bytes for image data chunk + 12 bytes
// for image end chunk.
constexpr std::size_t pngBufferSize(std::size_t size) {
  std::size_t signature = 8;
  std::size_t imageHeader = pngChunkSize(13);
  std::size_t imageData = pngChunkSize(pngZlibStreamSize(size));
  std::size_t imageEnd = pngChunkSize(0);
  return signature + imageHeader + imageData + imageEnd;
}

class Cobble : public Napi::Addon<Cobble> {
public:
  Cobble(Napi::Env _, Napi::Object exports) {
    DefineAddon(exports, {
      InstanceMethod("decodeJpeg", &Cobble::decodeJpeg),
      InstanceMethod("decodeWebP", &Cobble::decodeWebP),
      InstanceMethod("encodePng", &Cobble::encodePng),
    });
  }

  Napi::Value decodeJpeg(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    // Validate JPEG input buffer.
    if (info.Length() != 1) {
      throw Napi::TypeError::New(env, "Expected exactly 1 argument");
    }
    if (!info[0].IsTypedArray()) {
      throw Napi::TypeError::New(env, "Expected TypedArray as argument");
    }
    Napi::TypedArray typedArray = info[0].As<Napi::TypedArray>();
    if (typedArray.TypedArrayType() != napi_uint8_array) {
      throw Napi::TypeError::New(env, "Expected Uint8Array as argument");
    }
    Napi::Uint8Array input = typedArray.As<Napi::Uint8Array>();

    // Create TurboJPEG instance.
    JpegHandle handle(TJINIT_DECOMPRESS);
    if (handle.get() == nullptr) {
      throw Napi::Error::New(env, handle.getError());
    }

    // Treat decoding warning as fatal error.
    if (tj3Set(handle, TJPARAM_STOPONWARNING, true) < 0) {
      throw Napi::Error::New(env, handle.getError());
    }

    // Do not decode additional metadata except for the ICC profile.
    if (tj3Set(handle, TJPARAM_SAVEMARKERS, 4) < 0) {
      throw Napi::Error::New(env, handle.getError());
    }

    // Decode JPEG header for metadata.
    if (tj3DecompressHeader(handle, input.Data(), input.ElementLength()) < 0) {
      throw Napi::Error::New(env, handle.getError());
    }

    // Retrieve JPEG width.
    int width = tj3Get(handle, TJPARAM_JPEGWIDTH);
    if (width < 0) {
      throw Napi::Error::New(env, "JPEG width is unknown");
    }

    // Retrieve JPEG height.
    int height = tj3Get(handle, TJPARAM_JPEGHEIGHT);
    if (height < 0) {
      throw Napi::Error::New(env, "JPEG height is unknown");
    }

    // Make sure JPEG has 8-bit data precision.
    int precision = tj3Get(handle, TJPARAM_PRECISION);
    if (precision < 0) {
      throw Napi::Error::New(env, "JPEG data precision is unknown");
    }
    if (precision != 8) {
      throw Napi::Error::New(env, "Only 8 bit data precision is supported");
    }

    // Make sure JPEG has RGB or YCbCr color space.
    int rawColorSpace = tj3Get(handle, TJPARAM_COLORSPACE);
    if (rawColorSpace < 0) {
      throw Napi::Error::New(env, "JPEG color space is unknown");
    }
    TJCS colorSpace = static_cast<TJCS>(rawColorSpace);
    if (colorSpace != TJCS_RGB && colorSpace != TJCS_YCbCr) {
      throw Napi::Error::New(env, "Only RGB and YCbCr color spaces are supported");
    }

    // Make sure JPEG is lossy.
    int rawLossless = tj3Get(handle, TJPARAM_LOSSLESS);
    if (rawLossless < 0) {
      throw Napi::Error::New(env, "JPEG compression algorithm is unknown");
    }
    bool lossless = rawLossless;
    if (lossless) {
      throw Napi::Error::New(env, "Only lossy compression is supported");
    }

    // Warnings are ignored when retrieving the ICC profile since no profile
    // emits a warning, and we want to support images with no ICC profile.
    JpegData rawIccProfile;
    if (
      tj3GetICCProfile(handle, &rawIccProfile.buffer, &rawIccProfile.size) < 0 &&
      tj3GetErrorCode(handle) != TJERR_WARNING
    ) {
      throw Napi::Error::New(env, handle.getError());
    }

    // Copy ICC profile to buffer if available.
    Napi::Uint8Array iccProfile = Napi::Uint8Array();
    if (rawIccProfile.size > 0) {
      iccProfile = Napi::Uint8Array::New(env, rawIccProfile.size);
      std::memcpy(iccProfile.Data(), rawIccProfile.buffer, rawIccProfile.size);
    }

    // Allocate output buffer for 8 bit data precision RGB JPEG. Cast is needed
    // because overflow could occur otherwise.
    Napi::Uint8Array output = Napi::Uint8Array::New(
      env,
      static_cast<std::size_t>(width) * height * tjPixelSize[TJPF_RGB]
    );

    // Decode JPEG image to buffer.
    if (
      tj3Decompress8(
        handle,
        input.Data(),
        input.ElementLength(),
        output.Data(),
        width * tjPixelSize[TJPF_RGB],
        TJPF_RGB
      ) < 0
    ) {
      throw Napi::Error::New(env, handle.getError());
    }

    // Create result object.
    Napi::Object result = Napi::Object::New(env);
    result.Set("buffer", output);
    result.Set("width", width);
    result.Set("height", height);
    result.Set("iccProfile", iccProfile.IsEmpty() ? env.Undefined() : iccProfile);

    return result;
  }

  Napi::Value decodeWebP(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    // Validate WebP input buffer.
    if (info.Length() != 1) {
      throw Napi::TypeError::New(env, "Expected exactly 1 argument");
    }
    if (!info[0].IsTypedArray()) {
      throw Napi::TypeError::New(env, "Expected TypedArray as argument");
    }
    Napi::TypedArray typedArray = info[0].As<Napi::TypedArray>();
    if (typedArray.TypedArrayType() != napi_uint8_array) {
      throw Napi::TypeError::New(env, "Expected Uint8Array as argument");
    }
    Napi::Uint8Array input = typedArray.As<Napi::Uint8Array>();

    // Create WebP demuxer.
    WebPDemuxerWrapper demux(input.Data(), input.ElementLength());
    if (demux.get() == nullptr) {
      throw Napi::Error::New(env, "WebP decoding failed");
    }

    // Retrieve WebP canvas dimensions.
    std::uint32_t width = WebPDemuxGetI(demux, WEBP_FF_CANVAS_WIDTH);
    std::uint32_t height = WebPDemuxGetI(demux, WEBP_FF_CANVAS_HEIGHT);

    // Make sure WebP is not animated or transparent.
    std::uint32_t flags = WebPDemuxGetI(demux, WEBP_FF_FORMAT_FLAGS);
    if (flags & ANIMATION_FLAG) {
      throw Napi::Error::New(env, "Only still images are supported");
    }
    if (flags & ALPHA_FLAG) {
      throw Napi::Error::New(env, "Only opaque images are supported");
    }

    // Make sure WebP only has 1 frame.
    std::uint32_t frames = WebPDemuxGetI(demux, WEBP_FF_FRAME_COUNT);
    if (frames != 1) {
      throw Napi::Error::New(
        env,
        "Impossible, still image has no frames or more than 1 frame"
      );
    }

    // Retrieve ICC profile if available.
    Napi::Uint8Array iccProfile = Napi::Uint8Array();
    if (flags & ICCP_FLAG) {
      // Retrieve ICC profile chunk, make sure that is is the first ICC profile
      // chunk, and make sure that there are not multiple ICC profile chunks.
      WebPChunkIteratorWrapper chunkIter;
      if (!WebPDemuxGetChunk(demux, "ICCP", 1, &chunkIter)) {
        throw Napi::Error::New(env, "ICC profile chunk disappeared");
      }
      if (chunkIter.chunk_num != 1) {
        throw Napi::Error::New(env, "Impossible, ICC profile chunk number changed");
      }
      if (chunkIter.num_chunks != 1) {
        throw Napi::Error::New(env, "Multiple ICC profiles");
      }

      // Copy ICC profile to buffer.
      iccProfile = Napi::Uint8Array::New(env, chunkIter.chunk.size);
      std::memcpy(iccProfile.Data(), chunkIter.chunk.bytes, chunkIter.chunk.size);
    }

    // Retrieve WebP frame, make sure that it is the first frame, make sure
    // there are not multiple frames, make sure the frame is complete, make sure
    // the frame is opaque, and sure the frame covers the entire canvas.
    WebPIteratorWrapper iter;
    if (!WebPDemuxGetFrame(demux, 1, &iter)) {
      throw Napi::Error::New(env, "Impossible, frame disappeared");
    }
    if (iter.frame_num != 1) {
      throw Napi::Error::New(env, "Impossible, frame number changed");
    }
    if (iter.num_frames != 1) {
      throw Napi::Error::New(env, "Impossible, frame count changed");
    }
    if (!iter.complete) {
      throw Napi::Error::New(env, "Impossible, frame is not complete");
    }
    if (iter.has_alpha) {
      throw Napi::Error::New(env, "Only opaque frames are supported");
    }
    if (iter.x_offset != 0 || iter.y_offset != 0) {
      throw Napi::Error::New(env, "Only frames with no offset are supported");
    }
    if (
      static_cast<std::uint32_t>(iter.width) != width ||
      static_cast<std::uint32_t>(iter.height) != height
    ) {
      throw Napi::Error::New(env, "Only frames filling canvas are supported");
    }

    // Allocate output buffer for 8 bit data precision RGB WebP. In this case,
    // there should be no overflow ever, but cast is done to be very safe.
    Napi::Uint8Array output =
      Napi::Uint8Array::New(env, static_cast<std::size_t>(width) * height * 3);

    // Decode WebP frame to buffer.
    if (
      WebPDecodeRGBInto(
        iter.fragment.bytes,
        iter.fragment.size,
        output.Data(),
        output.ElementLength(),
        width * 3
      ) == nullptr
    ) {
      throw Napi::Error::New(env, "WebP decoding failed");
    }

    // Create result object.
    Napi::Object result = Napi::Object::New(env);
    result.Set("buffer", output);
    result.Set("width", width);
    result.Set("height", height);
    result.Set("iccProfile", iccProfile.IsEmpty() ? env.Undefined() : iccProfile);

    return result;
  }

  Napi::Value encodePng(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    // Validate image object.
    if (info.Length() != 1) {
      throw Napi::TypeError::New(env, "Expected exactly 1 argument");
    }
    if (!info[0].IsObject()) {
      throw Napi::TypeError::New(env, "Expected image object as argument");
    }
    Napi::Object image = info[0].As<Napi::Object>();

    // Validate image buffer.
    Napi::Value bufferValue = image.Get("buffer");
    if (!bufferValue.IsTypedArray()) {
      throw Napi::TypeError::New(env, "Expected buffer to be TypedArray");
    }
    Napi::TypedArray bufferTypedArray = bufferValue.As<Napi::TypedArray>();
    if (bufferTypedArray.TypedArrayType() != napi_uint8_array) {
      throw Napi::TypeError::New(env, "Expected buffer to be Uint8Array");
    }
    Napi::Uint8Array buffer = bufferTypedArray.As<Napi::Uint8Array>();

    // Validate image width.
    Napi::Value widthValue = image.Get("width");
    if (!widthValue.IsNumber()) {
      throw Napi::TypeError::New(env, "Expected width to be number");
    }
    double widthDouble = widthValue.As<Napi::Number>();
    if (!std::isfinite(widthDouble)) {
      throw Napi::TypeError::New(env, "Expected width to finite");
    }
    if (std::modf(widthDouble, &widthDouble) != 0.0) {
      throw Napi::TypeError::New(env, "Expected width to be integer");
    }
    if (widthDouble <= 0 || widthDouble > PNG_MAX_WIDTH) {
      throw Napi::TypeError::New(env, "Expected width in range (0, 2^16-1]");
    }
    std::uint32_t width = static_cast<std::uint32_t>(widthDouble);

    // Validate image height.
    Napi::Value heightValue = image.Get("height");
    if (!heightValue.IsNumber()) {
      throw Napi::TypeError::New(env, "Expected height to be number");
    }
    double heightDouble = heightValue.As<Napi::Number>();
    if (!std::isfinite(heightDouble)) {
      throw Napi::TypeError::New(env, "Expected height to finite");
    }
    if (std::modf(heightDouble, &heightDouble) != 0.0) {
      throw Napi::TypeError::New(env, "Expected height to be integer");
    }
    if (heightDouble <= 0 || heightDouble > PNG_MAX_HEIGHT) {
      throw Napi::TypeError::New(env, "Expected height in range (0, 2^16-1]");
    }
    std::uint32_t height = static_cast<std::uint32_t>(heightDouble);

    // Validate ICC profile buffer.
    Napi::Value iccProfileValue = image.Get("iccProfile");
    Napi::Uint8Array iccProfile = Napi::Uint8Array();
    if (!iccProfileValue.IsUndefined()) {
      if (!iccProfileValue.IsTypedArray()) {
        throw Napi::TypeError::New(env, "Expected ICC profile to be TypedArray");
      }
      Napi::TypedArray iccProfileTypedArray = iccProfileValue.As<Napi::TypedArray>();
      if (iccProfileTypedArray.TypedArrayType() != napi_uint8_array) {
        throw Napi::TypeError::New(env, "Expected ICC profile to be Uint8Array");
      }
      iccProfile = iccProfileTypedArray.As<Napi::Uint8Array>();
    }

    // Make sure image buffer size matches width and height. Cast is done to
    // make sure there is no integer overflow.
    std::size_t bufferSize = static_cast<std::size_t>(width) * height * 3;
    if (bufferSize != buffer.ElementLength()) {
      throw Napi::TypeError::New(
        env,
        "Expected buffer to have width * height * 3 bytes"
      );
    }

    // Create PNG writer.
    PngWriter png(env);

    // Calculate data buffer capacity.
    std::size_t dataCapacity = pngBufferSize(buffer.ElementLength());
    if (!iccProfile.IsEmpty()) {
      dataCapacity += pngIccProfileChunkSize(iccProfile.ElementLength());
    }

    // Create output data buffer.
    PngData data(dataCapacity);

    // Enable all filters and max compression level for smaller results.
    png_set_filter(png, 0, PNG_ALL_FILTERS);
    png_set_compression_level(png, 9 /* Z_BEST_COMPRESSION */);

    // Increase encoding buffer size to match zlib stream worst case compression
    // to ensure there will only be one IDAT chunk.
    png_set_compression_buffer_size(png, pngZlibStreamSize(buffer.ElementLength()));

    // Configure libpng to write encoded PNG to in-memory buffer.
    png_set_write_fn(
      png,
      &data,
      [](png_struct* png, std::uint8_t* data, std::size_t size) {
        PngData* pngData = static_cast<PngData*>(png_get_io_ptr(png));

        if (pngData->size + size > pngData->capacity) {
          png_error(png, "Impossible, PNG output data buffer is too small");
          return;
        }

        std::memcpy(pngData->buffer + pngData->size, data, size);
        pngData->size += size;
      },
      nullptr
    );

    // Set PNG image header.
    png_set_IHDR(
      png,
      png.getInfo(),
      width,
      height,
      8,
      PNG_COLOR_TYPE_RGB,
      PNG_INTERLACE_NONE,
      PNG_COMPRESSION_TYPE_BASE,
      PNG_FILTER_TYPE_BASE
    );

    // Set ICC profile if it exists.
    if (!iccProfile.IsEmpty()) {
      png_set_iCCP(
        png,
        png.getInfo(),
        "ICC Profile",
        PNG_COMPRESSION_TYPE_BASE,
        iccProfile.Data(),
        iccProfile.ElementLength()
      );
    }

    // Write header and ICC profile to buffer.
    png_write_info(png, png.getInfo());

    // Write each row of pixels.
    for (std::size_t i = 0; i < height; i++) {
      png_write_row(png, buffer.Data() + (i * width * 3));
    }

    // Finish writing pixels to buffer.
    png_write_end(png, png.getInfo());

    // Copy encoded PNG to smaller buffer.
    Napi::Uint8Array output = Napi::Uint8Array::New(env, data.size);
    std::memcpy(output.Data(), data.buffer, data.size);

    return output;
  }
};

NODE_API_ADDON(Cobble)
