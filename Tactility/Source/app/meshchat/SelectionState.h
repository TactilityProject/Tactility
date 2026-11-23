#pragma once

#include <cstdint>

#include <tactility/concurrent/mutex.h>

namespace tt::app::meshchat {

class SelectionState {
public:
    enum class Kind : uint8_t { Menu, AddContact, AddType, PathContact };

    struct Result {
        Kind kind;
        int32_t selected;
    };

    SelectionState() { mutex_construct(&mutex_); }
    ~SelectionState() { mutex_destruct(&mutex_); }

    bool begin(Kind kind) {
        mutex_lock(&mutex_);
        if (active_ || resultPending_) {
            mutex_unlock(&mutex_);
            return false;
        }
        activeKind_ = kind;
        active_ = true;
        mutex_unlock(&mutex_);
        return true;
    }

    bool isPending() {
        mutex_lock(&mutex_);
        const bool pending = active_;
        mutex_unlock(&mutex_);
        return pending;
    }

    bool complete(int32_t selected) {
        mutex_lock(&mutex_);
        if (!active_ || resultPending_) {
            mutex_unlock(&mutex_);
            return false;
        }
        result_ = { activeKind_, selected };
        resultPending_ = true;
        mutex_unlock(&mutex_);
        return true;
    }

    bool takeResult(Result& result) {
        mutex_lock(&mutex_);
        if (!resultPending_) {
            mutex_unlock(&mutex_);
            return false;
        }
        result = result_;
        resultPending_ = false;
        active_ = false;
        mutex_unlock(&mutex_);
        return true;
    }

    void failStart() {
        mutex_lock(&mutex_);
        if (!resultPending_) {
            active_ = false;
        }
        mutex_unlock(&mutex_);
    }

private:
    Mutex mutex_ {};
    Kind activeKind_ = Kind::Menu;
    Result result_ { Kind::Menu, -1 };
    bool active_ = false;
    bool resultPending_ = false;
};

} // namespace tt::app::meshchat