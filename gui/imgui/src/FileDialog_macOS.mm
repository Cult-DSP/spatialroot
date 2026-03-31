// FileDialog_macOS.mm — native NSOpenPanel file/folder picker (macOS only).
//
// Uses NSOpenPanel which integrates correctly with the macOS event loop.
// GLFW initialises NSApplication internally, so runModal works from any
// GLFW-hosted main thread without additional setup.
//
// Compiled as Objective-C++ (.mm) by CMake automatically on Apple targets.

#import <AppKit/AppKit.h>
#include "FileDialog.hpp"

// Extract bare extension from a glob pattern: "*.wav" → "wav"
static NSString* patternToExt(const std::string& pattern) {
    auto dot = pattern.rfind('.');
    if (dot == std::string::npos || dot + 1 >= pattern.size()) return nil;
    return [NSString stringWithUTF8String:pattern.substr(dot + 1).c_str()];
}

std::string pickFile(const std::string&              title,
                     const std::vector<std::string>& filterPatterns,
                     const std::string&              /*filterDescription*/) {
    NSOpenPanel* panel            = [NSOpenPanel openPanel];
    panel.title                   = [NSString stringWithUTF8String:title.c_str()];
    panel.canChooseFiles          = YES;
    panel.canChooseDirectories    = NO;
    panel.allowsMultipleSelection = NO;

    if (!filterPatterns.empty()) {
        NSMutableArray<NSString*>* exts = [NSMutableArray array];
        for (const auto& p : filterPatterns) {
            NSString* ext = patternToExt(p);
            if (ext) [exts addObject:ext];
        }
        if (exts.count > 0) {
            // allowedFileTypes is deprecated in macOS 12 but remains functional.
            // Switching to allowedContentTypes (UTType) is a future improvement.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
            panel.allowedFileTypes = exts;
#pragma clang diagnostic pop
        }
    }

    if ([panel runModal] == NSModalResponseOK)
        return std::string(panel.URL.path.UTF8String ?: "");
    return {};
}

std::string pickFileOrDirectory(const std::string& title) {
    NSOpenPanel* panel            = [NSOpenPanel openPanel];
    panel.title                   = [NSString stringWithUTF8String:title.c_str()];
    panel.canChooseFiles          = YES;
    panel.canChooseDirectories    = YES;
    panel.allowsMultipleSelection = NO;

    if ([panel runModal] == NSModalResponseOK)
        return std::string(panel.URL.path.UTF8String ?: "");
    return {};
}

// Set the macOS Dock and application switcher icon from a PNG file.
// Must be called after NSApplication is initialised (i.e. after glfwInit()).
// No-op if the file is not found.
void setMacOSAppIcon(const std::string& pngPath) {
    @autoreleasepool {
        NSString* path = [NSString stringWithUTF8String:pngPath.c_str()];
        NSImage* icon  = [[NSImage alloc] initWithContentsOfFile:path];
        if (icon) [NSApp setApplicationIconImage:icon];
    }
}
