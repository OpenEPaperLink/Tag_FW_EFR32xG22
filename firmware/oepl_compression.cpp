#include "oepl_compression.hpp"
#include <vector>
#include <stdio.h>
#include <string.h>

// To get access to flash reading
extern "C" {
#include "oepl_flash_driver.h"
}

std::vector<decompress *> decompContexts;

int decompCallback(TINF_DATA *d) {
    // in the callback for uzlib we'll get a uzlib context from which it originates
    for (uint8_t i = 0; i < decompContexts.size(); i++) {
        // see if we can find which decompression context that callback belongs to
        if (decompContexts.at(i)->ctx == d) {
            decompress *dec = decompContexts.at(i);
            return dec->getNextCompressedBlockFromFlash();
        }
    }
    #ifdef DEBUG_COMPRESSION
    printf("FS: Couldn't find callback...\n");
    #endif
    return -1;
}

decompress::decompress() {
    // register the context for the callback
    decompContexts.push_back(this);

    // allocate decompressed data cache
    this->outCache = (uint8_t *)malloc(OUT_CACHE_SIZE);
}

void decompress::seek(uint32_t address) {
}

bool decompress::readHeader() {
    uzlib_init();

    // read the window size from the zlib header
    int res = uzlib_zlib_parse_header(this->ctx);
    if (res < 0) {
        printf("FS: Invalid zlib header\n");
        return false;
    }

    // the window size is reported as 2^(x-8).
    uint16_t window = 0x100 << res;

    // check if the file served has a sensible window size
    if (window > MAX_WINDOW_SIZE) {
#ifdef DEBUG_COMPRESSION
        printf("FS: Asked to decompress a file with a specified window size of %d, I don't see that happening\n", window);
#endif
        return false;
    } else {
#ifdef DEBUG_COMPRESSION
        printf("FS: Opened compressed file with dictionary size %d\n", window);
#endif
    }

    // window = 8192;

    // allocate dict/window if not already allocated
    if (!this->dictionary) this->dictionary = (uint8_t *)malloc(window);
    if (!this->dictionary) printf("FS: window malloc failed\n");

    uzlib_uncompress_init(this->ctx, this->dictionary, window);
    return true;
}

bool decompress::openFromFlash(uint32_t eepBase, uint32_t cSize) {
    this->setupContext();
    this->ctx->source_read_cb = decompCallback;
    this->compressedPos += ZLIB_CACHE_SIZE;
    this->compressedSize = cSize - 4;
    this->eepromBase = eepBase;
    HAL_flashRead(this->eepromBase, (uint8_t *)&this->decompressedSize, 4);
    HAL_flashRead(this->eepromBase + 4, this->compBuffer, ZLIB_CACHE_SIZE);
    this->fromFile = false;
    return this->readHeader();
}

void decompress::setupContext() {
    if (!this->ctx) this->ctx = new struct uzlib_uncomp;
    if (!this->compBuffer) this->compBuffer = (uint8_t *)malloc(ZLIB_CACHE_SIZE);
    this->ctx->source = this->compBuffer;
    this->ctx->source_limit = this->compBuffer + ZLIB_CACHE_SIZE;
    compressedPos = 0;
    decompressedPos = 0;
}

decompress::~decompress() {
    for (uint8_t i = 0; i < decompContexts.size(); i++) {
        if (decompContexts.at(i) == this)
            decompContexts.erase(decompContexts.begin() + i);
    }
    if (this->dictionary) free(this->dictionary);
    this->dictionary = nullptr;
    if (this->ctx) delete this->ctx;
    this->ctx = nullptr;
    if (this->compBuffer) free(this->compBuffer);
    this->compBuffer = nullptr;
    if (this->outCache) free(this->outCache);
    this->outCache = nullptr;
}

int decompress::getNextCompressedBlockFromFlash() {
    int32_t bytesLeft = compressedSize - compressedPos;
    if (bytesLeft <= 0) return -1;
    if (bytesLeft > ZLIB_CACHE_SIZE) bytesLeft = ZLIB_CACHE_SIZE;
    HAL_flashRead(this->eepromBase + 4 + compressedPos, this->compBuffer, bytesLeft);
    ctx->source = this->compBuffer + 1;
    ctx->source_limit = this->compBuffer + bytesLeft;
    this->compressedPos += bytesLeft;
    return this->compBuffer[0];
}

uint32_t decompress::getBlock(uint32_t address, uint8_t *target, uint32_t len) {
    // check if we have the requested block of data in cache
    if (address >= cacheStart) {
        if ((address + len) <= (cacheStart + cacheLen)) {
            memcpy(target, (this->outCache) + (address - cacheStart), len);
            // cache hit, copy cache to target
            return len;
        }
    }

    if (address + len > decompressedSize) return 0;
    if (address < this->decompressedPos) {
        // reload file, start from scratch
        this->ctx->source = this->compBuffer;
        compressedPos = 0;
        decompressedPos = 0;
        this->getNextCompressedBlockFromFlash();
        this->ctx->source = this->compBuffer;
        this->readHeader();
    }

    uint32_t bufferStart = (address + len) - OUT_CACHE_SIZE;
    uint32_t bufferEnd;

    // don't read from before the start if a low address is requested
    if (bufferStart > 512000) bufferStart = 0;

    // don't start reading data that starts before the current pointer
    if (bufferStart < this->decompressedPos) {
        bufferStart = this->decompressedPos;
        bufferEnd = bufferStart + len;
    } else {
        bufferEnd = bufferStart + OUT_CACHE_SIZE;
    }
    if (bufferEnd > this->decompressedSize) bufferEnd = this->decompressedSize;

    // skip to the next part of the output stream
    if (bufferStart != decompressedPos) {
        uint8_t temp[OUT_CACHE_SIZE];
        while (this->decompressedPos < bufferStart) {
            uint32_t readBytes = bufferStart - decompressedPos;
            if (readBytes > OUT_CACHE_SIZE) readBytes = OUT_CACHE_SIZE;
            decompressedPos += readBytes;
            this->ctx->dest = this->outCache;
            ctx->dest_start = ctx->dest;
            ctx->dest_limit = ctx->dest + readBytes;
            uzlib_uncompress(ctx);
        }
    }

    uint32_t bytesLeft = this->decompressedSize - this->decompressedPos;
    if (len > bytesLeft) len = bytesLeft;

    this->ctx->dest = (unsigned char *)this->outCache;
    ctx->dest_start = ctx->dest;
    ctx->dest_limit = ctx->dest + (bufferEnd - bufferStart);

    uzlib_uncompress(ctx);

    // save cache metadata
    this->cacheLen = bufferEnd - bufferStart;
    this->cacheStart = bufferStart;

    this->decompressedPos += (bufferEnd - bufferStart);
    return this->getBlock(address, target, len);
}

uint8_t decompress::readByte(uint32_t address) {
    uint8_t a;
    this->getBlock(address, &a, 1);
    return a;
}
