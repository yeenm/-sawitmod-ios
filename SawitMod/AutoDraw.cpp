// ============================================================================
//  AutoDraw.cpp — 自动绘画（按 PC 版 Bugtopia DrawUploadFeature / DrawColorCodec 移植）
// ----------------------------------------------------------------------------
//  为什么重写一条新链路：Android 版原来那条"UIManager.GetView<DrawingPanel> →
//  CanvasPainter.SetPixelColor 逐点写"的路，名字全来自服务器 payload（op=20），而
//  这份离线 dump 里**没有** op=20 的信封（LocalCatalogData.h 的
//  kEmbeddedMissingPayloadWires 含 9db079ae8797），于是点了没反应。
//
//  PC 版走的是另一条、名字完全已知的路（同一套游戏逻辑程序集）：
//
//    1) 调色板  DrawingConfig.ColorLut（128x1 RGBA32 的 Texture2D 'drawing_lut'）
//    2) 画布    XDTLevelAndEntity.Gameplay.Component.Drawing.DrawingBoardComponent
//               .get_CurrentPainting() -> CanvasPainter
//               get_Width() / get_Height() / 字段 pixelData(byte[]) / Apply()
//    3) 编码    每个像素存 1 字节：不透明 = 0x80|调色板下标，透明 = 124
//    4) 上传    XDTDataAndProtocol.ProtocolService.DrawBoard.DrawBoardProtoManager
//               .DrawingOperation(Dictionary<byte,List<DrawingBatchOperationSequentialInfo>>,
//                                 step, part) —— 静态方法，服务器据此在画板上落笔
//               每个 run：Start = 该色第一个像素的平坦下标，Length = 连续长度 - 1
//               （Definition 见 XDT.Scene.Shared.Modules.DrawBoard.DrawingBatchOperationSequentialInfo）
//
//  画布对象（CanvasPainter）优先按 PC 那条取（DrawSystem → GetInteractionNetId →
//  Entities.GetEntity → DrawingBoardComponent），取不到再退回 UI 那条
//  （UIManager → DrawingPanel → painter 字段）。两条都拿不到就报"先把画板打开"。
//
//  ⚠️ DrawingOperation 作用在"当前正在编辑的那块画板"上，所以必须站在画板前、
//     把绘画界面打开再按。
// ============================================================================

#include "ModFeatures.h"
#include "ModUtils.h"
#include "MonoGameApi.h"
#include "Includes/Logger.h"
#include "Includes/obfuscate.h"
#include "Starcoolxdl/xdl.h"

#include <atomic>
#include <cmath>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace MonoApi;

namespace {

// 与 PC 版 DrawColorCodec 完全一致的编码常量
constexpr uint8_t kTransparentIndexByte = 124;
constexpr uint8_t kOpaqueFlag = 0x80;

const char *kDrawImages[] = {
        OBFUSCATE("XDTLevelAndEntity"), OBFUSCATE("XDTGameUI"), OBFUSCATE("EcsClient"),
        OBFUSCATE("EcsSystem"), OBFUSCATE("XDTDataAndProtocol"), OBFUSCATE("XDTGameSystem"),
        OBFUSCATE("XDTBaseService"), OBFUSCATE("Plugins"),
};
constexpr int kDrawImageCount = sizeof(kDrawImages) / sizeof(kDrawImages[0]);

std::atomic<bool> g_autoDrawBusy{false};

void *FindDrawClass(const char *nameSpace, const char *className) {
    return FindClass(kDrawImages, kDrawImageCount, nameSpace, className, nullptr, true);
}

// ── 纹理小工具（Texture2D：宽/高/格式/GetRawTextureData）────────────────────
struct TextureReader {
    void *getWidth = nullptr;
    void *getHeight = nullptr;
    void *getFormat = nullptr;
    void *getRaw = nullptr;

    bool Resolve(void *textureKlass, void *texture2DKlass) {
        getWidth = DrawFindMethodOn(textureKlass, "get_width", 0, "width");
        getHeight = DrawFindMethodOn(textureKlass, "get_height", 0, "height");
        getFormat = texture2DKlass ? DrawFindMethodOn(texture2DKlass, "get_format", 0, "format")
                                   : nullptr;
        getRaw = texture2DKlass ? DrawFindMethodOn(texture2DKlass, "GetRawTextureData", 0,
                                                   "RawTexture")
                                : nullptr;
        return getWidth && getHeight && getRaw;
    }

