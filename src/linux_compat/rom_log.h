#pragma once

#ifdef M88_LINUX_PORT

void M88RomLogBegin();
void M88RomLogLoaded(const char* path, const char* detail);
void M88RomLogSkipped(const char* path, const char* reason);
void M88RomLogFallback(const char* reason);
void M88RomLogEnd(bool ok);
void M88LogRequiredRomMissing(const char* rom_dir);

void M88WavLogBegin();
void M88WavLogLoaded(const char* path, const char* detail);
void M88WavLogSkipped(const char* path, const char* reason);
void M88WavLogEnd(bool ok);

#else

inline void M88RomLogBegin() {}
inline void M88RomLogLoaded(const char*, const char*) {}
inline void M88RomLogSkipped(const char*, const char*) {}
inline void M88RomLogFallback(const char*) {}
inline void M88RomLogEnd(bool) {}
inline void M88LogRequiredRomMissing(const char*) {}
inline void M88WavLogBegin() {}
inline void M88WavLogLoaded(const char*, const char*) {}
inline void M88WavLogSkipped(const char*, const char*) {}
inline void M88WavLogEnd(bool) {}

#endif
