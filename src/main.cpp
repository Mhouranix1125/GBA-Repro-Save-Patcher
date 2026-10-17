
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using Byte = std::uint8_t;

extern "C" {
    extern unsigned char payload_bin[];
    extern unsigned int payload_bin_len;
}

constexpr wchar_t APP_TITLE[] = L"GBA Repro Save Patcher";
constexpr wchar_t APP_VERSION[] = L"0.4.3";
constexpr std::size_t MAX_GBA_ROM = 0x02000000;
constexpr std::size_t ROM_ALIGN = 0x40000;

enum ControlId {
    IDC_ROM_PATH = 1001,
    IDC_BROWSE_ROM,
    IDC_ANALYZE,
    IDC_TITLE_VALUE,
    IDC_CODE_VALUE,
    IDC_LIBRARY_VALUE,
    IDC_SIZE_VALUE,
    IDC_PATCH_STATE_VALUE,
    IDC_EFFECTIVE_VALUE,
    IDC_COMPAT_VALUE,
    IDC_TARGET_COMBO,
    IDC_OUTPUT_FOLDER,
    IDC_BROWSE_OUTPUT_FOLDER,
    IDC_OUTPUT_FILE_VALUE,
    IDC_PATCH,
    IDC_OPEN_OUTPUT,
    IDC_LOG,
    IDC_STATUS,
    IDC_ABOUT
};

enum class TargetMode {
    None = 0,
    Sram = 1,
    Flash512 = 2
};

struct BinaryPatch {
    std::vector<Byte> marker;
    std::vector<Byte> replacement;
    std::vector<bool> wildcard;
};

struct RomInfo {
    bool loaded = false;
    bool analyzed = false;
    bool flash512Patched = false;
    fs::path path;
    std::vector<Byte> rom;
    std::wstring title;
    std::wstring gameCode;
    std::string saveTag = "UNKNOWN";
    bool stage1Patched = false;
    std::wstring patchState;
    std::wstring effectiveTarget;
    std::wstring compatibility;
};

static HWND g_hwnd = nullptr;
static HWND g_romPath = nullptr;
static HWND g_titleValue = nullptr;
static HWND g_codeValue = nullptr;
static HWND g_libraryValue = nullptr;
static HWND g_sizeValue = nullptr;
static HWND g_patchStateValue = nullptr;
static HWND g_effectiveValue = nullptr;
static HWND g_compatValue = nullptr;
static HWND g_targetCombo = nullptr;
static HWND g_outputFolder = nullptr;
static HWND g_outputFileValue = nullptr;
static HWND g_patchBtn = nullptr;
static HWND g_log = nullptr;
static HWND g_status = nullptr;
static HFONT g_font = nullptr;
static HFONT g_fontBold = nullptr;
static HBRUSH g_bgBrush = nullptr;
static RomInfo g_info;
static fs::path g_outputDirectory;
static fs::path g_lastOutput;

static std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) return std::wstring(s.begin(), s.end());
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

static std::string narrow(const std::wstring& s) {
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return std::string(s.begin(), s.end());
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), out.data(), n, nullptr, nullptr);
    return out;
}

static void set_text(HWND h, const std::wstring& s) {
    if (h) SetWindowTextW(h, s.c_str());
}

static std::wstring get_text(HWND h) {
    int len = GetWindowTextLengthW(h);
    std::wstring s(static_cast<std::size_t>(len), L'\0');
    GetWindowTextW(h, s.data(), len + 1);
    return s;
}

static void append_log(const std::wstring& text) {
    if (!g_log) return;
    int len = GetWindowTextLengthW(g_log);
    SendMessageW(g_log, EM_SETSEL, len, len);
    std::wstring line = text;
    if (line.empty() || line.back() != L'\n') line += L"\r\n";
    SendMessageW(g_log, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(line.c_str()));
    SendMessageW(g_log, EM_SCROLLCARET, 0, 0);
}

static void set_status(const std::wstring& s) {
    set_text(g_status, s);
}

static std::vector<Byte> read_file(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) throw std::runtime_error("Could not open ROM: " + p.string());
    f.seekg(0, std::ios::end);
    auto n = f.tellg();
    if (n <= 0) throw std::runtime_error("The ROM is empty.");
    if (static_cast<std::uintmax_t>(n) > MAX_GBA_ROM)
        throw std::runtime_error("The file is larger than the maximum 32 MiB GBA ROM size.");
    f.seekg(0, std::ios::beg);
    std::vector<Byte> data(static_cast<std::size_t>(n));
    f.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!f) throw std::runtime_error("Failed while reading the ROM.");
    return data;
}

static void write_file(const fs::path& p, const std::vector<Byte>& data) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) throw std::runtime_error("Could not create the output ROM.");
    f.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!f) throw std::runtime_error("Failed while writing the output ROM.");
}

static bool contains_bytes(const std::vector<Byte>& data, const std::vector<Byte>& needle) {
    if (needle.empty() || data.size() < needle.size()) return false;
    return std::search(data.begin(), data.end(), needle.begin(), needle.end()) != data.end();
}

static bool matches_at(const std::vector<Byte>& data, std::size_t at,
                       const std::vector<Byte>& sig) {
    return at + sig.size() <= data.size() &&
           std::equal(sig.begin(), sig.end(), data.begin() + static_cast<std::ptrdiff_t>(at));
}

static std::optional<std::size_t> find_patch(const std::vector<Byte>& data, const BinaryPatch& p) {
    if (p.marker.empty() || data.size() < p.marker.size()) return std::nullopt;
    const bool useMask = p.wildcard.size() == p.marker.size();
    for (std::size_t i = 0; i + p.marker.size() <= data.size(); ++i) {
        bool ok = true;
        for (std::size_t j = 0; j < p.marker.size(); ++j) {
            if (useMask && p.wildcard[j]) continue;
            if (data[i + j] != p.marker[j]) { ok = false; break; }
        }
        if (ok) return i;
    }
    return std::nullopt;
}

static std::vector<bool> make_mask(std::size_t n, std::initializer_list<std::size_t> wild) {
    std::vector<bool> m(n, false);
    for (auto i : wild) if (i < n) m[i] = true;
    return m;
}