    static void *DrawFindMethodOn(void *klass, const char *name, int argc, const char *substr) {
        if (!klass) return nullptr;
        void *method = FindMethodInHierarchy(klass, name, argc);
        if (method) return method;
        MonoClassGetMethodsFn getMethods = ClassGetMethods();
        MonoMethodGetNameFn getName = MethodGetName();
        if (!getMethods || !getName) return nullptr;
        void *iter = nullptr;
        void *candidate = nullptr;
        while ((candidate = getMethods(klass, &iter)) != nullptr) {
            const char *candidateName = getName(candidate);
            if (!candidateName || !strstr(candidateName, substr)) continue;
            if (argc >= 0 && MethodParamCount(candidate) != argc) continue;
            return candidate;
        }
        return nullptr;
    }

    bool ReadSize(void *texture, int *width, int *height) {
        if (!texture || !width || !height) return false;
        void *exception = nullptr;
        void *w = _monoRuntimeInvoke(getWidth, texture, nullptr, &exception);
        if (exception || !w) return false;
        void *h = _monoRuntimeInvoke(getHeight, texture, nullptr, &exception);
        if (exception || !h) return false;
        memcpy(width, UnboxValue(w), sizeof(int));
        memcpy(height, UnboxValue(h), sizeof(int));
        return *width > 0 && *height > 0;
    }

