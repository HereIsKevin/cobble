#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>

#include <napi.h>
#include <png.h>
#include <turbojpeg.h>
#include <webp/decode.h>
#include <webp/demux.h>

static_assert(sizeof(int) == 4, "int must be exactly 32 bits");
static_assert(sizeof(std::size_t) >= 8, "size_t must be at least 64 bits");

constexpr std::uint32_t MAX_WIDTH = (2 << 13) - 1;
constexpr std::uint32_t MAX_HEIGHT = (2 << 13) - 1;

// JPEG Decoding

class JpegDecodeWorker : public Napi::AsyncWorker {
  // Result Promise
  Napi::Promise::Deferred promise;

  // Encoded Input Data
  std::size_t inputSize;
  std::uint8_t* inputBuffer;

  // TurboJPEG Instance
  tjhandle handle = nullptr;

  // Image Dimensions
  std::uint32_t width = 0;
  std::uint32_t height = 0;

  // Decoded Output Data
  std::size_t outputSize = 0;
  std::uint8_t* outputBuffer = nullptr;

  // ICC Profile Data
  std::size_t iccProfileSize = 0;
  std::uint8_t* iccProfileBuffer = nullptr;

public:
  JpegDecodeWorker(const Napi::Env& env, const Napi::Uint8Array& input) :
    Napi::AsyncWorker(env, "JpegDecodeWorker"),
    promise(Napi::Promise::Deferred::New(env)),
    inputSize(input.ElementLength()),
    inputBuffer(new std::uint8_t[inputSize])
  {
    // Copying input to new buffer is needed because modifications to the input
    // could happen during decoding, which would be unsafe.
    std::memcpy(inputBuffer, input.Data(), inputSize);
  }

  ~JpegDecodeWorker() {
    // Destroy TurboJPEG instance.
    tj3Destroy(handle);

    // Deallocate all the buffers.
    delete[] inputBuffer;
    delete[] outputBuffer;
    delete[] iccProfileBuffer;
  }