static void apply_binary_patch(std::vector<Byte>& data, const BinaryPatch& p, const wchar_t* label) {
    auto at = find_patch(data, p);
    if (!at) {
        std::wstring msg = L"Could not find ";
        msg += label;
        msg += L". The ROM may already be patched or may use an unsupported save-library revision.";
        throw std::runtime_error(narrow(msg));
    }
    if (*at + p.replacement.size() > data.size())
        throw std::runtime_error("Patch replacement would exceed ROM size.");
    std::copy(p.replacement.begin(), p.replacement.end(),
              data.begin() + static_cast<std::ptrdiff_t>(*at));

    std::wstringstream ss;
    ss << L"Patched " << label << L" at 0x" << std::hex << *at;
    append_log(ss.str());
}

static std::uint32_t read_le32(const Byte* p) {
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}

static void write_le32(std::vector<Byte>& data, std::size_t at, std::uint32_t value) {
    if (at + 4 > data.size()) throw std::runtime_error("Internal patch write exceeded ROM size.");
    data[at + 0] = static_cast<Byte>(value & 0xFF);
    data[at + 1] = static_cast<Byte>((value >> 8) & 0xFF);
    data[at + 2] = static_cast<Byte>((value >> 16) & 0xFF);
    data[at + 3] = static_cast<Byte>((value >> 24) & 0xFF);
}

static const std::vector<std::string> SAVE_TAGS = {
    "FLASH1M_V103","FLASH1M_V102",
    "FLASH512_V133","FLASH512_V131","FLASH512_V130",
    "FLASH_V126","FLASH_V125","FLASH_V124","FLASH_V123","FLASH_V121","FLASH_V120",
    "EEPROM_V126","EEPROM_V125","EEPROM_V124","EEPROM_V122","EEPROM_V121","EEPROM_V120","EEPROM_V111",
    "SRAM_F_V110","SRAM_F_V103","SRAM_F_V102","SRAM_F_V100",
    "SRAM_V113","SRAM_V112","SRAM_V111","SRAM_V110"
};

static std::string detect_save_tag(const std::vector<Byte>& rom) {
    for (const auto& s : SAVE_TAGS) {
        if (contains_bytes(rom, std::vector<Byte>(s.begin(), s.end()))) return s;
    }
    return "UNKNOWN";
}

static bool is_eeprom(const std::string& s) { return s.rfind("EEPROM", 0) == 0; }
static bool is_sram(const std::string& s)   { return s.rfind("SRAM", 0) == 0; }
static bool is_flash(const std::string& s)  { return s.rfind("FLASH", 0) == 0; }

static bool looks_stage1_patched(const std::vector<Byte>& rom) {
    const std::vector<Byte> sig = {
        0x70,0xB5,0x00,0x04,0x0A,0x1C,0x40,0x0B,
        0xE0,0x21,0x09,0x05,0x41,0x18,0x07,0x31
    };
    return contains_bytes(rom, sig);
}

static bool looks_flash512_patched(const std::vector<Byte>& rom) {
    // The payload's first 28 bytes include an EEPROM metadata pointer that
    // can change at patch time. The remaining payload body is stable.
    constexpr std::size_t mutableHeader = 28;
    if (payload_bin_len <= mutableHeader + 32)
        return false;

    const unsigned char* begin = payload_bin + mutableHeader;
    const unsigned char* end = payload_bin + payload_bin_len;
    return std::search(rom.begin(), rom.end(), begin, end) != rom.end();
}

static std::wstring native_effective_target(const std::string& saveTag) {
    if (saveTag.rfind("EEPROM", 0) == 0) return L"EEPROM";
    if (saveTag.rfind("SRAM", 0) == 0) return L"SRAM";
    if (saveTag.rfind("FLASH1M", 0) == 0) return L"Flash1M";
    if (saveTag.rfind("FLASH512", 0) == 0) return L"Flash512";
    if (saveTag.rfind("FLASH", 0) == 0) return L"Flash";
    return L"Unknown";
}

static std::wstring header_text(const std::vector<Byte>& rom, std::size_t off, std::size_t len) {
    if (off + len > rom.size()) return L"";
    std::string s;
    for (std::size_t i = 0; i < len; ++i) {
        char c = static_cast<char>(rom[off + i]);
        if (c == '\0') break;
        if (c >= 32 && c <= 126) s += c;
    }
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return widen(s);
}

static std::wstring human_size(std::uintmax_t bytes) {
    std::wstringstream ss;
    double mib = static_cast<double>(bytes) / (1024.0 * 1024.0);
    ss.setf(std::ios::fixed);
    ss.precision(2);
    ss << mib << L" MiB";
    return ss.str();
}

