#pragma once
#include <string>
#include <cstdint>
#include "Global.h"
#include "ModUtils.h"

// --- Data lookup ---
const char *GetCookerNameByStaticId(int staticId);

// --- Speed hack ---
void SetGlobalTimeScale(float speed);

struct ModMatrix4x4;

// --- Auto Garden ---
void GetPlayerNetIdOnUnityThread();
uint32_t GetSeedNetId(int staticId);
void ScanPotsNative();
void ExecuteAutoHarvest();
void ExecutePlanting();
void AutoGardenBotThread();
void ExecuteWateringAll();
bool LoadAllCropseeds();
void PickAllOwnedMaturePlants(const std::string& dllName, const std::string& protocolNamespace,
                              const std::string& protocolClass, const std::string& payloadRaw);

// --- Auto Plant Flower (Instant & Dense Grid) ---
bool LoadAllFlowerSeeds();
int GetFlowerPlantingCapacity();
bool TryGetHomelandWorldToLocal(const Vector3& playerPos, uint32_t& outFieldNetId, ModMatrix4x4& outW2L);
void ExecuteAutoPlantFlower();

// --- Auto Puzzle (Jigsaw) ---
void PuzzleBingoPiece(uint32_t pieceNetId, bool lockFirst = true);
std::vector<uint32_t> ScanPuzzleBagPiecesNative();
void SafePuzzleSleep(float seconds);
void AutoPuzzleBotThread();

// --- Auto Cook ---
void FetchCookerNamesFromTable();
void ScanCookersNative();
void GetRecipesNative(int targetCookerId);
void ExecChefAction(std::string methodName, uint32_t netId, uint64_t targetId);
int GetCookerStatusNative(CookerTarget cooker);
void AutoCookRoutine(CookerTarget cooker, int recipeId); // legacy fallback
void AutoCookBatchRoutine(int recipeId);
void FailedCookRoutine();
void HelperCookRoutine();
void ProcessBirdWatchingNative();

// --- Anti AFK ---
void AntiAfkRoutine();

// --- Fishing / God Hand Catch ---
void ExecuteGodHandCatchNative();
void AutoCatchLoopThread();
void AutoFarmSmartThread();
void ApplyFishingHackNative();
void UseFishingLureBall();
void AutoLureBallLoop(uint32_t generation);
void AutoFishBaitLoop(uint32_t generation);

// --- Swim / Skate ---
void ExecuteGlobalSwimToggleNative(bool enableSwim);
void ExecuteGlobalSkateToggleNative(bool enableSkate);
void PatchSkateConfigNative(float targetTime, int targetEnergyCost);

// --- Jump Hack ---
void PatchJumpHeightNative(float height);

// --- Remote Shop ---
void OpenRemoteShopNative();

// --- Gacha Banner Selector ---
void ScanGachaPoolsNative();
void OpenGachaRewardsNative(int poolId, int selectedTab = 0);

// --- Local Weather Changer ---
void ScanWeatherTypesNative();
void ApplyLocalWeatherNative(int weatherId, float transitionSeconds);
void ApplySnowSurfaceAmountNative(float amount);
void RestoreLiveWeatherNative();

// --- Local Player Action ---
void ScanPlayerActionsNative();
void PlayPlayerActionNative(int actionId, bool forceLoop);
void StopPlayerActionNative();
// 走游戏自己的 SocialReqTask.OnResp 回放链路在本地演一个社交动作/表情。
// ⚠️ 必须在 Unity 线程调用（EmoteUnlock 与 PlayerAction 共用）。
bool PlaySocialActionLocalNative(int actionId, bool forceLoop);

// --- Unlock All Emotes（移植自 PC 版 buddy/EmoteUnlockFeature.cs）---
// 面板半边：把 ExpressionActionClientService.ExpressionActionComponent() 换成
// 一份"包含全部 id"的列表；点击半边：SendSingleAction / SendEmotion 拦下来，
// 下一帧在 Unity 线程本地补演（未拥有的才补，已拥有的交给服务器回执）。
void ProcessEmoteUnlockOnUpdate();
void ReinstallEmoteUnlockHooksNative();
void PlayEmoteUnlockLocalNowNative(int actionId, bool forceLoop);
std::string GetEmoteUnlockStatus();

// --- Unlock All Two-Person Interactions（移植自 buddy/FriendInteractUnlockFeature.cs）---
// 遍历 TableData.GetFriendInteract(id, false)，把事件锁的行改写成 gainCondition=0 /
// unlockType=0；friendshipLv 与 isDisable 保持原样。
void ApplyFriendInteractUnlockNative();
void ProcessFriendInteractUnlockOnUpdate();
std::string GetFriendInteractUnlockStatus();

