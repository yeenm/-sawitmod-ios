#pragma once

#include <string>

// Agent diaktifkan dari GameApp_Update setelah siklus TestIndex pertama,
// bukan dari startup worker, agar akses akun/device menunggu Unity siap.
#define SAWIT_CLIENT_CONTROL_ENABLED 1

namespace ClientControlAgent {
    // Pasang hook lifecycle akun setelah Mono/assembly siap, lalu mulai worker.
    // Tidak bergantung pada GameApp_Update yang belum aktif di layar login.
    bool InitializeAfterMono();
    void Start();
    void Stop();

    // Wajib dipanggil dari GameApp_Update agar seluruh akses managed tetap
    // berjalan pada Unity thread.
    void PumpUnityCommands();

    // Sinkronisasi status login mod dan full license key untuk telemetry.
    void SetModSession(const std::string& license);
    void ClearModSession();
}