static std::vector<BinaryPatch> eeprom_patches_for(const std::string& save) {
    const std::vector<Byte> replA = {
        0x00,0x04,0x0A,0x1C,0x40,0x0B,0xE0,0x21,
        0x09,0x05,0x41,0x18,0x07,0x31,0x00,0x23,
        0x08,0x78,0x10,0x70,0x01,0x33,0x01,0x32,
        0x01,0x39,0x07,0x2B,0xF8,0xD9,0x00,0x20,
        0x70,0xBC,0x02,0xBC,0x08,0x47
    };
    const std::vector<Byte> replB = {
        0x70,0xB5,0x00,0x04,0x0A,0x1C,0x40,0x0B,
        0xE0,0x21,0x09,0x05,0x41,0x18,0x07,0x31,
        0x00,0x23,0x10,0x78,0x08,0x70,0x01,0x33,
        0x01,0x32,0x01,0x39,0x07,0x2B,0xF8,0xD9,
        0x00,0x20,0x70,0xBC,0x02,0xBC,0x08,0x47
    };

    if (save == "EEPROM_V120" || save == "EEPROM_V121" || save == "EEPROM_V122") {
        return {
            {
                {0xA2,0xB0,0x0D,0x1C,0x00,0x04,0x03,0x0C,0x03,0x48,0x00,0x68,
                 0x80,0x88,0x83,0x42,0x05,0xD3,0x01,0x48,0x00,0xE0},
                replA, make_mask(22,{20})
            },
            {
                {0x30,0xB5,0xA9,0xB0,0x0D,0x1C,0x00,0x04,0x04,0x0C,0x03,0x48,
                 0x00,0x68,0x80,0x88,0x84,0x42,0x05,0xD3,0x01,0x48,0x00,0xE0},
                replB, make_mask(24,{22})
            }
        };
    }

    if (save == "EEPROM_V124") {
        return {
            {
                {0xA2,0xB0,0x0D,0x1C,0x00,0x04,0x03,0x0C,0x03,0x48,0x00,0x68,
                 0x80,0x88,0x83,0x42,0x05,0xD3,0x01,0x48,0x00,0xE0},
                replA, make_mask(22,{20})
            },
            {
                {0xF0,0xB5,0xAC,0xB0,0x0D,0x1C,0x00,0x04,0x01,0x0C,0x12,0x06,
                 0x17,0x0E,0x03,0x48,0x00,0x68,0x80,0x88,0x81,0x42,0x05,0xD3},
                replB, {}
            }
        };
    }

    if (save == "EEPROM_V126") {
        return {
            {
                {0xA2,0xB0,0x0D,0x1C,0x00,0x04,0x03,0x0C,0x03,0x48,0x00,0x68,
                 0x80,0x88,0x83,0x42,0x05,0xD3,0x01,0x48,0x4A,0xE0},
                replA, {}
            },
            {
                {0xF0,0xB5,0x47,0x46,0x80,0xB4,0xAC,0xB0,0x0E,0x1C,0x00,0x04,
                 0x05,0x0C,0x12,0x06,0x12,0x0E,0x90,0x46,0x03,0x48,0x00,0x68},
                replB, {}
            }
        };
    }

    return {};
}

static std::vector<Byte> make_sram_stage(std::vector<Byte> rom, const RomInfo& info) {
    if (info.stage1Patched) {
        append_log(L"SRAM-compatible EEPROM routines already detected. Skipping EEPROM conversion.");
        return rom;
    }

    auto patches = eeprom_patches_for(info.saveTag);
    if (patches.empty())
        throw std::runtime_error("This EEPROM revision is not supported by the built-in SRAM conversion yet.");

    append_log(L"Applying EEPROM -> SRAM-compatible routines...");
    apply_binary_patch(rom, patches[0], L"EEPROM routine A");
    apply_binary_patch(rom, patches[1], L"EEPROM routine B");
    return rom;
}

