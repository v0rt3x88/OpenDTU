#pragma once

#include <TaskSchedulerDeclarations.h>
#include <cstdint>
#include <ctime>
#include <esp_err.h>
#include <mutex>

class GridMeasurementClass {
public:
    enum class State : uint8_t {
        Never,
        Uploading,
        Success,
        Error
    };

    enum class SkipReason : uint8_t {
        None,
        NoNetwork,
        NoTime,
        NoLocation,
        NoData,
        RadioBusy
    };

    struct RuntimeStatus {
        State state = State::Never;
        SkipReason skipReason = SkipReason::None;
        time_t lastAttempt = 0;
        time_t lastSuccess = 0;
        int httpStatus = 0;
        esp_err_t errorCode = ESP_OK;
    };

    GridMeasurementClass();
    void init(Scheduler& scheduler);
    RuntimeStatus getRuntimeStatus() const;

private:
    struct Snapshot {
        time_t observedAt;
        double latitude;
        double longitude;
        float l1Voltage;
        float frequency;
    };

    struct TaskContext {
        GridMeasurementClass* owner;
        Snapshot snapshot;
    };

    struct UploadResult {
        bool success;
        int httpStatus;
        esp_err_t errorCode;
    };

    void loop();
    void markUploadStarted();
    void markUploadFinished(const UploadResult& result);
    void markSkipped(SkipReason reason);
    SkipReason createSnapshot(Snapshot& snapshot) const;
    UploadResult send(const Snapshot& snapshot) const;
    static void sendTask(void* context);

    Task _task;
    mutable std::mutex _mutex;
    bool _uploadInProgress = false;
    bool _radioRetryPending = false;
    RuntimeStatus _runtimeStatus;
};

extern GridMeasurementClass GridMeasurement;