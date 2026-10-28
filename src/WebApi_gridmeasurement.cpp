#include "WebApi_gridmeasurement.h"
#include "Configuration.h"
#include "GridMeasurement.h"
#include "WebApi.h"
#include <AsyncJson.h>
#include <esp_err.h>

namespace {
const char* stateName(const GridMeasurementClass::State state)
{
    switch (state) {
    case GridMeasurementClass::State::Never:
        return "never";
    case GridMeasurementClass::State::Uploading:
        return "uploading";
    case GridMeasurementClass::State::Success:
        return "success";
    case GridMeasurementClass::State::Error:
        return "error";
    }
    return "never";
}

const char* skipReasonName(const GridMeasurementClass::SkipReason reason)
{
    switch (reason) {
    case GridMeasurementClass::SkipReason::None:
        return "none";
    case GridMeasurementClass::SkipReason::NoNetwork:
        return "no_network";
    case GridMeasurementClass::SkipReason::NoTime:
        return "no_time";
    case GridMeasurementClass::SkipReason::NoLocation:
        return "no_location";
    case GridMeasurementClass::SkipReason::NoData:
        return "no_data";
    case GridMeasurementClass::SkipReason::RadioBusy:
        return "radio_busy";
    }
    return "none";
}
}

void WebApiGridMeasurementClass::init(AsyncWebServer& server, Scheduler&)
{
    using std::placeholders::_1;

    server.on("/api/gridmeasurement/status", HTTP_GET, static_cast<ArRequestHandlerFunction>(std::bind(&WebApiGridMeasurementClass::onStatus, this, _1)));
}

void WebApiGridMeasurementClass::onStatus(AsyncWebServerRequest* request)
{
    if (!WebApi.checkCredentialsReadonly(request)) {
        return;
    }

    const auto status = GridMeasurement.getRuntimeStatus();
    AsyncJsonResponse* response = new AsyncJsonResponse();
    auto& root = response->getRoot();

    root["enabled"] = Configuration.get().Ntp.UploadGridMeasurements;
    root["state"] = stateName(status.state);
    root["skip_reason"] = skipReasonName(status.skipReason);
    root["http_status"] = status.httpStatus;
    root["error"] = status.errorCode == ESP_OK ? "" : esp_err_to_name(status.errorCode);
    root["last_attempt"] = status.lastAttempt > 0 ? static_cast<uint64_t>(status.lastAttempt) : 0;
    root["last_success"] = status.lastSuccess > 0 ? static_cast<uint64_t>(status.lastSuccess) : 0;

    WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
}