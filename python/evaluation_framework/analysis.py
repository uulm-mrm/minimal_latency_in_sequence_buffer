from typing import List
from dataclasses import asdict

from minimal_latency_buffer.evaluation_framework.monte_carlo_framework import RunData, format_timedelta


def map_to_time_scaling(time_unit: str):
    # allow easy switch between different time units
    if time_unit == 's' or time_unit == 'seconds':
        time_scaling = 1
        unit_abbreviation = 's'
    elif time_unit == 'ms' or time_unit == 'milliseconds':
        time_scaling = 1e3
        unit_abbreviation = 'ms'
    elif time_unit == 'us' or time_unit == 'microseconds':
        time_scaling = 1e6
        unit_abbreviation = 'us'
    elif time_unit == 'ns' or time_unit == 'nanoseconds':
        time_scaling = 1e9
        unit_abbreviation = 'ns'
    else:
        raise NotImplementedError("Unsupported time unit")

    return time_scaling, unit_abbreviation


def print_statistics(data: List[RunData], time_unit='ms'):
    time_scaling, unit_abbreviation = map_to_time_scaling(time_unit)

    print("\n### Analysis ###")

    # count number of discarded messages
    total_messages = 0
    total_discarded = 0
    per_input_discarded = {}
    for run_data in data:
        for pop_result in run_data.outputs.values():
            for element in pop_result.discarded_data:
                total_discarded += 1
                total_messages += 1
                if element.id not in per_input_discarded:
                    per_input_discarded[element.id] = 0

                per_input_discarded[element.id] += 1

            for element in pop_result.data:
                total_messages += 1

    print(f"Total number of discarded messages: {total_discarded} / {total_messages} ({total_discarded / total_messages * 100:.2f} %)")
    print("Discarded messages per input source: ")
    for source, count in per_input_discarded.items():
        print(f"  {source}: {count}")

    print("Final latency and period of all inputs (only considering first MC run)")

    max_time = max(data[0].estimates.keys())

    for (id, estimate) in data[0].estimates[max_time].items():
        print(f"{id}: ")
        for key, value in asdict(estimate).items():
            if 'jitter' in key:
                # output contains mean + jitter quantile
                mean_key = key.split('_jitter')[0]
                mean = asdict(estimate)[mean_key]
                print(f"  {key} (mean + jitter): {format_timedelta(value)}")
            else:
                print(f"  {key}: {format_timedelta(value)}")

