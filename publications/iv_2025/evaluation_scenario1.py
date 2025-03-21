import datetime
from matplotlib import pyplot as plt
import numpy as np
import random

from minimal_latency_buffer import Params, Mode, MinimalLatencyBuffer

from minimal_latency_buffer.buffer import FixedLagBuffer, FixedLagBufferParams
from minimal_latency_buffer.evaluation_framework.generators import MeasGenParams
from minimal_latency_buffer.evaluation_framework.monte_carlo_framework import evaluate_MC, format_timedelta
from minimal_latency_buffer.evaluation_framework.visualization import create_plots

random.seed(10)


if __name__ == "__main__":
    print("### Monte-Carlo Evaluation for buffer - Two Sensor Scenario ###")

    pop_period = datetime.timedelta(milliseconds=1)

    sensor_0 = MeasGenParams(
        period=datetime.timedelta(milliseconds=100),
        period_stddev=datetime.timedelta(milliseconds=1),
        latency=datetime.timedelta(milliseconds=100),
        latency_stddev=datetime.timedelta(milliseconds=10),
        drop_rate=0.0
    )

    sensor_1 = MeasGenParams(
        period=datetime.timedelta(milliseconds=50),
        period_stddev=datetime.timedelta(milliseconds=1),
        latency=datetime.timedelta(milliseconds=15),
        latency_stddev=datetime.timedelta(milliseconds=1),
        drop_rate=0.0
    )



    sensors1 = [sensor_0, sensor_1]

    adaptive_buffer_params = Params()
    adaptive_buffer_params.mode = Mode.Single
    adaptive_buffer_params.jitter_quantile = 0.99
    adaptive_buffer_params.max_jitter = datetime.timedelta(milliseconds=10000000)
    adaptive_buffer_params.max_wait_duration_quantile = 0.99
    adaptive_buffer_params.max_wait_duration = datetime.timedelta(milliseconds=1000000)

    # fixed_lag_buffer_params = FixedLagBufferParams(lag=datetime.timedelta(milliseconds=100))
    # fixed_lag_buffer_params = FixedLagBufferParams(lag=datetime.timedelta(milliseconds=123.38))
    fixed_lag_buffer_params = FixedLagBufferParams()
    slow_sensor = sensor_1
    if sensor_0.latency > sensor_1.latency:
        slow_sensor = sensor_0
    fixed_lag_buffer_params.delay_mean = slow_sensor.latency
    # fixed_lag_buffer_params.delay_stddev = None
    fixed_lag_buffer_params.delay_stddev = slow_sensor.latency_stddev
    # intervals are defined symmetrically with the adaptive buffer,
    # thus, for the FixedLagBuffer the quantile must be shifted accordingly
    fixed_lag_buffer_params.delay_quantile = ((1 - adaptive_buffer_params.max_wait_duration_quantile) / 2
                                              + adaptive_buffer_params.max_wait_duration_quantile)


    results = evaluate_MC(pop_period=pop_period,
                          meas_gen_params=sensors1,
                          # toggle this to switch between buffer implementations
                          buffer_params=adaptive_buffer_params,
                          # buffer_params=fixed_lag_buffer_params,
                          num_iterations_per_run=40000,
                          num_runs=10,
                          num_warm_up=10000,
                          skip_verification=True)

    num_data = {}
    num_drops = {}
    drop_ratio = {}
    for sensor_id, sensor in enumerate(sensors1):
        num_data[sensor_id] = np.sum([len([time for time, data in mc_run.inputs.items() if data.id==sensor_id]) for mc_run in results])
        num_drops[sensor_id] = np.sum([np.sum([len([r for r in output.discarded_data if r.id==sensor_id]) for time, output in mc_run.outputs.items()]) for mc_run in results])
        print("Sensor", sensor_id, "Drop Rate", num_drops[sensor_id]/num_data[sensor_id]*100, "%")
        drop_ratio[sensor_id] = num_drops[sensor_id]/num_data[sensor_id]

    delays = {}
    for run_data in results:
        for pop_time, pop_result in run_data.outputs.items():
            for element in pop_result.data:
                delay = pop_time - element.data.receipt_time
                if element.id not in delays:
                    delays[element.id] = []
                delays[element.id].append(delay.total_seconds())


    for sensor_id, delay in delays.items():
        print("Sensor Delays", sensor_id)
        print("Mean", np.mean(delay))
        print("Median", np.median(delay))
        print("Min/Max", np.min(delay), np.max(delay))
        print("q_2", np.quantile(delay, 0.2))
        print("q_8", np.quantile(delay, 0.8))
        print("output for tikz box plot:")
        print(f"0 {np.median(delay)} {np.quantile(delay, 0.75)} {np.quantile(delay, 0.25)} {np.max(delay)} {np.min(delay)}")



    if len(results[0].estimates) > 0:
        for sensor_id, sensor in enumerate(sensors1):
            print("Sensor", sensor_id)
            print("estimated period ", format_timedelta(np.mean([np.mean([mc_run.estimates[time][sensor_id].period for time
                                                                          in mc_run.estimates]) for mc_run in results])))
            print("estimated period std", format_timedelta(np.mean([np.mean([mc_run.estimates[time][sensor_id].period_stddev for time
                                                                         in mc_run.estimates]) for mc_run in results])))
            print("estimated latency", format_timedelta(np.mean([np.mean([mc_run.estimates[time][sensor_id].latency for time
                                                                           in mc_run.estimates]) for mc_run in results])))
            print("estimated latency std ", format_timedelta(np.mean([np.mean([mc_run.estimates[time][sensor_id].latency_stddev for time
                                                                               in mc_run.estimates]) for mc_run in results])))


    create_plots(results, time_unit='ms')

    plt.show()
