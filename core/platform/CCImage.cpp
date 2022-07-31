/****************************************************************************
Copyright (c) 2010-2012 cocos2d-x.org
Copyright (c) 2013-2016 Chukong Technologies Inc.
Copyright (c) 2017-2018 Xiamen Yaji Software Co., Ltd.
Copyright (c) 2020 C4games Ltd
Copyright (c) 2021-2022 Bytedance Inc.

https://axis-project.github.io/

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
****************************************************************************/

#include "platform/CCImage.h"
#include "renderer/backend/PixelFormatUtils.h"

#include <string>
#include <ctype.h>

#include "base/ccConfig.h"  // AX_USE_JPEG, AX_USE_WEBP

#define STBI_NO_JPEG
#define STBI_NO_PNG
#define STBI_NO_GIF
#define STBI_NO_PSD
#define STBI_NO_PIC
#define STBI_NO_PNM
#define STBI_NO_HDR
#define STBI_NO_TGA
#define STB_IMAGE_IMPLEMENTATION
#if AX_TARGET_PLATFORM == AX_PLATFORM_IOS
#    define STBI_NO_THREAD_LOCALS
#endif
#include "stb/stb_image.h"

extern "C" {
// To resolve link error when building 32bits with Xcode 6.
// More information please refer to the discussion in https://github.com/cocos2d/cocos2d-x/pull/6986
#if defined(__unix) || (AX_TARGET_PLATFORM == AX_PLATFORM_IOS)
#    ifndef __ENABLE_COMPATIBILITY_WITH_UNIX_2003__
#        define __ENABLE_COMPATIBILITY_WITH_UNIX_2003__
#        include <stdio.h>
#        include <dirent.h>
FILE* fopen$UNIX2003(const char* filename, const char* mode)
{
    return fopen(filename, mode);
}
size_t fwrite$UNIX2003(const void* a, size_t b, size_t c, FILE* d)
{
    return fwrite(a, b, c, d);
}
int fputs$UNIX2003(const char* res1, FILE* res2)
{
    return fputs(res1, res2);
}
char* strerror$UNIX2003(int errnum)
{
    return strerror(errnum);
}
DIR* opendir$INODE64$UNIX2003(char* dirName)
{
    return opendir(dirName);
}
DIR* opendir$INODE64(char* dirName)
{
    return opendir(dirName);
}

int closedir$UNIX2003(DIR* dir)
{
    return closedir(dir);
}

struct dirent* readdir$INODE64(DIR* dir)
{
    return readdir(dir);
}
#    endif
#endif

#if AX_USE_PNG
#    include "png.h"
#endif  // AX_USE_PNG

#if AX_USE_JPEG
#    include "jpeglib.h"
#    include <setjmp.h>
#endif  // AX_USE_JPEG
} /* extern "C" */

#include "base/ktxspec_v1.h"

#include "base/s3tc.h"
#include "base/atitc.h"
#include "base/pvr.h"
#include "base/TGAlib.h"

#include "base/etc1.h"
#include "base/etc2.h"

#include "base/astc.h"

#if AX_USE_WEBP
#    include "decode.h"
#endif  // AX_USE_WEBP

#include "base/ccMacros.h"
#include "platform/CCCommon.h"
#include "platform/CCStdC.h"
#include "platform/CCFileUtils.h"
#include "base/CCConfiguration.h"
#include "base/ccUtils.h"
#include "base/ZipUtils.h"
#if (AX_TARGET_PLATFORM == AX_PLATFORM_ANDROID)
#    include "platform/android/CCFileUtils-android.h"
#    include "platform/CCGL.h"
#endif


NS_AX_BEGIN

//////////////////////////////////////////////////////////////////////////
// struct and data for pvr structure

namespace
{
static const int PVR_TEXTURE_FLAG_TYPE_MASK = 0xff;

// Values taken from PVRTexture.h from http://www.imgtec.com
enum class PVR2TextureFlag
{
    Mipmap       = (1 << 8),   // has mip map levels
    Twiddle      = (1 << 9),   // is twiddled
    Bumpmap      = (1 << 10),  // has normals encoded for a bump map
    Tiling       = (1 << 11),  // is bordered for tiled pvr
    Cubemap      = (1 << 12),  // is a cubemap/skybox
    FalseMipCol  = (1 << 13),  // are there false colored MIP levels
    Volume       = (1 << 14),  // is this a volume texture
    Alpha        = (1 << 15),  // v2.1 is there transparency info in the texture
    VerticalFlip = (1 << 16),  // v2.1 is the texture vertically flipped
};

enum class PVR3TextureFlag
{
    PremultipliedAlpha = (1 << 1)  // has premultiplied alpha
};

static const char gPVRTexIdentifier[5] = "PVR!";

// v2
enum class PVR2TexturePixelFormat : uint8_t
{
    RGBA4444 = 0x10,
    RGBA5551,
    RGBA8888,
    RGB565,
    RGB555,  // unsupported
    RGB888,
    I8,
    AI88,
    PVRTC2BPP_RGBA,
    PVRTC4BPP_RGBA,
    BGRA8888,
    A8,
};

// v3
enum class PVR3TexturePixelFormat : uint64_t
{
    PVRTC2BPP_RGB     = 0ULL,
    PVRTC2BPP_RGBA    = 1ULL,
    PVRTC4BPP_RGB     = 2ULL,
    PVRTC4BPP_RGBA    = 3ULL,
    PVRTC2_2BPP_RGBA  = 4ULL,
    PVRTC2_4BPP_RGBA  = 5ULL,
    ETC1              = 6ULL,
    DXT1              = 7ULL,
    DXT2              = 8ULL,
    DXT3              = 9ULL,
    DXT4              = 10ULL,
    DXT5              = 11ULL,
    BC1               = 7ULL,
    BC2               = 9ULL,
    BC3               = 11ULL,
    BC4               = 12ULL,
    BC5               = 13ULL,
    BC6               = 14ULL,
    BC7               = 15ULL,
    UYVY              = 16ULL,
    YUY2              = 17ULL,
    BW1bpp            = 18ULL,
    R9G9B9E5          = 19ULL,
    RGBG8888          = 20ULL,
    GRGB8888          = 21ULL,
    ETC2_RGB          = 22ULL,
    ETC2_RGBA         = 23ULL,
    ETC2_RGBA1        = 24ULL,
    EAC_R11_Unsigned  = 25ULL,
    EAC_R11_Signed    = 26ULL,
    EAC_RG11_Unsigned = 27ULL,
    EAC_RG11_Signed   = 28ULL,

    BGRA8888 = 0x0808080861726762ULL,
    RGBA8888 = 0x0808080861626772ULL,
    RGBA4444 = 0x0404040461626772ULL,
    RGBA5551 = 0x0105050561626772ULL,
    RGB565   = 0x0005060500626772ULL,
    RGB888   = 0x0008080800626772ULL,
    A8       = 0x0000000800000061ULL,
    L8       = 0x000000080000006cULL,
    LA88     = 0x000008080000616cULL,
};

// v2
typedef const std::map<PVR2TexturePixelFormat, backend::PixelFormat> _pixel2_formathash;

static const _pixel2_formathash::value_type v2_pixel_formathash_value[] = {
    _pixel2_formathash::value_type(PVR2TexturePixelFormat::BGRA8888, backend::PixelFormat::BGRA8),
    _pixel2_formathash::value_type(PVR2TexturePixelFormat::RGBA8888, backend::PixelFormat::RGBA8),
    _pixel2_formathash::value_type(PVR2TexturePixelFormat::RGBA4444, backend::PixelFormat::RGBA4),
    _pixel2_formathash::value_type(PVR2TexturePixelFormat::RGBA5551, backend::PixelFormat::RGB5A1),
    _pixel2_formathash::value_type(PVR2TexturePixelFormat::RGB565, backend::PixelFormat::RGB565),
    _pixel2_formathash::value_type(PVR2TexturePixelFormat::RGB888, backend::PixelFormat::RGB8),
    _pixel2_formathash::value_type(PVR2TexturePixelFormat::A8, backend::PixelFormat::A8),
    _pixel2_formathash::value_type(PVR2TexturePixelFormat::I8, backend::PixelFormat::L8),
    _pixel2_formathash::value_type(PVR2TexturePixelFormat::AI88, backend::PixelFormat::LA8),

    _pixel2_formathash::value_type(PVR2TexturePixelFormat::PVRTC2BPP_RGBA, backend::PixelFormat::PVRTC2A),
    _pixel2_formathash::value_type(PVR2TexturePixelFormat::PVRTC4BPP_RGBA, backend::PixelFormat::PVRTC4A),
};

static const int PVR2_MAX_TABLE_ELEMENTS = sizeof(v2_pixel_formathash_value) / sizeof(v2_pixel_formathash_value[0]);
static const _pixel2_formathash v2_pixel_formathash(v2_pixel_formathash_value,
                                                    v2_pixel_formathash_value + PVR2_MAX_TABLE_ELEMENTS);

// v3
typedef const std::map<PVR3TexturePixelFormat, backend::PixelFormat> _pixel3_formathash;
static _pixel3_formathash::value_type v3_pixel_formathash_value[] = {
    _pixel3_formathash::value_type(PVR3TexturePixelFormat::BGRA8888, backend::PixelFormat::BGRA8),
    _pixel3_formathash::value_type(PVR3TexturePixelFormat::RGBA8888, backend::PixelFormat::RGBA8),
    _pixel3_formathash::value_type(PVR3TexturePixelFormat::RGBA4444, backend::PixelFormat::RGBA4),
    _pixel3_formathash::value_type(PVR3TexturePixelFormat::RGBA5551, backend::PixelFormat::RGB5A1),
    _pixel3_formathash::value_type(PVR3TexturePixelFormat::RGB565, backend::PixelFormat::RGB565),
    _pixel3_formathash::value_type(PVR3TexturePixelFormat::RGB888, backend::PixelFormat::RGB8),
    _pixel3_formathash::value_type(PVR3TexturePixelFormat::A8, backend::PixelFormat::A8),
    _pixel3_formathash::value_type(PVR3TexturePixelFormat::L8, backend::PixelFormat::L8),
    _pixel3_formathash::value_type(PVR3TexturePixelFormat::LA88, backend::PixelFormat::LA8),

    _pixel3_formathash::value_type(PVR3TexturePixelFormat::PVRTC2BPP_RGB, backend::PixelFormat::PVRTC2),
    _pixel3_formathash::value_type(PVR3TexturePixelFormat::PVRTC2BPP_RGBA, backend::PixelFormat::PVRTC2A),
    _pixel3_formathash::value_type(PVR3TexturePixelFormat::PVRTC4BPP_RGB, backend::PixelFormat::PVRTC4),
    _pixel3_formathash::value_type(PVR3TexturePixelFormat::PVRTC4BPP_RGBA, backend::PixelFormat::PVRTC4A),

    _pixel3_formathash::value_type(PVR3TexturePixelFormat::ETC1, backend::PixelFormat::ETC1),
};

static const int PVR3_MAX_TABLE_ELEMENTS = sizeof(v3_pixel_formathash_value) / sizeof(v3_pixel_formathash_value[0]);

static const _pixel3_formathash v3_pixel_formathash(v3_pixel_formathash_value,
                                                    v3_pixel_formathash_value + PVR3_MAX_TABLE_ELEMENTS);

typedef struct _PVRTexHeader
{
    unsigned int headerLength;
    unsigned int height;
    unsigned int width;
    unsigned int numMipmaps;
    unsigned int flags;
    unsigned int dataLength;
    unsigned int bpp;
    unsigned int bitmaskRed;
    unsigned int bitmaskGreen;
    unsigned int bitmaskBlue;
    unsigned int bitmaskAlpha;
    unsigned int pvrTag;
    unsigned int numSurfs;
} PVRv2TexHeader;

#ifdef _MSC_VER
#    pragma pack(push, 1)
#endif
typedef struct
{
    uint32_t version;
    uint32_t flags;
    uint64_t pixelFormat;
    uint32_t colorSpace;
    uint32_t channelType;
    uint32_t height;
    uint32_t width;
    uint32_t depth;
    uint32_t numberOfSurfaces;
    uint32_t numberOfFaces;
    uint32_t numberOfMipmaps;
    uint32_t metadataLength;
#ifdef _MSC_VER
} PVRv3TexHeader;
#    pragma pack(pop)
#else
} __attribute__((packed)) PVRv3TexHeader;
#endif
}  // namespace
// pvr structure end