  Napi::Promise GetPromise() {
    return promise.Promise();
  }

protected:
  void Execute() override {
    // Create TurboJPEG instance.
    handle = tj3Init(TJINIT_DECOMPRESS);
    if (handle == nullptr) {
      throw std::runtime_error(GetError());
    }

    // Treat decoding warning as fatal error.
    if (tj3Set(handle, TJPARAM_STOPONWARNING, true) < 0) {
      throw std::runtime_error(GetError());
    }

    // Do not decode additional metadata except for the ICC profile.
    if (tj3Set(handle, TJPARAM_SAVEMARKERS, 4) < 0) {
      throw std::runtime_error(GetError());
    }

    // Decode JPEG header for metadata.
    if (tj3DecompressHeader(handle, inputBuffer, inputSize) < 0) {
      throw std::runtime_error(GetError());
    }

    // Retrieve JPEG width.
    int rawWidth = tj3Get(handle, TJPARAM_JPEGWIDTH);
    if (rawWidth < 0) {
      throw std::runtime_error("JPEG width is unknown");
    }
    width = rawWidth;
    if (width > MAX_WIDTH) {
      throw std::runtime_error("JPEG width cannot be over 2^14-1");
    }

    // Retrieve JPEG height.
    int rawHeight = tj3Get(handle, TJPARAM_JPEGHEIGHT);
    if (rawHeight < 0) {
      throw std::runtime_error("JPEG height is unknown");
    }
    height = rawHeight;
    if (height > MAX_HEIGHT) {
      throw std::runtime_error("JPEG height cannot be over 2^14-1");
    }

    // Make sure JPEG has 8-bit data precision.
    int precision = tj3Get(handle, TJPARAM_PRECISION);
    if (precision < 0) {
      throw std::runtime_error("JPEG data precision is unknown");
    }
    if (precision != 8) {
      throw std::runtime_error("Only 8 bit data precision is supported");
    }

    // Make sure JPEG has RGB or YCbCr color space.
    int rawColorSpace = tj3Get(handle, TJPARAM_COLORSPACE);
    if (rawColorSpace < 0) {
      throw std::runtime_error("JPEG color space is unknown");
    }
    TJCS colorSpace = static_cast<TJCS>(rawColorSpace);
    if (colorSpace != TJCS_RGB && colorSpace != TJCS_YCbCr) {
      throw std::runtime_error("Only RGB and YCbCr color spaces are supported");
    }

    // Make sure JPEG is lossy.
    int rawLossless = tj3Get(handle, TJPARAM_LOSSLESS);
    if (rawLossless < 0) {
      throw std::runtime_error("JPEG compression algorithm is unknown");
    }
    bool lossless = rawLossless;
    if (lossless) {
      throw std::runtime_error("Only lossy compression is supported");
    }

    // Warnings are ignored when retrieving the ICC profile size since no
    // profile emits a warning, and we want to support images with no ICC
    // profile.
    if (
      tj3GetICCProfile(handle, nullptr, &iccProfileSize) < 0 &&
      tj3GetErrorCode(handle) != TJERR_WARNING
    ) {
      throw std::runtime_error(GetError());
    }

    // ICC profile size is greater than 0 if there is an ICC profile.
    if (iccProfileSize > 0) {
      // Allocate buffer for ICC profile.
      iccProfileBuffer = new std::uint8_t[iccProfileSize];

      // Retrieve ICC profile to buffer.
      if (tj3GetICCProfile(handle, &iccProfileBuffer, &iccProfileSize) < 0) {
        throw std::runtime_error(GetError());
      }
    }

    // Allocate output buffer for 8 bit data precision RGB JPEG.
    outputSize = width * height * tjPixelSize[TJPF_RGB];
    outputBuffer = new std::uint8_t[outputSize];

    // Decode JPEG image to buffer.
    if (
      tj3Decompress8(
        handle,
        inputBuffer,
        inputSize,
        outputBuffer,
        width * tjPixelSize[TJPF_RGB],
        TJPF_RGB
      ) < 0
    ) {
      throw std::runtime_error(GetError());
    }
  }

  void OnOK() override {
    Napi::Env env = Env();

    // Create result object.
    Napi::Object result = Napi::Object::New(env);

    // Set width and height.
    result.Set("width", width);
    result.Set("height", height);

    // Copy output to a Uint8Array.
    Napi::Uint8Array buffer = Napi::Uint8Array::New(env, outputSize);
    std::memcpy(buffer.Data(), outputBuffer, outputSize);
    result.Set("buffer", buffer);

    // Copy ICC profile to a Uint8Array if possible.
    if (iccProfileSize > 0) {
      Napi::Uint8Array iccProfile = Napi::Uint8Array::New(env, iccProfileSize);
      std::memcpy(iccProfile.Data(), iccProfileBuffer, iccProfileSize);
      result.Set("iccProfile", iccProfile);
    }

    // Resolve the promise with success.
    promise.Resolve(result);
  }

  void OnError(const Napi::Error& error) override {
    promise.Reject(error.Value());
  }

private:
  std::string GetError() {
    const char* message = tj3GetErrorStr(handle);
    if (message == nullptr) {
      return "JPEG decoding failed";
    }

    return message;
  }
};

// WebP Decoding

struct WebPChunkIteratorWrapper : public WebPChunkIterator {
  ~WebPChunkIteratorWrapper() {
    WebPDemuxReleaseChunkIterator(this);
  }
};

struct WebPIteratorWrapper : public WebPIterator {
  ~WebPIteratorWrapper() {
    WebPDemuxReleaseIterator(this);
  }
};

class WebPDecodeWorker : public Napi::AsyncWorker {
  // Result Promise
  Napi::Promise::Deferred promise;

  // Encoded Input Data
  std::size_t inputSize;
  std::uint8_t* inputBuffer;

  // WebPData-ified Input Data
  WebPData inputData;

  // WebP Demuxer
  WebPDemuxer* demux = nullptr;

