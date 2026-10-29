# Thock maimai

键盘音效与全屏烟花。macOS 基于 [Thock 1.23.0](https://github.com/kamillobinski/thock/tree/1.23.0)，Windows 使用 Win32、Direct3D 11 和 XAudio2。

## 下载与使用

在 [Releases](https://github.com/wzk112/thock-maimai/releases) 下载 `Thock-maimai-Windows-x64.exe`，双击即可。**只需要这一个 EXE**：原烟花贴图、六种声音、动画参数与着色器都编入程序，直接从内存读取，不需要旁边的素材文件夹，不自解压，也不需要另装 Thock 或 .NET。

Windows 10 / 11，64 位。首次打开设置，关闭窗口后留在托盘，右键托盘可退出。应用未签名，Windows 可能提示来源未知。

| 模式 | 按键 |
| --- | --- |
| 混合 | 普通键 Tap；空格 Touch；回车绝赞；退格、Esc、Delete 正解音；Tab 烟花 |
| 正解音 / Tap / Touch / 绝赞 / 烟花 Touch | 所有普通键使用所选声音 |

默认音量 35%，烟花开启，**HDR 关闭**。音效、烟花、音量和显示器范围均可调整。长按不重复触发，纯修饰键不触发；设置窗口内的键盘操作不会播放打字音效。音量为 0 或关闭键盘音效会停止按键声音和烟花；仅关闭烟花则继续播放声音。音频设备不可用时仍可显示烟花。

默认只在鼠标所在屏幕显示烟花。烟花结束后停止绘制并释放叠加窗口，无常驻预热图层。HDR 暂不作为本版实机验收项，保持默认关闭；普通显示器使用 SDR。不改变系统亮度。

按键只在内存中用于触发，不记录输入内容、不上传。运行不需要网络。偏好设置保存于 `%LOCALAPPDATA%/ThockMaimai/settings.json`。

## 自定义素材

已经内置完整素材，不必导入。需要替换时，点击“选择素材文件夹”；“恢复内置素材”会切回 EXE 内的数据。旧自定义文件夹失效时，程序自动恢复内置素材。

仓库的 `assets/` 是 Windows 构建输入；`Thock/MaimaiResources/` 是 macOS 构建输入。转换既有 macOS 资源目录：

```sh
python3 tools/prepare_local_assets.py '/path/to/MaimaiResources' private-assets/windows
```

## Windows 构建与检查

需要 Visual Studio 2022 的 C++ 桌面工具、Windows SDK、CMake 和 Python 3。

```powershell
cmake -S windows -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
& 'build/Release/Thock maimai.exe' --self-test
& 'build/Release/Thock maimai.exe' --integration-test
```

构建时由 `tools/embed_resources.py` 生成 RCDATA 资源表。也可以用 `-DTHOCK_ASSET_DIR=...` 指定其他素材。无需打包器、额外 DLL 或运行时下载。

`--self-test` 在 Windows 上验证原素材解码、HLSL 编译、透明画面、HDR 数值和叠加窗口，输出 `self-test.json`、`preview-sdr.png`。`--integration-test` 在应用创建的测试窗口中使用 Windows 输入 API 验证真实消息路径、模式切换、长按去重、开关、静音、窗口清理和恢复，输出 `integration-test.json`。测试配置与结果保存在测试 EXE 旁，不更改日常偏好设置。

CI 会把 EXE 单独复制到带空格和中文的空目录，再运行上述检查。CI 无可用音频输出设备时，会明确记录 `audioDeviceAvailable: false`，不会将解码或调用请求宣称为扬声器播放验证。测试不代替真人听音和实体 HDR 亮度测量。独占全屏游戏、安全桌面等环境可能不显示普通桌面叠加窗口。

## macOS

macOS 13.5+，Xcode 26。首次运行需在系统设置的辅助功能中启用全局按键权限。HDR 默认关闭，普通烟花和音效可独立运行。

```sh
xcodebuild -project Thock.xcodeproj -scheme Thock -configuration Release \
  -derivedDataPath build CODE_SIGNING_ALLOWED=NO ARCHS=arm64 ONLY_ACTIVE_ARCH=YES build
```

## 代码与素材许可

程序、烟花模拟、动画曲线处理、透明窗口、HDR 后处理、输入与音频逻辑、转换和打包工具均公开。Thock 原作者为 **Kamil Łobiński**，原 MIT 许可保留；新增应用代码同样采用 MIT。Windows JSON 解析使用 [nlohmann/json 3.12.0](https://github.com/nlohmann/json)，许可位于 `windows/vendor/json.LICENSE`。

游戏素材由项目维护者确认已取得公开再分发授权后纳入仓库和 Release。**游戏图片、音频、参数及由原游戏转换的着色器不重新授予 MIT 许可**，仍保留各自原权利归属，详见素材目录的 `ASSET-LICENSE.txt`。本项目与 SEGA 无关联，不代表官方认可。HDR 为本程序新增高光增强，并非原游戏素材自带 HDR。

技术参考：[Apple EDR](https://developer.apple.com/documentation/metal/performing-your-own-tone-mapping)、[DirectComposition](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/nf-dxgi1_2-idxgifactory2-createswapchainforcomposition)、[Windows Raw Input](https://learn.microsoft.com/en-us/windows/win32/inputdev/using-raw-input)。