//////////////////////////////////////////////////////////////////////////

// struct and data for s3tc(dds) struct
namespace
{
struct DDColorKey
{
    uint32_t colorSpaceLowValue;
    uint32_t colorSpaceHighValue;
};

struct DDSCaps
{
    uint32_t caps;
    uint32_t caps2;
    uint32_t caps3;
    uint32_t caps4;
};

struct DDPixelFormat
{
    uint32_t size;
    uint32_t flags;
    uint32_t fourCC;
    uint32_t RGBBitCount;
    uint32_t RBitMask;
    uint32_t GBitMask;
    uint32_t BBitMask;
    uint32_t ABitMask;
};

struct DDSURFACEDESC2
{
    uint32_t size;
    uint32_t flags;
    uint32_t height;
    uint32_t width;

    union
    {
        uint32_t pitch;
        uint32_t linearSize;
    } DUMMYUNIONNAMEN1;

    union
    {
        uint32_t backBufferCount;
        uint32_t depth;
    } DUMMYUNIONNAMEN5;

    union
    {
        uint32_t mipMapCount;
        uint32_t refreshRate;
        uint32_t srcVBHandle;
    } DUMMYUNIONNAMEN2;

    uint32_t alphaBitDepth;
    uint32_t reserved;
    uint32_t surface;

    union
    {
        DDColorKey ddckCKDestOverlay;
        uint32_t emptyFaceColor;
    } DUMMYUNIONNAMEN3;

    DDColorKey ddckCKDestBlt;
    DDColorKey ddckCKSrcOverlay;
    DDColorKey ddckCKSrcBlt;

    union
    {
        DDPixelFormat ddpfPixelFormat;
        uint32_t FVF;
    } DUMMYUNIONNAMEN4;

    DDSCaps ddsCaps;
    uint32_t textureStage;
};

#pragma pack(push, 1)

struct S3TCTexHeader
{
    char fileCode[4];
    DDSURFACEDESC2 ddsd;
};

#pragma pack(pop)

}  // namespace
// s3tc struct end

//////////////////////////////////////////////////////////////////////////

namespace
{
typedef struct
{
    const uint8_t* data;
    ssize_t size;
    int offset;
} tImageSource;

#if AX_USE_PNG
void pngWriteCallback(png_structp png_ptr, png_bytep data, size_t length)
{
    if (png_ptr == NULL)
        return;

    FileStream* fileStream = (FileStream*)png_get_io_ptr(png_ptr);

    const auto check = fileStream->write(data, length);

    if (check != length)
        png_error(png_ptr, "Write Error");
}

static void pngReadCallback(png_structp png_ptr, png_bytep data, png_size_t length)
{
    tImageSource* isource = (tImageSource*)png_get_io_ptr(png_ptr);

    if ((int)(isource->offset + length) <= isource->size)
    {
        memcpy(data, isource->data + isource->offset, length);
        isource->offset += length;
    }
    else
    {
        png_error(png_ptr, "pngReaderCallback failed");
    }
}
#endif  // AX_USE_PNG
}  // namespace

/*
 * Notes: PVR file Specification have many pixel formats, cocos2d-x-v2~v4 and axis only support pvrtc and etc1
 * see: https://cdn.imgtec.com/sdk-documentation/PVR+File+Format.Specification.pdf
 */
static backend::PixelFormat getDevicePVRPixelFormat(backend::PixelFormat format)
{
    switch (format)
    {
    case backend::PixelFormat::PVRTC4:
    case backend::PixelFormat::PVRTC4A:
    case backend::PixelFormat::PVRTC2:
    case backend::PixelFormat::PVRTC2A:
        if (Configuration::getInstance()->supportsPVRTC())
            return format;
        else
            return backend::PixelFormat::RGBA8;
    case backend::PixelFormat::ETC1:
        if (Configuration::getInstance()->supportsETC1())
            return format;
        else if (Configuration::getInstance()->supportsETC2())
            return backend::PixelFormat::ETC2_RGB;
        else
            return backend::PixelFormat::RGBA8;
    default:
        return format;
    }
}

namespace
{
bool testFormatForPvr2TCSupport(PVR2TexturePixelFormat /*format*/)
{
    return true;
}

bool testFormatForPvr3TCSupport(PVR3TexturePixelFormat format)
{
    switch (format)
    {
    case PVR3TexturePixelFormat::DXT1:
    case PVR3TexturePixelFormat::DXT3:
    case PVR3TexturePixelFormat::DXT5:
        return Configuration::getInstance()->supportsS3TC();

    case PVR3TexturePixelFormat::BGRA8888:
        return Configuration::getInstance()->supportsBGRA8888();

    case PVR3TexturePixelFormat::PVRTC2BPP_RGB:
    case PVR3TexturePixelFormat::PVRTC2BPP_RGBA:
    case PVR3TexturePixelFormat::PVRTC4BPP_RGB:
    case PVR3TexturePixelFormat::PVRTC4BPP_RGBA:
    case PVR3TexturePixelFormat::ETC1:
    case PVR3TexturePixelFormat::RGBA8888:
    case PVR3TexturePixelFormat::RGBA4444:
    case PVR3TexturePixelFormat::RGBA5551:
    case PVR3TexturePixelFormat::RGB565:
    case PVR3TexturePixelFormat::RGB888:
    case PVR3TexturePixelFormat::A8:
    case PVR3TexturePixelFormat::L8:
    case PVR3TexturePixelFormat::LA88:
        return true;

    default:
        return false;
    }
}
}  // namespace

namespace
{
static uint32_t makeFourCC(char ch0, char ch1, char ch2, char ch3)
{
    const uint32_t fourCC = ((uint32_t)(char)(ch0) | ((uint32_t)(char)(ch1) << 8) | ((uint32_t)(char)(ch2) << 16) |
                             ((uint32_t)(char)(ch3) << 24));
    return fourCC;
}
}  // namespace

//////////////////////////////////////////////////////////////////////////
// Implement Image
//////////////////////////////////////////////////////////////////////////
bool Image::PNG_PREMULTIPLIED_ALPHA_ENABLED = true;
uint32_t Image::COMPRESSED_IMAGE_PMA_FLAGS  = Image::CompressedImagePMAFlag::DUAL_SAMPLER;

void Image::setCompressedImagesHavePMA(uint32_t targets, bool havePMA)
{
    if (havePMA)
        COMPRESSED_IMAGE_PMA_FLAGS |= targets;
    else
        COMPRESSED_IMAGE_PMA_FLAGS &= ~targets;
}

bool Image::isCompressedImageHavePMA(uint32_t target)
{
    return target & COMPRESSED_IMAGE_PMA_FLAGS;
}

Image::Image()
    : _data(nullptr)
    , _dataLen(0)
    , _offset(0)
    , _width(0)
    , _height(0)
    , _unpack(false)
    , _fileType(Format::UNKNOWN)
    , _pixelFormat(backend::PixelFormat::NONE)
    , _numberOfMipmaps(0)
    , _hasPremultipliedAlpha(false)
{}

Image::~Image()
{
    if (!_unpack)
    {
        AX_SAFE_FREE(_data);
    }
    else
    {
        for (int i = 0; i < _numberOfMipmaps; ++i)
            AX_SAFE_FREE(_mipmaps[i].address);
    }
}

bool Image::initWithImageFile(std::string_view path)
{
    bool ret  = false;
    _filePath = FileUtils::getInstance()->fullPathForFilename(path);

    Data data = FileUtils::getInstance()->getDataFromFile(_filePath);

    if (!data.isNull())
    {
        ssize_t n = 0;
        auto buf  = data.takeBuffer(&n);
        ret       = initWithImageData(buf, n, true);
    }

    return ret;
}

bool Image::initWithImageFileThreadSafe(std::string_view fullpath)
{
    bool ret  = false;
    _filePath = fullpath;

    Data data = FileUtils::getInstance()->getDataFromFile(_filePath);

    if (!data.isNull())
    {
        ssize_t n = 0;
        auto buf  = data.takeBuffer(&n);
        ret       = initWithImageData(buf, n, true);
    }

    return ret;
}

bool Image::initWithImageData(const uint8_t* data, ssize_t dataLen)
{
    return initWithImageData(const_cast<uint8_t*>(data), dataLen, false);
}

