import argparse
import rosbag2_py
import os
import math
import matplotlib.pyplot as plt
import fnmatch
import numpy as np
from datetime import datetime, timedelta, UTC
from typing import Dict, Set

from rclpy.serialization import deserialize_message
from rosidl_runtime_py.utilities import get_message
from tqdm import tqdm

from minimal_latency_buffer import Buffer, Params, Mode, PushReturn, FixedLagBuffer, FixedLagBufferParams

from minimal_latency_buffer.evaluation_framework.generators import PopGenerator
from minimal_latency_buffer.evaluation_framework.monte_carlo_framework import Estimates, Input, RunData, print_params
from minimal_latency_buffer.evaluation_framework.visualization import create_plots
from minimal_latency_buffer.evaluation_framework.analysis import print_statistics

def initialize_shortening_LUT(topics_of_interest: Set) -> Dict[str, str]:
    lut = {}

    # topics usually follow the convention /sensor_type/sensor_placement/more_specific_stuff
    # --> remove the too specific stuff to have shorter names in the legend
    for t in topics_of_interest:
        lut[t] = '_'.join(t.split('/')[1:3])

    return lut


def evaluate_rosbag(pop_period: timedelta,
                    buffer_params: Params,
                    rosbag_path: str,
                    topic_filter: Set[str] = None) -> RunData:
    assert os.path.isdir(rosbag_path), f"ROSbag \'{rosbag_path}\' does not exist!"

    # open rosbag for reading
    reader = rosbag2_py.SequentialReader()
    reader.open_uri(rosbag_path)

    bag_metadata = reader.get_metadata()
    topic_types = reader.get_all_topics_and_types()

    # determine all available and valid (i.e. containing an header) topics
    available_topics = []
    type_LUT = {}
    for topic_metadata in topic_types:
        message_type = get_message(topic_metadata.type)

        if 'header' in message_type.get_fields_and_field_types().keys():
            available_topics.append(topic_metadata.name)
            type_LUT[topic_metadata.name] = topic_metadata.type
        else:
            print(f"Skipping topic '{topic_metadata.name}' - no header available!")

    if topic_filter is None or len(topic_filter) == 0:
        # using all available topics from the rosbag
        topic_filter = available_topics

    # resolve potential wildcards in topics
    topics_of_interest = set()

    for pattern in topic_filter:
        if '*' not in pattern:
            topics_of_interest.add(pattern)
            continue

        # check all available topics
        for metadata in topic_types:
            if fnmatch.fnmatch(metadata.name, pattern):
                topics_of_interest.add(metadata.name)

    print_params(pop_period, None, buffer_params, None, None)

    assert len(topic_filter) > 0, "Invalid topic filter list provided, zero length!"
    print("Considered topics: ")
    for topic in topics_of_interest:
        print(" - ", topic)

    name_shortening_LUT = initialize_shortening_LUT(topics_of_interest)

    # setup required classes
    pop_gen = None  # fully initialized once the timestamp of the first message is known

    # toggle here to switch between buffer implementations
    # buffer = Buffer(buffer_params)
    buffer = FixedLagBuffer(FixedLagBufferParams(lag=timedelta(milliseconds=180)))
    # buffer = FixedLagBuffer(FixedLagBufferParams(lag=timedelta(milliseconds=225)))

    print("\n### Iterating over the rosbag ###")

    count = 0
    PRE_SKIP = 5000
    run_data = RunData()
    with tqdm(total=bag_metadata.message_count, unit="msgs") as progress_bar:
        while reader.has_next():
            (topic, data, t) = reader.read_next()

            if topic not in topics_of_interest or count < PRE_SKIP:
                count += 1
                progress_bar.update()
                continue

            if pop_gen is None:
                # floor to have the pop times on a nice grid
                init_time = math.floor(t / 1e9)

                pop_gen = PopGenerator(period=pop_period, init_time=datetime.fromtimestamp(init_time, UTC))
                cur_time = pop_gen._time

            # progress in time with pop-operations until we have reached the reception timestamp of the next input
            while (t - cur_time.timestamp() * 1e9) > pop_period.total_seconds() * 1e9:
                cur_time = next(pop_gen)

                estimates = {}
                for topic_name in topics_of_interest:
                    id = name_shortening_LUT[topic_name]
                    estimates[id] = Estimates(
                        period=buffer.estimated_period(id),
                        period_stddev=buffer.estimated_period_stddev(id),
                        period_jitter=buffer.estimated_period_jitter(id, buffer_params.jitter_quantile),
                        latency=buffer.estimated_latency(id),
                        latency_stddev=buffer.estimated_latency_stddev(id),
                        latency_jitter=buffer.estimated_latency_jitter(id, buffer_params.jitter_quantile)
                    )

                run_data.estimates[cur_time] = estimates

                res = buffer.pop(cur_time)
                if len(res.data) > 0 or len(res.discarded_data) > 0:
                    run_data.outputs[cur_time] = res

            # construct simulation input from real-world timestamps to keep compatibility for the evaluation
            deserialized_msg = deserialize_message(data, get_message(type_LUT[topic]))
            meas_stamp_ns = deserialized_msg.header.stamp.sec * 1e9 + deserialized_msg.header.stamp.nanosec

            # timedelta is unfortunately limited to microsecond precision, however,
            # this is enough for our use case
            input = Input(id=name_shortening_LUT[topic],
                          meas_time=datetime.fromtimestamp(meas_stamp_ns / 1e9, UTC),
                          receipt_time=datetime.fromtimestamp(t / 1e9, UTC))
            res = buffer.push(input.id, input.receipt_time, input.meas_time, input)

            run_data.inputs[input.receipt_time] = input
            assert res == PushReturn.Ok, "Unable to push input to buffer"

            progress_bar.update()

    return run_data


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument('--path', type=str, required=True, help="Path to the input rosbag.")
    parser.add_argument('--pop_period', type=float, default=10, help="Pop period to use within the buffer evaluation [unit: milliseconds]")
    parser.add_argument('--topics', type=str, nargs='+', help="Topics to consider for the evaluation. Wildcards will be matched against"
                                                              "availabe topics within the rosbag.")

    args = parser.parse_args()

    print("### Buffer Evaluation Using Real-World Data ###")

    pop_period = timedelta(milliseconds=args.pop_period)

    buffer_params = Params()
    buffer_params.mode = Mode.Single
    buffer_params.jitter_quantile = 0.99
    buffer_params.max_jitter = timedelta(milliseconds=5)
    buffer_params.max_wait_duration_quantile = 0.99

    results = evaluate_rosbag(pop_period=pop_period,
                              buffer_params=buffer_params,
                              rosbag_path=args.path,
                              topic_filter=args.topics)

    create_plots([results], time_unit='ms')
    print_statistics([results], time_unit='ms')

    delays = {}
    for run_data in [results]:
        for pop_time, pop_result in run_data.outputs.items():
            for element in pop_result.data:
                merged_id = element.id.split('_')[0]
                delay = pop_time - element.data.receipt_time
                if merged_id not in delays:
                    delays[merged_id] = []
                delays[merged_id].append(delay.total_seconds())


    for sensor_id, delay in delays.items():
        print("Sensor Delays", sensor_id)
        print("Mean", np.mean(delay))
        print("Median", np.median(delay))
        print("Min/Max", np.min(delay), np.max(delay))
        print("q_2", np.quantile(delay, 0.2))
        print("q_8", np.quantile(delay, 0.8))
        print("output for tikz box plot:")
        print(f"0 {np.median(delay)} {np.quantile(delay, 0.75)} {np.quantile(delay, 0.25)} {np.max(delay)} {np.min(delay)}")

    plt.show()