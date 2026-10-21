#pragma once

#include "../imgui.h"
#include "../stb_image.h"
#include "data/newbie_background_gif.h"
#include <GLES3/gl3.h>
#include "Includes/Logger.h"
#include <chrono>
#include <vector>
#include <cstdlib>
#include <algorithm>
#include <cstdint>

namespace NewbieGifBackgroundAndroid {

inline GLuint texture = 0;
inline int width = 0;
inline int height = 0;
inline int frameCount = 0;
inline int uploadedFrame = -1;
inline bool attempted = false;
inline std::vector<int> delaysMs;
inline std::vector<unsigned char> decodedFrames;
inline std::chrono::steady_clock::time_point started;

inline void LogError(const char* message) {
    __android_log_print(ANDROID_LOG_ERROR, "NewbieGIF", "%s", message ? message : "unknown error");
}

inline void Release() {
    // EGLContext lama mungkin sudah tidak current. Jangan glDelete texture lama
    // dari context baru karena ID-nya dapat digunakan resource milik game.
    texture = 0;
    width = height = frameCount = 0;
    uploadedFrame = -1;
    attempted = false;
    delaysMs.clear();
    decodedFrames.clear();
}

inline bool UploadFrame(int index, bool allocate) {
    if (texture == 0 || index < 0 || index >= frameCount || decodedFrames.empty())
        return false;

    // Upload terjadi sebelum backend ImGui membuat backup state. Karena itu semua
    // state GL yang disentuh harus dipulihkan sendiri agar render game tidak
    // berubah/hilang sampai UI game (mis. backpack) menginisialisasi state ulang.
    GLint previousActiveTexture = GL_TEXTURE0;
    GLint previousTexture = 0;
    GLint previousUnpackAlignment = 4;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &previousActiveTexture);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &previousUnpackAlignment);

    const size_t stride = (size_t) width * (size_t) height * 4u;
    const unsigned char* pixels = decodedFrames.data() + stride * (size_t) index;
    glBindTexture(GL_TEXTURE_2D, texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (allocate) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height,
                        GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    }
    const GLenum error = glGetError();

    glPixelStorei(GL_UNPACK_ALIGNMENT, previousUnpackAlignment);
    glBindTexture(GL_TEXTURE_2D, (GLuint) previousTexture);
    glActiveTexture((GLenum) previousActiveTexture);

    if (error != GL_NO_ERROR) {
        __android_log_print(ANDROID_LOG_ERROR, "NewbieGIF",
                            "OpenGL upload gagal: 0x%x (frame=%d)", error, index);
        return false;
    }
    uploadedFrame = index;
    return true;
}

inline bool Load() {
    if (attempted)
        return texture != 0 && !decodedFrames.empty();
    attempted = true;

    int* delays = nullptr;
    int count = 0;
    int comp = 0;
    unsigned char* pixels = stbi_load_gif_from_memory(
            kNewbieBackgroundGif, (int) kNewbieBackgroundGifSize,
            &delays, &width, &height, &count, &comp, 4);
    if (!pixels || width <= 0 || height <= 0 || count <= 0) {
        const char* reason = stbi_failure_reason();
        __android_log_print(ANDROID_LOG_ERROR, "NewbieGIF",
                            "Decode gagal: %s (w=%d h=%d frames=%d)",
                            reason ? reason : "unknown", width, height, count);
        if (pixels) stbi_image_free(pixels);
        if (delays) STBI_FREE(delays);
        return false;
    }

    frameCount = count;
    const size_t totalBytes = (size_t) width * (size_t) height * 4u * (size_t) frameCount;
    decodedFrames.assign(pixels, pixels + totalBytes);
    delaysMs.resize((size_t) frameCount, 100);
    for (int i = 0; i < frameCount; ++i)
        delaysMs[(size_t) i] = delays && delays[i] > 0 ? delays[i] : 100;
    stbi_image_free(pixels);
    if (delays) STBI_FREE(delays);

    // Hanya satu texture GPU. Frame aktif diperbarui dengan glTexSubImage2D,
    // sehingga GIF 77 frame tidak menghabiskan puluhan MiB texture GPU.
    glGenTextures(1, &texture);
    if (texture == 0) {
        LogError("glGenTextures mengembalikan 0");
        decodedFrames.clear();
        delaysMs.clear();
        return false;
    }
    GLint previousActiveTexture = GL_TEXTURE0;
    GLint previousTexture = 0;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &previousActiveTexture);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, (GLuint) previousTexture);
    glActiveTexture((GLenum) previousActiveTexture);

    if (!UploadFrame(0, true)) {
        texture = 0;
        decodedFrames.clear();
        delaysMs.clear();
        return false;
    }

    started = std::chrono::steady_clock::now();
    __android_log_print(ANDROID_LOG_INFO, "NewbieGIF",
                        "Loaded %dx%d, %d frames, one streaming texture",
                        width, height, frameCount);
    return true;
}

inline void Draw(ImDrawList* dl, ImVec2 min, ImVec2 max, float rounding) {
    if (!dl || !Load() || width <= 0 || height <= 0 || frameCount <= 0)
        return;

    const long long elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started).count();
    long long cycle = 0;
    for (int delay : delaysMs)
        cycle += std::max(10, delay);
    if (cycle <= 0)
        return;

    long long local = elapsed % cycle;
    int index = 0;
    for (; index + 1 < frameCount; ++index) {
        const int delay = std::max(10, delaysMs[(size_t) index]);
        if (local < delay)
            break;
        local -= delay;
    }
    if (index != uploadedFrame && !UploadFrame(index, false))
        return;

    const float boxW = max.x - min.x;
    const float boxH = max.y - min.y;
    if (boxW <= 0.0f || boxH <= 0.0f)
        return;

    const float imageAspect = (float) width / (float) height;
    const float boxAspect = boxW / boxH;
    ImVec2 uv0(0, 0), uv1(1, 1);
    if (imageAspect > boxAspect) {
        const float visible = boxAspect / imageAspect;
        uv0.x = (1.0f - visible) * 0.5f;
        uv1.x = 1.0f - uv0.x;
    } else {
        const float visible = imageAspect / boxAspect;
        uv0.y = (1.0f - visible) * 0.5f;
        uv1.y = 1.0f - uv0.y;
    }

    dl->AddImageRounded((ImTextureID) (intptr_t) texture,
                        min, max, uv0, uv1, IM_COL32_WHITE, rounding);
}

} // namespace NewbieGifBackgroundAndroid