/*
 Integrated Flash512 engine.

 The algorithm is an in-process reimplementation of the patching flow used by
 metroid-maniac/gba-flash-patcher. The ARM payload compiled from that MIT-
 licensed project's payload.c is embedded into this executable at build time.
*/
static std::vector<Byte> patch_to_flash512(std::vector<Byte> rom) {
    if (payload_bin_len < 28)
        throw std::runtime_error("Embedded Flash512 payload is missing or invalid.");

    if (rom.size() > MAX_GBA_ROM)
        throw std::runtime_error("ROM is too large.");

    // The upstream patcher pads trimmed ROMs to a 256 KiB boundary with 0xFF.
    std::size_t padded = rom.size();
    if (padded & (ROM_ALIGN - 1)) {
        padded = (padded + ROM_ALIGN - 1) & ~(ROM_ALIGN - 1);
        if (padded > MAX_GBA_ROM)
            throw std::runtime_error("ROM cannot be padded without exceeding 32 MiB.");
        rom.resize(padded, 0xFF);
        append_log(L"Padded trimmed ROM to 256 KiB alignment.");
    }

    enum PayloadOffsetIndex {
        WRITE_SRAM_PATCHED = 0,
        WRITE_EEPROM_PATCHED = 1,
        READ_SRAM_PATCHED = 2,
        READ_EEPROM_PATCHED = 3,
        VERIFY_SRAM_PATCHED = 4,
        VERIFY_EEPROM_PATCHED = 5,
        EEPROM_META = 6
    };

    auto payload_offset = [](int index) -> std::uint32_t {
        return read_le32(payload_bin + (index * 4));
    };

    std::size_t payloadBase = static_cast<std::size_t>(-1);
    if (rom.size() >= payload_bin_len) {
        for (std::size_t at = rom.size() - payload_bin_len;;) {
            bool allZero = true;
            bool allFF = true;
            for (std::size_t i = 0; i < payload_bin_len; ++i) {
                if (rom[at + i] != 0x00) allZero = false;
                if (rom[at + i] != 0xFF) allFF = false;
                if (!allZero && !allFF) break;
            }
            if (allZero || allFF) {
                payloadBase = at;
                break;
            }
            if (at < 4) break;
            at -= 4;
        }
    }

    if (payloadBase == static_cast<std::size_t>(-1)) {
        if (rom.size() + payload_bin_len > MAX_GBA_ROM)
            throw std::runtime_error("No room for the Flash512 payload and the ROM is already at maximum size.");
        payloadBase = rom.size();
        rom.resize(rom.size() + payload_bin_len, 0xFF);
        append_log(L"Expanded ROM to make room for the Flash512 payload.");
    }

    std::copy(payload_bin, payload_bin + payload_bin_len,
              rom.begin() + static_cast<std::ptrdiff_t>(payloadBase));

    {
        std::wstringstream ss;
        ss << L"Installed embedded Flash512 payload at ROM offset 0x"
           << std::hex << payloadBase;
        append_log(ss.str());
    }

    const std::vector<Byte> thumbThunk = {0x00,0x4B,0x18,0x47};
    const std::vector<Byte> armThunk   = {0x00,0x30,0x9F,0xE5,0x13,0xFF,0x2F,0xE1};

    const std::vector<Byte> writeSramSig = {
        0x30,0xB5,0x05,0x1C,0x0C,0x1C,0x13,0x1C,0x0B,0x4A,0x10,0x88,0x0B,0x49,0x08,0x40
    };
    const std::vector<Byte> writeSram2Sig = {
        0x80,0xB5,0x83,0xB0,0x6F,0x46,0x38,0x60,0x79,0x60,0xBA,0x60,0x09,0x48,0x09,0x49
    };
    const std::vector<Byte> writeSramFastSig = {
        0x04,0xC0,0x90,0xE4,0x01,0xC0,0xC1,0xE4,0x2C,0xC4,0xA0,0xE1,0x01,0xC0,0xC1,0xE4
    };
    const std::vector<Byte> readSramSig = {
        0x70,0xB5,0xA0,0xB0,0x04,0x1C,0x0D,0x1C,0x16,0x1C,0x08,0x4A,0x10,0x88,0x08,0x49
    };
    const std::vector<Byte> verifySramSig = {
        0x70,0xB5,0xB0,0xB0,0x04,0x1C,0x0D,0x1C,0x16,0x1C,0x08,0x4A,0x10,0x88,0x08,0x49
    };
    const std::vector<Byte> writeEepromSig = {
        0x70,0xB5,0x00,0x04,0x0A,0x1C,0x40,0x0B,0xE0,0x21,0x09,0x05,0x41,0x18,0x07,0x31,0x00,0x23,0x10,0x78
    };
    const std::vector<Byte> readEepromSig = {
        0x70,0xB5,0x00,0x04,0x0A,0x1C,0x40,0x0B,0xE0,0x21,0x09,0x05,0x41,0x18,0x07,0x31
    };
    const std::vector<Byte> verifyEepromSig = {
        0x30,0xB5,0x82,0xB0,0x0C,0x1C,0x00,0x04,0x01,0x0C,0x00,0x25,0x03,0x48,0x00,0x68
    };
    const std::vector<Byte> identifyEepromSig = {
        0x00,0x04,0x00,0x0C,0x00,0x22,0x04,0x28,0x08,0xD1,0x02,0x49,0x02,0x48,0x08,0x60
    };

    bool foundHook = false;

    const std::size_t scanEnd = rom.size() > 64 ? rom.size() - 64 : 0;
    for (std::size_t at = 0; at < scanEnd; at += 2) {
        auto patch_thumb = [&](int offsetIndex, const wchar_t* name) {
            foundHook = true;
            std::copy(thumbThunk.begin(), thumbThunk.end(),
                      rom.begin() + static_cast<std::ptrdiff_t>(at));
            const std::uint32_t target =
                0x08000000u +
                static_cast<std::uint32_t>(payloadBase) +
                payload_offset(offsetIndex);
            write_le32(rom, at + 4, target);
            std::wstringstream ss;
            ss << name << L" at 0x" << std::hex << at;
            append_log(ss.str());
        };

        if (matches_at(rom, at, writeSramSig)) {
            patch_thumb(WRITE_SRAM_PATCHED, L"Patched WriteSram");
        } else if (matches_at(rom, at, writeSram2Sig)) {
            patch_thumb(WRITE_SRAM_PATCHED, L"Patched WriteSram variant 2");
        } else if (matches_at(rom, at, writeSramFastSig)) {
            foundHook = true;
            std::copy(armThunk.begin(), armThunk.end(),
                      rom.begin() + static_cast<std::ptrdiff_t>(at));
            const std::uint32_t target =
                0x08000000u +
                static_cast<std::uint32_t>(payloadBase) +
                payload_offset(WRITE_SRAM_PATCHED);
            write_le32(rom, at + 8, target);
            std::wstringstream ss;
            ss << L"Patched WriteSramFast at 0x" << std::hex << at;
            append_log(ss.str());
        } else if (matches_at(rom, at, readSramSig)) {
            patch_thumb(READ_SRAM_PATCHED, L"Patched ReadSram");
        } else if (matches_at(rom, at, verifySramSig)) {
            patch_thumb(VERIFY_SRAM_PATCHED, L"Patched VerifySram");
        } else if (matches_at(rom, at, writeEepromSig)) {
            patch_thumb(WRITE_EEPROM_PATCHED, L"Patched SRAM-style EEPROM write");
        } else if (matches_at(rom, at, readEepromSig)) {
            patch_thumb(READ_EEPROM_PATCHED, L"Patched SRAM-style EEPROM read");
        } else if (matches_at(rom, at, verifyEepromSig)) {
            patch_thumb(VERIFY_EEPROM_PATCHED, L"Patched SRAM-style EEPROM verify");
        }

        if (matches_at(rom, at, identifyEepromSig)) {
            foundHook = true;
            if (at + 24 <= rom.size()) {
                const std::uint32_t metaPtr = read_le32(rom.data() + at + 20);
                const std::size_t metaAt = payloadBase + payload_offset(EEPROM_META);
                write_le32(rom, metaAt, metaPtr);

                std::wstringstream ss;
                ss << L"Captured EEPROM metadata pointer at 0x" << std::hex << at;
                append_log(ss.str());
            }
        }
    }

    if (!foundHook) {
        throw std::runtime_error(
            "Could not find a compatible SRAM/EEPROM save routine to hook. "
            "The ROM may use unsupported/custom save code."
        );
    }

    return rom;
}

static std::wstring compatibility_for(const RomInfo& info) {
    if (!info.loaded) return L"Load a ROM to begin.";
    if (!info.analyzed) return L"ROM loaded. Click Analyze to inspect its save routines.";

    if (info.flash512Patched)
        return L"Existing Flash512 patch detected. Further automatic conversion is disabled.";

    if (is_eeprom(info.saveTag)) {
        if (info.stage1Patched)
            return L"SRAM-compatible EEPROM patch detected. Flash512 conversion is available.";
        auto patches = eeprom_patches_for(info.saveTag);
        if (!patches.empty())
            return L"Supported EEPROM revision. SRAM and Flash512 conversions are available.";
        return L"EEPROM detected, but this revision is not supported automatically yet.";
    }

    if (is_sram(info.saveTag))
        return L"SRAM save library detected. Flash512 conversion is available.";

    if (is_flash(info.saveTag))
        return L"Native FLASH save library detected. Direct FLASH/FLASH1M conversion is not enabled yet.";

    return L"Save library not recognized. No automatic conversion is enabled.";
}

static TargetMode selected_target() {
    LRESULT idx = SendMessageW(g_targetCombo, CB_GETCURSEL, 0, 0);
    if (idx == CB_ERR) return TargetMode::None;
    return static_cast<TargetMode>(idx);
}

