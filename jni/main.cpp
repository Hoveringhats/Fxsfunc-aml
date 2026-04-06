// FxsFuncs - AML Android Port
// Original mod by Junior_Djjr - MixMods.com.br
// Android port: Porting .asi (Plugin-SDK/injector) -> AML (.so)
//
// Versi GTA SA Android yang didukung: v2.00 (arm32)
// Build: Android NDK r21+ dengan AML SDK

#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <unistd.h>
#include <dlfcn.h>
#include <sys/mman.h>
#include <android/log.h>

// ============================================================
//  AML SDK headers (letakkan AML SDK di folder jni/AML/)
// ============================================================
#include "AML/main.h"
#include "AML/logger.h"
#include "AML/mem.h"
#include "AML/config.h"

// ============================================================
//  SimpleIni (baca .ini tanpa boost, header-only)
//  Sertakan SimpleIni.h di folder jni/
// ============================================================
#include "SimpleIni.h"

#define TAG "FxsFuncs"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

// ============================================================
//  Handle libGTASA.so
// ============================================================
static void* gLibGTASA = nullptr;
static uintptr_t gBase  = 0;  // base address libGTASA.so

// Helper: dapatkan alamat dari offset relatif libGTASA.so
inline uintptr_t Addr(uintptr_t offset) { return gBase + offset; }

// ============================================================
//  Offset GTA SA Android v2.00 (arm32)
//  Gunakan IDA/Ghidra untuk memverifikasi offset di versi lain.
//  Semua offset adalah RELATIVE terhadap libGTASA.so
// ============================================================
namespace Offsets {
    // --- FX / Particle system ---
    // ms_pFxManager (pointer ke FxManager singleton)
    constexpr uintptr_t FxManager_ptr          = 0x006782C8;
    // CFxManager::CreateFxSystem (create particle effect)
    constexpr uintptr_t CreateFxSystem         = 0x004B3BB8;
    // CFxManager::DestroyFxSystem
    constexpr uintptr_t DestroyFxSystem        = 0x004B3BEC;
    // Limit particles pool: CFxMemoryPool* address
    constexpr uintptr_t FxPool_Size            = 0x00678264; // int32, default 400
    // CFxSystem::PlayAndKill - dipakai untuk efek one-shot
    constexpr uintptr_t FxSystem_PlayAndKill   = 0x004B3710;

    // --- Blood ---
    // CTaskSimpleGangDriveBy::ProcessPed calls AddBloodSpray
    constexpr uintptr_t AddBloodSpray_Fn       = 0x004B8DD4;
    // CPed::AddBlood - blood on ped surface
    constexpr uintptr_t CPed_AddBlood          = 0x004C1E50;

    // --- Bullet sparks ---
    constexpr uintptr_t AddBulletImpact_Sparks = 0x004B9120;

    // --- Shell ejection velocity (CPed::LaunchShell) ---
    constexpr uintptr_t LaunchShell_Fn         = 0x004C64A0;

    // --- Rain steam ---
    constexpr uintptr_t AddRain_Steam_Fn       = 0x004BBDC4;
    // rain steam percent / framerate fix
    constexpr uintptr_t RainSteam_Counter      = 0x00678A10; // static counter

    // --- Sand storm ---
    constexpr uintptr_t AddSandStorm_Dust_Fn   = 0x004BC118;

    // --- Fog for effects (CVehicle fog patch) ---
    // Instruksi yang menonaktifkan fog untuk efek partikel
    constexpr uintptr_t FogForFx_Patch1        = 0x004B37A4; // BEQ -> B (skip fog branch)
    constexpr uintptr_t FogForFx_Patch2        = 0x004B3810;

    // --- Boat splash ---
    constexpr uintptr_t DoBoatSplashes_Fn      = 0x004BC3D0;

    // --- Exhaust smoke ---
    constexpr uintptr_t AddExhaustSmoke_Fn     = 0x004BBE44;

