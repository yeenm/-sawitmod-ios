#pragma once

#include <string>
#include "Global.h"

namespace PayloadVM {
    struct Context {
        std::string nonce;
        std::string hwid;
        std::string wireId;
        std::string version;
        std::string channel;
        int expectedOp = 0;
    };

    // Core decoder untuk jalur sinkron yang sudah berada di target Virbox lain.
    bool InterpretRaw(const std::string& envelope, const Context& ctx, InvokeRequest& req);

    // Wrapper target Virbox mandiri. Hanya dipanggil dari Unity thread, bukan
    // dari dalam Virbox_FetchPayload/Virbox_FetchAndExecute.
    bool InterpretProtected(const std::string& envelope, const Context& ctx, InvokeRequest& req);

    // Adaptor metadata biner SVMN untuk handler fitur khusus lama.
    std::string MetadataValue(const std::string& packed, const std::string& key);

    // Menghapus isi buffer sebelum dilepas.
    void WipeString(std::string& value);
}