static fs::path default_output_for(TargetMode mode) {
    if (!g_info.loaded || g_outputDirectory.empty()) return {};
    const std::wstring stem = g_info.path.stem().wstring();

    switch (mode) {
        case TargetMode::Sram:
            return g_outputDirectory / fs::path(stem + L"_SRAM.gba");
        case TargetMode::Flash512:
            return g_outputDirectory / fs::path(stem + L"_FLASH512.gba");
        case TargetMode::None:
        default:
            return {};
    }
}

static void update_output_preview() {
    if (!g_info.loaded) {
        set_text(g_outputFolder, L"");
        set_text(g_outputFileValue, L"—");
        return;
    }

    set_text(g_outputFolder, g_outputDirectory.wstring());

    const auto p = default_output_for(selected_target());
    if (p.empty())
        set_text(g_outputFileValue, L"Select a conversion target.");
    else
        set_text(g_outputFileValue, p.filename().wstring());
}

static void update_patch_enabled() {
    if (!g_info.loaded || !g_info.analyzed) {
        SetWindowTextW(g_patchBtn, L"Create converted ROM");
        EnableWindow(g_patchBtn, FALSE);
        return;
    }

    const TargetMode mode = selected_target();

    if (mode == TargetMode::None) {
        SetWindowTextW(g_patchBtn, L"Create converted ROM");
        EnableWindow(g_patchBtn, FALSE);
        return;
    }

    if (g_info.flash512Patched) {
        SetWindowTextW(g_patchBtn, L"Already Flash512 patched");
        EnableWindow(g_patchBtn, FALSE);
        return;
    }

    if (mode == TargetMode::Sram) {
        SetWindowTextW(g_patchBtn, L"Create SRAM ROM");
        const bool ok =
            is_eeprom(g_info.saveTag) &&
            (g_info.stage1Patched || !eeprom_patches_for(g_info.saveTag).empty());
        EnableWindow(g_patchBtn, ok ? TRUE : FALSE);
        return;
    }

    if (mode == TargetMode::Flash512) {
        SetWindowTextW(g_patchBtn, L"Create Flash512 ROM");
        const bool ok =
            is_sram(g_info.saveTag) ||
            (is_eeprom(g_info.saveTag) &&
             (g_info.stage1Patched || !eeprom_patches_for(g_info.saveTag).empty()));
        EnableWindow(g_patchBtn, ok ? TRUE : FALSE);
        return;
    }
}

static void refresh_ui_from_info() {
    if (!g_info.loaded) {
        set_text(g_titleValue, L"—");
        set_text(g_codeValue, L"—");
        set_text(g_libraryValue, L"—");
        set_text(g_sizeValue, L"—");
        set_text(g_patchStateValue, L"—");
        set_text(g_effectiveValue, L"—");
        set_text(g_compatValue, L"Load a ROM to begin.");
        update_output_preview();
        update_patch_enabled();
        return;
    }

    if (!g_info.analyzed) {
        set_text(g_titleValue, L"(not analyzed)");
        set_text(g_codeValue, L"—");
        set_text(g_libraryValue, L"—");
        set_text(g_sizeValue, human_size(g_info.rom.size()));
        set_text(g_patchStateValue, L"Not analyzed");
        set_text(g_effectiveValue, L"—");
        set_text(g_compatValue, L"ROM loaded. Click Analyze to inspect it.");
        update_output_preview();
        update_patch_enabled();
        return;
    }

    set_text(g_titleValue, g_info.title.empty() ? L"(no title)" : g_info.title);
    set_text(g_codeValue, g_info.gameCode.empty() ? L"(unknown)" : g_info.gameCode);
    set_text(g_libraryValue, widen(g_info.saveTag));
    set_text(g_sizeValue, human_size(g_info.rom.size()));
    set_text(g_patchStateValue, g_info.patchState);
    set_text(g_effectiveValue, g_info.effectiveTarget);
    set_text(g_compatValue, g_info.compatibility);

    update_output_preview();
    update_patch_enabled();
}

static void load_rom(const fs::path& p) {
    try {
        set_status(L"Loading ROM...");

        RomInfo next{};
        next.path = p;
        next.rom = read_file(p);
        next.loaded = true;
        next.analyzed = false;

        g_info = std::move(next);
        g_outputDirectory = p.parent_path();
        g_lastOutput.clear();

        if (g_targetCombo)
            SendMessageW(g_targetCombo, CB_SETCURSEL, 0, 0);

        set_text(g_romPath, p.wstring());
        append_log(L"Loaded source ROM: " + p.filename().wstring());
        append_log(L"Source ROM is now the active in-memory source of truth.");
        refresh_ui_from_info();

        EnableWindow(GetDlgItem(g_hwnd, IDC_OPEN_OUTPUT), FALSE);
        set_status(L"ROM loaded. Click Analyze.");
    } catch (const std::exception& e) {
        append_log(L"LOAD ERROR: " + widen(e.what()));
        set_status(L"ROM load failed.");
        MessageBoxW(g_hwnd, widen(e.what()).c_str(), APP_TITLE, MB_ICONERROR | MB_OK);
    }
}

static void analyze_loaded_rom() {
    if (!g_info.loaded) {
        MessageBoxW(
            g_hwnd,
            L"No source ROM is loaded.\n\nUse the Browse button at the top, or drag a .gba ROM onto the window first.",
            APP_TITLE,
            MB_ICONINFORMATION | MB_OK
        );
        return;
    }

    try {
        set_status(L"Analyzing loaded ROM...");

        // Analyze the already-loaded bytes. Do not reopen the file.
        g_info.title = header_text(g_info.rom, 0xA0, 12);
        g_info.gameCode = header_text(g_info.rom, 0xAC, 4);
        g_info.saveTag = detect_save_tag(g_info.rom);
        g_info.flash512Patched = looks_flash512_patched(g_info.rom);
        g_info.stage1Patched =
            !g_info.flash512Patched && looks_stage1_patched(g_info.rom);
        g_info.analyzed = true;

        if (g_info.flash512Patched) {
            g_info.patchState = L"Flash512 payload + hooks detected";
            g_info.effectiveTarget = L"Flash512";
        } else if (g_info.stage1Patched && is_eeprom(g_info.saveTag)) {
            g_info.patchState = L"EEPROM -> SRAM-compatible patch detected";
            g_info.effectiveTarget = L"SRAM-compatible";
        } else {
            g_info.patchState = L"Unpatched / native save routines";
            g_info.effectiveTarget = native_effective_target(g_info.saveTag);
        }

        g_info.compatibility = compatibility_for(g_info);

        append_log(L"Analysis complete: " + g_info.path.filename().wstring());
        append_log(L"Original save library: " + widen(g_info.saveTag));
        append_log(L"Patch state: " + g_info.patchState);
        append_log(L"Effective save target: " + g_info.effectiveTarget);

        refresh_ui_from_info();
        set_status(L"Analysis complete.");
    } catch (const std::exception& e) {
        append_log(L"ANALYZE ERROR: " + widen(e.what()));
        set_status(L"Analysis failed.");
        MessageBoxW(g_hwnd, widen(e.what()).c_str(), APP_TITLE, MB_ICONERROR | MB_OK);
    }
}