bool Image::initWithImageData(uint8_t* data, ssize_t dataLen, bool ownData)
{
    bool ret = false;

    do
    {
        AX_BREAK_IF(!data || dataLen == 0);

        uint8_t* unpackedData = nullptr;
        ssize_t unpackedLen   = 0;

        // detect and unzip the compress file
        if (ZipUtils::isCCZBuffer(data, dataLen))
        {
            unpackedLen = ZipUtils::inflateCCZBuffer(data, dataLen, &unpackedData);
        }
        else if (ZipUtils::isGZipBuffer(data, dataLen))
        {
            unpackedLen = ZipUtils::inflateMemory(const_cast<uint8_t*>(data), dataLen, &unpackedData);
        }
        else
        {
            unpackedData = const_cast<uint8_t*>(data);
            unpackedLen  = dataLen;
        }

        if (unpackedData != data)
        {  // free old data and own the unpackedData
            if (ownData)
                free((void*)data);
            ownData = true;
        }

        _fileType = detectFormat(unpackedData, unpackedLen);

        switch (_fileType)
        {
        case Format::PNG:
            ret = initWithPngData(unpackedData, unpackedLen);
            break;
        case Format::JPG:
            ret = initWithJpgData(unpackedData, unpackedLen);
            break;
        case Format::WEBP:
            ret = initWithWebpData(unpackedData, unpackedLen);
            break;
        case Format::PVR:
            ret = initWithPVRData(unpackedData, unpackedLen, ownData);
            break;
        case Format::ETC1:
            ret = initWithETCData(unpackedData, unpackedLen, ownData);
            break;
        case Format::ETC2:
            ret = initWithETC2Data(unpackedData, unpackedLen, ownData);
            break;
        case Format::S3TC:
            ret = initWithS3TCData(unpackedData, unpackedLen, ownData);
            break;
        case Format::ATITC:
            ret = initWithATITCData(unpackedData, unpackedLen, ownData);
            break;
        case Format::ASTC:
            ret = initWithASTCData(unpackedData, unpackedLen, ownData);
            break;
        case Format::BMP:
            ret = initWithBmpData(unpackedData, unpackedLen);
            break;
        default:
        {
            // load and detect image format
            tImageTGA* tgaData = tgaLoadBuffer(unpackedData, unpackedLen);

            if (tgaData != nullptr && tgaData->status == TGA_OK)
            {
                ret = initWithTGAData(tgaData);
            }
            else
            {
                AXLOG("cocos2d: unsupported image format!");
            }

            free(tgaData);
            break;
        }
        }

        if (_data != unpackedData && ownData)
            free(unpackedData);
        // else, the hardware texture decoder used, the compressed data was stored directly
    } while (0);

    return ret;
}

bool Image::initWithRawData(const uint8_t* data,
                            ssize_t /*dataLen*/,
                            int width,
                            int height,
                            int /*bitsPerComponent*/,
                            bool preMulti)
{
    bool ret = false;
    do
    {
        AX_BREAK_IF(0 == width || 0 == height);

        _height                = height;
        _width                 = width;
        _hasPremultipliedAlpha = preMulti;
        _pixelFormat           = backend::PixelFormat::RGBA8;

        // only RGBA8888 supported
        int bytesPerComponent = 4;
        _dataLen              = height * width * bytesPerComponent;
        _data                 = static_cast<uint8_t*>(malloc(_dataLen));
        AX_BREAK_IF(!_data);
        memcpy(_data, data, _dataLen);

        ret = true;
    } while (0);

    return ret;
}

bool Image::isPng(const uint8_t* data, ssize_t dataLen)
{
    if (dataLen <= 8)
    {
        return false;
    }

    static const uint8_t PNG_SIGNATURE[] = {0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a};

    return memcmp(PNG_SIGNATURE, data, sizeof(PNG_SIGNATURE)) == 0;
}

bool Image::isBmp(const uint8_t* data, ssize_t dataLen)
{
    return dataLen > 54 && data[0] == 'B' && data[1] == 'M';
}

bool Image::isEtc1(const uint8_t* data, ssize_t /*dataLen*/)
{
    return !!etc1_pkm_is_valid((etc1_byte*)data);
}

bool Image::isEtc2(const uint8_t* data, ssize_t dataLen)
{
    return !!etc2_pkm_is_valid((etc2_byte*)data);
}

bool Image::isS3TC(const uint8_t* data, ssize_t /*dataLen*/)
{

    S3TCTexHeader* header = (S3TCTexHeader*)data;

    return (strncmp(header->fileCode, "DDS", 3) == 0);
}

bool Image::isASTC(const uint8_t* data, ssize_t /*dataLen*/)
{
    astc_header* hdr = (astc_header*)data;

    uint32_t magicval = astc_unpack_bytes(hdr->magic[0], hdr->magic[1], hdr->magic[2], hdr->magic[3]);

    return (magicval == ASTC_MAGIC_ID);
}

bool Image::isJpg(const uint8_t* data, ssize_t dataLen)
{
    if (dataLen <= 4)
    {
        return false;
    }

    static const uint8_t JPG_SOI[] = {0xFF, 0xD8};

    return memcmp(data, JPG_SOI, 2) == 0;
}

bool Image::isWebp(const uint8_t* data, ssize_t dataLen)
{
    if (dataLen <= 12)
    {
        return false;
    }

    static const char* WEBP_RIFF = "RIFF";
    static const char* WEBP_WEBP = "WEBP";

    return memcmp(data, WEBP_RIFF, 4) == 0 && memcmp(static_cast<const uint8_t*>(data) + 8, WEBP_WEBP, 4) == 0;
}

bool Image::isPvr(const uint8_t* data, ssize_t dataLen)
{
    if (static_cast<size_t>(dataLen) < sizeof(PVRv2TexHeader) || static_cast<size_t>(dataLen) < sizeof(PVRv3TexHeader))
    {
        return false;
    }

    const PVRv2TexHeader* headerv2 = static_cast<const PVRv2TexHeader*>(static_cast<const void*>(data));
    const PVRv3TexHeader* headerv3 = static_cast<const PVRv3TexHeader*>(static_cast<const void*>(data));

    return memcmp(&headerv2->pvrTag, gPVRTexIdentifier, strlen(gPVRTexIdentifier)) == 0 ||
           AX_SWAP_INT32_BIG_TO_HOST(headerv3->version) == 0x50565203;
}

Image::Format Image::detectFormat(const uint8_t* data, ssize_t dataLen)
{
    if (isPng(data, dataLen))
    {
        return Format::PNG;
    }
    else if (isJpg(data, dataLen))
    {
        return Format::JPG;
    }
    else if (isBmp(data, dataLen))
    {
        return Format::BMP;
    }
    else if (isWebp(data, dataLen))
    {
        return Format::WEBP;
    }
    else if (isPvr(data, dataLen))
    {
        return Format::PVR;
    }
    else if (isEtc1(data, dataLen))
    {
        return Format::ETC1;
    }
    else if (isEtc2(data, dataLen))
    {
        return Format::ETC2;
    }
    else if (isS3TC(data, dataLen))
    {
        return Format::S3TC;
    }
    else if (isASTC(data, dataLen))
    {
        return Format::ASTC;
    }
    else if (dataLen >= KTX_V1_HEADER_SIZE)
    {  // Check whether ktxspec v1.1 file format
        auto header = (KTXv1Header*)data;
        if (memcmp(&header->identifier[1], KTX_V1_MAGIC, sizeof(KTX_V1_MAGIC) - 1) == 0)
        {
            switch (header->glInternalFormat)
            {
            case KTXv1Header::InternalFormat::ATC_RGB_AMD:
            case KTXv1Header::InternalFormat::ATC_RGBA_INTERPOLATED_ALPHA_AMD:
            case KTXv1Header::InternalFormat::ATC_RGBA_EXPLICIT_ALPHA_AMD:
                return Format::ATITC;
            case KTXv1Header::InternalFormat::ETC2_RGB8:
            case KTXv1Header::InternalFormat::ETC2_RGBA8:
                return Format::ETC2;
            case KTXv1Header::InternalFormat::ETC1_RGB8:
                return Format::ETC1;
            default:;
            }
        }
    }

    return Format::UNKNOWN;
}

int Image::getBitPerPixel()
{
    return backend::PixelFormatUtils::getFormatDescriptor(_pixelFormat).bpp;
}

bool Image::hasAlpha()
{
    return backend::PixelFormatUtils::getFormatDescriptor(_pixelFormat).alpha;
}

bool Image::isCompressed()
{
    return backend::PixelFormatUtils::isCompressed(_pixelFormat);
}

namespace
{
/*
 * ERROR HANDLING:
 *
 * The JPEG library's standard error handler (jerror.c) is divided into
 * several "methods" which you can override individually.  This lets you
 * adjust the behavior without duplicating a lot of code, which you might
 * have to update with each future release.
 *
 * We override the "error_exit" method so that control is returned to the
 * library's caller when a fatal error occurs, rather than calling exit()
 * as the standard error_exit method does.
 *
 * We use C's setjmp/longjmp facility to return control.  This means that the
 * routine which calls the JPEG library must first execute a setjmp() call to
 * establish the return point.  We want the replacement error_exit to do a
 * longjmp().  But we need to make the setjmp buffer accessible to the
 * error_exit routine.  To do this, we make a private extension of the
 * standard JPEG error handler object.  (If we were using C++, we'd say we
 * were making a subclass of the regular error handler.)
 *
 * Here's the extended error handler struct:
 */
#if AX_USE_JPEG
struct MyErrorMgr
{
    struct jpeg_error_mgr pub; /* "public" fields */
    jmp_buf setjmp_buffer;     /* for return to caller */
};

typedef struct MyErrorMgr* MyErrorPtr;

/*
 * Here's the routine that will replace the standard error_exit method:
 */

METHODDEF(void)
myErrorExit(j_common_ptr cinfo)
{
    /* cinfo->err really points to a MyErrorMgr struct, so coerce pointer */
    MyErrorPtr myerr = (MyErrorPtr)cinfo->err;

    /* Always display the message. */
    /* We could postpone this until after returning, if we chose. */
    /* internal message function can't show error message in some platforms, so we rewrite it here.
     * edit it if has version conflict.
     */
    //(*cinfo->err->output_message) (cinfo);
    char buffer[JMSG_LENGTH_MAX];
    (*cinfo->err->format_message)(cinfo, buffer);
    AXLOG("jpeg error: %s", buffer);

    /* Return control to the setjmp point */
    longjmp(myerr->setjmp_buffer, 1);
}
#endif  // AX_USE_JPEG
}  // namespace