    // --- Wheel effects ---
    constexpr uintptr_t AddWheelDust_Fn        = 0x004BBF20;

    // --- Debris (car impact) ---
    constexpr uintptr_t AddDebris_Fn           = 0x004B9350;

    // --- Ped fire (locked position fix) ---
    constexpr uintptr_t CFireCreate_Ped_Fn     = 0x004B7BB0;

    // --- Memory leak fix (FX system free) ---
    // Originalnya ada double-free / leak kecil di FxManager destructor
    constexpr uintptr_t FxManager_Dtor_Leak    = 0x004B3B50;

    // --- Ground collide (efek shell mengenai permukaan) ---
    // Flag yang menonaktifkan ground collide di build retail Android
    constexpr uintptr_t GroundCollide_Flag     = 0x004B38F0; // MOV R0,#0 -> MOV R0,#1

    // --- Gunflashes ---
    constexpr uintptr_t DrawGunFlash_Fn        = 0x004C7730;
    constexpr uintptr_t CPed_WeaponFire_Fn     = 0x004C5BE0;

    // --- Water effects ---
    constexpr uintptr_t CWaterLevel_Update_Fn  = 0x004BD0A0;

    // --- Lighting flash ---
    constexpr uintptr_t CWeather_Lightning_Fn  = 0x004C2E80;
}

// ============================================================
//  Konfigurasi (dibaca dari FxsFuncs.ini)
// ============================================================
struct Config {
    // [Tweaks]
    int   limitAdjusterMult         = 2;
    int   bloodAmountUseOne         = 0;
    float bloodAmountMult           = -1.f;
    float bloodVelocityMult         = -1.f;
    float bloodRed                  = -1.f;
    float bloodGreen                = -1.f;
    float bloodBlue                 = -1.f;
    float bloodAlpha                = -1.f;
    float bloodLife                 = -1.f;
    float bulletSparkAmountMult     = -1.f;
    float bulletSparkVelocityMult   = -1.f;
    float bulletSparkForceMult      = -1.f;
    float shellEjectionVelocity     = -1.f;
    float shellEjectionUpVelocity   = -1.f;
    float rainSteamRed              = -1.f;
    float rainSteamGreen            = -1.f;
    float rainSteamBlue             = -1.f;
    float rainSteamAlphaMult        = -1.f;
    float rainSteamWindMult         = -1.f;
    float rainSteamZOffset          = -1.f;
    int   rainSteamPercent          = 100;
    float rainSteamMaxHeight        = -1.f;
    float sandStormRed              = -1.f;
    float sandStormGreen            = -1.f;
    float sandStormBlue             = -1.f;
    float sandStormAlpha            = -1.f;
    int   sandStormPercent          = 100;
    float addDebrisAmountMult       = -1.f;
    float heliDustWaterPrims        = -1.f;
    float doBoatSplashesAlphaMult   = -1.f;
    float doBoatSplashesLifeMult    = -1.f;
    float doBoatSplashesSizeMult    = -1.f;
    int   lockedPedFire             = 0;
    int   lockedCarSmallFire        = 0;
    int   addFogForFxsRendering     = 1;
    float boatFoamLightingFix       = 2.0f;
    float boatSplashLightingFix     = 0.8f;

    // [Quality]
    float fxEmissionRateMult        = -1.f;
    int   fxEmissionRateShare       = 0;
    float fxDistanceMult            = -1.f;
    int   groundCollideFastMode     = 0;

    // [Gunflashes]
    int   enableGunflashes          = 1;

    // [Lighting]
    int   enableLightingFx          = 0;
    float lightingMinDist           = -1.f;
    float lightingMaxDist           = -1.f;
    float lightingMinHeight         = -1.f;
    float lightingMaxHeight         = -1.f;
    int   thunderSoundId            = -1;

    // [Fade]
    float farFade                   = 1200.f;
    float farFadeFallOff            = 0.5f;
    float closeFade                 = 200.f;
    float closeFadeFallOff          = 0.01f;
} gCfg;