    // 返回像素数据首地址 + 字节数；bpp 由 get_format 推（3 = RGB24，其余按 4）
    uint8_t *RawData(void *texture, int *length, int *bpp) {
        if (!texture || !_monoRuntimeInvoke) return nullptr;
        void *exception = nullptr;
        if (getFormat) {
            void *fmt = _monoRuntimeInvoke(getFormat, texture, nullptr, &exception);
            if (!exception && fmt) {
                int formatValue = 0;
                memcpy(&formatValue, UnboxValue(fmt), sizeof(int));
                if (bpp) *bpp = (formatValue == 3) ? 3 : 4;
            }
        }
        exception = nullptr;
        void *raw = _monoRuntimeInvoke(getRaw, texture, nullptr, &exception);
        if (exception || !raw) return nullptr;
        if (length) *length = *reinterpret_cast<int *>(reinterpret_cast<uint8_t *>(raw)
                                                      + OFF_ARR_COUNT);
        return reinterpret_cast<uint8_t *>(raw) + OFF_ARR_DATA;
    }
};

TextureReader g_textureReader;
void *g_textureKlass = nullptr;
void *g_texture2DKlass = nullptr;

bool EnsureTextureApi() {
    if (g_textureReader.getWidth && g_textureKlass && g_texture2DKlass) return true;
    void *coreModule = LoadedImage(OBFUSCATE("UnityEngine.CoreModule"));
    if (!coreModule) coreModule = LoadedImage(OBFUSCATE("UnityEngine"));
    g_textureKlass = FindDrawClass(OBFUSCATE("UnityEngine"), OBFUSCATE("Texture"));
    g_texture2DKlass = FindDrawClass(OBFUSCATE("UnityEngine"), OBFUSCATE("Texture2D"));
    if (!g_textureKlass || !g_texture2DKlass) return false;
    return g_textureReader.Resolve(g_textureKlass, g_texture2DKlass);
}

// mono_array_new 懒解析
using MonoArrayNewFn = void *(*)(void *, void *, uintptr_t);
MonoArrayNewFn MonoArrayNew() {
    static MonoArrayNewFn fn = nullptr;
    if (!fn) {
        void *handle = xdl_open(OBFUSCATE("libmonosgen-2.0.so"), 0);
        if (!handle) handle = xdl_open(OBFUSCATE("libmonosgen-2.0.so"), XDL_TRY_FORCE_LOAD);
        if (handle) fn = reinterpret_cast<MonoArrayNewFn>(
                xdl_sym(handle, OBFUSCATE("mono_array_new"), nullptr));
    }
    return fn;
}

// ── 调色板 ─────────────────────────────────────────────────────────────────
struct Palette {
    std::vector<uint8_t> rgb;   // 3 字节一组
    int count = 0;
};

bool LoadPalette(Palette *palette, std::string *status) {
    if (!palette) return false;
    void *config = GetDrawingConfigInstanceNative();
    if (!config) {
        *status = u8"取不到 DrawingConfig（调色板来源）";
        return false;
    }
    void *texture = ExtractColorLutTextureNative(config);
    if (!texture) {
        *status = u8"DrawingConfig.ColorLut 为空（画板资源还没加载？）";
        return false;
    }
    if (!EnsureTextureApi()) {
        *status = u8"UnityEngine.Texture/Texture2D 解析失败";
        return false;
    }

    int width = 0, height = 0;
    if (!g_textureReader.ReadSize(texture, &width, &height)) {
        *status = u8"调色板纹理尺寸读不到";
        return false;
    }
    int length = 0, bpp = 4;
    uint8_t *raw = g_textureReader.RawData(texture, &length, &bpp);
    if (!raw || length <= 0) {
        *status = u8"调色板 GetRawTextureData 拿不到";
        return false;
    }

    palette->count = width;
    palette->rgb.assign(static_cast<size_t>(width) * 3, 0);
    for (int i = 0; i < width; ++i) {
        const int offset = i * bpp;
        if (offset + 2 >= length) break;
        palette->rgb[static_cast<size_t>(i) * 3 + 0] = raw[offset + 0];
        palette->rgb[static_cast<size_t>(i) * 3 + 1] = raw[offset + 1];
        palette->rgb[static_cast<size_t>(i) * 3 + 2] = raw[offset + 2];
    }
    *status = u8"调色板 " + std::to_string(palette->count) + u8" 色";
    return palette->count > 0;
}

// ── 图片解码（走游戏自己的 ImageConversion.LoadImage）──────────────────────
bool DecodeImageToRgba(const std::string &path, std::vector<uint8_t> *rgba, int *width,
                       int *height, std::string *status) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        *status = u8"打不开文件：" + path;
        return false;
    }
    const std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    if (!file.read(reinterpret_cast<char *>(bytes.data()), size)) {
        *status = u8"读取图片字节失败";
        return false;
    }
    file.close();

    void *domain = _monoGetRootDomain ? _monoGetRootDomain() : nullptr;
    if (!domain || !_monoObjectNew || !_monoRuntimeInvoke) {
        *status = u8"Mono 未就绪";
        return false;
    }
    if (!EnsureTextureApi()) {
        *status = u8"Texture2D 类解析失败";
        return false;
    }

    // Texture2D tex = new Texture2D(2, 2)
    void *texture = _monoObjectNew(domain, g_texture2DKlass);
    if (!texture) {
        *status = u8"Texture2D 分配失败";
        return false;
    }
    void *ctor = FindMethodBySignature(g_texture2DKlass, OBFUSCATE(".ctor"), "ii");
    if (!ctor) ctor = FindMethodInHierarchy(g_texture2DKlass, OBFUSCATE(".ctor"), 2);
    if (ctor) {
        int two = 2;
        void *ctorArgs[2] = {&two, &two};
        void *exception = nullptr;
        _monoRuntimeInvoke(ctor, texture, ctorArgs, &exception);
        if (exception) {
            *status = u8"Texture2D(2,2) 构造异常";
            return false;
        }
    }

    // byte[] <- 文件内容
    MonoArrayNewFn arrayNew = MonoArrayNew();
    void *corlib = LoadedImage(OBFUSCATE("mscorlib"));
    if (!corlib) corlib = LoadedImage(OBFUSCATE("System.Private.CoreLib"));
    void *byteKlass = corlib ? _monoClassFromName(corlib, OBFUSCATE("System"), OBFUSCATE("Byte"))
                             : nullptr;
    if (!arrayNew || !byteKlass) {
        *status = u8"mono_array_new / System.Byte 不可用";
        return false;
    }
    void *byteArray = arrayNew(domain, byteKlass, bytes.size());
    if (!byteArray) {
        *status = u8"托管 byte[] 分配失败";
        return false;
    }
    memcpy(reinterpret_cast<uint8_t *>(byteArray) + OFF_ARR_DATA, bytes.data(), bytes.size());

    // ImageConversion.LoadImage(tex, byteArray)
    void *imageConversion = FindDrawClass(OBFUSCATE("UnityEngine"), OBFUSCATE("ImageConversion"));
    if (!imageConversion) imageConversion = FindClass(nullptr, 0, OBFUSCATE("UnityEngine"),
                                                      OBFUSCATE("ImageConversion"));
    void *loadImage = imageConversion
            ? FindMethodBySignature(imageConversion, OBFUSCATE("LoadImage"), "o?")
            : nullptr;
    if (!loadImage) {
        *status = u8"ImageConversion.LoadImage(Texture2D,byte[]) 找不到";
        return false;
    }
    void *loadArgs[2] = {texture, byteArray};
    void *exception = nullptr;
    _monoRuntimeInvoke(loadImage, nullptr, loadArgs, &exception);
    if (exception) {
        *status = u8"LoadImage 抛异常（图片格式不支持？用 .jpg/.png）";
        return false;
    }

    int w = 0, h = 0;
    if (!g_textureReader.ReadSize(texture, &w, &h)) {
        *status = u8"解码后读不到尺寸";
        return false;
    }
    int length = 0, bpp = 4;
    uint8_t *raw = g_textureReader.RawData(texture, &length, &bpp);
    if (!raw || length < w * h * 3) {
        *status = u8"解码后取不到像素";
        return false;
    }

    rgba->assign(raw, raw + static_cast<size_t>(w) * h * bpp);
    *width = w;
    *height = h;
    *status = u8"图片 " + std::to_string(w) + u8"x" + std::to_string(h);
    return true;
}

