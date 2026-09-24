#pragma once
#include "editor_preview_texture.h"
#if defined(Q_OS_MACOS) && defined(PHOTO_EDITOR_ENABLE_METAL_PREVIEW_BACKEND) && PHOTO_EDITOR_ENABLE_METAL_PREVIEW_BACKEND
QImage read_native_preview_texture(photo_editor::desktop::EditorPreviewTexture* texture);
#else
inline QImage read_native_preview_texture(photo_editor::desktop::EditorPreviewTexture*) { return {}; }
#endif