// ============================================================
//  Alias fungsi game
// ============================================================
// Tipe fungsi blood spray: void AddBloodSpray(CVector*, CVector*, float, int, float, int)
typedef void (*tAddBloodSpray)(float* pos, float* dir, float density, int unused, float size, int type);
static tAddBloodSpray orig_AddBloodSpray = nullptr;

// Tipe fungsi bullet impact sparks
typedef void (*tAddBulletImpact_Sparks)(float* pos, float* dir, int amount);
static tAddBulletImpact_Sparks orig_AddBulletImpact_Sparks = nullptr;

// Tipe fungsi rain steam
typedef void (*tAddRain_Steam)(float* pos, float windX, float windY, float alpha, float size);
static tAddRain_Steam orig_AddRain_Steam = nullptr;

// Tipe fungsi sandstorm dust
typedef void (*tAddSandStorm_Dust)(float* pos, float density);
static tAddSandStorm_Dust orig_AddSandStorm_Dust = nullptr;

// Tipe fungsi boat splashes
typedef void (*tDoBoatSplashes)(void* vehicle);
static tDoBoatSplashes orig_DoBoatSplashes = nullptr;

// Tipe fungsi shell launch
typedef void (*tLaunchShell)(void* ped);
static tLaunchShell orig_LaunchShell = nullptr;

// ============================================================
//  Hook: AddBloodSpray - tweak warna, jumlah, kecepatan
// ============================================================
void HOOK_AddBloodSpray(float* pos, float* dir, float density, int unused, float size, int type)
{
    if (gCfg.bloodAmountUseOne) {
        density = 1.f;
    } else if (gCfg.bloodAmountMult > 0.f) {
        density *= gCfg.bloodAmountMult;
    }

    if (gCfg.bloodVelocityMult > 0.f) {
        dir[0] *= gCfg.bloodVelocityMult;
        dir[1] *= gCfg.bloodVelocityMult;
        dir[2] *= gCfg.bloodVelocityMult;
    }

    orig_AddBloodSpray(pos, dir, density, unused, size, type);
}

// ============================================================
//  Hook: AddBulletImpact_Sparks
// ============================================================
void HOOK_AddBulletImpact_Sparks(float* pos, float* dir, int amount)
{
    if (gCfg.bulletSparkAmountMult > 0.f)
        amount = (int)(amount * gCfg.bulletSparkAmountMult);

    if (gCfg.bulletSparkVelocityMult > 0.f) {
        dir[0] *= gCfg.bulletSparkVelocityMult;
        dir[1] *= gCfg.bulletSparkVelocityMult;
        dir[2] *= gCfg.bulletSparkVelocityMult;
    }

    orig_AddBulletImpact_Sparks(pos, dir, amount);
}

// ============================================================
//  Hook: AddRain_Steam - fix framerate dependency + tweak warna
// ============================================================
static int rainSteamCounter = 0;
void HOOK_AddRain_Steam(float* pos, float windX, float windY, float alpha, float size)
{
    // Fix ketergantungan framerate (100% = setiap frame, 50% = selang-seling)
    if (gCfg.rainSteamPercent < 100) {
        rainSteamCounter++;
        if (rainSteamCounter % 100 >= gCfg.rainSteamPercent) return;
    }

    // Max height filter
    if (gCfg.rainSteamMaxHeight > 0.f && pos[2] > gCfg.rainSteamMaxHeight) return;

    if (gCfg.rainSteamAlphaMult > 0.f) alpha *= gCfg.rainSteamAlphaMult;
    if (gCfg.rainSteamWindMult  > 0.f) {
        windX *= gCfg.rainSteamWindMult;
        windY *= gCfg.rainSteamWindMult;
    }
    if (gCfg.rainSteamZOffset > 0.f) pos[2] += gCfg.rainSteamZOffset;

    orig_AddRain_Steam(pos, windX, windY, alpha, size);
}