// ── 取当前画布的 CanvasPainter ─────────────────────────────────────────────
// 路线 A（PC 同款）：DrawSystem.GetInteractionNetId -> Entities.GetEntity ->
//                    DrawingBoardComponent.GetComponent -> get_CurrentPainting
// 路线 B（UI）：     UIManager.Instance -> GetView<DrawingPanel> -> painter 字段
void *ResolvePaintingObject(std::string *status) {
    // ---- 路线 A ----
    void *drawSystem = GetManagerInstanceNative(OBFUSCATE("DrawSystem"));
    if (!drawSystem) {
        void *drawSystemClass = FindDrawClass(OBFUSCATE("XDTLevelAndEntity.Game.Module.Draw"),
                                              OBFUSCATE("DrawSystem"));
        if (drawSystemClass) {
            void *getInstance = FindMethodInHierarchy(drawSystemClass, OBFUSCATE("get_Instance"), 0);
            if (!getInstance)
                getInstance = FindMethodInHierarchy(drawSystemClass, OBFUSCATE("get_instance"), 0);
            if (getInstance && _monoRuntimeInvoke) {
                void *exception = nullptr;
                drawSystem = _monoRuntimeInvoke(getInstance, nullptr, nullptr, &exception);
                if (exception) drawSystem = nullptr;
            }
        }
    }

    if (drawSystem) {
        void *getNetId = FindMethodInHierarchy(_monoObjectGetClass(drawSystem),
                                               OBFUSCATE("GetInteractionNetId"), 0);
        if (getNetId) {
            void *exception = nullptr;
            void *boxed = _monoRuntimeInvoke(getNetId, drawSystem, nullptr, &exception);
            uint32_t netId = 0;
            if (!exception && boxed) memcpy(&netId, UnboxValue(boxed), sizeof(uint32_t));
            if (netId != 0) {
                void *entitiesClass = FindDrawClass(
                        OBFUSCATE("XDTLevelAndEntity.BaseSystem.EntitiesManager"),
                        OBFUSCATE("Entities"));
                void *getEntity = entitiesClass
                        ? FindMethodBySignature(entitiesClass, OBFUSCATE("GetEntity"), "i")
                        : nullptr;
                if (!getEntity && entitiesClass)
                    getEntity = FindMethodBySignature(entitiesClass, OBFUSCATE("GetAnyEntity"), "i");
                void *boardEntity = nullptr;
                if (getEntity) {
                    uint32_t probe = netId;
                    void *args[1] = {&probe};
                    exception = nullptr;
                    boardEntity = _monoRuntimeInvoke(getEntity, nullptr, args, &exception);
                    if (exception) boardEntity = nullptr;
                }
                if (boardEntity) {
                    void *componentClass = FindDrawClass(
                            OBFUSCATE("XDTLevelAndEntity.Gameplay.Component.Drawing"),
                            OBFUSCATE("DrawingBoardComponent"));
                    void *componentType = componentClass && _monoClassGetType
                            ? _monoClassGetType(componentClass) : nullptr;
                    void *typeObject = (componentType && _monoTypeGetObject)
                            ? _monoTypeGetObject(_monoGetRootDomain(), componentType) : nullptr;
                    if (typeObject) {
                        void *entityClass = _monoObjectGetClass(boardEntity);
                        void *boardComponent = nullptr;
                        int includeInherited = 1;
                        void *getComp = entityClass ? FindMethodInHierarchy(
                                entityClass, OBFUSCATE("GetComponent"), 2) : nullptr;
                        if (getComp) {
                            void *args[2] = {typeObject, &includeInherited};
                            exception = nullptr;
                            boardComponent = _monoRuntimeInvoke(getComp, boardEntity, args,
                                                                &exception);
                            if (exception) boardComponent = nullptr;
                        }
                        if (!boardComponent && entityClass) {
                            getComp = FindMethodInHierarchy(entityClass,
                                                            OBFUSCATE("GetComponent"), 1);
                            if (getComp) {
                                void *args[1] = {typeObject};
                                exception = nullptr;
                                boardComponent = _monoRuntimeInvoke(getComp, boardEntity, args,
                                                                    &exception);
                                if (exception) boardComponent = nullptr;
                            }
                        }
                        if (boardComponent) {
                            void *getPainting = FindMethodInHierarchy(
                                    _monoObjectGetClass(boardComponent),
                                    OBFUSCATE("get_CurrentPainting"), 0);
                            if (getPainting) {
                                exception = nullptr;
                                void *painting = _monoRuntimeInvoke(getPainting, boardComponent,
                                                                    nullptr, &exception);
                                if (!exception && painting) {
                                    *status = u8"画布来自 DrawingBoardComponent（netId="
                                              + std::to_string(netId) + u8"）";
                                    return painting;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // ---- 路线 B（UI）----
    void *gameUi = LoadedImage(OBFUSCATE("XDTGameUI"));
    void *uiManagerClass = FindDrawClass(OBFUSCATE(""), OBFUSCATE("UIManager"));
    void *panelClass = FindDrawClass(OBFUSCATE(""), OBFUSCATE("DrawingPanel"));
    if (uiManagerClass && panelClass) {
        void *getInstance = FindMethodInHierarchy(uiManagerClass, OBFUSCATE("get_Instance"), 0);
        void *uiManager = nullptr;
        if (getInstance) {
            void *exception = nullptr;
            uiManager = _monoRuntimeInvoke(getInstance, nullptr, nullptr, &exception);
            if (exception) uiManager = nullptr;
        }
        if (uiManager) {
            void *getView = FindMethodBySignature(uiManagerClass, OBFUSCATE("GetView"), "?");
            void *panel = nullptr;
            if (getView) {
                void *closed = InflateGenericMethodForClass(getView, panelClass);
                if (closed) {
                    void *exception = nullptr;
                    panel = _monoRuntimeInvoke(closed, uiManager, nullptr, &exception);
                    if (exception) panel = nullptr;
                }
            }
            if (panel) {
                void *painterField = FindFieldInHierarchy(_monoObjectGetClass(panel),
                                                          OBFUSCATE("painter"));
                if (!painterField) {
                    // 退一步：名字里含 painter 的字段
                    if (_monoClassGetFields && _monoFieldName) {
                        void *iter = nullptr;
                        void *field = nullptr;
                        while ((field = _monoClassGetFields(_monoObjectGetClass(panel), &iter))) {
                            const char *name = _monoFieldName(field);
                            if (name && strstr(name, "ainter")) {
                                painterField = field;
                                break;
                            }
                        }
                    }
                }
                if (painterField) {
                    void *painter = nullptr;
                    _monoFieldGetValue(panel, painterField, &painter);
                    if (painter) {
                        *status = u8"画布来自 DrawingPanel.painter（UI 路线）";
                        return painter;
                    }
                }
            }
        }
    }
    (void) gameUi;

    *status = u8"取不到画布：站到画板前、把绘画界面打开再按（DrawingBoardComponent / DrawingPanel 都没命中）";
    return nullptr;
}

// ── 索引化 + RLE ───────────────────────────────────────────────────────────
// 最近色查找用 5bit/通道量化缓存，避免 1024x1024 图 × 128 色 = 上亿次比较。
void MapImageToIndexBytes(const std::vector<uint8_t> &rgba, int srcW, int srcH, int bpp,
                          const Palette &palette, int canvasW, int canvasH,
                          std::vector<uint8_t> *out) {
    out->assign(static_cast<size_t>(canvasW) * canvasH, kTransparentIndexByte);
    std::vector<int8_t> cache(32768, -1);

    for (int y = 0; y < canvasH; ++y) {
        for (int x = 0; x < canvasW; ++x) {
            // 最近邻缩放（pixelData 是 bottom-up，raw 数据也是 bottom-up，方向一致）
            const int sx = (canvasW > 0) ? (x * srcW / canvasW) : 0;
            const int sy = (canvasH > 0) ? (y * srcH / canvasH) : 0;
            const size_t offset = (static_cast<size_t>(sy) * srcW + sx) * bpp;
            if (offset + 3 >= rgba.size()) continue;
            const uint8_t r = rgba[offset + 0];
            const uint8_t g = rgba[offset + 1];
            const uint8_t b = rgba[offset + 2];
            const uint8_t a = (bpp == 4) ? rgba[offset + 3] : 255;
            if (a < 128) {
                (*out)[static_cast<size_t>(y) * canvasW + x] = kTransparentIndexByte;
                continue;
            }

            const int key = ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3);
            int best = cache[key];
            if (best < 0) {
                int bestDistance = 1 << 30;
                best = 0;
                for (int i = 0; i < palette.count; ++i) {
                    const int dr = static_cast<int>(r) - palette.rgb[i * 3 + 0];
                    const int dg = static_cast<int>(g) - palette.rgb[i * 3 + 1];
                    const int db = static_cast<int>(b) - palette.rgb[i * 3 + 2];
                    const int distance = dr * dr + dg * dg + db * db;
                    if (distance < bestDistance) {
                        bestDistance = distance;
                        best = i;
                        if (distance == 0) break;
                    }
                }
                cache[key] = static_cast<int8_t>(best);
            }
            (*out)[static_cast<size_t>(y) * canvasW + x] =
                    static_cast<uint8_t>(kOpaqueFlag | static_cast<uint8_t>(best));
        }
    }
}

struct ColorRun {
    int start;
    int length;   // = 连续长度 - 1（和 PC 版一致）
};

// Dictionary<byte, List<Run>>：平坦下标上按"同字节"做游程
void BuildRuns(const std::vector<uint8_t> &indexBytes,
               std::vector<std::pair<uint8_t, std::vector<ColorRun>>> *out) {
    out->clear();
    const int total = static_cast<int>(indexBytes.size());
    std::vector<int> colorSlot(256, -1);
    int i = 0;
    while (i < total) {
        const uint8_t value = indexBytes[i];
        int j = i + 1;
        while (j < total && indexBytes[j] == value) ++j;
        int slot = colorSlot[value];
        if (slot < 0) {
            colorSlot[value] = static_cast<int>(out->size());
            out->emplace_back(value, std::vector<ColorRun>());
            slot = colorSlot[value];
        }
        (*out)[slot].second.push_back(ColorRun{i, (j - i) - 1});
        i = j;
    }
}

// ── 上传（PC 同款 DrawingOperation）────────────────────────────────────────
bool SendDrawingOperation(const std::vector<std::pair<uint8_t, std::vector<ColorRun>>> &runs,
                          int step, std::string *status) {
    if (runs.empty()) {
        *status = u8"没有可发送的色块";
        return false;
    }
    void *protocolClass = FindDrawClass(OBFUSCATE("XDTDataAndProtocol.ProtocolService.DrawBoard"),
                                        OBFUSCATE("DrawBoardProtoManager"));
    if (!protocolClass) {
        *status = u8"DrawBoardProtoManager 找不到";
        return false;
    }
    void *drawOperation = FindMethodBySignature(protocolClass, OBFUSCATE("DrawingOperation"),
                                                "?ii");
    if (!drawOperation) drawOperation = FindMethodInHierarchy(protocolClass,
                                                              OBFUSCATE("DrawingOperation"), 3);
    if (!drawOperation) {
        *status = u8"DrawBoardProtoManager.DrawingOperation(dict,int,int) 找不到";
        return false;
    }

    void *seqClass = FindDrawClass(OBFUSCATE("XDT.Scene.Shared.Modules.DrawBoard"),
                                   OBFUSCATE("DrawingBatchOperationSequentialInfo"));
    if (!seqClass) {
        seqClass = FindClass(nullptr, 0, OBFUSCATE(""),
                             OBFUSCATE("DrawingBatchOperationSequentialInfo"));
    }
    void *startField = seqClass ? _monoClassGetFieldFromName(seqClass, OBFUSCATE("Start")) : nullptr;
    void *lengthField = seqClass ? _monoClassGetFieldFromName(seqClass, OBFUSCATE("Length"))
                                 : nullptr;
    if (!seqClass || !startField || !lengthField) {
        *status = u8"DrawingBatchOperationSequentialInfo / Start / Length 没解析出来";
        return false;
    }

    // Dictionary<byte, List<SeqInfo>>：按 PC 版的原样用"程序集限定名"构造
    const char *assemblyCandidates[] = {OBFUSCATE("EcsClient"), OBFUSCATE("EcsSystem"),
                                       OBFUSCATE("XDTDataAndProtocol"),
                                       OBFUSCATE("Assembly-CSharp")};
    void *dict = nullptr;
    for (const char *assembly: assemblyCandidates) {
        if (dict) break;
        std::string seqQualified =
                std::string("XDT.Scene.Shared.Modules.DrawBoard."
                            "DrawingBatchOperationSequentialInfo, ") + assembly;
        std::string listName = std::string("System.Collections.Generic.List`1[[") + seqQualified
                               + "]]";
        std::string dictName = std::string("System.Collections.Generic.Dictionary`2[[System.Byte],[")
                               + listName + "]]";
        dict = CreateBclInstance(dictName);
        if (dict) {
            LOGI(OBFUSCATE("[AutoDraw] dict 类型命中程序集 %s"), assembly);
            break;
        }
    }
    if (!dict) {
        *status = u8"Dictionary<byte,List<SeqInfo>> 构造失败（类型名/程序集名不符）";
        return false;
    }

    void *dictClass = _monoObjectGetClass(dict);
    void *setItem = dictClass ? FindMethodInHierarchy(dictClass, OBFUSCATE("set_Item"), 2)
                              : nullptr;
    if (!setItem) {
        *status = u8"Dictionary.set_Item 找不到";
        return false;
    }

    int totalRuns = 0;
    for (const auto &entry: runs) {
        // List<SeqInfo> —— 用 dict 的值类型反推最稳：直接 new 一个 List<SeqInfo>
        std::string listName;
        for (const char *assembly: assemblyCandidates) {
            std::string seqQualified =
                    std::string("XDT.Scene.Shared.Modules.DrawBoard."
                                "DrawingBatchOperationSequentialInfo, ") + assembly;
            std::string candidate = std::string("System.Collections.Generic.List`1[[")
                                    + seqQualified + "]]";
            void *probe = CreateBclInstance(candidate);
            if (probe) {
                listName = candidate;
                // 复用 probe 作为本色的列表
                void *addMethod = FindMethodInHierarchy(_monoObjectGetClass(probe),
                                                        OBFUSCATE("Add"), 1);
                if (!addMethod) {
                    *status = u8"List<SeqInfo>.Add 找不到";
                    return false;
                }
                for (const ColorRun &run: entry.second) {
                    void *seq = _monoObjectNew(_monoGetRootDomain(), seqClass);
                    if (!seq) {
                        *status = u8"SeqInfo 分配失败";
                        return false;
                    }
                    uint16_t start = static_cast<uint16_t>(run.start);
                    uint16_t length = static_cast<uint16_t>(run.length);
                    _monoFieldSetValue(seq, startField, &start);
                    _monoFieldSetValue(seq, lengthField, &length);
                    void *addArgs[1] = {UnboxValue(seq)};
                    void *exception = nullptr;
                    _monoRuntimeInvoke(addMethod, probe, addArgs, &exception);
                    if (exception) {
                        *status = u8"SeqInfo 加入列表时抛异常";
                        return false;
                    }
                    ++totalRuns;
                }
                uint8_t key = entry.first;
                void *setArgs[2] = {&key, probe};
                void *exception = nullptr;
                _monoRuntimeInvoke(setItem, dict, setArgs, &exception);
                if (exception) {
                    *status = u8"Dictionary.set_Item 抛异常";
                    return false;
                }
                break;
            }
        }
        if (listName.empty()) {
            *status = u8"List<SeqInfo> 构造失败";
            return false;
        }
    }

    int stepValue = step;
    int partValue = 0;
    void *opArgs[3] = {dict, &stepValue, &partValue};
    void *exception = nullptr;
    _monoRuntimeInvoke(drawOperation, nullptr, opArgs, &exception);
    if (exception) {
        *status = u8"DrawingOperation 抛异常（画板会话没开？）";
        return false;
    }
    *status = u8"已发送 " + std::to_string(runs.size()) + u8" 种颜色 / "
              + std::to_string(totalRuns) + u8" 段（step=" + std::to_string(step) + u8"）";
    return true;
}

} // namespace

// ============================================================================
//  对外接口
// ============================================================================
// 必须在 Unity 线程上调用（内部大量 mono invoke + 直接写托管数组）。
bool AutoDrawImageToBoardNative(const std::string &imagePath) {
    if (g_autoDrawBusy.exchange(true)) return false;

    std::string status = u8"开始…";
    auto finish = [&](bool ok, const std::string &text) {
        SetDrawStatusText(text);
        LOGI(OBFUSCATE("[AutoDraw] %s"), text.c_str());
        g_autoDrawBusy.store(false, std::memory_order_release);
        return ok;
    };

    void *domain = _monoGetRootDomain ? _monoGetRootDomain() : nullptr;
    if (!domain) return finish(false, u8"失败：Mono 域没起来");
    if (_monoThreadAttach) _monoThreadAttach(domain);

    // 1) 调色板
    Palette palette;
    std::string paletteStatus;
    if (!LoadPalette(&palette, &paletteStatus)) {
        return finish(false, u8"失败（调色板）：" + paletteStatus);
    }

    // 2) 画布：尺寸和 pixelData 都在 CanvasPainter 上
    std::string paintingStatus;
    void *painting = ResolvePaintingObject(&paintingStatus);
    if (!painting) return finish(false, u8"失败：" + paintingStatus);

    int canvasWidth = 0, canvasHeight = 0;
    {
        // ⚠️ CanvasPainter 不是 Texture，绝不能用 Texture 的 get_width/get_height 去 invoke
        // 它的实例（方法体和字段布局完全不同，会读飞）。这里单独按 get_Width/get_Height
        // 解析，名字不对再按子串找。
        void *paintingClass = _monoObjectGetClass(painting);
        TextureReader canvasReader;
        canvasReader.getWidth = TextureReader::DrawFindMethodOn(paintingClass, "get_Width", 0,
                                                                "Width");
        canvasReader.getHeight = TextureReader::DrawFindMethodOn(paintingClass, "get_Height", 0,
                                                                 "Height");
        if (!canvasReader.getWidth || !canvasReader.getHeight
            || !canvasReader.ReadSize(painting, &canvasWidth, &canvasHeight)) {
            return finish(false, u8"失败：画布宽高读不到（" + paintingStatus
                                + u8"；期望 CanvasPainter.get_Width/get_Height）");
        }
    }

    // 3) 解码图片
    std::vector<uint8_t> rgba;
    int imageWidth = 0, imageHeight = 0, imageBpp = 4;
    std::string imageStatus;
    if (!DecodeImageToRgba(imagePath, &rgba, &imageWidth, &imageHeight, &imageStatus)) {
        return finish(false, u8"失败（图片）：" + imageStatus);
    }
    {
        // 解码后 bpp 由纹理格式决定（RGBA32 = 4，RGB24 = 3）
        int formatValue = 0;
        if (g_textureReader.getFormat && _monoRuntimeInvoke) { /* 尺寸已读，格式在 RawData 里判 */ }
        (void) formatValue;
    }
    imageBpp = (rgba.size() >= static_cast<size_t>(imageWidth) * imageHeight * 4) ? 4 : 3;

    // 4) 缩放到画布尺寸 + 映射到调色板下标
    std::vector<uint8_t> indexBytes;
    MapImageToIndexBytes(rgba, imageWidth, imageHeight, imageBpp, palette, canvasWidth,
                         canvasHeight, &indexBytes);

    // 5) 本地画布也写一遍（和 PC 版一样：写 pixelData + Apply，画面立刻出来）
    if (indexBytes.size() == static_cast<size_t>(canvasWidth) * canvasHeight) {
        void *paintingClass = _monoObjectGetClass(painting);
        void *pixelField = paintingClass
                ? _monoClassGetFieldFromName(paintingClass, OBFUSCATE("pixelData")) : nullptr;
        if (!pixelField) {
            // 名字不同就按子串找一个 byte[] 字段
            if (_monoClassGetFields && _monoFieldName) {
                void *iter = nullptr;
                void *field = nullptr;
                while ((field = _monoClassGetFields(paintingClass, &iter))) {
                    const char *name = _monoFieldName(field);
                    if (name && (strstr(name, "pixel") || strstr(name, "Pixel"))) {
                        pixelField = field;
                        break;
                    }
                }
            }
        }
        if (pixelField) {
            void *pixelArray = nullptr;
            _monoFieldGetValue(painting, pixelField, &pixelArray);
            if (pixelArray) {
                const int arrayLength = *reinterpret_cast<int *>(
                        reinterpret_cast<uint8_t *>(pixelArray) + OFF_ARR_COUNT);
                if (arrayLength == static_cast<int>(indexBytes.size())) {
                    memcpy(reinterpret_cast<uint8_t *>(pixelArray) + OFF_ARR_DATA,
                           indexBytes.data(), indexBytes.size());
                    void *apply = FindMethodInHierarchy(paintingClass, OBFUSCATE("Apply"), 0);
                    if (apply) {
                        void *exception = nullptr;
                        _monoRuntimeInvoke(apply, painting, nullptr, &exception);
                    }
                }
            }
        }
    }

    // 6) 上传给服务器（这才是真正"画上去"的一步）
    std::vector<std::pair<uint8_t, std::vector<ColorRun>>> runs;
    BuildRuns(indexBytes, &runs);
    std::string sendStatus;
    const bool sent = SendDrawingOperation(runs, 1, &sendStatus);
    if (!sent) return finish(false, u8"失败（上传）：" + sendStatus);

    return finish(true, u8"成功：" + imageStatus + u8"，画布 " + std::to_string(canvasWidth)
                        + u8"x" + std::to_string(canvasHeight) + u8"，" + sendStatus
                        + u8"。" + paintingStatus);
}

void StartAutoDrawImageNative(std::string imagePath) {
    RunOnUnityThreadAsync([imagePath]() { AutoDrawImageToBoardNative(imagePath); });
}
