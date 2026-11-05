# Explorer Keep View On Lock (Windhawk mod)

## 问题
Win+L 锁屏后（即使立刻解锁），资源管理器文件列表的滚动位置和选中项丢失。

## 原因（2026-11-05 实测）
- 锁屏瞬间 Windows 连发两次 `WM_SETTINGCHANGE("ImmersiveColorSet")`，即使主题/颜色没变。
- 资源管理器收到后在原窗口内重建 items view（DirectUIHWND 句柄不变），滚动和选中被重置。
- 手动广播同一消息可 100% 复现；已安装的 Windhawk explorer mod 均不处理该消息，与它们无关。
- 无显示器断开、无 DPI/分辨率变化、explorer.exe 未崩溃。

## 修复
子类化 `CabinetWClass`，收到 `ImmersiveColorSet` 时用**该窗口自己的**颜色设置快照比较
（AppsUseLightTheme / SystemUsesLightTheme / EnableTransparency / ColorPrevalence /
DWM ColorPrevalence / AccentColor / ColorizationColor / AccentColorMenu / CurrentTheme /
高对比度及其配色方案 / 系统颜色），没变化就吞掉，有变化则放行。
吞掉后约 400 ms 再检查一次，若设置已变化则补发通知（应对先广播后写注册表的程序）。
只影响资源管理器文件夹窗口，不影响任务栏。

## 安装
Windhawk → 「创建新 Mod」→ 用 `explorer-keep-view-on-lock.wh.cpp` 全部内容替换编辑器内容 →
「编译 Mod」→ 退出编辑器并启用。

## 验证
先滚动并选中文件，然后执行广播脚本或 Win+L 锁屏再解锁，位置应保持。
设置中打开 Verbose log 可在 Windhawk 日志看到 `blocked (no color change)`。
