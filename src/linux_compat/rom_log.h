#pragma once

#ifdef M88_LINUX_PORT

void M88RomLogBegin();
void M88RomLogLoaded(const char* path, const char* detail);
void M88RomLogSkipped(const char* path, const char* reason);
void M88RomLogFallback(const char* reason);
void M88RomLogEnd(bool ok);

#else

inline void M88RomLogBegin() {}
inline void M88RomLogLoaded(const char*, const char*) {}
inline void M88RomLogSkipped(const char*, const char*) {}
inline void M88RomLogFallback(const char*) {}
inline void M88RomLogEnd(bool) {}

#endif
