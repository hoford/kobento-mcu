// Hand-written replacement for ThorVG's meson-generated config.h.
// CPU (software) engine only, with the loaders the RemoteCompose backend needs:
// raw pixels (bitmaps), PNG and JPEG (inline images) and TTF (text). No SVG/Lottie/WebP.
#pragma once
#define THORVG_VERSION_STRING "1.0.7"
#define THORVG_CPU_ENGINE_SUPPORT 1
#define THORVG_PNG_LOADER_SUPPORT 1
#define THORVG_JPG_LOADER_SUPPORT 1
#define THORVG_SFNT_LOADER_SUPPORT 1
#define THORVG_TTF_LOADER_SUPPORT 1
#define THORVG_FILE_IO_SUPPORT 1
#ifdef RC_TVG_THREADS
#define THORVG_THREAD_SUPPORT 1
#endif
