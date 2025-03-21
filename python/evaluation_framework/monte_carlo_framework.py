import multiprocessing
import queue
import math
import random
import time

from dataclasses import dataclass, field
from minimal_latency_buffer.evaluation_framework.generators import MeasGenerator, MeasGenParams, PopGenerator
from minimal_latency_buffer import MLParams, MinimalLatencyBuffer, FLParams, FixedLagBuffer, PopReturn
from typing import List, Dict, Any
from tqdm import tqdm
from datetime import datetime, timedelta, UTC

from multiprocessing import Process, Queue

# MAX_NUM_PROCESSES = multiprocessing.cpu_count() - 1
MAX_NUM_PROCESSES = 8

@dataclass
class Estimates:
    latency: float
    latency_stddev: float
    latency_jitter: float
    period: float
    period_stddev: float
    period_jitter: float

@dataclass
class Input:
    id: Any
    meas_time: datetime
    receipt_time: datetime

@dataclass
class RunData:
    inputs: Dict = field(default_factory=lambda: {})    # key is receipt_time
    outputs: Dict = field(default_factory=lambda: {})   # key is requested time within pop() call
    estimates: Dict = field(default_factory=lambda: {}) # key is requested time within pop() call, maps to dict with
                                                        # sensor id as key

def format_timedelta(delta: timedelta) -> str:
    return str(delta / timedelta(milliseconds=1)) + "ms"


def print_data_source(params: MeasGenParams, offset: int=0):
    offset_str = ' ' * offset

    print(f"{offset_str} period: ", format_timedelta(params.period))
    print(f"{offset_str} period_stddev: ", format_timedelta(params.period_stddev))
    print(f"{offset_str} latency: ", format_timedelta(params.latency))
    print(f"{offset_str} latency_stddev: ", format_timedelta(params.latency_stddev))
    print(f"{offset_str} drop_rate: ", params.drop_rate)


def print_params(pop_period: timedelta,
                 meas_gen_params: List[MeasGenParams],
                 buffer_params: MLParams | FLParams,
                 num_iterations_per_run: int,
                 num_runs: int):
    print("### Parameters ###")
    print("pop period: ", format_timedelta(pop_period))

    print("buffer params: ", buffer_params)
    # if isinstance(buffer_params, MLParams):
    #     print("Adaptive buffer parameters: ", buffer_params)
    # else:
    #     print(f"Fixed Lag Buffer with lag: "
    #           f"{format_timedelta(buffer_params.delay_mean)} (mean), "
    #           f"{format_timedelta(buffer_params.delay_stddev)} (stddev), "
    #           f"{buffer_params.delay_quantile} (quantile)")
    if meas_gen_params is not None and len(meas_gen_params) > 0:
        print("Num data sources: ", len(meas_gen_params))
        for i, source in enumerate(meas_gen_params):
            print(f" - Source {i}: ")
            print_data_source(source, 2)
            print("")

    if num_iterations_per_run is not None:
        print("Number of iterations per run: ", num_iterations_per_run)

    if num_runs is not None:
        print("Number of Monte-Carlo runs: ", num_runs)