bool Image::initWithJpgData(uint8_t* data, ssize_t dataLen)
{
#if AX_USE_JPEG
    /* these are standard libjpeg structures for reading(decompression) */
    struct jpeg_decompress_struct cinfo;
    /* We use our private extension JPEG error handler.
     * Note that this struct must live as long as the main JPEG parameter
     * struct, to avoid dangling-pointer problems.
     */
    struct MyErrorMgr jerr;
    /* libjpeg data structure for storing one row, that is, scanline of an image */
    JSAMPROW row_pointer[1] = {0};
    uint32_t location       = 0;

    bool ret = false;
    do
    {
        /* We set up the normal JPEG error routines, then override error_exit. */
        cinfo.err           = jpeg_std_error(&jerr.pub);
        jerr.pub.error_exit = myErrorExit;
        /* Establish the setjmp return context for MyErrorExit to use. */
        if (setjmp(jerr.setjmp_buffer))
        {
            /* If we get here, the JPEG code has signaled an error.
             * We need to clean up the JPEG object, close the input file, and return.
             */
            jpeg_destroy_decompress(&cinfo);
            break;
        }

        /* setup decompression process and source, then read JPEG header */
        jpeg_create_decompress(&cinfo);

#    ifndef AX_TARGET_QT5
        jpeg_mem_src(&cinfo, const_cast<uint8_t*>(data), dataLen);
#    endif /* AX_TARGET_QT5 */

        /* reading the image header which contains image information */
#    if (JPEG_LIB_VERSION >= 90)
        // libjpeg 0.9 adds stricter types.
        jpeg_read_header(&cinfo, TRUE);
#    else
        jpeg_read_header(&cinfo, TRUE);
#    endif  //(JPEG_LIB_VERSION >= 90)

        // we only support RGB or grayscale
        if (cinfo.jpeg_color_space == JCS_GRAYSCALE)
        {
            _pixelFormat = backend::PixelFormat::L8;
        }
        else
        {
            cinfo.out_color_space = JCS_RGB;
            _pixelFormat          = backend::PixelFormat::RGB8;
        }

        /* Start decompression jpeg here */
        jpeg_start_decompress(&cinfo);

        /* init image info */
        _width  = cinfo.output_width;
        _height = cinfo.output_height;

        _dataLen = cinfo.output_width * cinfo.output_height * cinfo.output_components;
        _data    = static_cast<uint8_t*>(malloc(_dataLen));
        AX_BREAK_IF(!_data);

        /* now actually read the jpeg into the raw buffer */
        /* read one scan line at a time */
        while (cinfo.output_scanline < cinfo.output_height)
        {
            row_pointer[0] = _data + location;
            location += cinfo.output_width * cinfo.output_components;
            jpeg_read_scanlines(&cinfo, row_pointer, 1);
        }

        /* When read image file with broken data, jpeg_finish_decompress() may cause error.
         * Besides, jpeg_destroy_decompress() shall deallocate and release all memory associated
         * with the decompression object.
         * So it doesn't need to call jpeg_finish_decompress().
         */
        // jpeg_finish_decompress( &cinfo );
        jpeg_destroy_decompress(&cinfo);
        /* wrap up decompression, destroy objects, free pointers and close open files */
        ret = true;
    } while (0);

    return ret;
#else
    AXLOG("jpeg is not enabled, please enable it in ccConfig.h");
    return false;
#endif  // AX_USE_JPEG
}

bool Image::initWithPngData(uint8_t* data, ssize_t dataLen)
{
#if AX_USE_PNG
    // length of bytes to check if it is a valid png file
#    define PNGSIGSIZE 8
    bool ret                    = false;
    png_byte header[PNGSIGSIZE] = {0};
    png_structp png_ptr         = 0;
    png_infop info_ptr          = 0;

    do
    {
        // png header len is 8 bytes
        AX_BREAK_IF(dataLen < PNGSIGSIZE);

        // check the data is png or not
        memcpy(header, data, PNGSIGSIZE);
        AX_BREAK_IF(png_sig_cmp(header, 0, PNGSIGSIZE));

        // init png_struct
        png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, 0, 0, 0);
        AX_BREAK_IF(!png_ptr);

        // init png_info
        info_ptr = png_create_info_struct(png_ptr);
        AX_BREAK_IF(!info_ptr);

        AX_BREAK_IF(setjmp(png_jmpbuf(png_ptr)));

        // set the read call back function
        tImageSource imageSource;
        imageSource.data   = (uint8_t*)data;
        imageSource.size   = dataLen;
        imageSource.offset = 0;
        png_set_read_fn(png_ptr, &imageSource, pngReadCallback);

        // read png header info

        // read png file info
        png_read_info(png_ptr, info_ptr);

        _width                 = png_get_image_width(png_ptr, info_ptr);
        _height                = png_get_image_height(png_ptr, info_ptr);
        png_byte bit_depth     = png_get_bit_depth(png_ptr, info_ptr);
        png_uint_32 color_type = png_get_color_type(png_ptr, info_ptr);

        // AXLOG("color type %u", color_type);

        // force palette images to be expanded to 24-bit RGB
        // it may include alpha channel
        if (color_type == PNG_COLOR_TYPE_PALETTE)
        {
            png_set_palette_to_rgb(png_ptr);
        }
        // low-bit-depth grayscale images are to be expanded to 8 bits
        if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
        {
            bit_depth = 8;
            png_set_expand_gray_1_2_4_to_8(png_ptr);
        }
        // expand any tRNS chunk data into a full alpha channel
        if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS))
        {
            png_set_tRNS_to_alpha(png_ptr);
        }
        // reduce images with 16-bit samples to 8 bits
        if (bit_depth == 16)
        {
            png_set_strip_16(png_ptr);
        }

        // Expanded earlier for grayscale, now take care of palette and rgb
        if (bit_depth < 8)
        {
            png_set_packing(png_ptr);
        }
        // update info
        png_read_update_info(png_ptr, info_ptr);
        color_type = png_get_color_type(png_ptr, info_ptr);

        switch (color_type)
        {
        case PNG_COLOR_TYPE_GRAY:
            _pixelFormat = backend::PixelFormat::L8;
            break;
        case PNG_COLOR_TYPE_GRAY_ALPHA:
            _pixelFormat = backend::PixelFormat::LA8;
            break;
        case PNG_COLOR_TYPE_RGB:
            _pixelFormat = backend::PixelFormat::RGB8;
            break;
        case PNG_COLOR_TYPE_RGB_ALPHA:
            _pixelFormat = backend::PixelFormat::RGBA8;
            break;
        default:
            break;
        }

        // read png data
        png_size_t rowbytes;
        png_bytep* row_pointers = (png_bytep*)malloc(sizeof(png_bytep) * _height);

        rowbytes = png_get_rowbytes(png_ptr, info_ptr);

        _dataLen = rowbytes * _height;
        _data    = static_cast<uint8_t*>(malloc(_dataLen));
        if (!_data)
        {
            if (row_pointers != nullptr)
            {
                free(row_pointers);
            }
            break;
        }

        for (unsigned short i = 0; i < _height; ++i)
        {
            row_pointers[i] = _data + i * rowbytes;
        }
        png_read_image(png_ptr, row_pointers);

        png_read_end(png_ptr, nullptr);

        // premultiplied alpha for RGBA8888
        if (color_type == PNG_COLOR_TYPE_RGB_ALPHA)
        {
            if (PNG_PREMULTIPLIED_ALPHA_ENABLED)
            {
                premultiplyAlpha();
            }
            else
            {
                // if PNG_PREMULTIPLIED_ALPHA_ENABLED == false && AX_ENABLE_PREMULTIPLIED_ALPHA != 0,
                // you must do PMA at shader, such as modify positionTextureColor.frag
                _hasPremultipliedAlpha = !!AX_ENABLE_PREMULTIPLIED_ALPHA;
            }
        }

        if (row_pointers != nullptr)
        {
            free(row_pointers);
        }

        ret = true;
    } while (0);

    if (png_ptr)
    {
        png_destroy_read_struct(&png_ptr, (info_ptr) ? &info_ptr : 0, 0);
    }
    return ret;
#else
    AXLOG("png is not enabled, please enable it in ccConfig.h");
    return false;
#endif  // AX_USE_PNG
}

bool Image::initWithBmpData(uint8_t* data, ssize_t dataLen)
{
    const int nrChannels = 4;
    _data                = stbi_load_from_memory(data, dataLen, &_width, &_height, nullptr, nrChannels);
    if (_data)
    {
        _dataLen     = _width * _height * nrChannels;
        _fileType    = Format::BMP;
        _pixelFormat = backend::PixelFormat::RGBA8;
        return true;
    }
    return false;
}

bool Image::initWithWebpData(uint8_t* data, ssize_t dataLen)
{
#if AX_USE_WEBP
    bool ret = false;

    do
    {
        WebPDecoderConfig config;
        if (WebPInitDecoderConfig(&config) == 0)
            break;
        if (WebPGetFeatures(static_cast<const uint8_t*>(data), dataLen, &config.input) != VP8_STATUS_OK)
            break;
        if (config.input.width == 0 || config.input.height == 0)
            break;

        config.output.colorspace = config.input.has_alpha ? MODE_rgbA : MODE_RGB;
        _pixelFormat             = config.input.has_alpha ? backend::PixelFormat::RGBA8 : backend::PixelFormat::RGB8;
        _width                   = config.input.width;
        _height                  = config.input.height;

        // we ask webp to give data with premultiplied alpha
        _hasPremultipliedAlpha = (config.input.has_alpha != 0);

        _dataLen = _width * _height * (config.input.has_alpha ? 4 : 3);
        _data    = static_cast<uint8_t*>(malloc(_dataLen));

        config.output.u.RGBA.rgba        = static_cast<uint8_t*>(_data);
        config.output.u.RGBA.stride      = _width * (config.input.has_alpha ? 4 : 3);
        config.output.u.RGBA.size        = _dataLen;
        config.output.is_external_memory = 1;

        if (WebPDecode(static_cast<const uint8_t*>(data), dataLen, &config) != VP8_STATUS_OK)
        {
            free(_data);
            _data = nullptr;
            break;
        }

        ret = true;
    } while (0);
    return ret;
#else
    AXLOG("webp is not enabled, please enable it in ccConfig.h");
    return false;
#endif  // AX_USE_WEBP
}

