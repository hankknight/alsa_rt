# alsa_rt

## 前言

`alsa_rt` 是一个基于 ALSA + FFmpeg(libswresample) 的 Linux 音频硬件抽象库，面向低延迟、可自愈的实时音频采集与播放。它以 ROS 2 (ament_cmake) 包的形式组织，编译为静态库 `libalsa_rt.a`，封装了 ALSA PCM 设备的打开/恢复/重开、单生产单消费(SPSC)无锁环形缓冲、标准/优先级抢占两种播放流，以及基于 FFmpeg 的多模式重采样器，并通过 `SCHED_FIFO` + `mlockall` 提供实时调度保障。

> A low-latency, self-healing ALSA audio library for Linux with lock-free buffering, priority playback, and FFmpeg resampling.

## 特性

- **采集与播放**：统一的 `BaseAlsaDevice` 抽象，子类实现采集流与播放流，各跑独立实时线程。
- **自愈机制**：单次 xrun 走 `snd_pcm_recover`；连续失败达到阈值后指数退避重开设备，期间尊重停止信号。
- **SPSC 无锁环形缓冲**：容量向上取 2 的幂、位与取模、`alignas(64)` 避免伪共享；生产端满时丢旧保新，不阻塞采集回调与上游投递。
- **优先级抢占播放**：高优先级音频可瞬时打断低优先级缓冲数据，消费侧跳到最新写位置丢弃旧帧。
- **多模式重采样**：基于 libswresample，支持单通道挑选、子集映射、全通道透传三种模式；内置 ALSA→FFmpeg 格式归一化（如 `S24_3LE → S32LE`）。
- **可插拔日志**：通过 `LogSink` 回调输出设备状态、xrun、重开等信息，默认空实现。
- **Release / Debug 双实现**：通过继承重写 `loop()` 提供带统计的调试流类，运行期选用，无需宏开关。

## 目录结构

```
alsa_rt/
├── CMakeLists.txt
├── package.xml
├── include/alsa_rt/
│   ├── device/           # BaseAlsaDevice, AlsaConfig, 异常/状态格式化
│   ├── capture/          # CaptureStream (+ Debug)
│   ├── playback/         # PlayBackStream, PlayBackPriorityStream (+ Debug)
│   ├── utils/            # SpscByteRing, LogSink
│   └── resampler/        # BaseResampler, 三模式 resampler, 格式归一化
├── src/                  # 对应实现
└── example/              # quick-start 示例（默认不构建）
    ├── capture_example.cpp
    ├── playback_example.cpp
    ├── playback_priority_example.cpp
    └── CMakeLists.txt
```

## 依赖

- C++17
- ALSA 开发库（`libasound2-dev`）
- FFmpeg（`libswresample`、`libavutil`）
- ament_cmake（ROS 2 构建工具链）
- 线程库（pthread）

## 快速上手

### 构建

```bash
# 仅构建核心库
colcon build --packages-select alsa_rt

# 同时构建 quick-start 示例
colcon build --packages-select alsa_rt \
    --cmake-args -DALSA_RT_BUILD_EXAMPLES=ON
```

### 运行示例

```bash
# 采集：录制 5s 写入 capture.pcm 裸数据（参数: 设备 采样率 通道 秒数）
./capture_example                              # 默认 default 48000 2 5
./capture_example "plughw:0,0"                 # 指定设备
./capture_example "plughw:1,0" 44100 1 10      # 10s, 44.1k 单声道
aplay -t raw -f S16_LE -r 48000 -c 2 capture.pcm  # 播放验证（按实际参数调整 -r/-c）

# 播放：合成正弦波播放（默认 440Hz，可选指定频率）
./playback_example
./playback_example "plughw:0,0" 660

# 优先级抢占：背景 440Hz(prio 0) 播 3s 后被 880Hz beep(prio 5) 抢占 2s
./playback_priority_example
```

> **关于设备字符串**
> - `plughw:C,D`：ALSA 插件层，自动转换格式/采样率/通道，几乎接受任意参数组合，推荐优先使用。
> - `hw:C,D`：直连硬件，要求设备原生支持请求的采样率/通道/格式，否则打开失败。
> - `default`：通常经 plughw/Pulse 路由，兼容性最好。
>
> 若录到的 WAV 播放速度/音调异常，说明设备通过 `set_rate_near` 协商到了不同的采样率——改用 `plughw` 设备或匹配硬件原生采样率即可。

### 最小代码片段

**采集**

```cpp
#include "alsa_rt/capture/capture_stream.hpp"
#include "alsa_rt/device/alsa_config.hpp"

alsa_rt::AlsaConfig cfg("default", 2, 48000, SND_PCM_FORMAT_S16_LE, 10.0);
alsa_rt::CaptureStream cap(cfg);

cap.setLogSink([](alsa_rt::LogLevel, std::string_view msg) {
    fprintf(stderr, "[cap] %.*s\n", (int)msg.size(), msg.data());
});

cap.setRawPcmCallback([](const uint8_t *data, size_t len) {
    // 每个周期收到一帧 interleaved PCM，这里可做编码/转发
});

cap.start();   // 内部起实时线程跑 loop()
// ...
cap.stop();    // join 线程并关闭设备
```

**播放**

```cpp
#include "alsa_rt/playback/playback_stream.hpp"
#include "alsa_rt/device/alsa_config.hpp"

alsa_rt::AlsaConfig cfg("default", 2, 48000, SND_PCM_FORMAT_S16_LE, 10.0);
alsa_rt::PlayBackStream pb(cfg);

pb.setBufferSeconds(1);   // SPSC ring 缓存约 1s 音频
pb.start();

// 在你的生产线程里按周期投递（不足时库会自动补静音）
std::vector<uint8_t> period(period_frames * cfg.getFrameSize());
pb.feed(period);
```

## 核心概念

| 概念 | 说明 |
|------|------|
| `AlsaConfig` | 设备参数（设备名/通道/采样率/格式/period_ms），自动派生 frame_size、byte_rate、period/buffer 大小 |
| `BaseAlsaDevice` | 设备基类，管理打开/恢复/重开与实时调度，子类实现 `stream()`/`loop()` |
| `CaptureStream` | 采集流，`snd_pcm_readi` 一个 period 后通过 `RawPcmCallback` 回调 |
| `PlayBackStream` | 标准播放流，`feed()` 入 ring，消费线程每周期 `pop` + 不足补静音后写设备 |
| `PlayBackPriorityStream` | 优先级播放流，`feed(priority, data)` 高优先级抢占低优先级缓冲 |
| `SpscByteRing` | 无锁字节环形缓冲，`pushDiscard` 满时丢旧保新，`skipTo` 用于优先级抢占跳跃 |
| `BaseResampler` | 封装 SwrContext，支持通道选择矩阵与格式归一化 |

## 实时调度说明

库内部会尝试设置 `SCHED_FIFO` 实时优先级与 `mlockall` 锁定内存。以 root 运行或赋予 `CAP_SYS_NICE` / `CAP_IPC_LOCK` 能力可消除相关告警，但示例与库在无权限时仍可正常工作（仅退化为普通调度）。

## 许可证

Apache-2.0