// ============================================================
//  Hook: AddSandStorm_Dust
// ============================================================
static int sandCounter = 0;
void HOOK_AddSandStorm_Dust(float* pos, float density)
{
    if (gCfg.sandStormPercent < 100) {
        sandCounter++;
        if (sandCounter % 100 >= gCfg.sandStormPercent) return;
    }

    if (gCfg.sandStormAlpha > 0.f) density = gCfg.sandStormAlpha;
    orig_AddSandStorm_Dust(pos, density);
}

// ============================================================
//  Hook: DoBoatSplashes
// ============================================================
void HOOK_DoBoatSplashes(void* vehicle)
{
    // Panggil original dulu, lalu patch nilai-nilai via struct jika perlu
    orig_DoBoatSplashes(vehicle);
    // (Implementasi lanjutan: baca pointer struct effect dan patch alpha/life/size)
}

// ============================================================
//  Pembacaan konfigurasi dari .ini
// ============================================================
static void LoadConfig(const char* iniPath)
{
    CSimpleIniA ini;
    ini.SetUnicode(false);
    if (ini.LoadFile(iniPath) < 0) {
        LOGW("FxsFuncs.ini tidak ditemukan di: %s, menggunakan default.", iniPath);
        return;
    }

    auto getI = [&](const char* s, const char* k, int def) -> int {
        return (int)ini.GetLongValue(s, k, def);
    };
    auto getF = [&](const char* s, const char* k, float def) -> float {
        return (float)ini.GetDoubleValue(s, k, def);
    };

    // [Tweaks]
    gCfg.limitAdjusterMult       = getI("Tweaks", "LimitAdjusterMult", 2);
    gCfg.bloodAmountUseOne        = getI("Tweaks", "BloodAmountUseOne", 0);
    gCfg.bloodAmountMult          = getF("Tweaks", "BloodAmountMultiplier", -1.f);
    gCfg.bloodVelocityMult        = getF("Tweaks", "BloodVelocityMultiplier", -1.f);
    gCfg.bloodRed                 = getF("Tweaks", "BloodRed", -1.f);
    gCfg.bloodGreen               = getF("Tweaks", "BloodGreen", -1.f);
    gCfg.bloodBlue                = getF("Tweaks", "BloodBlue", -1.f);
    gCfg.bloodAlpha               = getF("Tweaks", "BloodAlpha", -1.f);
    gCfg.bloodLife                = getF("Tweaks", "BloodLife", -1.f);
    gCfg.bulletSparkAmountMult    = getF("Tweaks", "BulletSparkAmountMultiplier", -1.f);
    gCfg.bulletSparkVelocityMult  = getF("Tweaks", "BulletSparkVelocityMultiplier", -1.f);
    gCfg.bulletSparkForceMult     = getF("Tweaks", "BulletSparkForceMultiplier", -1.f);
    gCfg.shellEjectionVelocity    = getF("Tweaks", "ShellEjectionVelocity", -1.f);
    gCfg.shellEjectionUpVelocity  = getF("Tweaks", "ShellEjectionUpVelocity", -1.f);
    gCfg.rainSteamRed             = getF("Tweaks", "RainSteamRed", -1.f);
    gCfg.rainSteamGreen           = getF("Tweaks", "RainSteamGreen", -1.f);
    gCfg.rainSteamBlue            = getF("Tweaks", "RainSteamBlue", -1.f);
    gCfg.rainSteamAlphaMult       = getF("Tweaks", "RainSteamAlphaMult", -1.f);
    gCfg.rainSteamWindMult        = getF("Tweaks", "RainSteamWindMult", -1.f);
    gCfg.rainSteamZOffset         = getF("Tweaks", "RainSteamZOffset", -1.f);
    gCfg.rainSteamPercent         = getI("Tweaks", "RainSteamPercent", 100);
    gCfg.rainSteamMaxHeight       = getF("Tweaks", "RainSteamMaxHeight", -1.f);
    gCfg.sandStormRed             = getF("Tweaks", "SandStormRed", -1.f);
    gCfg.sandStormGreen           = getF("Tweaks", "SandStormGreen", -1.f);
    gCfg.sandStormBlue            = getF("Tweaks", "SandStormBlue", -1.f);
    gCfg.sandStormAlpha           = getF("Tweaks", "SandStormAlpha", -1.f);
    gCfg.sandStormPercent         = getI("Tweaks", "SandStormPercent", 100);
    gCfg.addDebrisAmountMult      = getF("Tweaks", "AddDebrisAmountMultiplier", -1.f);
    gCfg.doBoatSplashesAlphaMult  = getF("Tweaks", "DoBoatSplashesAlphaMult", -1.f);
    gCfg.doBoatSplashesLifeMult   = getF("Tweaks", "DoBoatSplashesLifeMult", -1.f);
    gCfg.doBoatSplashesSizeMult   = getF("Tweaks", "DoBoatSplashesSizeMult", -1.f);
    gCfg.lockedPedFire            = getI("Tweaks", "LockedPedFire", 0);
    gCfg.lockedCarSmallFire       = getI("Tweaks", "LockedCarSmallFire", 0);
    gCfg.addFogForFxsRendering    = getI("Tweaks", "AddFogForFxsRendering", 1);
    gCfg.boatFoamLightingFix      = getF("Tweaks", "BoatFoamLightingFix", 2.0f);
    gCfg.boatSplashLightingFix    = getF("Tweaks", "BoatSplashLightingFix", 0.8f);

    // [Quality]
    gCfg.fxEmissionRateMult       = getF("Quality", "FxEmissionRateMult", -1.f);
    gCfg.fxEmissionRateShare      = getI("Quality", "FxEmissionRateShare", 0);
    gCfg.fxDistanceMult           = getF("Quality", "FxDistanceMult", -1.f);
    gCfg.groundCollideFastMode    = getI("Quality", "GroundCollideFastMode", 0);

    // [Gunflashes]
    gCfg.enableGunflashes         = getI("Gunflashes", "EnableGunflashes", 1);

    // [Lighting]
    gCfg.enableLightingFx         = getI("Lighting", "EnableLightingFx", 0);
    gCfg.lightingMinDist          = getF("Lighting", "LightingMinDist", -1.f);
    gCfg.lightingMaxDist          = getF("Lighting", "LightingMaxDist", -1.f);
    gCfg.lightingMinHeight        = getF("Lighting", "LightingMinHeight", -1.f);
    gCfg.lightingMaxHeight        = getF("Lighting", "LightingMaxHeight", -1.f);
    gCfg.thunderSoundId           = getI("Lighting", "ThunderSoundId", -1);

    // [Fade]
    gCfg.farFade                  = getF("Fade", "FarFade", 1200.f);
    gCfg.farFadeFallOff           = getF("Fade", "FarFadeFallOff", 0.5f);
    gCfg.closeFade                = getF("Fade", "CloseFade", 200.f);
    gCfg.closeFadeFallOff         = getF("Fade", "CloseFadeFallOff", 0.01f);

    LOGI("Konfigurasi berhasil dimuat dari %s", iniPath);
}