def run_evaluation(pop_period: timedelta,
                   meas_gen_params: List[MeasGenParams],
                   buffer_params: MLParams|FLParams,
                   num_iterations_per_run: int,
                   skip_verification: bool,
                   store_estimations: bool,
                   num_warm_up: int,
                   modifies_meas_gen_params: List[MeasGenParams],
                   change_step: int = None):

    current_time = datetime.fromtimestamp(0, tz=None)
    pop_gen = PopGenerator(period=pop_period)
    meas_gens = []
    for params in meas_gen_params:
        generator = MeasGenerator(params, #datetime.fromtimestamp((random.uniform(0, 1) * params.period).total_seconds(),
                                          #                      tz=None),
                                  init_time=current_time)
        meas_gens.append(generator)

    # instantiate buffer within loop to ensure its properly resetted
    if isinstance(buffer_params, MLParams):
        buffer = MinimalLatencyBuffer(buffer_params)
    else:
        buffer = FixedLagBuffer(buffer_params)

    run_data = RunData()
    latest_receipt = None
    # for step in tqdm(range(num_iterations_per_run), unit="iterations", leave=False):
    for step in range(num_iterations_per_run):
        if change_step is not None and step == change_step:
            meas_gens = []
            for params in modified_meas_gen_params:
                generator = MeasGenerator(params, init_time=current_time)
                meas_gens.append(generator)
        # steps always happen with the period of the pop() call
        current_time = next(pop_gen)
        # print("Stepping to next time: ", current_time)

        # check which new measurements have been received within the latest step
        unsorted_measurements = []
        for i, gen in enumerate(meas_gens):
            meas_time, receipt_time = gen.get_next(current_time)

            # received meas
            if meas_time and receipt_time:
                unsorted_measurements.append(Input(id=i,
                                                   receipt_time=receipt_time,
                                                   meas_time=meas_time))

        # ensure that the buffer receives the inputs in order of their associated reception time
        sorted_measurements = sorted(unsorted_measurements, key=lambda d: d.receipt_time)

        for input in sorted_measurements:
            # passing the 'whole' input into the buffer allows a deeper evaluation once the element is popped again
            if latest_receipt and input.receipt_time < latest_receipt:
                continue
            latest_receipt = input.receipt_time
            res = buffer.push(input.id, input.receipt_time, input.meas_time, input)
            if step > num_warm_up:
                run_data.inputs[input.receipt_time] = input
            # assert res == PushReturn.Ok, f"Unable to push input to buffer {current_time}, {input}, {step}"

        # extract estimates
        if store_estimations:
            estimates = {}
            if isinstance(buffer_params, MLParams):
                for id in range(len(meas_gens)):
                    estimates[id] = Estimates(
                        period=buffer.estimated_period(id),
                        period_stddev=buffer.estimated_period_stddev(id),
                        period_jitter=buffer.estimated_period_jitter(id, buffer_params.jitter_quantile),
                        latency=buffer.estimated_latency(id),
                        latency_stddev=buffer.estimated_latency_stddev(id),
                        latency_jitter=buffer.estimated_latency_jitter(id, buffer_params.jitter_quantile)
                    )
                    if step > num_warm_up:
                        run_data.estimates[current_time] = estimates
        res = buffer.pop(current_time)
        if len(res.data) > 0 or len(res.discarded_data) > 0:
            if step > num_warm_up:
                # pickleable_output = {
                #     'buffer_time': res.buffer_time,
                #     'data': [element for element in res.data],
                #     'discarded_data': [element for element in res.discarded_data]
                # }
                # run_data.outputs[current_time] = pickleable_output
                run_data.outputs[current_time] = res

    if not skip_verification:
        # verify that all outputs happen chronologically ordered (according to the meas time)
        sample_time = None
        for elem in run_data.outputs.values():
            for return_elem in elem.data:
                if sample_time is None:
                    sample_time = return_elem.meas_time
                assert sample_time <= return_elem.meas_time, "Found output with wrong chronological order"

        # verify that for all popped elements the meas and receipt time are still the same
        for elem in run_data.outputs.values():
            for return_elem in elem.data:
                assert return_elem.meas_time - return_elem.data.meas_time < timedelta(microseconds=1)
                assert return_elem.receipt_time - return_elem.data.receipt_time < timedelta(microseconds=1)

    return run_data


def run_eval_iterations(blobs: Queue, num_iters: int, args: list):
    for _ in range(num_iters):
        result = run_evaluation(*args)
        blobs.put(result)


def evaluate_MC(pop_period: timedelta,
                meas_gen_params: List[MeasGenParams],
                buffer_params: [MLParams|FLParams],
                num_iterations_per_run: int,
                num_runs: int,
                skip_verification=False,
                store_estimations=False,
                num_warm_up: int=1000,
                modified_meas_gen_params: List[MeasGenParams] = None,
                change_step: int = None):
    print_params(pop_period, meas_gen_params, buffer_params, num_iterations_per_run, num_runs)

    assert (modified_meas_gen_params is None) == (modified_meas_gen_params is None), "Only one of two params for changing scenario set"

    if modified_meas_gen_params is not None and change_step is not None:
        print(f"Modified generator params (active after {change_step} steps): ")
        print_params(pop_period, modified_meas_gen_params, buffer_params, num_iterations_per_run, num_runs)

    eval_args = [pop_period, meas_gen_params, buffer_params, num_iterations_per_run, skip_verification,
                 store_estimations, num_warm_up, modified_meas_gen_params, change_step]

    # results = []
    # for _ in tqdm(range(num_runs)):
    #     results.append(run_evaluation(*eval_args))

    iter_per_process = math.ceil(num_runs / MAX_NUM_PROCESSES)

    blobs = Queue()
    processes = []
    num_iterations_started = 0
    for _ in range(MAX_NUM_PROCESSES):
        iters = min(iter_per_process, num_runs - num_iterations_started)
        if iters == 0:
            break
        p = Process(target=run_eval_iterations,
                    args=(
                        blobs,
                        iters,
                        eval_args),
                    daemon=True)
        processes.append(p)
        num_iterations_started += iters

    print(f"starting {len(processes)} processes")
    for process in processes:
        process.start()

    results = []
    num_jobs_done = 0
    with tqdm(total=num_runs, unit="runs_finished", ) as progress:
        while not (num_jobs_done == num_runs):
            while not blobs.empty():
                try:
                    data = blobs.get_nowait()
                    num_jobs_done += 1
                    # pickleable_outputs = data.outputs
                    # data.outputs = {key: PopReturn(val['buffer_time'], val['data'], val['discarded_data']) for key, val in pickleable_outputs.items()}
                    results.append(data)
                    progress.update()
                except queue.Empty as e:
                    pass

            time.sleep(0.1)

    for p in processes:
        if p.is_alive():
            p.join()

    return results