// --- Mono 符号自检 / 类名导出（诊断，见 MonoSymbolProbe.cpp）---
void RunMonoSymbolProbeNative();
// 进世界后自动跑一次（只读元数据，不装 detour、不开功能）。
void MaybeRunAutoSymbolProbe();
std::string GetMonoSymbolProbeReport();

// --- 自动绘画（按 PC 版 DrawUploadFeature / DrawColorCodec 移植，见 AutoDraw.cpp）---
// 走 DrawBoardProtoManager.DrawingOperation 的服务端指令 + CanvasPainter.pixelData，
// 不依赖 op=20 的服务器 payload。必须在 Unity 线程 / 世界就绪后调用。
void StartAutoDrawImageNative(std::string imagePath);
bool AutoDrawImageToBoardNative(const std::string &imagePath);
void SetDrawStatusText(const std::string &text);   // 实现于 Main.cpp

// --- 世界就绪判定（进了游戏世界才允许装 hook / 写表）---
bool IsGameWorldReadyNative();

// --- Bubble ---
void ExecuteAutoClaimBubblesNative(std::string dllName, std::string nameSpace, std::string className);
void AutoBubbleLoopThread(std::string dllName, std::string nameSpace, std::string className);

// --- Instant Farm / Gather ---
void PatchGatherConfigNative();
void ExecuteInstantFarmThread(std::string dllName, const std::string& nameSpace, const std::string& className);
void ExecuteLegacyInstantFarmThread(std::string dllName, std::string nameSpace, std::string className);

// --- Misc feature toggles ---
// Varian Now wajib dipanggil hanya dari callback managed/Unity thread.
void DisableTestIndexNowNative();
void AutoDisableTestIndexNative();
void ExecuteNpcCosplayNative(int npcChoice, std::string dllName);

// --- Music Player / Import Picture / Import As Avatar (port dari PC) ---
void PlayMusicBinNative(std::string binPath, bool loop);
void StopMusicPlaybackNative();
void ImportPhotoToGameNative(std::string imagePath);
void ImportPhotoAsAvatarNative(std::string imagePath);

// --- Drawing / Color LUT ---
void *ExtractColorLutTextureNative(void *drawingConfigInst);

// --- Anti Cheat Patch ---
void PatchAntiCheatNative();

// --- ESP / Radar ---
void ExecuteVisualESPScanner();
void RadarLoopThread();

// --- Game Time ---
void SetGameTimeNative(float timeVal);

// --- Fast House ---
void ExecuteFastHouseSetupNative();
void FastHouseLoopThread();

// --- Auto Restore Tool ---
void RefreshCurrentToolDurability();
void AutoRestoreToolRoutine();

// --- Auto Fishing / Auto Eat / Auto Snow ---
void AutoFishingLoop(uint32_t generation);
bool TakeUpRodNative(uint32_t rodNetId = 0);
void PullFishingRodNative(bool pressed);
void CancelFishingNative();
void SetAutoFishHoverNative(bool enable);
void ScanFoodItems();
void AutoEatLoop();
void RunAutoSnowSculptureOnce();
void RunAutoSnowSculptureLoop();
#include "AutoPumpkinSculpture.h"
#include "AutoSandSculpture.h"
#include "BuildFeatures.h"
void AutoCleanSeaLoop();
void AutoOpenBoxes();
void AutoClearTaskLoop();
void AutoQuestUiLoop();

// --- Pet ---
void ScanNearbyPets();
void FeedSelectedPetOnce();
void InteractSelectedPetOnce();
void BathePetOnce();
void DumpMyPets();
void DumpPetFoodCandidates();

// --- Player Scanner ---
void Transfer2EntityNative(uint32_t entityNetId);
void *GetHomelandClientService();
void ExperimentForceSenderSwimNative(uint32_t targetNetId, bool enableSwim);
void ExecutePlayerScanner();

// --- Teleportation & Movement ---
void EnsureTeleportAntiCrashPatched();
void AutoMoveTransferDirect(Vector3 pos);
bool AutoMoveTransferNative(Vector3 pos);
void Transfer2EntityDirect(uint32_t entityNetId);
Vector3 GetSelfPositionNative();
void ManualTeleportNative(Vector3 pos); 
std::string GetPlayerNameByNetId(uint32_t playerNetId);

// --- Camera Controls ---
void *GetFollowCameraControllerNative();
void ApplyCameraSettingsNative();
void ResetCameraSettings();
std::string GetCameraStatusString();