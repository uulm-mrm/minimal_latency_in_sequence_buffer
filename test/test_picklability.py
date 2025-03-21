
import pickle
from minimal_latency_buffer import MLParams, FLParams, PopReturn, Mode, BatchParams, MatchParams

from minimal_latency_buffer import FLParams,MLParams
from minimal_latency_buffer import Mode, BatchParams, MatchParams, PushReturn, TimeData, TimeDataList, PopReturn

filename = 'test.pickle'

ml_params = MLParams()
fl_params = FLParams()
batch_params = BatchParams()
match_params = MatchParams()
pop_return = PopReturn()
push_return = PushReturn.Ok
time_data = TimeData()
time_data_list = TimeDataList()
mode = Mode.Single

types = [
    ml_params,
    fl_params,
    batch_params,
    match_params,
    pop_return,
    push_return,
    time_data,
    time_data_list,
    mode
]

for element in types:

    print('testing: ', element)
    with open(filename, 'wb') as f:
        pickle.dump(element, f)

    with open(filename, 'rb') as f:
        pickle.load(f)
    print('success')
