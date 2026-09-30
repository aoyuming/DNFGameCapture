# 击杀语音：默认静音与固定台词归档

## 音色

现有音色顺序保留，避免改变 config.ini 中 fxVoice 的下标含义：

|下标|选项|音色 ID|
|---|---|---|
|0|默认（按风格）|水墨国风用古风雅韵 2.0，其他风格用魅惑女声|
|1|魅惑女声|zh_female_jiaochuannv_uranus_bigtts|
|2|古风雅韵 2.0|zh_female_gufengshaoyu_uranus_bigtts|
|3|古风雅韵 1.0|zh_female_gufengshaoyu_mars_bigtts|
|4|武则天 1.0|zh_female_wuzetian_mars_bigtts|
|5|Windows 系统语音（离线）|本机 SAPI|

新配置与恢复默认均为 fxVoiceOn=0、fxVoice=0。之前用户明确保存的勾选状态仍保留，不会每次启动强制重置。
只有开启特效并勾选「语音播报」才会播放，试听也受同一开关控制。取消勾选后，设置同步到本机 C++ 时停止当前播报、作废已排队的原生播放；展示页同步设置时撤销延迟语音。OBS 浏览器源不重复播报。

## 不再反复付费

- `web前端/voice/` 已有四套各 21 句 WAV，共 84 句；这是长期音频资产，不是启动时清空的临时缓存。
- 客户端优先播放这些文件，缺失时仅使用免费的 Windows 本地语音及 `%APPDATA%/DNFGameCapture/voice-cache` 缓存。客户端不调用豆包 API，也不需要 API Key。
- 不勾选时，HTTP 播放接口直接返回 muted；试听及原生后台线程同样检查开关。
- 安装包只带 WAV（不带生成脚本、密钥、临时文件、锁和元数据）；更新 ZIP 同样包含语音，打包前检查发布目录 WAV 与源码音频 SHA-256 一致。发布目录缺文件时应复制现有文件，不要重新合成。
- 无需增加在线语音生成服务器。将带 WAV 的安装包或更新包沿现有发布渠道分发，所有客户端共享同一套已生成音频。本次未上传服务器或发布更新。

## 开发机生成脚本

`generate-kill-voices.ps1` 的默认行为仅检查／复用，不发请求、不覆写文件，且与当前工作目录无关。

```powershell
# 只读检查全部现有音频；缺失、损坏或元数据不一致时报错，完全离线
.\DNFGameCapture\scripts\generate-kill-voices.ps1 -CheckOnly

# 以下操作会收费，需要开发者主动执行；只生成缺失／损坏文件
.\DNFGameCapture\scripts\generate-kill-voices.ps1 -Generate -Voice doubao-gufeng2 -Only ink-double

# 仅在确定要重新生成时增加 -Force；有效旧 WAV 会先留 .bak
# -Force 必须搭配 -Generate，不会隐式触发收费
```

- 已有合法的旧 WAV 即使没有元数据也直接保留；只能确认 WAV 格式和非静音，不能从文件本身证明历史音色／文本，脚本会明确标记 legacy-valid。
- 新生成音频保存「音色 ID + 模型资源 + 台词 + 语言 + 音频参数」指纹和 WAV SHA-256。元数据改变时要求显式 Force，不偷偷付费更新。
- 生成时持有目录互斥锁，防止两个脚本同时为同一缺失文件付费；只对完整、非空、非静音的 PCM 写临时 WAV，验证后再原子替换。
- 网络错误、API 错误或截断回复不覆盖旧音频，也不会自动重试。
- API Key 仅在确实要生成时从 `DOUBAO_TTS_API_KEY` 进程／用户环境变量读取，不写入仓库或音频元数据。

## 离线回归

```powershell
node .\DNFGameCapture\scripts\kill-voice-feature-test.js
.\DNFGameCapture\scripts\kill-voice-cache-test.ps1
```

缓存测试在执行前用本地 PCM 假数据替换整个 HTTP 函数，并检查网络代码已移除；不访问豆包 API、不发出声音、不启动主程序。覆盖命中复用、旧音频、不合法文件、显式 Force、失败保留、元数据改变和并发锁。

人工验收：使用新版 EXE 与配套前端，首次启动不勾选无声音，试听禁用；勾选后选古风默认／四个指定音色试听；在有播报和延迟事件时取消勾选应停止声音；重开程序保留自己的勾选选择。编译和离线测试不等于实际音色听感验收。