  // Image Dimensions
  std::uint32_t width = 0;
  std::uint32_t height = 0;

  // Decoded Output Data
  std::size_t outputSize = 0;
  std::uint8_t* outputBuffer = nullptr;

  // ICC Profile Data
  std::size_t iccProfileSize = 0;
  std::uint8_t* iccProfileBuffer = nullptr;

public:
  WebPDecodeWorker(const Napi::Env& env, const Napi::Uint8Array& input) :
    Napi::AsyncWorker(env, "WebPDecodeWorker"),
    promise(Napi::Promise::Deferred::New(env)),
    inputSize(input.ElementLength()),
    inputBuffer(new std::uint8_t[inputSize]),
    inputData({.bytes = inputBuffer, .size = inputSize})
  {
    // Copying input to new buffer is needed because modifications to the input
    // could happen during decoding, which would be unsafe.
    std::memcpy(inputBuffer, input.Data(), inputSize);
  }

  ~WebPDecodeWorker() {
    // Destroy WebP demuxer.
    WebPDemuxDelete(demux);

    // Deallocate all the buffers.
    delete[] inputBuffer;
    delete[] outputBuffer;
    delete[] iccProfileBuffer;
  }

  Napi::Promise GetPromise() {
    return promise.Promise();
  }

protected:
  void Execute() override {
    // Create WebP demuxer.
    demux = WebPDemux(&inputData);
    if (demux == nullptr) {
      throw std::runtime_error("WebP decoding failed");
    }

    // Retrieve WebP canvas width.
    width = WebPDemuxGetI(demux, WEBP_FF_CANVAS_WIDTH);
    if (width > MAX_WIDTH) {
      throw std::runtime_error("WebP width cannot be over 2^14-1");
    }

    // Retrieve WebP canvas height.
    height = WebPDemuxGetI(demux, WEBP_FF_CANVAS_HEIGHT);
    if (height > MAX_HEIGHT) {
      throw std::runtime_error("WebP height cannot be over 2^14-1");
    }

    // Make sure WebP is not animated or transparent.
    std::uint32_t flags = WebPDemuxGetI(demux, WEBP_FF_FORMAT_FLAGS);
    if (flags & ANIMATION_FLAG) {
      throw std::runtime_error("Only still images are supported");
    }
    if (flags & ALPHA_FLAG) {
      throw std::runtime_error("Only opaque images are supported");
    }

    // Make sure WebP only has 1 frame.
    std::uint32_t frames = WebPDemuxGetI(demux, WEBP_FF_FRAME_COUNT);
    if (frames != 1) {
      throw std::runtime_error("Impossible, still must only have 1 frame");
    }

    // Retrieve ICC profile if available.
    if (flags & ICCP_FLAG) {
      // Retrieve ICC profile chunk, make sure that is is the first ICC profile
      // chunk, and make sure that there are not multiple ICC profile chunks.
      WebPChunkIteratorWrapper chunkIter;
      if (!WebPDemuxGetChunk(demux, "ICCP", 1, &chunkIter)) {
        throw std::runtime_error("ICC profile chunk disappeared");
      }
      if (chunkIter.chunk_num != 1) {
        throw std::runtime_error("Impossible, ICC profile chunk # changed");
      }
      if (chunkIter.num_chunks != 1) {
        throw std::runtime_error("Multiple ICC profiles");
      }

      // Copy ICC profile to buffer.
      iccProfileSize = chunkIter.chunk.size;
      iccProfileBuffer = new std::uint8_t[iccProfileSize];
      std::memcpy(iccProfileBuffer, chunkIter.chunk.bytes, iccProfileSize);
    }

    // Retrieve WebP frame, make sure that it is the first frame, make sure
    // there are not multiple frames, make sure the frame is complete, make sure
    // the frame is opaque, and sure the frame covers the entire canvas.
    WebPIteratorWrapper iter;
    if (!WebPDemuxGetFrame(demux, 1, &iter)) {
      throw std::runtime_error("Impossible, frame disappeared");
    }
    if (iter.frame_num != 1) {
      throw std::runtime_error("Impossible, frame # changed");
    }
    if (iter.num_frames != 1) {
      throw std::runtime_error("Impossible, frame count changed");
    }
    if (!iter.complete) {
      throw std::runtime_error("Impossible, frame is not complete");
    }
    if (iter.has_alpha) {
      throw std::runtime_error("Only opaque frames are supported");
    }
    if (iter.x_offset != 0 || iter.y_offset != 0) {
      throw std::runtime_error("Only frames with no offset are supported");
    }
    if (
      static_cast<std::uint32_t>(iter.width) != width ||
      static_cast<std::uint32_t>(iter.height) != height
    ) {
      throw std::runtime_error("Only frames filling canvas are supported");
    }

    // Allocate output buffer for 8 bit data precision RGB WebP.
    outputSize = width * height * 3;
    outputBuffer = new std::uint8_t[outputSize];

    // Decode WebP frame to buffer.
    if (
      WebPDecodeRGBInto(
        iter.fragment.bytes,
        iter.fragment.size,
        outputBuffer,
        outputSize,
        width * 3
      ) == nullptr
    ) {
      throw std::runtime_error("WebP decoding failed");
    }
  }

