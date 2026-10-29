# Thock maimai

键盘音效与全屏烟花。macOS 基于 [Thock 1.23.0](https://github.com/kamillobinski/thock/tree/1.23.0)，Windows 使用独立的 Win32 / Direct3D 11 / XAudio2 实现。

只需运行一个应用。HDR **默认关闭**，需要时手动开启。没有常驻预热图层，烟花结束后停止绘制；Windows 会释放叠加窗口。按键只在内存中用于触发，不记录输入内容、不上传、不需要网络。

## Windows

下载 Releases 中的 Windows x64 ZIP，**完整解压**，打开 `Thock maimai.exe`。Windows 10 / 11，64 位；不需要另装 Thock 或 .NET。应用未签名，系统可能提示来源未知。

公开安装包包含自制演示声音和图案，不包含 maimai 游戏素材。在设置里选择你有权使用的素材文件夹，可以替换演示素材。第一次运行打开设置，关闭窗口后留在托盘，右键托盘退出。

| 模式 | 按键 |
| --- | --- |
| 混合 | 普通键 Tap；空格 Touch；回车绝赞；退格、Esc、Delete 正解音；Tab 烟花 |
| 正解音 / Tap / Touch / 绝赞 / 烟花 Touch | 所有普通键使用所选声音 |

音量、音效开关、烟花开关、HDR 与显示器范围均可调整。长按不重复触发，纯修饰键不触发。设置窗口内的键盘操作不会播放打字音效。默认仅在鼠标所在屏幕显示烟花。HDR 需要 Windows 开启 HDR 且显示器支持，其他情况回退 SDR；不改变系统亮度。

源码构建（Visual Studio 2022 C++ 桌面工具与 Windows SDK、CMake、Python 3）：

```powershell
cmake -S windows -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
python tools/generate_demo.py build/Release/assets
& 'build/Release/Thock maimai.exe' --self-test
```

`--self-test` 使用 WARP 检查渲染像素、HDR 数值、透明度、叠加窗口和 WAV 解码，输出 `self-test.json`、`preview-sdr.png`。这些检查不等于真人听音、实体 HDR 亮度测量或所有游戏全屏模式验证。独占全屏游戏、安全桌面等环境可能不显示普通桌面叠加窗口。

## macOS

macOS 13.5+，Xcode 26。首次运行需在系统设置的辅助功能中启用全局按键权限。HDR 高光使用透明 Metal 浮点图层，普通烟花使用相同粒子动画，不需要预热。

```sh
python3 tools/generate_demo.py Thock/MaimaiResources
xcodebuild -project Thock.xcodeproj -scheme Thock -configuration Release \
  -derivedDataPath build CODE_SIGNING_ALLOWED=NO ARCHS=arm64 ONLY_ACTIVE_ARCH=YES build
```

自制演示资源仅用于效果预览；完整 maimai 音效包与原效果需要自行提供合法的 `MaimaiResources`，在构建前复制进 `Thock/MaimaiResources/`。现有 macOS 音效包也可通过 Thock 的自定义音效包界面导入。

## 原素材的本地导入

如果已有包含 `maimai-fireworks.json`、PNG、`maimai-rainbow.fsh` 和 `maimai-packs.zip` 的本地 Mac 资源目录：

```sh
python3 tools/prepare_local_assets.py '/path/to/MaimaiResources' private-assets/windows
```

把输出文件夹放到 Windows 应用旁的 `assets/`，或在设置中选择它。转换工具只读取本地文件，不提供下载渠道。资源文件夹和 ZIP 已列入 `.gitignore`。

## 开源范围与许可

程序、烟花粒子模拟与动画曲线处理、透明窗口、HDR 后处理、输入与音频逻辑、素材转换工具均公开。Thock 原作者为 **Kamil Łobiński**；保留其 MIT 许可证，新增代码和演示素材同样使用 MIT。Windows JSON 解析使用 [nlohmann/json 3.12.0](https://github.com/nlohmann/json)，其 MIT 许可位于 `windows/vendor/json.LICENSE`。

从 maimai 提取的图片、音频、粒子参数及游戏着色器不在此仓库中，也不受本项目 MIT 许可覆盖。HDR 是本程序新增的高光增强，不表示原游戏素材本身含 HDR。maimai 商标与游戏资源属于各自权利人；本项目与 SEGA 无关联。

渲染依据：[Apple EDR](https://developer.apple.com/documentation/metal/performing-your-own-tone-mapping)、[DirectComposition](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/nf-dxgi1_2-idxgifactory2-createswapchainforcomposition)、[Windows Raw Input](https://learn.microsoft.com/en-us/windows/win32/inputdev/using-raw-input)。
