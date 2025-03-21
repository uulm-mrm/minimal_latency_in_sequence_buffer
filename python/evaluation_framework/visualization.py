import signal
import numpy as np
from scipy import stats
import matplotlib.cm as cm
import matplotlib.pyplot as plt
import csv
import os

from minimal_latency_buffer.evaluation_framework.analysis import map_to_time_scaling

signal.signal(signal.SIGINT, signal.SIG_DFL)  # abort even when the matplotlib figure is not in focus


def create_latency_plot(data, time_scaling: float, time_unit: str):
    # extract data
    # timestamps are identical across all runs
    timestamps = [e.timestamp() * time_scaling for e in data[0].estimates.keys()]
    if len(timestamps) == 0:
        return

    averaged_per_sensor_data = {}
    for run_data in data:
        for idx, combined_input_estimates in enumerate(run_data.estimates.values()):
            for (id, estimate) in combined_input_estimates.items():
                if id not in averaged_per_sensor_data.keys():
                    averaged_per_sensor_data[id] = {'latency': np.zeros(len(timestamps)),
                                                    'latency_stddev': np.zeros(len(timestamps)),
                                                    'latency_jitter': np.zeros(len(timestamps))}

                averaged_per_sensor_data[id]['latency'][idx] += estimate.latency.total_seconds() * time_scaling
                averaged_per_sensor_data[id]['latency_stddev'][idx] += estimate.latency_stddev.total_seconds() * time_scaling
                averaged_per_sensor_data[id]['latency_jitter'][idx] += estimate.latency_jitter.total_seconds() * time_scaling

    for id in averaged_per_sensor_data.keys():
        for key in ['latency', 'latency_stddev', 'latency_jitter']:
            averaged_per_sensor_data[id][key] /= len(data)

    # create figure
    fig = plt.figure()

    ax = fig.add_subplot(1, 1, 1)
    plt.xlabel(f"Pop Time [{time_unit}]")
    plt.ylabel(time_unit)
    plt.title(f"Num runs = {len(data)}")

    for i, estimates in averaged_per_sensor_data.items():
        plt.plot(timestamps, estimates['latency'], label=f"Latency {i}")
        plt.plot(timestamps, estimates['latency_stddev'], label=f"Latency Stddev {i}")
        plt.plot(timestamps, estimates['latency_jitter'], label=f"Latency Jitter {i}")
    plt.legend()

    # scatter_data = [['x','latency','latency_25', 'latency_75']]
    # scatter_data += [[timestamp-10, period, stats.norm.ppf(0.25, period, stddev), stats.norm.ppf(0.75, period, stddev)] for timestamp, period, stddev in zip(timestamps, averaged_per_sensor_data[0]['latency'], averaged_per_sensor_data[0]['latency_stddev'])]
    # scatter_data = scatter_data[::10]
    # with open('latency_data.csv', 'w') as file:
    #     wr = csv.writer(file)#, quoting=csv.QUOTE_ALL)
    #     wr.writerows(scatter_data)

    return ax


def create_period_plot(data, time_scaling: float, time_unit: str, sharex=None):
    # extract data
    # timestamps are identical across all runs
    timestamps = [e.timestamp() * time_scaling for e in data[0].estimates.keys()]
    if len(timestamps) == 0:
        return

    averaged_per_source_data = {}
    for run_data in data:
        for idx, combined_input_estimates in enumerate(run_data.estimates.values()):
            for (id, estimate) in combined_input_estimates.items():
                if id not in averaged_per_source_data.keys():
                    averaged_per_source_data[id] = {'period': np.zeros(len(timestamps)),
                                                    'period_stddev': np.zeros(len(timestamps)),
                                                    'period_jitter': np.zeros(len(timestamps))}

                averaged_per_source_data[id]['period'][idx] += estimate.period.total_seconds() * time_scaling
                averaged_per_source_data[id]['period_stddev'][idx] += estimate.period_stddev.total_seconds() * time_scaling
                averaged_per_source_data[id]['period_jitter'][idx] += estimate.period_jitter.total_seconds() * time_scaling

    for id in averaged_per_source_data.keys():
        for key in ['period', 'period_stddev', 'period_jitter']:
            averaged_per_source_data[id][key] /= len(data)

    # create figure
    fig = plt.figure()
    fig.add_subplot(1, 1, 1, sharex=sharex)
    plt.xlabel(f"Pop Time [{time_unit}]")
    plt.ylabel(time_unit)
    plt.title(f"Num runs = {len(data)}")

    for i, estimates in averaged_per_source_data.items():
        plt.plot(timestamps, estimates['period'], label=f"Period {i}")
        plt.plot(timestamps, estimates['period_stddev'], label=f"Period Stddev {i}")
        plt.plot(timestamps, estimates['period_jitter'], label=f"Period Jitter {i}")
    plt.legend()

    # scatter_data = [['x','period','period_25', 'period_75']]
    # scatter_data += [[timestamp-10, period, stats.norm.ppf(0.25, period, stddev), stats.norm.ppf(0.75, period, stddev)] for timestamp, period, stddev in zip(timestamps, averaged_per_source_data[0]['period'], averaged_per_source_data[0]['period_stddev'])]
    # scatter_data = scatter_data[::10]
    #
    # with open('period_data.csv', 'w') as file:
    #     wr = csv.writer(file)#, quoting=csv.QUOTE_ALL)
    #     wr.writerows(scatter_data)