  void OnOK() override {
    Napi::Env env = Env();

    // Create result object.
    Napi::Object result = Napi::Object::New(env);

    // Set width and height.
    result.Set("width", width);
    result.Set("height", height);

    // Copy output to a Uint8Array.
    Napi::Uint8Array buffer = Napi::Uint8Array::New(env, outputSize);
    std::memcpy(buffer.Data(), outputBuffer, outputSize);
    result.Set("buffer", buffer);

    // Copy ICC profile to a Uint8Array if possible.
    if (iccProfileSize > 0) {
      Napi::Uint8Array iccProfile = Napi::Uint8Array::New(env, iccProfileSize);
      std::memcpy(iccProfile.Data(), iccProfileBuffer, iccProfileSize);
      result.Set("iccProfile", iccProfile);
    }

    // Resolve the promise with success.
    promise.Resolve(result);
  }

  void OnError(const Napi::Error& error) override {
    promise.Reject(error.Value());
  }
};

// PNG Encoding

class PngEncodeWorker : public Napi::AsyncWorker {
  // Result Promise
  Napi::Promise::Deferred promise;

  // Image Dimensions
  std::uint32_t width;
  std::uint32_t height;

  // Decoded Input Data
  std::size_t inputSize;
  std::uint8_t* inputBuffer;

  // ICC Profile Data
  std::size_t iccProfileSize = 0;
  std::uint8_t* iccProfileBuffer = nullptr;

  // PNG Writer
  png_struct* png = nullptr;
  png_info* pngInfo = nullptr;

  // Encoded Output Data
  std::size_t outputSize = 0;
  std::size_t outputCapacity = 0;
  std::uint8_t* outputBuffer = nullptr;

public:
  PngEncodeWorker(
    const Napi::Env& env,
    std::uint32_t width,
    std::uint32_t height,
    const Napi::Uint8Array& input,
    const Napi::Uint8Array& iccProfile
  ) :
    Napi::AsyncWorker(env, "PngEncodeWorker"),
    promise(Napi::Promise::Deferred::New(env)),
    width(width),
    height(height),
    inputSize(input.ElementLength()),
    inputBuffer(new std::uint8_t[inputSize])
  {
    // Verify image dimensions and pixel buffer size are valid.
    if (width > MAX_WIDTH) {
      throw Napi::Error::New(env, "PNG width cannot be over 2^14-1");
    }
    if (height > MAX_HEIGHT) {
      throw Napi::Error::New(env, "PNG height cannot be over 2^14-1");
    }
    if (width * height * 3 != inputSize) {
      throw Napi::Error::New(env, "PNG pixel buffer must be w*h*3 bytes");
    }

    // Copying input to new buffer is needed because modifications to the input
    // could happen during decoding, which would be unsafe.
    std::memcpy(inputBuffer, input.Data(), inputSize);

    // ICC profile only needs to be copied if it is available.
    if (!iccProfile.IsEmpty()) {
      iccProfileSize = iccProfile.ElementLength();
      iccProfileBuffer = new std::uint8_t[iccProfileSize];
      std::memcpy(iccProfileBuffer, input.Data(), iccProfileSize);
    }
  }

