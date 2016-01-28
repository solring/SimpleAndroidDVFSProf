BUILD
=====

1. change `ANDROID_NDK` in `build-profiler.sh` to your android ndk directory

2. change `C_NUM` in `build-profiler.sh` to match the core number of your platform

3. run `build-profiler.sh`

RUN
===

1. push the binary to `/data/local/tmp` of your phone

2. change permission
```
$ chmod 755 /data/local/tmp/profile_sys
```

3. run
```
$ ./profile_sys [sampling sec] [sampling usec] [output file] [duration]

```
The output will be a .csv file with:
- CPU frequency
- Working cycles of each core
- Normalized utilization of each core
- Number of process running
- Number of context switches


NOTE
====

Since sysfs entries for CPU DVFS control vary among different devices, you may need to change the sysfs path/file names of the following entries in the source code :

- scaling_max_freq
- scaling_cur_freq