bool Image::initWithTGAData(tImageTGA* tgaData)
{
    bool ret = false;

    do
    {
        AX_BREAK_IF(tgaData == nullptr);

        // tgaLoadBuffer only support type 2, 3, 10
        if (2 == tgaData->type || 10 == tgaData->type)
        {
            // true color
            // unsupported RGB555
            if (tgaData->pixelDepth == 16)
            {
                _pixelFormat = backend::PixelFormat::RGB5A1;
            }
            else if (tgaData->pixelDepth == 24)
            {
                _pixelFormat = backend::PixelFormat::RGB8;
            }
            else if (tgaData->pixelDepth == 32)
            {
                _pixelFormat = backend::PixelFormat::RGBA8;
            }
            else
            {
                AXLOG("Image WARNING: unsupported true color tga data pixel format. FILE: %s", _filePath.c_str());
                break;
            }
        }
        else if (3 == tgaData->type)
        {
            // gray
            if (8 == tgaData->pixelDepth)
            {
                _pixelFormat = backend::PixelFormat::L8;
            }
            else
            {
                // actually this won't happen, if it happens, maybe the image file is not a tga
                AXLOG("Image WARNING: unsupported gray tga data pixel format. FILE: %s", _filePath.c_str());
                break;
            }
        }

        _width    = tgaData->width;
        _height   = tgaData->height;
        _data     = tgaData->imageData;
        _dataLen  = _width * _height * tgaData->pixelDepth / 8;
        _fileType = Format::TGA;

        ret = true;

    } while (false);

    if (ret)
    {
        if (FileUtils::getInstance()->getFileExtension(_filePath) != ".tga")
        {
            AXLOG("Image WARNING: the image file suffix is not tga, but parsed as a tga image file. FILE: %s",
                  _filePath.c_str());
        }
    }
    else
    {
        if (tgaData && tgaData->imageData != nullptr)
        {
            free(tgaData->imageData);
            _data = nullptr;
        }
    }

    return ret;
}

bool Image::initWithPVRv2Data(uint8_t* data, ssize_t dataLen, bool ownData)
{
    int blockSize = 0, widthBlocks = 0, heightBlocks = 0;
    int width = 0, height = 0;

    // Cast first sizeof(PVRTexHeader) bytes of data stream as PVRTexHeader
    const PVRv2TexHeader* header = static_cast<const PVRv2TexHeader*>(static_cast<const void*>(data));

    // Make sure that tag is in correct formatting
    if (memcmp(&header->pvrTag, gPVRTexIdentifier, strlen(gPVRTexIdentifier)) != 0)
    {
        return false;
    }

    Configuration* configuration = Configuration::getInstance();

    // can not detect the premultiplied alpha from pvr file, use _PVRHaveAlphaPremultiplied instead.
    _hasPremultipliedAlpha = isCompressedImageHavePMA(CompressedImagePMAFlag::PVR);

    unsigned int flags                 = AX_SWAP_INT32_LITTLE_TO_HOST(header->flags);
    PVR2TexturePixelFormat formatFlags = static_cast<PVR2TexturePixelFormat>(flags & PVR_TEXTURE_FLAG_TYPE_MASK);
    bool flipped                       = (flags & (unsigned int)PVR2TextureFlag::VerticalFlip) ? true : false;
    if (flipped)
    {
        AXLOG("cocos2d: WARNING: Image is flipped. Regenerate it using PVRTexTool");
    }

    if (!configuration->supportsNPOT() && (static_cast<int>(header->width) != ccNextPOT(header->width) ||
                                           static_cast<int>(header->height) != ccNextPOT(header->height)))
    {
        AXLOG("cocos2d: ERROR: Loading an NPOT texture (%dx%d) but is not supported on this device", header->width,
              header->height);
        return false;
    }

    if (!testFormatForPvr2TCSupport(formatFlags))
    {
        AXLOG("cocos2d: WARNING: Unsupported PVR Pixel Format: 0x%02X. Re-encode it with a OpenGL pixel format variant",
              (int)formatFlags);
        return false;
    }

    if (v2_pixel_formathash.find(formatFlags) == v2_pixel_formathash.end())
    {
        AXLOG("cocos2d: WARNING: Unsupported PVR Pixel Format: 0x%02X. Re-encode it with a OpenGL pixel format variant",
              (int)formatFlags);
        return false;
    }

    auto pixelFormat = getDevicePVRPixelFormat(v2_pixel_formathash.at(formatFlags));
    auto& info       = backend::PixelFormatUtils::getFormatDescriptor(pixelFormat);
    int bpp          = info.bpp;
    if (!bpp)
    {
        AXLOG("cocos2d: WARNING: Unsupported PVR Pixel Format: 0x%02X. Re-encode it with a OpenGL pixel format variant",
              (int)formatFlags);
        return false;
    }

    _pixelFormat = pixelFormat;

    // Reset num of mipmaps
    _numberOfMipmaps = 0;

    // Get size of mipmap
    _width = width = AX_SWAP_INT32_LITTLE_TO_HOST(header->width);
    _height = height = AX_SWAP_INT32_LITTLE_TO_HOST(header->height);

    // Move by size of header
    const int pixelOffset = sizeof(PVRv2TexHeader);
    uint8_t* pixelData    = data + pixelOffset;

    int dataOffset = 0, dataSize = 0;
    // Get ptr to where data starts..
    int dataLength = AX_SWAP_INT32_LITTLE_TO_HOST(header->dataLength);

    // Calculate the data size for each texture level and respect the minimum number of blocks
    while (dataOffset < dataLength)
    {
        switch (formatFlags)
        {
        case PVR2TexturePixelFormat::PVRTC2BPP_RGBA:
            if (!Configuration::getInstance()->supportsPVRTC())
            {
                AXLOG("cocos2d: Hardware PVR decoder not present. Using software decoder");
                _unpack                            = true;
                _mipmaps[_numberOfMipmaps].len     = width * height * 4;
                _mipmaps[_numberOfMipmaps].address = (uint8_t*)malloc(width * height * 4);
                PVRTDecompressPVRTC(pixelData + dataOffset, width, height, _mipmaps[_numberOfMipmaps].address, true);
                bpp = 2;
            }
            blockSize    = 8 * 4;  // Pixel by pixel block size for 2bpp
            widthBlocks  = width / 8;
            heightBlocks = height / 4;
            break;
        case PVR2TexturePixelFormat::PVRTC4BPP_RGBA:
            if (!Configuration::getInstance()->supportsPVRTC())
            {
                AXLOG("cocos2d: Hardware PVR decoder not present. Using software decoder");
                _unpack                            = true;
                _mipmaps[_numberOfMipmaps].len     = width * height * 4;
                _mipmaps[_numberOfMipmaps].address = (uint8_t*)malloc(width * height * 4);
                PVRTDecompressPVRTC(pixelData + dataOffset, width, height, _mipmaps[_numberOfMipmaps].address, false);
                bpp = 4;
            }
            blockSize    = 4 * 4;  // Pixel by pixel block size for 4bpp
            widthBlocks  = width / 4;
            heightBlocks = height / 4;
            break;
        case PVR2TexturePixelFormat::BGRA8888:
            if (!Configuration::getInstance()->supportsBGRA8888())
            {
                AXLOG("cocos2d: Image. BGRA8888 not supported on this device");
                return false;
            }
        default:
            blockSize    = 1;
            widthBlocks  = width;
            heightBlocks = height;
            break;
        }

        // Clamp to minimum number of blocks
        if (widthBlocks < 2)
        {
            widthBlocks = 2;
        }
        if (heightBlocks < 2)
        {
            heightBlocks = 2;
        }

        dataSize         = widthBlocks * heightBlocks * ((blockSize * bpp) / 8);
        int packetLength = (dataLength - dataOffset);
        packetLength     = packetLength > dataSize ? dataSize : packetLength;

        // Make record to the mipmaps array and increment counter
        if (!_unpack)
        {
            _mipmaps[_numberOfMipmaps].address = pixelData + dataOffset;
            _mipmaps[_numberOfMipmaps].len     = packetLength;
        }
        _numberOfMipmaps++;

        dataOffset += packetLength;

        // Update width and height to the next lower power of two
        width  = MAX(width >> 1, 1);
        height = MAX(height >> 1, 1);
    }

    if (!_unpack)
    {  // hardware decoder, hold data directly
        forwardPixels(data, dataLen, pixelOffset, ownData);
    }
    else
    {
        _data    = _mipmaps[0].address;
        _dataLen = _mipmaps[0].len;
    }

    return true;
}