  ~PngEncodeWorker() {
    // Destroy PNG writer.
    png_destroy_write_struct(&png, &pngInfo);

    // Deallocate all the buffers.
    delete[] inputBuffer;
    delete[] outputBuffer;
    delete[] iccProfileBuffer;
  }

  Napi::Promise GetPromise() {
    return promise.Promise();
  }

protected:
  void Execute() override {
    // Create PNG writer.
    png = png_create_write_struct(
      PNG_LIBPNG_VER_STRING,
      nullptr,
      ErrorHandler,
      ErrorHandler
    );
    if (png == nullptr) {
      throw std::runtime_error("PNG encoder initialization failed");
    }
    pngInfo = png_create_info_struct(png);
    if (pngInfo == nullptr) {
      throw std::runtime_error("PNG encoder initialization failed");
    }

    // Allocate output buffer for encoded PNG.
    outputCapacity = BufferSize(inputSize);
    if (iccProfileSize > 0) {
      outputCapacity += IccProfileChunkSize(iccProfileSize);
    } else {
      outputCapacity += SrgbChunkSize();
    }
    outputBuffer = new std::uint8_t[outputCapacity];

    // Enable all filters and max compression level for smaller results.
    png_set_filter(png, 0, PNG_ALL_FILTERS);
    png_set_compression_level(png, 9 /* Z_BEST_COMPRESSION */);

    // Increase encoding buffer size to match zlib stream worst case compression
    // to ensure there will only be one IDAT chunk.
    png_set_compression_buffer_size(png, ZlibStreamSize(inputSize));

    // Configure libpng to write encoded PNG to buffer instead of file.
    png_set_write_fn(png, this, WriteHandler, nullptr);

    // Set PNG image header
    png_set_IHDR(
      png,
      pngInfo,
      width,
      height,
      8,
      PNG_COLOR_TYPE_RGB,
      PNG_INTERLACE_NONE,
      PNG_COMPRESSION_TYPE_BASE,
      PNG_FILTER_TYPE_BASE
    );

    // Set ICC profile if it exists.
    if (iccProfileSize > 0) {
      png_set_iCCP(
        png,
        pngInfo,
        "ICC Profile",
        PNG_COMPRESSION_TYPE_BASE,
        iccProfileBuffer,
        iccProfileSize
      );
    }
    // Otherwise mark PNG as sRGB, a very sensible default.
    else {
      png_set_sRGB(png, pngInfo, PNG_sRGB_INTENT_PERCEPTUAL);
    }

    // Write header and ICC profile to buffer.
    png_write_info(png, pngInfo);

    // Write each row of pixels.
    for (std::size_t row = 0; row < height; row++) {
      png_write_row(png, inputBuffer + (row * width * 3));
    }

    // Finish writing pixels to buffer.
    png_write_end(png, pngInfo);
  }

  void OnOK() override {
    Napi::Uint8Array buffer = Napi::Uint8Array::New(Env(), outputSize);
    std::memcpy(buffer.Data(), outputBuffer, outputSize);
    promise.Resolve(buffer);
  }

  void OnError(const Napi::Error& error) override {
    promise.Reject(error.Value());
  }

private:
  static void ErrorHandler(
    [[maybe_unused]] png_struct* png,
    const char* message
  ) {
    if (message == nullptr) {
      message = "PNG encoding failed";
    }

    throw std::runtime_error(message);
  }

