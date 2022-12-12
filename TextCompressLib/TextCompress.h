#pragma once

#if defined(TEXTCOMPRESS_EXPORT)  // inside DLL
#define TEXTAPI __declspec(dllexport)
#else  // outside DLL
#define TEXTAPI __declspec(dllimport)
#endif  // XYZLIBRARY_EXPORT

__interface TEXTAPI ITextCompressor {
  virtual int Compress(const void *data, const unsigned int size, const unsigned int Strength, void **cdata) = 0;
  virtual unsigned int Decompress(const void *data, const unsigned int size, void **cdata) = 0;
};

TEXTAPI ITextCompressor *CreateTextCompressor(int CBS);