bool Image::initWithPVRv3Data(uint8_t* data, ssize_t dataLen, bool ownData)
{
    if (static_cast<size_t>(dataLen) < sizeof(PVRv3TexHeader))
    {
        return false;
    }

    const PVRv3TexHeader* header = static_cast<const PVRv3TexHeader*>(static_cast<const void*>(data));

    // validate version
    if (AX_SWAP_INT32_BIG_TO_HOST(header->version) != 0x50565203)
    {
        AXLOG("cocos2d: WARNING: pvr file version mismatch");
        return false;
    }

    // parse pixel format
    PVR3TexturePixelFormat pixelFormat = static_cast<PVR3TexturePixelFormat>(header->pixelFormat);

    if (!testFormatForPvr3TCSupport(pixelFormat))
    {
        AXLOG(
            "cocos2d: WARNING: Unsupported PVR Pixel Format: 0x%016llX. Re-encode it with a OpenGL pixel format "
            "variant",
            static_cast<unsigned long long>(pixelFormat));
        return false;
    }

    if (v3_pixel_formathash.find(pixelFormat) == v3_pixel_formathash.end())
    {
        AXLOG(
            "cocos2d: WARNING: Unsupported PVR Pixel Format: 0x%016llX. Re-encode it with a OpenGL pixel format "
            "variant",
            static_cast<unsigned long long>(pixelFormat));
        return false;
    }

    auto finalPixelFormat = getDevicePVRPixelFormat(v3_pixel_formathash.at(pixelFormat));
    auto& info            = backend::PixelFormatUtils::getFormatDescriptor(finalPixelFormat);
    int bpp               = info.bpp;
    if (!info.bpp)
    {
        AXLOG(
            "cocos2d: WARNING: Unsupported PVR Pixel Format: 0x%016llX. Re-encode it with a OpenGL pixel format "
            "variant",
            static_cast<unsigned long long>(pixelFormat));
        return false;
    }

    _pixelFormat = finalPixelFormat;

    // flags
    int flags = AX_SWAP_INT32_LITTLE_TO_HOST(header->flags);

    // PVRv3 specifies premultiply alpha in a flag -- should always respect this in PVRv3 files
    if (flags & (unsigned int)PVR3TextureFlag::PremultipliedAlpha)
    {
        _hasPremultipliedAlpha = true;
    }

    // sizing
    int width  = AX_SWAP_INT32_LITTLE_TO_HOST(header->width);
    int height = AX_SWAP_INT32_LITTLE_TO_HOST(header->height);
    _width     = width;
    _height    = height;

    const int pixelOffset = (sizeof(PVRv3TexHeader) + header->metadataLength);
    uint8_t* pixelData    = data + pixelOffset;
    int pixelLen          = dataLen - pixelOffset;

    int dataOffset = 0, dataSize = 0;
    int blockSize = 0, widthBlocks = 0, heightBlocks = 0;

    _numberOfMipmaps = header->numberOfMipmaps;
    AXASSERT(_numberOfMipmaps < MIPMAP_MAX,
             "Image: Maximum number of mimpaps reached. Increase the AX_MIPMAP_MAX value");

    for (int i = 0; i < _numberOfMipmaps; i++)
    {
        switch ((PVR3TexturePixelFormat)pixelFormat)
        {
        case PVR3TexturePixelFormat::PVRTC2BPP_RGB:
        case PVR3TexturePixelFormat::PVRTC2BPP_RGBA:
            if (!Configuration::getInstance()->supportsPVRTC())
            {
                AXLOG("cocos2d: Hardware PVR decoder not present. Using software decoder");
                _unpack             = true;
                _mipmaps[i].len     = width * height * 4;
                _mipmaps[i].address = (uint8_t*)malloc(width * height * 4);
                PVRTDecompressPVRTC(pixelData + dataOffset, width, height, _mipmaps[i].address, true);
                bpp = 2;
            }
            blockSize    = 8 * 4;  // Pixel by pixel block size for 2bpp
            widthBlocks  = width / 8;
            heightBlocks = height / 4;
            break;
        case PVR3TexturePixelFormat::PVRTC4BPP_RGB:
        case PVR3TexturePixelFormat::PVRTC4BPP_RGBA:
            if (!Configuration::getInstance()->supportsPVRTC())
            {
                AXLOG("cocos2d: Hardware PVR decoder not present. Using software decoder");
                _unpack             = true;
                _mipmaps[i].len     = width * height * 4;
                _mipmaps[i].address = (uint8_t*)malloc(width * height * 4);
                PVRTDecompressPVRTC(pixelData + dataOffset, width, height, _mipmaps[i].address, false);
                bpp = 4;
            }
            blockSize    = 4 * 4;  // Pixel by pixel block size for 4bpp
            widthBlocks  = width / 4;
            heightBlocks = height / 4;
            break;
        case PVR3TexturePixelFormat::ETC1:
            if (!Configuration::getInstance()->supportsETC1())
            {
                AXLOG("cocos2d: Hardware ETC1 decoder not present. Using software decoder");
                const int bytePerPixel = 4;
                _unpack                = true;
                _mipmaps[i].len        = width * height * bytePerPixel;
                _mipmaps[i].address    = (uint8_t*)malloc(width * height * bytePerPixel);
                if (etc2_decode_image(ETC2_RGB_NO_MIPMAPS, pixelData + dataOffset,
                                      static_cast<etc1_byte*>(_mipmaps[i].address), width, height) != 0)
                {
                    return false;
                }
            }
            blockSize    = 4 * 4;  // Pixel by pixel block size for 4bpp
            widthBlocks  = width / 4;
            heightBlocks = height / 4;
            break;
        case PVR3TexturePixelFormat::BGRA8888:
            if (!Configuration::getInstance()->supportsBGRA8888())
            {
                AXLOG("cocos2d: Image. BGRA8888 not supported on this device");
                return false;
            }
        default:
            blockSize    = 1;
            widthBlocks  = width;
            heightBlocks = height;
            break;
        }

        // Clamp to minimum number of blocks
        if (widthBlocks < 2)
        {
            widthBlocks = 2;
        }
        if (heightBlocks < 2)
        {
            heightBlocks = 2;
        }

        dataSize          = widthBlocks * heightBlocks * ((blockSize * bpp) / 8);
        auto packetLength = pixelLen - dataOffset;
        packetLength      = packetLength > dataSize ? dataSize : packetLength;

        if (!_unpack)
        {
            _mipmaps[i].address = pixelData + dataOffset;
            _mipmaps[i].len     = static_cast<int>(packetLength);
        }

        dataOffset += packetLength;
        AXASSERT(dataOffset <= pixelLen, "Image: Invalid length");

        width  = MAX(width >> 1, 1);
        height = MAX(height >> 1, 1);
    }

    if (!_unpack)
    {
        forwardPixels(data, dataLen, pixelOffset, ownData);
    }
    else
    {
        _data    = _mipmaps[0].address;
        _dataLen = _mipmaps[0].len;
    }

    return true;
}

bool Image::initWithETCData(uint8_t* data, ssize_t dataLen, bool ownData)
{
    const etc1_byte* header = static_cast<const etc1_byte*>(data);
    uint32_t pixelOffset;
    // check the data
    if (etc1_pkm_is_valid(header))
    {
        _width  = etc1_pkm_get_width(header);
        _height = etc1_pkm_get_height(header);

        if (0 == _width || 0 == _height)
            return false;
        pixelOffset = ETC_PKM_HEADER_SIZE;
    }
    else
    {  // we can safe trait as KTX v1 header
        auto header = (KTXv1Header*)data;
        _width      = header->pixelWidth;
        _height     = header->pixelHeight;

        if (0 == _width || 0 == _height)
            return false;
        pixelOffset = KTX_V1_HEADER_SIZE + header->bytesOfKeyValueData + 4;
    }

    // GL_ETC1_RGB8_OES is not available in any desktop GL extension but the compression
    // format is forwards compatible so just use the ETC2 format.
    backend::PixelFormat compressedFormat;
    if (Configuration::getInstance()->supportsETC1())
        compressedFormat = backend::PixelFormat::ETC1;
    else if (Configuration::getInstance()->supportsETC2())
        compressedFormat = backend::PixelFormat::ETC2_RGB;
    else
        compressedFormat = backend::PixelFormat::NONE;

    if (compressedFormat != backend::PixelFormat::NONE)
    {
        _pixelFormat = compressedFormat;
        forwardPixels(data, dataLen, pixelOffset, ownData);
        return true;
    }
    else
    {
        AXLOG("cocos2d: Hardware ETC1 decoder not present. Using software decoder");

        _dataLen = _width * _height * 4;
        _data    = static_cast<uint8_t*>(malloc(_dataLen));
        if (etc2_decode_image(ETC2_RGB_NO_MIPMAPS, static_cast<const uint8_t*>(data) + pixelOffset,
                              static_cast<etc2_byte*>(_data), _width, _height) == 0)
        {  // if it is not gles or device do not support ETC1, decode texture by software
           // directly decode ETC1_RGB to RGBA8888
            _pixelFormat = backend::PixelFormat::RGBA8;
            return true;
        }

        // software decode fail, release pixels data
        AX_SAFE_FREE(_data);
        _dataLen = 0;
        return false;
    }
}

bool Image::initWithETC2Data(uint8_t* data, ssize_t dataLen, bool ownData)
{
    const etc2_byte* header = static_cast<const etc2_byte*>(data);

    do
    {
        uint32_t format, pixelOffset;
        // check the data
        if (etc2_pkm_is_valid(header))
        {

            _width  = etc2_pkm_get_width(header);
            _height = etc2_pkm_get_height(header);

            if (0 == _width || 0 == _height)
                break;

            format      = etc2_pkm_get_format(header);
            pixelOffset = ETC2_PKM_HEADER_SIZE;
        }
        else
        {  // we can safe trait as KTX v1 header
            auto header = (KTXv1Header*)data;
            _width      = header->pixelWidth;
            _height     = header->pixelHeight;

            if (0 == _width || 0 == _height)
                break;

            format      = header->glInternalFormat == KTXv1Header::InternalFormat::ETC2_RGBA8 ? ETC2_RGBA_NO_MIPMAPS
                                                                                              : ETC2_RGB_NO_MIPMAPS;
            pixelOffset = KTX_V1_HEADER_SIZE + header->bytesOfKeyValueData + 4;
        }

        // We only support ETC2_RGBA_NO_MIPMAPS and ETC2_RGB_NO_MIPMAPS
        assert(format == ETC2_RGBA_NO_MIPMAPS || format == ETC2_RGB_NO_MIPMAPS);

        if (Configuration::getInstance()->supportsETC2())
        {
            _pixelFormat =
                format == ETC2_RGBA_NO_MIPMAPS ? backend::PixelFormat::ETC2_RGBA : backend::PixelFormat::ETC2_RGB;

            forwardPixels(data, dataLen, pixelOffset, ownData);
        }
        else
        {
            AXLOG("cocos2d: Hardware ETC2 decoder not present. Using software decoder");

            // if device do not support ETC2, decode texture by software
            // etc2_decode_image always decode to RGBA8888
            _dataLen = _width * _height * 4;
            _data    = static_cast<uint8_t*>(malloc(_dataLen));
            if (UTILS_UNLIKELY(etc2_decode_image(format, static_cast<const uint8_t*>(data) + pixelOffset,
                                                 static_cast<etc2_byte*>(_data), _width, _height) != 0))
            {
                // software decode fail, release pixels data
                AX_SAFE_FREE(_data);
                _dataLen = 0;
                break;
            }
            _pixelFormat = backend::PixelFormat::RGBA8;
        }

        _hasPremultipliedAlpha = isCompressedImageHavePMA(CompressedImagePMAFlag::ETC2);

        return true;
    } while (false);

    return false;
}

bool Image::initWithASTCData(uint8_t* data, ssize_t dataLen, bool ownData)
{
    astc_header* hdr = (astc_header*)data;

    do
    {
        // Ensure these are not zero to avoid div by zero
        unsigned int block_x = (std::max)((unsigned int)hdr->block_x, 1u);
        unsigned int block_y = (std::max)((unsigned int)hdr->block_y, 1u);
        // unsigned int block_z = std::max((unsigned int) hdr->block_z, 1u);

        unsigned int dim_x = astc_unpack_bytes(hdr->dim_x[0], hdr->dim_x[1], hdr->dim_x[2], 0);
        unsigned int dim_y = astc_unpack_bytes(hdr->dim_y[0], hdr->dim_y[1], hdr->dim_y[2], 0);
        // unsigned int dim_z = astc_unpack_bytes(hdr->dim_z[0], hdr->dim_z[1], hdr->dim_z[2], 0);

        if (dim_x == 0 || dim_y == 0)
            break;

        _width  = dim_x;
        _height = dim_y;

        if (block_x < 4 || block_y < 4)
        {
            AXLOG("cocos2d: The ASTC block with and height should be >= 4");
            break;
        }

        if (Configuration::getInstance()->supportsASTC())
        {
            if (block_x == 4 && block_y == 4)
            {
                _pixelFormat = backend::PixelFormat::ASTC4x4;
            }
            else if (block_x == 5 && block_y == 5)
            {
                _pixelFormat = backend::PixelFormat::ASTC5x5;
            }
            else if (block_x == 6 && block_y == 6)
            {
                _pixelFormat = backend::PixelFormat::ASTC6x6;
            }
            else if (block_x == 8 && block_y == 5)
            {
                _pixelFormat = backend::PixelFormat::ASTC8x5;
            }
            else if (block_x == 8 && block_y == 6)
            {
                _pixelFormat = backend::PixelFormat::ASTC8x6;
            }
            else if (block_x == 8 && block_y == 8)
            {
                _pixelFormat = backend::PixelFormat::ASTC8x8;
            }
            else if (block_x == 10 && block_y == 5)
            {
                _pixelFormat = backend::PixelFormat::ASTC10x5;
            }

            forwardPixels(data, dataLen, ASTC_HEAD_SIZE, ownData);
        }
        else
        {
            AXLOG("cocos2d: Hardware ASTC decoder not present. Using software decoder");

            _dataLen = _width * _height * 4;
            _data    = static_cast<uint8_t*>(malloc(_dataLen));
            if (UTILS_UNLIKELY(astc_decompress_image(static_cast<const uint8_t*>(data) + ASTC_HEAD_SIZE,
                                                     dataLen - ASTC_HEAD_SIZE, _data, _width, _height, block_x,
                                                     block_y) != 0))
            {
                AX_SAFE_FREE(_data);
                _dataLen = 0;
                break;
            }

            _pixelFormat = backend::PixelFormat::RGBA8;
        }

        _hasPremultipliedAlpha = isCompressedImageHavePMA(CompressedImagePMAFlag::ASTC);

        return true;
    } while (false);

    return false;
}

