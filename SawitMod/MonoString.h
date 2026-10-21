#pragma once
// ============================================================================
//  MonoString.h — iOS 版（托管字符串 / 数组的最小封装）
// ----------------------------------------------------------------------------
//  与 Android 版的差别：
//    * il2cpp_string_new 不再 dlsym("libil2cpp.so")，改走 IL2CPP/Il2CppApi.h
//    * toChars() 原实现返回局部 std::string 的 c_str()（悬垂指针），改成返回值
//    * `template<typename T> typedef struct monoArray {...}` 是非标准写法，
//      改为标准的 `template<typename T> struct monoArray`（名字不变）
//  其余字段布局（klass/monitor/length/chars、数组的 bounds/capacity）保持原样：
//  il2cpp 与 Mono 的对象头在 AArch64 上一致。
// ============================================================================

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "IL2CPP/Il2CppApi.h"

inline long mono_address = 0;

inline std::string utf16le_to_utf8(const std::u16string &u16str) {
    if (u16str.empty()) return std::string();

    const char16_t *p = u16str.data();
    std::u16string::size_type len = u16str.length();
    if (p[0] == 0xFEFF) {
        p += 1;
        len -= 1;
    }
    std::string u8str;
    u8str.reserve(len * 3);
    char16_t u16char;
    for (std::u16string::size_type i = 0; i < len; ++i) {
        u16char = p[i];
        if (u16char < 0x0080) {
            u8str.push_back((char)(u16char & 0x00FF));
            continue;
        }
        if (u16char >= 0x0080 && u16char <= 0x07FF) {
            u8str.push_back((char)(((u16char >> 6) & 0x1F) | 0xC0));
            u8str.push_back((char)((u16char & 0x3F) | 0x80));
            continue;
        }
        if (u16char >= 0xD800 && u16char <= 0xDBFF) {
            const uint32_t highSur = u16char;
            const uint32_t lowSur = p[++i];
            uint32_t codePoint = highSur - 0xD800;
            codePoint <<= 10;
            codePoint |= lowSur - 0xDC00;
            codePoint += 0x10000;
            u8str.push_back((char)((codePoint >> 18) | 0xF0));
            u8str.push_back((char)(((codePoint >> 12) & 0x3F) | 0x80));
            u8str.push_back((char)(((codePoint >> 06) & 0x3F) | 0x80));
            u8str.push_back((char)((codePoint & 0x3F) | 0x80));
            continue;
        }
        u8str.push_back((char)(((u16char >> 12) & 0x0F) | 0xE0));
        u8str.push_back((char)(((u16char >> 6) & 0x3F) | 0x80));
        u8str.push_back((char)((u16char & 0x3F) | 0x80));
    }
    return u8str;
}

typedef struct _monoString {
    void *klass;
    void *monitor;
    int length;
    char chars[1];

    std::string toChars() const {
        return utf16le_to_utf8(std::u16string((const char16_t *)chars, 0, (size_t)length));
    }
    char *getChars() { return chars; }
    int getLength() const { return length; }
    std::string get_string() const { return toChars(); }
} monoString;

// Android 版这里 dlsym("libil2cpp.so")；iOS 直接走已解析的 il2cpp_* 表。
inline monoString *il2cpp_string_new(const char *str) {
    const SMIL2CPP::API &api = SMIL2CPP::Get();
    if (!api.string_new) return nullptr;
    return (monoString *)api.string_new(str);
}

template <typename T>
struct monoArray {
    void *klass;
    void *monitor;
    void *bounds;
    int capacity;
    int max_length;
    T m_Items[0];

    int getCapacity() { return capacity; }
    int getLength() const { return max_length; }
    T *getPointer() { return m_Items; }

    template <typename V = T>
    std::vector<V> toCPPlist() {
        std::vector<V> ret;
        for (int i = 0; i < capacity; i++) ret.push_back(m_Items[i]);
        return ret;
    }

    void copyFrom(std::vector<T> vec) { copyFrom(vec.data(), (int)vec.size()); }

    void copyFrom(T *arr, int size) {
        if (size > capacity) Resize(size);
        memcpy(m_Items, arr, sizeof(T) * (size_t)size);
    }

    void copyTo(T *arr) { memcpy(arr, m_Items, sizeof(T) * (size_t)capacity); }

    T operator[](int index) { return m_Items[index]; }

    void Resize(int newSize) {
        if (newSize <= capacity) return;
        T *newArr = new T[newSize];
        memcpy(newArr, m_Items, sizeof(T) * (size_t)capacity);
        capacity = newSize;
    }

    monoArray<T> *MonoCreate(int size) {
        monoArray<T> *monoArr =
            (monoArray<T> *)malloc(sizeof(monoArray) + sizeof(T) * (size_t)size);
        monoArr->capacity = size;
        return monoArr;
    }

    template <typename t>
    static monoArray<t> *Create(int size) {
        monoArray<t> *monoArr =
            (monoArray<t> *)malloc(sizeof(monoArray<t>) + sizeof(t) * (size_t)size);
        monoArr->capacity = size;
        return monoArr;
    }

    template <typename t>
    static monoArray<t> *Create(std::vector<t> vec) {
        return Create<t>((int)vec.size());
    }
};
