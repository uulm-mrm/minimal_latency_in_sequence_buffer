import random
import datetime
from dataclasses import dataclass
from collections.abc import Generator

@dataclass
class MeasGenParams:
    period: datetime.timedelta
    period_stddev: datetime.timedelta
    latency: datetime.timedelta
    latency_stddev: datetime.timedelta
    drop_rate: float = 0
    run_in_sync: bool = False
    initial_phase_offset: datetime.timedelta = None


class MeasGenerator:
    def __init__(self, params: MeasGenParams, init_time: datetime = datetime.datetime.fromtimestamp(0, tz=None)):
        # to be able to synchronize different generators, the update time must consider noise separately
        self.meas_time = init_time
        self.meas_quantile = 0
        self.receipt_time = init_time
        if params.initial_phase_offset is not None:
            self.meas_time += params.initial_phase_offset
            self.receipt_time += params.initial_phase_offset
        self.step = 0
        self.wait_skip = 10
        self.params = params

        self._generate_next_input()

    def get_next(self, query_time: datetime):
        if self.receipt_time < query_time:
            current_input = (self.meas_time + self.meas_quantile, self.receipt_time)

            self._generate_next_input()

            return current_input
        else:
            return None, None


    def _generate_next_input(self):
        self.step += 1
        # TODO: replace message dropping with geometric distribution
        num_drop = 1
        if self.step > self.wait_skip:
            while random.random() < self.params.drop_rate:
                num_drop += 1

        meas_jitter = datetime.timedelta(
            microseconds=random.gauss(mu=0, sigma=self.params.period_stddev / datetime.timedelta(microseconds=1)))
        if self.params.run_in_sync:
            self.meas_time += self.params.period * num_drop

            self.meas_quantile = meas_jitter
        else:
            # without considering the noise separately, two meas generators will slowly drift apart
            self.meas_time += max(datetime.timedelta(0), self.params.period * num_drop + meas_jitter)
            self.meas_quantile = datetime.timedelta(0)

        latency_mean_us = self.params.latency / datetime.timedelta(microseconds=1)
        latency_stddev_us = self.params.latency_stddev / datetime.timedelta(microseconds=1)
        latency = datetime.timedelta(microseconds=max(0, random.gauss(mu=latency_mean_us, sigma=latency_stddev_us)))
        self.receipt_time = self.meas_time + latency


class PopGenerator(Generator):
    def __init__(self,
                 period: datetime.timedelta,
                 init_time: datetime = datetime.datetime.fromtimestamp(0, tz=None)):
        self._period = period
        self._time = init_time

    def send(self, ignored_arg):
        self._time += self._period
        return self._time

    def throw(self, type=None, value=None, traceback=None):
        raise StopIteration