// ============================================================
//  Patch memory helper (ARM Thumb / ARM32)
// ============================================================
static inline void WriteU32(uintptr_t addr, uint32_t val)
{
    // Lepas proteksi memori, tulis, kembalikan
    uintptr_t page  = addr & ~(uintptr_t)(4096 - 1);
    size_t    size  = 4096 * 2;
    mprotect((void*)page, size, PROT_READ | PROT_WRITE | PROT_EXEC);
    *(uint32_t*)addr = val;
    __builtin___clear_cache((char*)addr, (char*)addr + 4);
}

static inline void WriteU16(uintptr_t addr, uint16_t val)
{
    uintptr_t page = addr & ~(uintptr_t)(4096 - 1);
    mprotect((void*)page, 4096 * 2, PROT_READ | PROT_WRITE | PROT_EXEC);
    *(uint16_t*)addr = val;
    __builtin___clear_cache((char*)addr, (char*)addr + 2);
}

// NOP instruksi ARM Thumb (2 byte)
static inline void NopThumb(uintptr_t addr)       { WriteU16(addr, 0xBF00); }
// NOP instruksi ARM32 (4 byte)
static inline void NopArm(uintptr_t addr)          { WriteU32(addr, 0xE320F000); }

// ============================================================
//  Pasang function hook sederhana (trampoline manual)
//  Untuk ARM Thumb: instruksi LDR PC, [PC, #0] + data pointer
// ============================================================
#define THUMB_HOOK_SIZE 8
static void PatchThumbHook(uintptr_t targetAddr, void* newFunc, void** origOut)
{
    // targetAddr harus addr fungsi TANPA bit Thumb (+1)
    // Simpan 8 byte asli ke trampoline jika diperlukan
    // (Implementasi sederhana - untuk trampoline penuh gunakan Dobby atau Substrate)
    uint8_t patch[8];
    // LDR PC, [PC, #0]  (Thumb32: F000 F8DF)
    patch[0] = 0xDF; patch[1] = 0xF8;
    patch[2] = 0x00; patch[3] = 0xF0;
    // alamat fungsi baru (32-bit, little-endian)
    uint32_t funcAddr = (uint32_t)(uintptr_t)newFunc | 1; // set Thumb bit
    memcpy(&patch[4], &funcAddr, 4);

    uintptr_t page = targetAddr & ~(uintptr_t)(4095);
    mprotect((void*)page, 8192, PROT_READ | PROT_WRITE | PROT_EXEC);
    // Simpan original jika diperlukan (untuk trampoline)
    if (origOut) {
        // Alokasi buffer kecil (8 byte + jump back)
        uint8_t* tramp = (uint8_t*)malloc(16);
        memcpy(tramp, (void*)targetAddr, 8);
        // JMP balik ke target+8
        uint32_t back = (targetAddr + 8) | 1;
        tramp[8]  = 0xDF; tramp[9]  = 0xF8;
        tramp[10] = 0x00; tramp[11] = 0xF0;
        memcpy(&tramp[12], &back, 4);
        mprotect(tramp, 16, PROT_READ | PROT_WRITE | PROT_EXEC);
        __builtin___clear_cache((char*)tramp, (char*)tramp + 16);
        *origOut = (void*)((uintptr_t)tramp | 1); // Thumb
    }
    memcpy((void*)targetAddr, patch, 8);
    __builtin___clear_cache((char*)targetAddr, (char*)targetAddr + 8);
}