def create_buffer_delay_plot(data, time_scaling: float, time_unit: str, sharex=None):
    # 'buffer delay' is defined as the time an input is queued within the buffer, i.e. pop_time - receipt_time
    max_buffer_delays = {}
    delays = {}
    for run_data in data:
        for pop_time, pop_result in run_data.outputs.items():
            for element in pop_result.data:
                delay = pop_time - element.data.receipt_time
                if element.id not in delays:
                    delays[element.id] = []
                delays[element.id].append((pop_time, delay))

    # create figure
    fig = plt.figure()
    fig.add_subplot(1, 1, 1, sharex=sharex)
    plt.xlabel(f"Pop Time [{time_unit}]")
    plt.ylabel(time_unit)
    # plt.title(f"Num runs = {len(data)} - max delay over all runs and times: {max_buffer_delay.total_seconds() * time_scaling} {time_unit}")

    scatter_data = [['x','y','label']]
    for source_id, delay_data in delays.items():
        plt.scatter([e[0].timestamp() * time_scaling for e in delay_data],
                    [e[1].total_seconds() * time_scaling for e in delay_data], label=source_id, s=3.5)
        scatter_data += [[e[0].timestamp(), e[1].total_seconds(), source_id] for e in delay_data]
    plt.legend()
    #
    # with open('scatter_data.csv', 'w') as file:
    #     wr = csv.writer(file)#, quoting=csv.QUOTE_ALL)
    #     wr.writerows(scatter_data)


def create_buffer_delay_histogram(data, time_scaling: float, time_unit: str, colors: list = None, export_data:bool = False, shared_ids:dict = None):
    # extract data

    # 'buffer delay' is defined as the time an input is queued within the buffer, i.e. pop_time - receipt_time
    buffer_delays = {}
    for run_data in data:
        for pop_time, pop_result in run_data.outputs.items():
            for element in pop_result.data:
                if not element.id in buffer_delays:
                    buffer_delays[element.id] = [(pop_time - element.data.receipt_time).total_seconds() * time_scaling]
                else:
                    buffer_delays[element.id].append((pop_time - element.data.receipt_time).total_seconds() * time_scaling)

    max_buffer_delay = max(buffer_delays)

    # create figure
    plt.figure("Buffer Delay Histogram")
    plt.xlabel(f"Buffer Delay [{time_unit}]")
    plt.ylabel("Number of occurrences")
    plt.title(f"Num runs = {len(data)} - max delay over all runs and times: {max_buffer_delay} {time_unit}")


    if shared_ids is not None:
        grouped_delays = {k:[] for k in shared_ids.keys()}
        for key, value in shared_ids.items():
            for id in value:
                grouped_delays[key].extend(buffer_delays[id])
    else:
        grouped_delays = buffer_delays

    for idx, (key, values) in enumerate(grouped_delays.items()):
        counts, bins = np.histogram(values, density=True, bins=200, range=(0, 90))
        # print(bins)
        # print(counts)
        if export_data:
            file_name_base = key + "_histogram_bin_data"
            file_name = file_name_base + ".tex"
            counter = 0
            while os.path.isfile(file_name):
                file_name = file_name_base + "_" + str(counter) + ".tex"

            with open(file_name, "w") as csv_file:
                for count, bin in zip(counts, bins):
                    csv_file.write(f'{bin},{count}\n')
        additional_plot_args = {}
        if colors is not None:
            additional_plot_args['color'] = colors[idx]
        plt.hist(bins[:-1], bins, weights=counts, *additional_plot_args)