  static void WriteHandler(
    png_struct* png,
    std::uint8_t* buffer,
    std::size_t size
  ) {
    PngEncodeWorker* worker =
      static_cast<PngEncodeWorker*>(png_get_io_ptr(png));

    // This throws an exception since png_error actually calls the configured
    // error handler, so there is no need to return to bail out.
    if (worker->outputSize + size > worker->outputCapacity) {
      png_error(png, "Impossible, PNG output buffer is too small");
    }

    std::memcpy(worker->outputBuffer + worker->outputSize, buffer, size);
    worker->outputSize += size;
  }

  static std::size_t ZlibStreamSize(std::size_t size) {
    // PNG zlib streams have a worst case compression of 2 byte compression
    // headers and flags + uncompressed data size + 5 bytes for each 32 kilobyte
    // chunk of uncompressed data + 4 byte checksum. However, I don't really
    // trust this estimation, so it is not actually used here.
    // std::size_t kb = 2 << 9;
    // std::size_t headerAndFlags = 2;
    // std::size_t chunks = (size + (32 * kb - 1)) / (32 * kb);
    // std::size_t checksum = 4;
    // return headerAndFlags + size + (5 * chunks) + checksum;

    // This is based on deflateBound from zlib. It overestimates more than my
    // estimation, but is probably safer to use.
    return size + ((size + 7) >> 3) + ((size + 63) >> 6) + 5 + 6;
  }

  static std::size_t ChunkSize(std::size_t size) {
    // PNG chunks need have 4 byte length + 4 byte chunk type + data size + 4
    // byte CRC at the end.
    std::size_t length = 4;
    std::size_t chunkType = 4;
    std::size_t crc = 4;
    return length + chunkType + size + crc;
  }

  static std::size_t IccProfileChunkSize(std::size_t size) {
    // PNG ICC profile chunks have a worst case compression of 93 byte header +
    // zlib stream size.
    return ChunkSize(81 + ZlibStreamSize(size));
  }

  static std::size_t SrgbChunkSize() {
    // Only value in a sRGB chunk is rendering intent, which is 1 byte.
    return ChunkSize(1);
  }

  static std::size_t BufferSize(std::size_t size) {
    // PNG images have a worst case compression of 8 byte signature + 25 byte
    // image header chunk + (12 + zlib stream size) bytes for image data chunk +
    // 12 bytes for image end chunk.
    std::size_t signature = 8;
    std::size_t imageHeader = ChunkSize(13);
    std::size_t imageData = ChunkSize(ZlibStreamSize(size));
    std::size_t imageEnd = ChunkSize(0);
    return signature + imageHeader + imageData + imageEnd;
  }
};

// Cobble Addon

class Cobble : public Napi::Addon<Cobble> {
public:
  Cobble([[maybe_unused]] Napi::Env env, Napi::Object exports) {
    DefineAddon(exports, {
      InstanceMethod("decodeJpeg", &Cobble::DecodeJpeg),
      InstanceMethod("decodeWebP", &Cobble::DecodeWebP),
      InstanceMethod("encodePng", &Cobble::EncodePng),
    });
  }

private:
  Napi::Value DecodeJpeg(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    // Validate input buffer.
    if (info.Length() != 1) {
      throw Napi::TypeError::New(env, "Expect exactly 1 argument");
    }
    if (!info[0].IsTypedArray()) {
      throw Napi::TypeError::New(env, "Expect TypedArray as argument");
    }
    Napi::TypedArray typedArray = info[0].As<Napi::TypedArray>();
    if (typedArray.TypedArrayType() != napi_uint8_array) {
      throw Napi::TypeError::New(env, "Expect Uint8Array as argument");
    }
    Napi::Uint8Array input = typedArray.As<Napi::Uint8Array>();

    // Decode JPEG asynchronously.
    JpegDecodeWorker* worker = new JpegDecodeWorker(env, input);
    worker->Queue();
    return worker->GetPromise();
  }

