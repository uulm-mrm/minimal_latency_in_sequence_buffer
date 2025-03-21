from enum import IntEnum
from datetime import datetime, timedelta, UTC
from minimal_latency_buffer import MinimalLatencyBuffer, MLParams, Mode, PushReturn, PopReturn


class Sensor(IntEnum):
    A = 1
    B = 2


def push_expect_ok(buffer: MinimalLatencyBuffer, id: Sensor, receipt_time_ms: int, meas_time_ms: int):
    receipt_time = datetime.fromtimestamp(0, UTC) + timedelta(milliseconds=receipt_time_ms)
    meas_time = datetime.fromtimestamp(0, UTC) + timedelta(milliseconds=meas_time_ms)
    res = buffer.push(int(id), receipt_time, meas_time, 'placeholder')

    assert res == PushReturn.Ok


def pop_expect_data(buffer: MinimalLatencyBuffer, cur_time_ms: int, num_data: int = 1, num_discarded: int = 0):
    cur_time = datetime.fromtimestamp(0, UTC) + timedelta(milliseconds=cur_time_ms)
    res = buffer.pop(cur_time)

    assert len(res.data) == num_data
    assert len(res.discarded_data) == num_discarded


def test_buffer_minimalistic():
    params = MLParams()
    params.mode = Mode.Single

    buffer = MinimalLatencyBuffer(params)

    pop_expect_data(buffer, 25, 0, 0)
    push_expect_ok(buffer, Sensor.A, 60, 50)
    pop_expect_data(buffer, 60, 1, 0)
    pop_expect_data(buffer, 61, 0, 0)
    push_expect_ok(buffer, Sensor.A, 110, 100)
    pop_expect_data(buffer, 110, 1, 0)
    push_expect_ok(buffer, Sensor.A, 160, 150)
    pop_expect_data(buffer, 160, 1, 0)


if __name__ == "__main__":
    test_buffer_minimalistic()