static fs::path browse_rom(HWND owner) {
    wchar_t file[MAX_PATH * 4]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"GBA ROMs (*.gba)\0*.gba\0All files (*.*)\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = static_cast<DWORD>(std::size(file));
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    ofn.lpstrDefExt = L"gba";
    return GetOpenFileNameW(&ofn) ? fs::path(file) : fs::path{};
}

static fs::path browse_output_folder(HWND owner) {
    BROWSEINFOW bi{};
    bi.hwndOwner = owner;
    bi.lpszTitle = L"Choose the folder where converted ROMs should be written";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return {};

    wchar_t folder[MAX_PATH * 4]{};
    fs::path result;
    if (SHGetPathFromIDListW(pidl, folder))
        result = fs::path(folder);

    CoTaskMemFree(pidl);
    return result;
}

static bool confirm_overwrite(const fs::path& p) {
    std::error_code ec;
    if (!fs::exists(p, ec)) return true;
    const std::wstring msg =
        L"The output file already exists:\n\n" + p.wstring() + L"\n\nReplace it?";
    return MessageBoxW(g_hwnd, msg.c_str(), APP_TITLE, MB_ICONQUESTION | MB_YESNO) == IDYES;
}

static void patch_rom() {
    if (!g_info.loaded) {
        MessageBoxW(g_hwnd, L"Load a source ROM first.", APP_TITLE, MB_ICONINFORMATION | MB_OK);
        return;
    }

    if (!g_info.analyzed) {
        MessageBoxW(g_hwnd, L"Analyze the loaded source ROM before converting it.", APP_TITLE, MB_ICONINFORMATION | MB_OK);
        return;
    }

    const TargetMode mode = selected_target();
    if (mode == TargetMode::None) {
        MessageBoxW(g_hwnd, L"Select a conversion target.", APP_TITLE, MB_ICONINFORMATION | MB_OK);
        return;
    }

    if (g_outputDirectory.empty()) {
        MessageBoxW(g_hwnd, L"Choose an output folder.", APP_TITLE, MB_ICONWARNING | MB_OK);
        return;
    }

    const fs::path output = default_output_for(mode);
    if (output.empty()) {
        MessageBoxW(g_hwnd, L"Could not determine the output filename.", APP_TITLE, MB_ICONERROR | MB_OK);
        return;
    }

    if (!confirm_overwrite(output)) return;

    try {
        if (mode == TargetMode::Sram) {
            if (g_info.flash512Patched)
                throw std::runtime_error("This source ROM is already Flash512 patched.");
            if (!is_eeprom(g_info.saveTag))
                throw std::runtime_error("SRAM conversion expects a supported EEPROM game.");

            set_status(L"Creating SRAM ROM...");
            append_log(L"--- SRAM conversion started ---");

            auto rom = make_sram_stage(g_info.rom, g_info);
            write_file(output, rom);
            g_lastOutput = output;

            append_log(L"SUCCESS: " + output.wstring());
            set_status(L"SRAM ROM created.");
            EnableWindow(GetDlgItem(g_hwnd, IDC_OPEN_OUTPUT), TRUE);

            MessageBoxW(
                g_hwnd,
                (L"SRAM ROM created successfully:\n\n" + output.wstring() +
                 L"\n\nThe loaded source ROM was not modified.").c_str(),
                APP_TITLE, MB_ICONINFORMATION | MB_OK
            );
            return;
        }

        if (mode == TargetMode::Flash512) {
            if (g_info.flash512Patched)
                throw std::runtime_error("This source ROM already contains the Flash512 patch.");

            set_status(L"Creating Flash512 ROM...");
            append_log(L"--- Flash512 conversion started ---");

            std::vector<Byte> stage = g_info.rom;

            if (is_eeprom(g_info.saveTag)) {
                stage = make_sram_stage(stage, g_info);
            } else if (is_sram(g_info.saveTag)) {
                append_log(L"Source already uses SRAM. EEPROM conversion is not needed.");
            } else {
                throw std::runtime_error("This save type is not supported for Flash512 conversion.");
            }

            auto patched = patch_to_flash512(std::move(stage));
            write_file(output, patched);
            g_lastOutput = output;

            append_log(L"SUCCESS: " + output.wstring());
            set_status(L"Flash512 ROM created.");
            EnableWindow(GetDlgItem(g_hwnd, IDC_OPEN_OUTPUT), TRUE);

            MessageBoxW(
                g_hwnd,
                (L"Flash512 ROM created successfully:\n\n" + output.wstring() +
                 L"\n\nThe loaded source ROM was not modified.").c_str(),
                APP_TITLE, MB_ICONINFORMATION | MB_OK
            );
            return;
        }
    } catch (const std::exception& e) {
        append_log(L"ERROR: " + widen(e.what()));
        set_status(L"Conversion failed.");
        MessageBoxW(g_hwnd, widen(e.what()).c_str(), APP_TITLE, MB_ICONERROR | MB_OK);
    }
}

static HWND make_control(
    LPCWSTR cls, LPCWSTR text, DWORD style,
    int x, int y, int w, int h, HWND parent, int id,
    DWORD exStyle = 0
) {
    HWND control = CreateWindowExW(
        exStyle, cls, text, style,
        x, y, w, h, parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandleW(nullptr), nullptr
    );
    if (control && g_font)
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
    return control;
}

static void set_bold(HWND h) {
    if (h && g_fontBold)
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(g_fontBold), TRUE);
}

