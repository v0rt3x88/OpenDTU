#include "GridMeasurement.h"
#include "Configuration.h"
#include "NetworkSettings.h"
#include "defaults.h"
#include <ArduinoJson.h>
#include <Hoymiles.h>
#include <MycilaNTP.h>
#include <esp_crt_bundle.h>
#include <esp_heap_caps.h>
#include <esp_http_client.h>
#include <esp_log.h>
#include <cmath>
#include <new>

namespace {
constexpr uint32_t UPLOAD_INTERVAL = 300 * TASK_SECOND;
constexpr uint32_t MAX_STATISTICS_AGE_MS = 5 * 60 * 1000;
constexpr uint32_t HTTP_TIMEOUT_MS = 10000;
constexpr uint32_t UPLOAD_TASK_STACK_SIZE = 16384;
constexpr uint32_t RADIO_RETRY_DELAY = 5 * TASK_SECOND;
constexpr size_t MINIMUM_TLS_HEAP_BLOCK = 45000;
constexpr char API_ENDPOINT[] = "https://www.ortsnetz-auslastung.de/v1/measurements";
#undef TAG
const char* TAG = "grid_measurement";
}

GridMeasurementClass::GridMeasurementClass()
    : _task(UPLOAD_INTERVAL, TASK_FOREVER, std::bind(&GridMeasurementClass::loop, this))
{
}

void GridMeasurementClass::init(Scheduler& scheduler)
{
    scheduler.addTask(_task);
    _task.enable();
}

GridMeasurementClass::RuntimeStatus GridMeasurementClass::getRuntimeStatus() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _runtimeStatus;
}

GridMeasurementClass::SkipReason GridMeasurementClass::createSnapshot(Snapshot& snapshot) const
{
    if (!NetworkSettings.isConnected()) {
        return SkipReason::NoNetwork;
    }
    if (!Mycila::NTP.isSynced()) {
        return SkipReason::NoTime;
    }
    if (!Hoymiles.isAllRadioIdle()) {
        return SkipReason::RadioBusy;
    }

    const CONFIG_T& config = Configuration.get();
    if (!std::isfinite(config.Ntp.Latitude) || config.Ntp.Latitude < -90.0 || config.Ntp.Latitude > 90.0
        || !std::isfinite(config.Ntp.Longitude) || config.Ntp.Longitude < -180.0 || config.Ntp.Longitude > 180.0
        || (std::fabs(config.Ntp.Latitude - NTP_LATITUDE) < 0.000001
            && std::fabs(config.Ntp.Longitude - NTP_LONGITUDE) < 0.000001)) {
        return SkipReason::NoLocation;
    }

    for (size_t i = 0; i < Hoymiles.getNumInverters(); i++) {
        const auto inverter = Hoymiles.getInverterByPos(static_cast<uint8_t>(i));
        if (inverter == nullptr || !inverter->getEnablePolling()) {
            continue;
        }

        auto* statistics = inverter->Statistics();
        if (!statistics->hasChannelFieldValue(TYPE_AC, CH0, FLD_UAC)
            || !statistics->hasChannelFieldValue(TYPE_AC, CH0, FLD_F)) {
            continue;
        }

        const uint32_t lastUpdate = statistics->getLastUpdate();
        if (lastUpdate == 0) {
            continue;
        }

        const uint32_t ageMs = millis() - lastUpdate;
        if (ageMs > MAX_STATISTICS_AGE_MS) {
            continue;
        }

        const float voltage = statistics->getChannelFieldValue(TYPE_AC, CH0, FLD_UAC);
        if (!std::isfinite(voltage) || voltage <= 0.0f || voltage > 500.0f) {
            continue;
        }

        const float frequency = statistics->getChannelFieldValue(TYPE_AC, CH0, FLD_F);
        if (!std::isfinite(frequency) || frequency < 45.0f || frequency > 55.0f) {
            continue;
        }

        const time_t now = time(nullptr);
        if (now <= 0) {
            return SkipReason::NoTime;
        }

        snapshot.observedAt = now - static_cast<time_t>(ageMs / 1000U);
        snapshot.latitude = std::round(config.Ntp.Latitude * 100.0) / 100.0;
        snapshot.longitude = std::round(config.Ntp.Longitude * 100.0) / 100.0;
        snapshot.l1Voltage = voltage;
        snapshot.frequency = frequency;

        return SkipReason::None;
    }

    return SkipReason::NoData;
}