bool Image::initWithS3TCData(uint8_t* data, ssize_t dataLen, bool ownData)
{
    const uint32_t FOURCC_DXT1 = makeFourCC('D', 'X', 'T', '1');
    const uint32_t FOURCC_DXT3 = makeFourCC('D', 'X', 'T', '3');
    const uint32_t FOURCC_DXT5 = makeFourCC('D', 'X', 'T', '5');

    /* load the .dds file */

    S3TCTexHeader* header = (S3TCTexHeader*)data;
    _width                = header->ddsd.width;
    _height               = header->ddsd.height;
    _numberOfMipmaps      = MAX(
             1,
             header->ddsd.DUMMYUNIONNAMEN2
                 .mipMapCount);  // if dds header reports 0 mipmaps, set to 1 to force correct software decoding (if needed).
    _dataLen      = 0;
    int blockSize = (FOURCC_DXT1 == header->ddsd.DUMMYUNIONNAMEN4.ddpfPixelFormat.fourCC) ? 8 : 16;

    /* calculate the dataLen */

    int width  = _width;
    int height = _height;

    const int pixelOffset = sizeof(S3TCTexHeader);
    uint8_t* pixelData    = data + pixelOffset;

    bool hardware = Configuration::getInstance()->supportsS3TC();
    /* if hardware supports s3tc, set pixelformat before loading mipmaps, to support non-mipmapped textures  */
    if (hardware)
    {  // decode texture through hardware

        if (FOURCC_DXT1 == header->ddsd.DUMMYUNIONNAMEN4.ddpfPixelFormat.fourCC)
        {
            _pixelFormat = backend::PixelFormat::S3TC_DXT1;
        }
        else if (FOURCC_DXT3 == header->ddsd.DUMMYUNIONNAMEN4.ddpfPixelFormat.fourCC)
        {
            _pixelFormat = backend::PixelFormat::S3TC_DXT3;
        }
        else if (FOURCC_DXT5 == header->ddsd.DUMMYUNIONNAMEN4.ddpfPixelFormat.fourCC)
        {
            _pixelFormat = backend::PixelFormat::S3TC_DXT5;
        }
    }
    else
    {  // will software decode
        _pixelFormat = backend::PixelFormat::RGBA8;

        // prepare data for software decompress
        for (int i = 0; i < _numberOfMipmaps && (width || height); ++i)
        {
            if (width == 0)
                width = 1;
            if (height == 0)
                height = 1;

            _dataLen += (height * width * 4);

            width >>= 1;
            height >>= 1;
        }
        _data = static_cast<uint8_t*>(malloc(_dataLen));
    }

    /* load the mipmaps */
    int encodeOffset = 0;
    int decodeOffset = 0;
    width            = _width;
    height           = _height;

    for (int i = 0; i < _numberOfMipmaps && (width || height); ++i)
    {
        if (width == 0)
            width = 1;
        if (height == 0)
            height = 1;

        int size = ((width + 3) / 4) * ((height + 3) / 4) * blockSize;

        if (Configuration::getInstance()->supportsS3TC())
        {  // decode texture through hardware
            _mipmaps[i].address = (uint8_t*)pixelData + encodeOffset;
            _mipmaps[i].len     = size;
        }
        else
        {  // if it is not gles or device do not support S3TC, decode texture by software

            AXLOG("cocos2d: Hardware S3TC decoder not present. Using software decoder");

            int bytePerPixel    = 4;
            unsigned int stride = width * bytePerPixel;

            std::vector<uint8_t> decodeImageData(stride * height);
            if (FOURCC_DXT1 == header->ddsd.DUMMYUNIONNAMEN4.ddpfPixelFormat.fourCC)
            {
                s3tc_decode(pixelData + encodeOffset, &decodeImageData[0], width, height, S3TCDecodeFlag::DXT1);
            }
            else if (FOURCC_DXT3 == header->ddsd.DUMMYUNIONNAMEN4.ddpfPixelFormat.fourCC)
            {
                s3tc_decode(pixelData + encodeOffset, &decodeImageData[0], width, height, S3TCDecodeFlag::DXT3);
            }
            else if (FOURCC_DXT5 == header->ddsd.DUMMYUNIONNAMEN4.ddpfPixelFormat.fourCC)
            {
                s3tc_decode(pixelData + encodeOffset, &decodeImageData[0], width, height, S3TCDecodeFlag::DXT5);
            }

            _mipmaps[i].address = (uint8_t*)_data + decodeOffset;
            _mipmaps[i].len     = (stride * height);
            memcpy((void*)_mipmaps[i].address, (void*)&decodeImageData[0], _mipmaps[i].len);
            decodeOffset += stride * height;
        }

        encodeOffset += size;
        width >>= 1;
        height >>= 1;
    }

    /* end load the mipmaps */

    if (hardware)
    {
        forwardPixels(data, dataLen, pixelOffset, ownData);
    }

    return true;
}

bool Image::initWithATITCData(uint8_t* data, ssize_t dataLen, bool ownData)
{
    /* load the .ktx file */
    KTXv1Header* header = (KTXv1Header*)data;
    _width              = header->pixelWidth;
    _height             = header->pixelHeight;
    _numberOfMipmaps    = header->numberOfMipmapLevels;

    int blockSize = 0;
    switch (header->glInternalFormat)
    {
    case KTXv1Header::InternalFormat::ATC_RGB_AMD:
        blockSize = 8;
        break;
    case KTXv1Header::InternalFormat::ATC_RGBA_EXPLICIT_ALPHA_AMD:
        blockSize = 16;
        break;
    case KTXv1Header::InternalFormat::ATC_RGBA_INTERPOLATED_ALPHA_AMD:
        blockSize = 16;
        break;
    default:
        break;
    }

    /* pixelData point to the compressed data address */
    int pixelOffset    = KTX_V1_HEADER_SIZE + header->bytesOfKeyValueData + 4;
    uint8_t* pixelData = (uint8_t*)data + pixelOffset;

    /* calculate the dataLen */
    int width  = _width;
    int height = _height;

    bool hardware = Configuration::getInstance()->supportsATITC();
    if (hardware)  // compressed data length
    {
        AXLOG("this is atitc H decode");

        switch (header->glInternalFormat)
        {
        case KTXv1Header::InternalFormat::ATC_RGB_AMD:
            _pixelFormat = backend::PixelFormat::ATC_RGB;
            break;
        case KTXv1Header::InternalFormat::ATC_RGBA_EXPLICIT_ALPHA_AMD:
            _pixelFormat = backend::PixelFormat::ATC_EXPLICIT_ALPHA;
            break;
        case KTXv1Header::InternalFormat::ATC_RGBA_INTERPOLATED_ALPHA_AMD:
            _pixelFormat = backend::PixelFormat::ATC_INTERPOLATED_ALPHA;
            break;
        default:
            break;
        }
    }
    else  // decompressed data length
    {     /* if it is not gles or device do not support ATITC, decode texture by software */

        AXLOG("cocos2d: Hardware ATITC decoder not present. Using software decoder");

        _pixelFormat = backend::PixelFormat::RGBA8;

        for (int i = 0; i < _numberOfMipmaps && (width || height); ++i)
        {
            if (width == 0)
                width = 1;
            if (height == 0)
                height = 1;

            _dataLen += (height * width * 4);

            width >>= 1;
            height >>= 1;
        }
        _data = static_cast<uint8_t*>(malloc(_dataLen));
    }

    /* load the mipmaps */
    int encodeOffset = 0;
    int decodeOffset = 0;
    width            = _width;
    height           = _height;

    for (int i = 0; i < _numberOfMipmaps && (width || height); ++i)
    {
        if (width == 0)
            width = 1;
        if (height == 0)
            height = 1;

        int size = ((width + 3) / 4) * ((height + 3) / 4) * blockSize;

        if (hardware)
        {
            /* decode texture through hardware */
            _mipmaps[i].address = (uint8_t*)pixelData + encodeOffset;
            _mipmaps[i].len     = size;
        }
        else
        {
            int bytePerPixel    = 4;
            unsigned int stride = width * bytePerPixel;

            std::vector<uint8_t> decodeImageData(stride * height);
            switch (header->glInternalFormat)
            {
            case KTXv1Header::InternalFormat::ATC_RGB_AMD:
                atitc_decode(pixelData + encodeOffset, &decodeImageData[0], width, height, ATITCDecodeFlag::ATC_RGB);
                break;
            case KTXv1Header::InternalFormat::ATC_RGBA_EXPLICIT_ALPHA_AMD:
                atitc_decode(pixelData + encodeOffset, &decodeImageData[0], width, height,
                             ATITCDecodeFlag::ATC_EXPLICIT_ALPHA);
                break;
            case KTXv1Header::InternalFormat::ATC_RGBA_INTERPOLATED_ALPHA_AMD:
                atitc_decode(pixelData + encodeOffset, &decodeImageData[0], width, height,
                             ATITCDecodeFlag::ATC_INTERPOLATED_ALPHA);
                break;
            default:
                break;
            }

            _mipmaps[i].address = (uint8_t*)_data + decodeOffset;
            _mipmaps[i].len     = (stride * height);
            memcpy((void*)_mipmaps[i].address, (void*)&decodeImageData[0], _mipmaps[i].len);
            decodeOffset += stride * height;
        }

        encodeOffset += (size + 4);
        width >>= 1;
        height >>= 1;
    }
    /* end load the mipmaps */

    if (hardware)
    {
        forwardPixels(data, dataLen, pixelOffset, ownData);
    }

    return true;
}

