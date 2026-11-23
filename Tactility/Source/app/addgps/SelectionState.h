#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <tactility/concurrent/mutex.h>

namespace tt::app::addgps {

class SelectionState {
public:
    enum class Kind : uint8_t { Uart, Model, Baud };

    struct Result {
        Kind kind;
        int32_t selected;
    };

    SelectionState() { mutex_construct(&mutex_); }
    ~SelectionState() { mutex_destruct(&mutex_); }

    void request(Kind kind) {
        mutex_lock(&mutex_);
        pending_[static_cast<size_t>(kind)] = true;
        mutex_unlock(&mutex_);
    }

    bool beginNext(Kind& kind) {
        mutex_lock(&mutex_);
        if (active_ || resultPending_) {
            mutex_unlock(&mutex_);
            return false;
        }

        for (size_t i = 0; i < pending_.size(); ++i) {
            if (pending_[i]) {
                pending_[i] = false;
                activeKind_ = static_cast<Kind>(i);
                active_ = true;
                kind = activeKind_;
                mutex_unlock(&mutex_);
                return true;
            }
        }
        mutex_unlock(&mutex_);
        return false;
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
        active_ = false;
        mutex_unlock(&mutex_);
    }

private:
    Mutex mutex_ {};
    std::array<bool, 3> pending_ {};
    Kind activeKind_ = Kind::Uart;
    Result result_ { Kind::Uart, -1 };
    bool active_ = false;
    bool resultPending_ = false;
};

} // namespace tt::app::addgps