void GridMeasurementClass::loop()
{
    const bool isRadioRetry = _radioRetryPending;
    _radioRetryPending = false;

    if (!Configuration.get().Ntp.UploadGridMeasurements) {
        return;
    }

    Snapshot snapshot {};
    const SkipReason skipReason = createSnapshot(snapshot);
    if (skipReason != SkipReason::None) {
        markSkipped(skipReason);
        if (skipReason == SkipReason::RadioBusy && !isRadioRetry) {
            _radioRetryPending = true;
            _task.delay(RADIO_RETRY_DELAY);
        }
        return;
    }

    auto* context = new (std::nothrow) TaskContext { this, snapshot };
    if (context == nullptr) {
        ESP_LOGE(TAG, "Unable to allocate upload task context");
        return;
    }

    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_uploadInProgress) {
            delete context;
            return;
        }
        _uploadInProgress = true;
    }

    const BaseType_t result = xTaskCreatePinnedToCore(
        sendTask,
        "grid_measurement",
        UPLOAD_TASK_STACK_SIZE,
        context,
        1,
        nullptr,
        tskNO_AFFINITY);

    if (result != pdPASS) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _uploadInProgress = false;
        }
        delete context;
        ESP_LOGE(TAG, "Unable to create upload task");
    }
}

void GridMeasurementClass::sendTask(void* context)
{
    auto* taskContext = static_cast<TaskContext*>(context);
    GridMeasurementClass* owner = taskContext->owner;
    owner->markUploadStarted();
    const UploadResult result = owner->send(taskContext->snapshot);
    delete taskContext;

    owner->markUploadFinished(result);

    vTaskDelete(nullptr);
}

void GridMeasurementClass::markUploadStarted()
{
    std::lock_guard<std::mutex> lock(_mutex);
    _runtimeStatus.state = State::Uploading;
    _runtimeStatus.skipReason = SkipReason::None;
    _runtimeStatus.lastAttempt = time(nullptr);
    _runtimeStatus.httpStatus = 0;
    _runtimeStatus.errorCode = ESP_OK;
}

void GridMeasurementClass::markUploadFinished(const UploadResult& result)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _runtimeStatus.state = result.success ? State::Success : State::Error;
    _runtimeStatus.httpStatus = result.httpStatus;
    _runtimeStatus.errorCode = result.errorCode;
    if (result.success) {
        _runtimeStatus.lastSuccess = time(nullptr);
    }
    _uploadInProgress = false;
}

void GridMeasurementClass::markSkipped(const SkipReason reason)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _runtimeStatus.skipReason = reason;
}

GridMeasurementClass::UploadResult GridMeasurementClass::send(const Snapshot& snapshot) const
{
    if (ESP.getMaxAllocHeap() < MINIMUM_TLS_HEAP_BLOCK) {
        ESP_LOGW(TAG, "Skipping grid measurement upload: insufficient contiguous heap for TLS");
        return { false, 0, ESP_ERR_NO_MEM };
    }

    struct tm utcTime;
    if (gmtime_r(&snapshot.observedAt, &utcTime) == nullptr) {
        ESP_LOGW(TAG, "Unable to format measurement timestamp");
        return { false, 0, ESP_FAIL };
    }

    char timestamp[21];
    if (strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", &utcTime) == 0) {
        ESP_LOGW(TAG, "Unable to format measurement timestamp");
        return { false, 0, ESP_FAIL };
    }

    JsonDocument document;
    document["observed_at"] = timestamp;
    document["latitude"] = snapshot.latitude;
    document["longitude"] = snapshot.longitude;
    document["l1_v"] = snapshot.l1Voltage;
    document["l2_v"] = -1;
    document["l3_v"] = -1;
    document["grid_frequency_hz"] = snapshot.frequency;

    String payload;
    serializeJson(document, payload);

    esp_http_client_config_t config {};
    config.url = API_ENDPOINT;
    config.timeout_ms = HTTP_TIMEOUT_MS;
    config.crt_bundle_attach = arduino_esp_crt_bundle_attach;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        ESP_LOGE(TAG, "Unable to initialize HTTPS client");
        return { false, 0, ESP_ERR_NO_MEM };
    }

    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, payload.c_str(), static_cast<int>(payload.length()));

    const esp_err_t result = esp_http_client_perform(client);
    const int statusCode = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (result == ESP_OK && statusCode == 202) {
        ESP_LOGI(TAG, "Grid measurement accepted");
    } else {
        ESP_LOGW(TAG, "Grid measurement upload failed: %s, HTTP %d", esp_err_to_name(result), statusCode);
    }

    return {
        result == ESP_OK && statusCode == 202,
        statusCode > 0 ? statusCode : 0,
        result == ESP_OK ? ESP_OK : result
    };
}

GridMeasurementClass GridMeasurement;