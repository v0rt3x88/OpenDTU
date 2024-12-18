// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

enum WebApiError {
    GenericBase = 1000,
    GenericSuccess,
    GenericNoValueFound,
    GenericDataTooLarge, // not used anymore
    GenericParseError,
    GenericValueMissing,
    GenericWriteFailed,
    GenericInternalServerError,

    DtuBase = 2000,
    DtuSerialZero,
    DtuPollZero,
    DtuInvalidPowerLevel,
    DtuInvalidCmtFrequency,
    DtuInvalidCmtCountry,

    ConfigBase = 3000,
    ConfigNotDeleted,
    ConfigSuccess,

    InverterBase = 4000,
    InverterSerialZero,
    InverterNameLength,
    InverterCount,
    InverterAdded,
    InverterInvalidId,
    InverterInvalidMaxChannel,
    InverterChanged,
    InverterDeleted,
    InverterOrdered,
    InverterStatsResetted,

    LimitBase = 5000,
    LimitSerialZero,
    LimitInvalidLimit,
    LimitInvalidType,
    LimitInvalidInverter,

    MaintenanceBase = 6000,
    MaintenanceRebootTriggered,
    MaintenanceRebootCancled,

    NetworkBase = 8000,
    NetworkIpInvalid,
    NetworkNetmaskInvalid,
    NetworkGatewayInvalid,
    NetworkDns1Invalid,
    NetworkDns2Invalid,
    NetworkApTimeoutInvalid,

    NtpBase = 9000,
    NtpServerLength,
    NtpTimezoneLength,
    NtpTimezoneDescriptionLength,
    NtpYearInvalid,
    NtpMonthInvalid,
    NtpDayInvalid,
    NtpHourInvalid,
    NtpMinuteInvalid,
    NtpSecondInvalid,
    NtpTimeUpdated,

    SecurityBase = 10000,
    SecurityPasswordLength,
    SecurityAuthSuccess,

    PowerBase = 11000,
    PowerSerialZero,
    PowerInvalidInverter,

    HardwareBase = 12000,
    HardwarePinMappingLength,
};
