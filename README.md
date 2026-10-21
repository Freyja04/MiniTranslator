# MiniTranslator

MiniTranslator 是一个 Windows 屏幕翻译工具：框选屏幕区域后，在本地识别文字，调用所选翻译服务，并把译文覆盖回原位置。

它使用 **C++20 + 原生 Win32/C++/WinRT** 实现，发布产物是静态链接 MSVC 运行库的独立 x64 EXE，不依赖 Python、Qt、.NET、NuGet 或额外 DLL。仅支持 Windows 10/11 x64。

## 主要功能

- 多显示器、物理像素坐标和 Per-Monitor V2 DPI 支持；
- 框选结束后重新截取干净画面，避免把选区遮罩截进 OCR 图片；
- Windows 系统自带 OCR，识别在本地完成；
- 免密钥传统翻译接口、DeepL，以及可配置多个端点的大模型翻译接口；
- 结果窗口支持移动、缩放、收起、复制、查看原文和重新翻译；
- 分块 OCR 校对，可只重译修改过的文本块；
- 托盘菜单提供文字翻译窗口和快速翻译面板；
- 截图与文字翻译使用隔离的后台请求通道；
- API 密钥使用当前 Windows 账户的 DPAPI 加密存储；
- 基于 GitHub Releases 的更新检查、下载、SHA-256 校验和安装。

## 翻译接口

**传统翻译 API**

| 接口 | 配置 | 说明 |
| --- | --- | --- |
| 腾讯交互翻译（免密） | 无 | 网页接口 |
| Microsoft 移动接口（免密） | 无 | 模拟官方客户端请求 |
| 金山词霸（免密） | 无 | 适合中英文短文本 |
| Yandex 移动接口（免密） | 无 | 客户端接口 |
| DeepL | Key | 支持免费版和正式版 Endpoint |

**大模型翻译 API**

可添加多个接口，每个接口配置名称、Base URL、API Key 和模型。支持三种协议：

| 接口类型 | 请求格式 |
| --- | --- |
| OpenAI Chat Completions | `POST <base_url>/chat/completions` |
| OpenAI Responses | `POST <base_url>/responses` |
| Anthropic Messages | `POST <base_url>/v1/messages` |

免密接口不是服务商承诺稳定的公开 API，可能限流、改版或停止工作。正式或高频用途应优先选择官方 API 或自建大模型端点。

当前接口缺少必需配置时，程序会回退到传统接口列表中第一个免密钥接口；已完整配置的接口始终按用户选择使用。

## 文字识别

截图识别固定使用 Windows 系统自带 OCR。识别在本地完成，不上传截图，也不需要密钥。可用语言取决于系统已安装的 OCR 语言包。

## 配置与隐私

配置文件位于 `%APPDATA%\MiniTranslator\config.json`。

API 密钥使用当前 Windows 账户的 DPAPI 加密，磁盘中的值以 `dpapi:v1:` 开头；换到其他账户或电脑后无法解密。DPAPI 可以降低误传配置文件的泄露风险，但同一 Windows 账户下运行的其他程序仍可能解密。

翻译文字会发送给当前选择的网络翻译服务。处理敏感内容时应关闭“翻译后自动复制”和 Windows 剪贴板历史，并避免把配置文件、日志或带密钥的截图提交到仓库。

## 技术结构

```text
native/                 原生 C++20 / Win32 / C++/WinRT 客户端
scripts/build-native.ps1 原生构建入口
installer/              Inno Setup 安装器
release/                版本、PE 和更新清单校验工具
.github/workflows/      CI 与 Release 流程
```

## 已知限制

- 只支持 Windows 10/11 x64；
- 文字识别只有 Windows 系统 OCR，依赖系统语言包，艺术字、竖排字和手写体的识别率有限；
- 免密翻译接口不是稳定性受保证的开发者 API；
- 复杂照片背景上的原文修补和译文排版仍可能不如纯色界面自然；
- 当前发布包没有 Authenticode 代码签名证书，Windows SmartScreen 警告属于预期行为。

## 许可证

MiniTranslator 自有代码采用 [MIT License](LICENSE)。发布版使用 Windows SDK 提供的系统接口，不捆绑 Qt、Python 或第三方运行时；来源声明见 [native/THIRD_PARTY_NOTICES.txt](native/THIRD_PARTY_NOTICES.txt)。