static void open_output_folder() {
    if (g_lastOutput.empty()) return;
    ShellExecuteW(g_hwnd, L"open", g_lastOutput.parent_path().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

static void show_about() {
    const wchar_t* text =
        L"GBA Repro Save Patcher v0.4.3\n\n"
        L"A universal GBA save-hardware conversion tool.\n\n"        L"Workflow: Browse to load a ROM, Analyze the loaded ROM, choose a target save type, then Create the converted ROM.\n\n"
        L"Flash512 support is now built directly into the application. "
        L"No external backend EXE is required.\n\n"
        L"Built-in EEPROM conversion patterns are adapted from BBsan's "
        L"Flash1M_Repro_SRAM_Patcher (MIT).\n\n"
        L"The embedded Flash512 ARM payload is built from metroid-maniac's "
        L"gba-flash-patcher payload (MIT).\n\n"
        L"The source ROM is never overwritten.";
    MessageBoxW(g_hwnd, text, L"About GBA Repro Save Patcher", MB_ICONINFORMATION | MB_OK);
}

static void create_ui(HWND hwnd) {
    g_hwnd = hwnd;

    NONCLIENTMETRICSW ncm{};
    ncm.cbSize = sizeof(ncm);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    g_font = CreateFontIndirectW(&ncm.lfMessageFont);

    LOGFONTW bold = ncm.lfMessageFont;
    bold.lfWeight = FW_SEMIBOLD;
    g_fontBold = CreateFontIndirectW(&bold);

    HWND title = make_control(
        L"STATIC", L"GBA Repro Save Patcher",
        WS_CHILD | WS_VISIBLE,
        20, 15, 400, 27, hwnd, 0
    );
    set_bold(title);

    make_control(
        L"STATIC",
        L"Workflow: Browse to load a ROM  ->  Analyze the loaded ROM  ->  Choose a target save type  ->  Create the converted ROM.",
        WS_CHILD | WS_VISIBLE,
        20, 43, 780, 20, hwnd, 0
    );

    make_control(
        L"STATIC",
        L"Tip: Browse only selects the source ROM. Analyze reads that loaded ROM. Target controls conversion. Output Folder controls where the new ROM is saved.",
        WS_CHILD | WS_VISIBLE,
        20, 62, 790, 18, hwnd, 0
    );

    HWND sourceGroup = make_control(
        L"BUTTON", L"1. Source ROM",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        20, 92, 779, 83, hwnd, 0
    );
    set_bold(sourceGroup);

    g_romPath = make_control(
        L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | ES_READONLY,
        38, 122, 620, 25, hwnd, IDC_ROM_PATH, WS_EX_CLIENTEDGE
    );

    make_control(
        L"BUTTON", L"Browse...",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        670, 121, 105, 27, hwnd, IDC_BROWSE_ROM
    );

    make_control(
        L"STATIC", L"Browse or drag/drop loads the ROM. That loaded ROM remains the source for Analyze and Convert.",
        WS_CHILD | WS_VISIBLE,
        38, 151, 720, 18, hwnd, 0
    );

    HWND romGroup = make_control(
        L"BUTTON", L"2. Analyze loaded ROM",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        20, 188, 779, 213, hwnd, 0
    );
    set_bold(romGroup);

    make_control(
        L"BUTTON", L"Analyze loaded ROM",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        615, 211, 160, 31, hwnd, IDC_ANALYZE
    );

    make_control(L"STATIC", L"Title:", WS_CHILD | WS_VISIBLE,
                 38, 220, 95, 18, hwnd, 0);
    g_titleValue = make_control(L"STATIC", L"—", WS_CHILD | WS_VISIBLE,
                                145, 220, 430, 18, hwnd, IDC_TITLE_VALUE);

    make_control(L"STATIC", L"Game code:", WS_CHILD | WS_VISIBLE,
                 38, 248, 95, 18, hwnd, 0);
    g_codeValue = make_control(L"STATIC", L"—", WS_CHILD | WS_VISIBLE,
                               145, 248, 120, 18, hwnd, IDC_CODE_VALUE);

    make_control(L"STATIC", L"ROM size:", WS_CHILD | WS_VISIBLE,
                 335, 248, 80, 18, hwnd, 0);
    g_sizeValue = make_control(L"STATIC", L"—", WS_CHILD | WS_VISIBLE,
                               420, 248, 155, 18, hwnd, IDC_SIZE_VALUE);

    make_control(L"STATIC", L"Original library:", WS_CHILD | WS_VISIBLE,
                 38, 276, 100, 18, hwnd, 0);
    g_libraryValue = make_control(L"STATIC", L"—", WS_CHILD | WS_VISIBLE,
                                  145, 276, 430, 18, hwnd, IDC_LIBRARY_VALUE);

    make_control(L"STATIC", L"Patch state:", WS_CHILD | WS_VISIBLE,
                 38, 304, 95, 18, hwnd, 0);
    g_patchStateValue = make_control(L"STATIC", L"—", WS_CHILD | WS_VISIBLE,
                                     145, 304, 600, 18, hwnd, IDC_PATCH_STATE_VALUE);

    make_control(L"STATIC", L"Effective target:", WS_CHILD | WS_VISIBLE,
                 38, 332, 100, 18, hwnd, 0);
    g_effectiveValue = make_control(L"STATIC", L"—", WS_CHILD | WS_VISIBLE,
                                    145, 332, 430, 18, hwnd, IDC_EFFECTIVE_VALUE);

    make_control(L"STATIC", L"Available:", WS_CHILD | WS_VISIBLE,
                 38, 360, 95, 18, hwnd, 0);
    g_compatValue = make_control(
        L"STATIC", L"Load a ROM to begin.",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        145, 358, 610, 35, hwnd, IDC_COMPAT_VALUE
    );

    HWND targetGroup = make_control(
        L"BUTTON", L"3. Conversion",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        20, 414, 779, 172, hwnd, 0
    );
    set_bold(targetGroup);

    make_control(L"STATIC", L"Target:", WS_CHILD | WS_VISIBLE,
                 38, 443, 95, 20, hwnd, 0);

    g_targetCombo = make_control(
        L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        145, 439, 610, 150, hwnd, IDC_TARGET_COMBO
    );

    SendMessageW(g_targetCombo, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(L"Select a conversion target..."));
    SendMessageW(g_targetCombo, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(L"SRAM — convert supported EEPROM games to SRAM-compatible routines"));
    SendMessageW(g_targetCombo, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(L"Flash512 — convert supported EEPROM/SRAM games for Flash512 hardware"));
    SendMessageW(g_targetCombo, CB_SETCURSEL, 0, 0);

    make_control(L"STATIC", L"Output folder:", WS_CHILD | WS_VISIBLE,
                 38, 480, 95, 20, hwnd, 0);

    g_outputFolder = make_control(
        L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | ES_READONLY,
        145, 477, 500, 25, hwnd, IDC_OUTPUT_FOLDER, WS_EX_CLIENTEDGE
    );

    make_control(
        L"BUTTON", L"Browse folder...",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        655, 476, 100, 27, hwnd, IDC_BROWSE_OUTPUT_FOLDER
    );

    make_control(L"STATIC", L"Output file:", WS_CHILD | WS_VISIBLE,
                 38, 515, 95, 20, hwnd, 0);

    g_outputFileValue = make_control(
        L"STATIC", L"Select a conversion target.",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        145, 515, 610, 20, hwnd, IDC_OUTPUT_FILE_VALUE
    );

    HWND embedded = make_control(
        L"STATIC",
        L"Flash512 engine is built in. No helper program is required.",
        WS_CHILD | WS_VISIBLE,
        145, 546, 430, 20, hwnd, 0
    );
    set_bold(embedded);

    g_patchBtn = make_control(
        L"BUTTON", L"Create converted ROM",
        WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
        20, 602, 205, 39, hwnd, IDC_PATCH
    );
    set_bold(g_patchBtn);

    HWND openOut = make_control(
        L"BUTTON", L"Open output folder",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        237, 602, 155, 39, hwnd, IDC_OPEN_OUTPUT
    );
    EnableWindow(openOut, FALSE);

    make_control(
        L"BUTTON", L"About",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        700, 607, 78, 29, hwnd, IDC_ABOUT
    );

    make_control(L"STATIC", L"Activity log", WS_CHILD | WS_VISIBLE,
                 20, 657, 100, 20, hwnd, 0);

    g_log = make_control(
        L"EDIT", L"Ready.\r\n",
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL |
        ES_READONLY | WS_VSCROLL,
        20, 679, 779, 105, hwnd, IDC_LOG, WS_EX_CLIENTEDGE
    );

    g_status = make_control(
        L"STATIC", L"Load a .gba ROM with Browse or drag-and-drop.",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        20, 794, 779, 22, hwnd, IDC_STATUS
    );

    refresh_ui_from_info();
    DragAcceptFiles(hwnd, TRUE);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            create_ui(hwnd);
            return 0;

        case WM_DROPFILES: {
            HDROP drop = reinterpret_cast<HDROP>(wParam);
            wchar_t path[MAX_PATH * 4]{};
            if (DragQueryFileW(drop, 0, path, static_cast<UINT>(std::size(path))))
                load_rom(fs::path(path));
            DragFinish(drop);
            return 0;
        }

        case WM_COMMAND: {
            const int id = LOWORD(wParam);
            const int code = HIWORD(wParam);

            if (id == IDC_BROWSE_ROM && code == BN_CLICKED) {
                auto p = browse_rom(hwnd);
                if (!p.empty()) load_rom(p);
                return 0;
            }

            if (id == IDC_ANALYZE && code == BN_CLICKED) {
                analyze_loaded_rom();
                return 0;
            }

            if (id == IDC_TARGET_COMBO && code == CBN_SELCHANGE) {
                update_output_preview();
                update_patch_enabled();
                return 0;
            }

            if (id == IDC_BROWSE_OUTPUT_FOLDER && code == BN_CLICKED) {
                auto p = browse_output_folder(hwnd);
                if (!p.empty()) {
                    g_outputDirectory = p;
                    update_output_preview();
                }
                return 0;
            }

            if (id == IDC_PATCH && code == BN_CLICKED) {
                patch_rom();
                return 0;
            }

            if (id == IDC_OPEN_OUTPUT && code == BN_CLICKED) {
                open_output_folder();
                return 0;
            }

            if (id == IDC_ABOUT && code == BN_CLICKED) {
                show_about();
                return 0;
            }
            break;
        }

        case WM_CTLCOLORSTATIC: {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            SetBkMode(hdc, TRANSPARENT);
            return reinterpret_cast<LRESULT>(g_bgBrush);
        }

        case WM_DESTROY:
            if (g_font) DeleteObject(g_font);
            if (g_fontBold) DeleteObject(g_fontBold);
            if (g_bgBrush) DeleteObject(g_bgBrush);
            PostQuitMessage(0);
            return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR cmdLine, int nCmdShow) {
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    g_bgBrush = CreateSolidBrush(RGB(248, 249, 251));

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.hInstance = hInst;
    wc.lpfnWndProc = WndProc;
    wc.lpszClassName = L"GBAReproSavePatcherWindow";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(101));
    wc.hIconSm = LoadIconW(hInst, MAKEINTRESOURCEW(101));
    wc.hbrBackground = g_bgBrush;

    if (!RegisterClassExW(&wc)) {
        MessageBoxW(nullptr, L"Could not register application window.", APP_TITLE, MB_ICONERROR);
        return 1;
    }

    HWND hwnd = CreateWindowExW(
        0, wc.lpszClassName, L"GBA Repro Save Patcher v0.4.3",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 840, 875,
        nullptr, nullptr, hInst, nullptr
    );

    if (!hwnd) {
        MessageBoxW(nullptr, L"Could not create application window.", APP_TITLE, MB_ICONERROR);
        return 1;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    if (cmdLine && *cmdLine) {
        std::wstring arg = cmdLine;
        if (arg.size() >= 2 && arg.front() == L'"' && arg.back() == L'"')
            arg = arg.substr(1, arg.size() - 2);
        if (!arg.empty() && fs::exists(fs::path(arg)))
            load_rom(fs::path(arg));
    }

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    CoUninitialize();
    return static_cast<int>(msg.wParam);
}