  Napi::Value DecodeWebP(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    // Validate input buffer.
    if (info.Length() != 1) {
      throw Napi::TypeError::New(env, "Expect exactly 1 argument");
    }
    if (!info[0].IsTypedArray()) {
      throw Napi::TypeError::New(env, "Expect TypedArray as argument");
    }
    Napi::TypedArray typedArray = info[0].As<Napi::TypedArray>();
    if (typedArray.TypedArrayType() != napi_uint8_array) {
      throw Napi::TypeError::New(env, "Expect Uint8Array as argument");
    }
    Napi::Uint8Array input = typedArray.As<Napi::Uint8Array>();

    // Decode JPEG asynchronously.
    WebPDecodeWorker* worker = new WebPDecodeWorker(env, input);
    worker->Queue();
    return worker->GetPromise();
  }

  Napi::Value EncodePng(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    // Validate image object.
    if (info.Length() != 1) {
      throw Napi::TypeError::New(env, "Expect exactly 1 argument");
    }
    if (!info[0].IsObject()) {
      throw Napi::TypeError::New(env, "Expect image object as argument");
    }
    Napi::Object image = info[0].As<Napi::Object>();

    // Validate image width.
    Napi::Value widthValue = image.Get("width");
    if (!widthValue.IsNumber()) {
      throw Napi::TypeError::New(env, "Expect width to be number");
    }
    double widthDouble = widthValue.As<Napi::Number>();
    if (!std::isfinite(widthDouble)) {
      throw Napi::TypeError::New(env, "Expect width to be finite");
    }
    if (std::modf(widthDouble, &widthDouble) != 0.0) {
      throw Napi::TypeError::New(env, "Expect width to be integer");
    }
    if (widthDouble <= 0 || widthDouble > UINT32_MAX) {
      throw Napi::TypeError::New(env, "Expect width to be positive");
    }
    std::uint32_t width = static_cast<std::uint32_t>(widthDouble);

    // Validate image height.
    Napi::Value heightValue = image.Get("height");
    if (!heightValue.IsNumber()) {
      throw Napi::TypeError::New(env, "Expect height to be number");
    }
    double heightDouble = heightValue.As<Napi::Number>();
    if (!std::isfinite(heightDouble)) {
      throw Napi::TypeError::New(env, "Expect height to be finite");
    }
    if (std::modf(heightDouble, &heightDouble) != 0.0) {
      throw Napi::TypeError::New(env, "Expect height to be integer");
    }
    if (heightDouble <= 0 || heightDouble > UINT32_MAX) {
      throw Napi::TypeError::New(env, "Expect height to be positive");
    }
    std::uint32_t height = static_cast<std::uint32_t>(heightDouble);

    // Validate image buffer.
    Napi::Value bufferValue = image.Get("buffer");
    if (!bufferValue.IsTypedArray()) {
      throw Napi::TypeError::New(env, "Expect TypedArray as argument");
    }
    Napi::TypedArray typedArray = bufferValue.As<Napi::TypedArray>();
    if (typedArray.TypedArrayType() != napi_uint8_array) {
      throw Napi::TypeError::New(env, "Expect Uint8Array as argument");
    }
    Napi::Uint8Array buffer = typedArray.As<Napi::Uint8Array>();

    // Validate ICC profile if present.
    Napi::Uint8Array iccProfile;
    Napi::Value iccProfileValue = image.Get("iccProfile");
    if (!iccProfileValue.IsUndefined()) {
      if (!iccProfileValue.IsTypedArray()) {
        throw Napi::TypeError::New(env, "Expect ICC profile to be TypedArray");
      }
      Napi::TypedArray typedArray = iccProfileValue.As<Napi::TypedArray>();
      if (typedArray.TypedArrayType() != napi_uint8_array) {
        throw Napi::TypeError::New(env, "Expect ICC profile to be Uint8Array");
      }
      iccProfile = typedArray.As<Napi::Uint8Array>();
    }

    // Encode PNG asynchronously.
    PngEncodeWorker* worker =
      new PngEncodeWorker(env, width, height, buffer, iccProfile);
    worker->Queue();
    return worker->GetPromise();
  }
};

NODE_API_ADDON(Cobble)