def create_timing_plot(run_data, time_scaling: float, time_unit: str, sharex=None):
    meas_times_x = []
    meas_times_y = []
    receipt_times_x = []
    receipt_times_y = []
    receipt_colors = []
    meas_colors = []
    discarded_meas_x = []
    discarded_meas_y = []
    discarded_meas_earliest_meas_x = []
    discarded_meas_earliest_meas_y = []
    discarded_meas_latest_receipt_x = []
    discarded_meas_latest_receipt_y = []
    meas_earliest_meas_x = []
    meas_earliest_meas_y = []
    meas_latest_receipt_x = []
    meas_latest_receipt_y = []
    discarded_colors = []
    pop_times = []
    pop_colors = []

    color_index = 0
    id_mapping = {}
    for pop_time, output in run_data.outputs.items():
        if len(output.data) == 0 and len(output.discarded_data) == 0:
            continue

        color_index = (color_index + 1) % 10
        color = plt.cm.tab10(color_index)

        pop_times.append(pop_time.timestamp() * time_scaling)
        pop_colors.append(color)

        for element in output.data:
            if element.id not in id_mapping:
                id_mapping[element.id] = len(id_mapping)

            meas_times_x.append(element.meas_time.timestamp() * time_scaling)
            meas_times_y.append(0.5 * id_mapping[element.id])
            meas_colors.append(color)

            receipt_times_x.append(element.receipt_time.timestamp() * time_scaling)
            receipt_times_y.append(0.5 * id_mapping[element.id] + 0.05)
            receipt_colors.append(color)
            meas_earliest_meas_x.append(element.earliest_meas_time.timestamp() * time_scaling)
            meas_earliest_meas_y.append(0.5 * id_mapping[element.id] + 0.01)
            meas_latest_receipt_x.append(element.latest_receipt_time.timestamp() * time_scaling)
            meas_latest_receipt_y.append(0.5 * id_mapping[element.id] + 0.02)

        for element in output.discarded_data:
            if element.id not in id_mapping:
                id_mapping[element.id] = len(id_mapping)

            discarded_meas_x.append(element.meas_time.timestamp() * time_scaling)
            discarded_meas_y.append(0.5 * id_mapping[element.id])
            discarded_meas_earliest_meas_x.append(element.earliest_meas_time.timestamp() * time_scaling)
            discarded_meas_earliest_meas_y.append(0.5 * id_mapping[element.id] + 0.01)
            discarded_meas_latest_receipt_x.append(element.latest_receipt_time.timestamp() * time_scaling)
            discarded_meas_latest_receipt_y.append(0.5 * id_mapping[element.id] + 0.02)
            discarded_colors.append(color)

            receipt_times_x.append(element.receipt_time.timestamp() * time_scaling)
            receipt_times_y.append(0.5 * id_mapping[element.id] + 0.05)
            receipt_colors.append(color)

    # create figure
    fig = plt.figure()
    fig.add_subplot(1, 1, 1, sharex=sharex)
    for time, color in zip(pop_times, pop_colors):
        plt.axvline(x=time, color=color)

    plt.scatter(meas_times_x, meas_times_y, label=f"Meas Times", color=meas_colors)
    plt.scatter(meas_earliest_meas_x, meas_earliest_meas_y, label=f"Meas Times", color=meas_colors, marker='.')
    plt.scatter(meas_latest_receipt_x, meas_latest_receipt_y, label=f"Meas Times", color=meas_colors, marker='^')

    plt.scatter(receipt_times_x, receipt_times_y, label=f"Receipt Times", color=receipt_colors, marker='v')
    plt.scatter(discarded_meas_x, discarded_meas_y, label=f"Receipt Times", color=discarded_colors, marker='x')
    plt.scatter(discarded_meas_earliest_meas_x, discarded_meas_earliest_meas_y, label=f"Receipt Times", color=discarded_colors, marker='.')
    plt.scatter(discarded_meas_latest_receipt_x, discarded_meas_latest_receipt_y, label=f"Receipt Times", color=discarded_colors, marker='^')

    min_time = min(pop_times)
    for key, value in id_mapping.items():
        plt.annotate(key, (min_time, 0.5 * value + 0.06), ha='right')

    plt.xlabel(f"Pop Time [{time_unit}]")


def create_plots(data, time_unit: str = 'seconds', colors: list = None, export_data: bool = False, shared_ids: dict = None):
    time_scaling, unit_abbreviation = map_to_time_scaling(time_unit)

    # all plots share the same x-axis to forward zooming to all of them
    # ax = create_latency_plot(data, time_scaling, unit_abbreviation)
    # create_period_plot(data, time_scaling, unit_abbreviation, sharex=ax)
    # create_buffer_delay_plot(data, time_scaling, unit_abbreviation, sharex=ax)
    # create_buffer_delay_histogram(data, time_scaling, unit_abbreviation, colors, export_data, shared_ids)

    # only available for a single run
    # create_timing_plot(data[0], time_scaling, unit_abbreviation, sharex=ax)
    create_timing_plot(data[0], time_scaling, unit_abbreviation)
