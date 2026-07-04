#include <Tactility/hal/gps/Satellites.h>
#include <Tactility/kernel/Kernel.h>

#include <tactility/log.h>

namespace tt::hal::gps {

constexpr auto* TAG = "Satellites";

constexpr bool hasTimeElapsed(TickType_t now, TickType_t timeInThePast, TickType_t expireTimeInTicks) {
    return (TickType_t)(now - timeInThePast) >= expireTimeInTicks;
}

SatelliteStorage::SatelliteRecord* SatelliteStorage::findRecord(int number) {
    auto result = records | std::views::filter([number](auto& record) {
        return record.inUse && record.data.nr == number;
    });

    if (!result.empty()) {
        return &result.front();
    } else {
        return nullptr;
    }
}

SatelliteStorage::SatelliteRecord* SatelliteStorage::findUnusedRecord() {
    auto lock = mutex.asScopedLock();
    lock.lock();

    auto result = records | std::views::filter([](auto& record) {
        return !record.inUse;
    });

    if (!result.empty()) {
        auto* record = &result.front();
        record->inUse = true;
        LOG_D(TAG, "Found unused record");
        return record;
    } else {
        return nullptr;
    }
}

SatelliteStorage::SatelliteRecord* SatelliteStorage::findRecordToRecycle() {
    auto lock = mutex.asScopedLock();
    lock.lock();

    int candidate_index = -1;
    auto candidate_age = kernel::MAX_TICKS;
    TickType_t expire_duration = kernel::secondsToTicks(recycleTimeSeconds);
    TickType_t now = kernel::getTicks();
    for (int i = 0; i < records.size(); ++i) {
        // First try to find a record that is "old enough"
        if (hasTimeElapsed(now, records[i].lastUpdated, expire_duration)) {
            LOG_D(TAG, "! [%d] %u < %u", i, records[i].lastUpdated, expire_duration);
            candidate_index = i;
            break;
        }

        // Otherwise keep finding the oldest record
        if (records[i].inUse && records[i].lastUpdated < candidate_age) {
            candidate_index = i;
            candidate_age = records[i].lastUpdated;
            LOG_D(TAG, "? [%d] %u < %u", i, records[i].lastUpdated, candidate_age);
        }
    }

    assert(candidate_index != -1);

    LOG_D(TAG, "Recycled record %d", candidate_index);

    return &records[candidate_index];
}

SatelliteStorage::SatelliteRecord* SatelliteStorage::findWithFallback(int number) {
    auto lock = mutex.asScopedLock();
    lock.lock();

    if (auto* found_record = findRecord(number)) {
        return found_record;
    } else if (auto* unused_record = findUnusedRecord()) {
        return unused_record;
    } else {
        return findRecordToRecycle();
    }
}

void SatelliteStorage::notify(const minmea_sat_info& data) {
    auto lock = mutex.asScopedLock();
    lock.lock();

    auto* record = findWithFallback(data.nr);
    if (record != nullptr) {
        record->inUse = true;
        record->lastUpdated = kernel::getTicks();
        record->data = data;
        LOG_D(TAG, "Updated satellite %d: elevation %d, azimuth %d, snr %d", record->data.nr, record->data.elevation, record->data.elevation, record->data.snr);
    }
}

void SatelliteStorage::getRecords(const std::function<void(const minmea_sat_info&)>& onRecord) const {
    auto lock = mutex.asScopedLock();
    lock.lock();

    TickType_t expire_duration = kernel::secondsToTicks(recentTimeSeconds);
    TickType_t now = kernel::getTicks();

    for (auto& record: records) {
        if (record.inUse && !hasTimeElapsed(now, record.lastUpdated, expire_duration)) {
            onRecord(record.data);
        }
    }
}

} // namespace tt::hal::gps