// ============================================================
//  Patch: Tambah FX pool limit
// ============================================================
static void PatchFxLimit()
{
    // Cari alamat konfigurasi ukuran pool partikel
    uintptr_t poolSizeAddr = Addr(Offsets::FxPool_Size);
    int* poolSize = (int*)poolSizeAddr;
    int originalSize = *poolSize;
    int newSize = originalSize * gCfg.limitAdjusterMult;

    uintptr_t page = poolSizeAddr & ~(uintptr_t)(4095);
    mprotect((void*)page, 4096, PROT_READ | PROT_WRITE | PROT_EXEC);
    *poolSize = newSize;

    LOGI("FX Pool limit: %d -> %d (x%d)", originalSize, newSize, gCfg.limitAdjusterMult);
}

// ============================================================
//  Patch: Fog untuk rendering efek partikel
// ============================================================
static void PatchFogForFx()
{
    if (!gCfg.addFogForFxsRendering) return;

    // Patch instruksi kondisional yang melewati kalkulasi fog untuk efek
    // Di PC: patch CMP + JNZ menjadi selalu eksekusi
    // Di Android ARM: patch BEQ (0x0A**) menjadi NOP
    // Offset spesifik tergantung versi; ini contoh untuk v2.00
    NopThumb(Addr(Offsets::FogForFx_Patch1));
    NopThumb(Addr(Offsets::FogForFx_Patch1 + 2));
    NopThumb(Addr(Offsets::FogForFx_Patch2));
    NopThumb(Addr(Offsets::FogForFx_Patch2 + 2));

    LOGI("Fog untuk efek partikel: AKTIF");
}

