#include "common/uzlib/src/uzlib.h"

#define MAX_WINDOW_SIZE 8192
#define ZLIB_CACHE_SIZE 256
#define OUT_CACHE_SIZE 1024

class decompress {
   public:
    bool openFromFlash(uint32_t eepBase, uint32_t cSize);
    uint32_t getBlock(uint32_t address, uint8_t *target, uint32_t len);
    uint8_t readByte(uint32_t address);
    void seek(uint32_t address);

    int getNextCompressedBlockFromFlash();

    decompress();
    ~decompress();

    bool fromFile = false;
    struct uzlib_uncomp *ctx = nullptr;
    void reset();
   protected:
    void setupContext();
    bool readHeader();
    uint8_t *compBuffer = nullptr;
    uint32_t decompressedSize;
    uint32_t decompressedPos;
    uint32_t compressedSize;
    uint32_t compressedPos;
    uint32_t eepromBase;
    uint8_t* outCache;
    uint32_t cacheLen;
    uint32_t cacheStart;
    uint8_t *dictionary = nullptr;
};