bool Image::initWithPVRData(uint8_t* data, ssize_t dataLen, bool ownData)
{
    return initWithPVRv2Data(data, dataLen, ownData) || initWithPVRv3Data(data, dataLen, ownData);
}

void Image::forwardPixels(uint8_t* data, ssize_t dataLen, int offset, bool ownData)
{
    if (ownData)
    {
        _data    = data;
        _dataLen = dataLen;
        _offset  = offset;
    }
    else
    {
        _dataLen = dataLen - offset;
        _data    = (uint8_t*)malloc(_dataLen);
        memcpy(_data, data + offset, _dataLen);
    }
}

#if (AX_TARGET_PLATFORM != AX_PLATFORM_IOS)
bool Image::saveToFile(std::string_view filename, bool isToRGB)
{
    // only support for backend::PixelFormat::RGB8 or backend::PixelFormat::RGBA8 uncompressed data
    if (isCompressed() || (_pixelFormat != backend::PixelFormat::RGB8 && _pixelFormat != backend::PixelFormat::RGBA8))
    {
        AXLOG(
            "cocos2d: Image: saveToFile is only support for backend::PixelFormat::RGB8 or backend::PixelFormat::RGBA8 "
            "uncompressed data for now");
        return false;
    }

    std::string fileExtension = FileUtils::getInstance()->getFileExtension(filename);

    if (fileExtension == ".png")
    {
        return saveImageToPNG(filename, isToRGB);
    }
    else if (fileExtension == ".jpg")
    {
        return saveImageToJPG(filename);
    }
    else
    {
        AXLOG("cocos2d: Image: saveToFile no support file extension(only .png or .jpg) for file: %s", filename.data());
        return false;
    }
}
#endif

bool Image::saveImageToPNG(std::string_view filePath, bool isToRGB)
{
#if AX_USE_PNG
    bool ret = false;
    do
    {
        png_structp png_ptr;
        png_infop info_ptr;
        png_bytep* row_pointers;

        auto outStream = FileUtils::getInstance()->openFileStream(filePath, FileStream::Mode::WRITE);

        AX_BREAK_IF(nullptr == outStream);

        png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);

        if (nullptr == png_ptr)
        {
            outStream.reset();
            break;
        }

        info_ptr = png_create_info_struct(png_ptr);
        if (nullptr == info_ptr)
        {
            outStream.reset();
            png_destroy_write_struct(&png_ptr, nullptr);
            break;
        }
        if (setjmp(png_jmpbuf(png_ptr)))
        {
            outStream.reset();
            png_destroy_write_struct(&png_ptr, &info_ptr);
            break;
        }

        // png_init_io(png_ptr, outStream);
        png_set_write_fn(png_ptr, outStream.get(), pngWriteCallback, nullptr);

        if (!isToRGB && hasAlpha())
        {
            png_set_IHDR(png_ptr, info_ptr, _width, _height, 8, PNG_COLOR_TYPE_RGB_ALPHA, PNG_INTERLACE_NONE,
                         PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);
        }
        else
        {
            png_set_IHDR(png_ptr, info_ptr, _width, _height, 8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
                         PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);
        }

        png_write_info(png_ptr, info_ptr);

        png_set_packing(png_ptr);

        row_pointers = (png_bytep*)malloc(_height * sizeof(png_bytep));
        if (row_pointers == nullptr)
        {
            outStream.reset();
            png_destroy_write_struct(&png_ptr, &info_ptr);
            break;
        }

        if (!hasAlpha())
        {
            for (int i = 0; i < (int)_height; i++)
            {
                row_pointers[i] = (png_bytep)_data + i * _width * 3;
            }

            png_write_image(png_ptr, row_pointers);

            free(row_pointers);
            row_pointers = nullptr;
        }
        else
        {
            if (isToRGB)
            {
                uint8_t* tempData = static_cast<uint8_t*>(malloc(_width * _height * 3));
                if (nullptr == tempData)
                {
                    outStream.reset();
                    png_destroy_write_struct(&png_ptr, &info_ptr);

                    free(row_pointers);
                    row_pointers = nullptr;
                    break;
                }

                for (int i = 0; i < _height; ++i)
                {
                    for (int j = 0; j < _width; ++j)
                    {
                        tempData[(i * _width + j) * 3]     = _data[(i * _width + j) * 4];
                        tempData[(i * _width + j) * 3 + 1] = _data[(i * _width + j) * 4 + 1];
                        tempData[(i * _width + j) * 3 + 2] = _data[(i * _width + j) * 4 + 2];
                    }
                }

                for (int i = 0; i < (int)_height; i++)
                {
                    row_pointers[i] = (png_bytep)tempData + i * _width * 3;
                }

                png_write_image(png_ptr, row_pointers);

                free(row_pointers);
                row_pointers = nullptr;

                if (tempData != nullptr)
                {
                    free(tempData);
                }
            }
            else
            {
                for (int i = 0; i < (int)_height; i++)
                {
                    row_pointers[i] = (png_bytep)_data + i * _width * 4;
                }

                png_write_image(png_ptr, row_pointers);

                free(row_pointers);
                row_pointers = nullptr;
            }
        }

        png_write_end(png_ptr, info_ptr);

        png_destroy_write_struct(&png_ptr, &info_ptr);

        outStream.reset();

        ret = true;
    } while (0);
    return ret;
#else
    AXLOG("png is not enabled, please enable it in ccConfig.h");
    return false;
#endif  // AX_USE_PNG
}

bool Image::saveImageToJPG(std::string_view filePath)
{
#if AX_USE_JPEG
    bool ret = false;
    do
    {
        struct jpeg_compress_struct cinfo;
        struct jpeg_error_mgr jerr;
        std::unique_ptr<FileStream> outfile; /* target file */
        JSAMPROW row_pointer[1];             /* pointer to JSAMPLE row[s] */
        int row_stride;                      /* physical row width in image buffer */

        cinfo.err = jpeg_std_error(&jerr);
        /* Now we can initialize the JPEG compression object. */
        jpeg_create_compress(&cinfo);

        outfile = FileUtils::getInstance()->openFileStream(filePath, FileStream::Mode::WRITE);
        AX_BREAK_IF(nullptr == outfile);

        unsigned char* outputBuffer = nullptr;
        unsigned long outputSize    = 0;
        jpeg_mem_dest(&cinfo, &outputBuffer, &outputSize);

        cinfo.image_width      = _width; /* image width and height, in pixels */
        cinfo.image_height     = _height;
        cinfo.input_components = 3;       /* # of color components per pixel */
        cinfo.in_color_space   = JCS_RGB; /* colorspace of input image */

        jpeg_set_defaults(&cinfo);
        jpeg_set_quality(&cinfo, 90, TRUE);

        jpeg_start_compress(&cinfo, TRUE);

        row_stride = _width * 3; /* JSAMPLEs per row in image_buffer */

        if (hasAlpha())
        {
            uint8_t* tempData = static_cast<uint8_t*>(malloc(_width * _height * 3));
            if (nullptr == tempData)
            {
                jpeg_finish_compress(&cinfo);
                jpeg_destroy_compress(&cinfo);

                outfile.reset();
                if (outputBuffer)
                {
                    free(outputBuffer);
                    outputBuffer = nullptr;
                }
                break;
            }

            for (int i = 0; i < _height; ++i)
            {
                for (int j = 0; j < _width; ++j)

                {
                    tempData[(i * _width + j) * 3]     = _data[(i * _width + j) * 4];
                    tempData[(i * _width + j) * 3 + 1] = _data[(i * _width + j) * 4 + 1];
                    tempData[(i * _width + j) * 3 + 2] = _data[(i * _width + j) * 4 + 2];
                }
            }

            while (cinfo.next_scanline < cinfo.image_height)
            {
                row_pointer[0] = &tempData[cinfo.next_scanline * row_stride];
                (void)jpeg_write_scanlines(&cinfo, row_pointer, 1);
            }

            if (tempData != nullptr)
            {
                free(tempData);
            }
        }
        else
        {
            while (cinfo.next_scanline < cinfo.image_height)
            {
                row_pointer[0] = &_data[cinfo.next_scanline * row_stride];
                (void)jpeg_write_scanlines(&cinfo, row_pointer, 1);
            }
        }

        jpeg_finish_compress(&cinfo);

        outfile->write(outputBuffer, outputSize);
        outfile.reset();

        if (outputBuffer)
        {
            free(outputBuffer);
            outputBuffer = nullptr;
        }

        jpeg_destroy_compress(&cinfo);

        ret = true;
    } while (0);
    return ret;
#else
    AXLOG("jpeg is not enabled, please enable it in ccConfig.h");
    return false;
#endif  // AX_USE_JPEG
}

void Image::premultiplyAlpha()
{
#if AX_ENABLE_PREMULTIPLIED_ALPHA
    AXASSERT(_pixelFormat == backend::PixelFormat::RGBA8, "The pixel format should be RGBA8888!");

    unsigned int* fourBytes = (unsigned int*)_data;
    for (int i = 0; i < _width * _height; i++)
    {
        uint8_t* p   = _data + i * 4;
        fourBytes[i] = AX_RGB_PREMULTIPLY_ALPHA(p[0], p[1], p[2], p[3]);
    }

    _hasPremultipliedAlpha = true;
#else
    _hasPremultipliedAlpha = false;
#endif
}

static inline uint8_t clamp(int x)
{
    return (uint8_t)(x >= 0 ? (x < 255 ? x : 255) : 0);
}

void Image::reversePremultipliedAlpha()
{
    AXASSERT(_pixelFormat == backend::PixelFormat::RGBA8, "The pixel format should be RGBA8888!");

    unsigned int* fourBytes = (unsigned int*)_data;
    for (int i = 0; i < _width * _height; i++)
    {
        uint8_t* p = _data + i * 4;
        if (p[3] > 0)
        {
            fourBytes[i] = clamp(int(std::ceil((p[0] * 255.0f) / p[3]))) |
                           clamp(int(std::ceil((p[1] * 255.0f) / p[3]))) << 8 |
                           clamp(int(std::ceil((p[2] * 255.0f) / p[3]))) << 16 | p[3] << 24;
        }
    }

    _hasPremultipliedAlpha = false;
}

NS_AX_END
