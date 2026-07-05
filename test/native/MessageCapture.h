#pragma once

#include "ApplicationManager.h"
#include <vector>

/// Captures AppMessages sent via ApplicationManager::sendMessage() during tests.
/// In native test mode, sendMessage calls are intercepted here instead of going
/// through the FreeRTOS queue.
struct MessageCapture {
    static std::vector<AppMessage> messages;

    static void reset() { messages.clear(); }
    static int count() { return static_cast<int>(messages.size()); }

    static const AppMessage* findFirst(AppMessageType type) {
        for (auto& m : messages) {
            if (m.type == type) return &m;
        }
        return nullptr;
    }

    static int countOf(AppMessageType type) {
        int n = 0;
        for (auto& m : messages) {
            if (m.type == type) n++;
        }
        return n;
    }
};

std::vector<AppMessage> MessageCapture::messages;

// In native test mode, ApplicationManager::sendMessage pushes to capture vector
// This is linked in lieu of the real ApplicationManager
namespace {
    struct FakeApplicationManager {
        static FakeApplicationManager& getInstance() {
            static FakeApplicationManager inst;
            return inst;
        }
        bool sendMessage(const AppMessage& msg, uint32_t = 0) {
            MessageCapture::messages.push_back(msg);
            return true;
        }
    };
}