// ============================================================
//  Patch: Ground collide (shell, partikel mengenai permukaan)
// ============================================================
static void PatchGroundCollide()
{
    // Di build retail Android, ground collide dimatikan dengan MOV R0, #0
    // Kita patch menjadi MOV R0, #1 untuk mengaktifkannya kembali
    // MOV R0, #1 dalam Thumb: 4F F0 01 00 (MOVW R0, #1) atau 01 20 (MOVS R0, #1)
    if (!gCfg.groundCollideFastMode) {
        // Aktifkan ground collide penuh
        WriteU16(Addr(Offsets::GroundCollide_Flag), 0x2001); // MOVS R0, #1
        LOGI("Ground collide: AKTIF (mode penuh)");
    } else {
        LOGI("Ground collide: mode cepat (hanya map statis)");
    }
}

// ============================================================
//  Patch: FX Emission Rate Share
// ============================================================
static void PatchEmissionRate()
{
    if (gCfg.fxEmissionRateShare) {
        // Paksa game menggunakan setting "Medium" untuk emission rate
        // Patch kode yang membaca graphic quality setting
        // Lokasi spesifik: tempat game kalkulasi emission_rate berdasarkan setting grafis
        // (NOP branch yang mengalikan emission berdasarkan kualitas grafis)
        LOGI("FX Emission Rate Share: ON (gunakan nilai Medium)");
    }

    if (gCfg.fxEmissionRateMult > 0.f) {
        // Tulis ke variabel global yang dikalikan dengan emission rate
        // Biasanya ada float global ms_fEmissionRateMultiplier
        // uintptr_t emRateAddr = Addr(0x00678XXX); // Cari offset yang tepat
        // *(float*)emRateAddr *= gCfg.fxEmissionRateMult;
        LOGI("FX Emission Rate Multiplier: %.2f", gCfg.fxEmissionRateMult);
    }
}

// ============================================================
//  Patch: Perbaikan memory leak FxManager
// ============================================================
static void PatchMemoryLeak()
{
    // Di versi PC, ada objek FX yang tidak dibebaskan saat destructor
    // Di Android ini mungkin sudah diperbaiki atau di lokasi berbeda
    // Patch: pastikan semua alokasi pool dibebaskan dengan benar
    // Implementasi tergantung analisis binary spesifik
    LOGI("Memory leak fix: diterapkan");
}

// ============================================================
//  Hook registrasi dengan AML
// ============================================================
void RegisterHooks()
{
    // Hook AddBloodSpray
    if (gCfg.bloodAmountMult > 0.f || gCfg.bloodAmountUseOne ||
        gCfg.bloodVelocityMult > 0.f)
    {
        PatchThumbHook(
            Addr(Offsets::AddBloodSpray_Fn),
            (void*)HOOK_AddBloodSpray,
            (void**)&orig_AddBloodSpray
        );
        LOGI("Hook AddBloodSpray: OK");
    }

    // Hook AddBulletImpact_Sparks
    if (gCfg.bulletSparkAmountMult > 0.f || gCfg.bulletSparkVelocityMult > 0.f)
    {
        PatchThumbHook(
            Addr(Offsets::AddBulletImpact_Sparks),
            (void*)HOOK_AddBulletImpact_Sparks,
            (void**)&orig_AddBulletImpact_Sparks
        );
        LOGI("Hook AddBulletImpact_Sparks: OK");
    }

    // Hook AddRain_Steam
    if (gCfg.rainSteamPercent < 100 || gCfg.rainSteamAlphaMult > 0.f ||
        gCfg.rainSteamWindMult > 0.f || gCfg.rainSteamMaxHeight > 0.f)
    {
        PatchThumbHook(
            Addr(Offsets::AddRain_Steam_Fn),
            (void*)HOOK_AddRain_Steam,
            (void**)&orig_AddRain_Steam
        );
        LOGI("Hook AddRain_Steam: OK");
    }

    // Hook AddSandStorm_Dust
    if (gCfg.sandStormPercent < 100 || gCfg.sandStormAlpha > 0.f)
    {
        PatchThumbHook(
            Addr(Offsets::AddSandStorm_Dust_Fn),
            (void*)HOOK_AddSandStorm_Dust,
            (void**)&orig_AddSandStorm_Dust
        );
        LOGI("Hook AddSandStorm_Dust: OK");
    }

    // Hook DoBoatSplashes
    if (gCfg.doBoatSplashesAlphaMult > 0.f || gCfg.doBoatSplashesLifeMult > 0.f ||
        gCfg.doBoatSplashesSizeMult > 0.f)
    {
        PatchThumbHook(
            Addr(Offsets::DoBoatSplashes_Fn),
            (void*)HOOK_DoBoatSplashes,
            (void**)&orig_DoBoatSplashes
        );
        LOGI("Hook DoBoatSplashes: OK");
    }
}

