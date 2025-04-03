# Adaptive Minimal Latency In-Sequence Ordering for Multi-Channel Data

Simple and dependency-free buffer for data from different channels with a timestamp. 
It assumes data from ONE channel is added in-sequence, i.e., with an increasing timestamp.
However, data from different channels could be arbitrarily sorted. 
It can handle an arbitrary number of channels.
This situation is common, e.g., when fusing the data from different sensors with different preprocessing and latencies.

The buffer solves the problem of ordering the data from all channels according to their timestamp, i.e., create a absolute order of the data.
This means data from channels with lower latency are held back until data with bigger latency are available.
The buffer solves the problem while introducing the smallest possible additional delay given some bounds on the tolerable data loss.
For more details, take a look at our paper.

If you use the buffer, please cite 
```
@misc{minimal_latency_buffer,
    title={Adaptive Minimal Latency In-Sequence Ordering for Multi-Channel Data Fusion in Autonomous Driving},
    author={Thomas Wodtko, Alexander Scheible, Dominik Authaler, and Michael Buchholz},
    howpublished = {\url{https://github.com/uulm-mrm/minimal_latency_in_sequence_buffer}},
    year={2025}
}
```


