"""Shared, strictly validated PEGATRON v1 MQTT payloads."""
import re
from typing import Annotated, Literal

from pydantic import BaseModel, ConfigDict, Field, model_validator

ACTIONS = ("ENABLE_SORTING", "PAUSE_SORTING", "TEST_PUSHER", "CALIBRATE_TOF")
TRAYS = ("S", "M", "L")
FRESH_SECONDS = 3
COMMAND_SECONDS = 5
Action = Literal["ENABLE_SORTING", "PAUSE_SORTING", "TEST_PUSHER", "CALIBRATE_TOF"]
Mode = Literal["idle", "sorting", "maintenance", "fault"]
Token = Annotated[str, Field(min_length=1, max_length=64, pattern=r"^[A-Za-z0-9_-]+$")]
Counter = Annotated[int, Field(ge=0, le=2**53 - 1)]
StationId = Annotated[int, Field(ge=1, le=2)]
Message = Annotated[str, Field(min_length=1, max_length=256)]
Measurement = Annotated[float, Field(ge=-2000, le=2000, allow_inf_nan=False)]


def topic_prefix(device_id: str) -> str:
    if not re.fullmatch(r"[A-Za-z0-9_-]{1,64}", device_id):
        raise ValueError("Invalid MQTT device ID")
    return f"pegatron/v1/{device_id}"


class StrictModel(BaseModel):
    model_config = ConfigDict(strict=True, extra="forbid")


class Versioned(StrictModel):
    schema_version: Literal[1]


class Station(StrictModel):
    height_mm: Measurement | None
    distance_mm: Annotated[float, Field(ge=0, le=4000, allow_inf_nan=False)] | None = None
    sample_valid: bool
    home: bool | None
    push_count: Counter
    reference_mm: Measurement | None


class TrayCounts(StrictModel):
    S: Counter | None
    M: Counter | None
    L: Counter | None


class Permission(StrictModel):
    allowed: bool
    reason: Message

    @model_validator(mode="after")
    def validate_allowed(self):
        if self.allowed != (self.reason == "OK"):
            raise ValueError("Allowed requires reason OK")
        return self


class StationPermissions(StrictModel):
    station_1: Permission
    station_2: Permission


class Permissions(StrictModel):
    ENABLE_SORTING: Permission
    PAUSE_SORTING: Permission
    TEST_PUSHER: StationPermissions
    CALIBRATE_TOF: StationPermissions


class Telemetry(Versioned):
    boot_id: Token
    link_id: Token
    seq: Counter
    uptime_ms: Counter
    control_epoch: Counter
    mode: Mode
    estop: bool | None
    faults: Annotated[list[Message], Field(max_length=12)]
    station_1: Station
    station_2: Station
    trays: TrayCounts
    permissions: Permissions


class Availability(Versioned):
    link_id: Token
    status: Literal["online", "offline"]


class Event(Versioned):
    boot_id: Token
    event_id: Token
    kind: Literal["fault_raised", "fault_cleared", "mode_changed", "calibrated"]
    message: Message
    station: StationId | None = None
    reference_mm: Measurement | None = None


class CommandInput(StrictModel):
    action: Action
    station: StationId | None = None

    @model_validator(mode="after")
    def validate_station(self):
        if (self.action in ("TEST_PUSHER", "CALIBRATE_TOF")) != (self.station is not None):
            raise ValueError("Station is required only for test and calibration")
        return self


class Command(CommandInput, Versioned):
    request_id: Token
    link_id: Token
    control_epoch: Counter
    observed_uptime_ms: Counter


WIFI_SECONDS = 40
Ssid = Annotated[str, Field(min_length=1, max_length=32, pattern=r"^[^\x00-\x1f\x7f]+$")]


class WifiNetwork(StrictModel):
    ssid: Annotated[str, Field(min_length=1, max_length=64)]
    rssi: Annotated[int, Field(ge=-127, le=0)]
    secure: bool


class WifiResult(Versioned):
    request_id: Token
    kind: Literal["scan", "connect"]
    ok: bool
    reason: Message
    ssid: Annotated[str, Field(max_length=64)] | None = None
    networks: Annotated[list[WifiNetwork], Field(max_length=16)]


class WifiConnectInput(StrictModel):
    ssid: Ssid
    password: Annotated[str, Field(max_length=63, pattern=r"^[^\x00-\x1f\x7f]*$")] = ""

    @model_validator(mode="after")
    def validate_lengths(self):
        # Wi-Fi limits are in bytes (SSID) and 8-63 characters (WPA password); empty means open network.
        if len(self.ssid.encode()) > 32 or 0 < len(self.password) < 8:
            raise ValueError("Invalid Wi-Fi credentials")
        return self


class Ack(Versioned):
    request_id: Token
    link_id: Token
    result: Literal["executed", "rejected"]
    reason: Message
    mode: Mode