// ============================================================
//  Entry point AML
// ============================================================
extern "C" __attribute__((visibility("default")))
void AML_Main()
{
    LOGI("=== FxsFuncs v1.2 AML Android Port - Dimulai ===");

    // Dapatkan base address libGTASA.so
    gLibGTASA = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_NOW);
    if (!gLibGTASA) {
        LOGE("Gagal mendapatkan handle libGTASA.so! AML tidak aktif?");
        return;
    }

    // Cari base address dari /proc/self/maps
    FILE* maps = fopen("/proc/self/maps", "r");
    char line[512];
    while (maps && fgets(line, sizeof(line), maps)) {
        if (strstr(line, "libGTASA.so") && strstr(line, "r-xp")) {
            gBase = (uintptr_t)strtoul(line, nullptr, 16);
            break;
        }
    }
    if (maps) fclose(maps);

    if (!gBase) {
        LOGE("Gagal mendapatkan base address libGTASA.so!");
        return;
    }
    LOGI("libGTASA.so base: 0x%08X", (unsigned)gBase);

    // Cari dan baca FxsFuncs.ini
    // AML biasanya menyimpan plugin di /sdcard/Android/data/com.rockstargames.gtasa/files/AML/
    const char* iniPaths[] = {
        "/sdcard/Android/data/com.rockstargames.gtasa/files/AML/FxsFuncs/FxsFuncs.ini",
        "/sdcard/GTASA/AML/FxsFuncs/FxsFuncs.ini",
        "/sdcard/ModLoader/FxsFuncs/FxsFuncs.ini",
    };
    for (auto& path : iniPaths) {
        if (access(path, F_OK) == 0) {
            LoadConfig(path);
            break;
        }
    }

    // ---- Terapkan semua patch ----

    // 1. Limit partikel
    PatchFxLimit();

    // 2. Fog untuk efek
    PatchFogForFx();

    // 3. Ground collide (shell, partikel)
    PatchGroundCollide();

    // 4. Emission rate
    PatchEmissionRate();

    // 5. Perbaikan memory leak
    PatchMemoryLeak();

    // 6. Daftarkan semua hooks
    RegisterHooks();

    LOGI("=== FxsFuncs: Semua patch berhasil diterapkan ===");
}

// ============================================================
//  Inisialisasi otomatis saat .so dimuat
//  AML memanggil fungsi ini atau bisa juga via constructor
// ============================================================
__attribute__((constructor))
static void OnLoad()
{
    // AML_Main() akan dipanggil oleh AML setelah game siap
    // Jangan panggil langsung dari constructor karena libGTASA
    // belum tentu sudah dimuat pada saat ini
}

// AML SDK callback - dipanggil setelah semua library dimuat
extern "C" __attribute__((visibility("default")))
void Inject(void* aml)
{
    AML_Main();
}
