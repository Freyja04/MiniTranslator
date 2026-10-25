# MiniTranslator

原生 Win32/GDI+D2D 的截图/文本翻译托盘应用（C++20, MSVC, WinRT）。
不要引入 GTK/Qt/Electron；UI 全部是手写 owner-draw 的原生控件。

## 构建与验证（先读这条，别自己摸索）
- cmake **不在 PATH**，用："D:/software/Visual Studio/BuildTools/.../cmake.exe"
  或跑 scripts/build-native.ps1
- 增量构建：cmake --build build/native --config Debug --target MiniTranslator
- 一轮对话中的所有问题改完再跑：MiniTranslator.exe --self-test（退出码 0 才算过；
  它用 %TEMP%\MiniTranslator-self-test 隔离配置，不碰真实 config.json）
- Release 重链前：先 taskkill 正在运行的 MiniTranslator.exe（托盘进程会锁住 exe）

## 架构速览
- native/src/app.cpp: AppHost 托盘/消息窗口/热键/pipeline 调度（主 UI 线程）
- settings_window*.{cpp} 按职责分片：state(数据读写)/layout(摆放)/paint(绘制)/
  pages(创建控件)/self_test
- pipeline.cpp: 双 worker 线程，lane 分 visual/text，latest-wins 取消
- translator.cpp: 各家接口；ai 走 openai_chat/openai_responses/anthropic_messages

## 设计不变量（违反会出 bug，不是风格问题）
- translator.ai_profiles 同时至多一个 enabled；normalize_enabled 在读入口兜底
  手改的配置文件
- 所有几何只写一份"逻辑像素"，经 px(value,dpi) 缩放；绘制、命中、布局共用
  settings_ai_list 的数字，绝不允许第二套常量
- 设置页 1 的卡片几何 = layout pass 算出的 translation_ 结构体，paint 只读不回算

## 配置
- 真实配置 %APPDATA%\MiniTranslator\config.json；API key 以 dpapi:v1: 前缀密文存储，
  仅本机本用户可解密；别把它明文写进任何文件/日志
