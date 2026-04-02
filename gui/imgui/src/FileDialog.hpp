#pragma once
// FileDialog — native OS file picker (open file only).
//
// Thin cross-platform wrapper:
//   macOS  — AppleScript "choose file" via osascript
//   Windows — Win32 GetOpenFileName (OPENFILENAME)
//   Linux   — zenity --file-selection (requires zenity package)
//
// Returns the selected absolute path, or empty string if cancelled.
//
// filterPatterns: glob-style patterns for the extension filter,
//                 e.g. {"*.wav"} or {"*.wav", "*.xml"}.
//                 Empty vector = accept all files.
// filterDescription: human-readable label shown in the dialog,
//                 e.g. "Audio files (WAV)".
// title:          dialog title bar text.

#include <string>
#include <vector>

std::string pickFile(const std::string&              title,
                     const std::vector<std::string>& filterPatterns   = {},
                     const std::string&              filterDescription = "");

// Open a native picker that accepts both files AND directories.
// macOS  — AppleScript "choose file or folder" (true combined picker).
// Windows/Linux — falls back to file-only (no standard cross-platform
//                 combined file+folder API without additional dependencies).
std::string pickFileOrDirectory(const std::string& title);
