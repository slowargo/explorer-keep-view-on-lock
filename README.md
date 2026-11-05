# Explorer Keep View On Lock

A [Windhawk](https://windhawk.net/) mod that stops File Explorer from losing its scroll position and selection when you lock and unlock Windows (Win+L) or wake it from sleep.

[中文说明](#中文说明)

## The problem

You scroll down a big folder in File Explorer, select a file, then lock (Win+L) or put the PC to sleep. After unlocking or waking, the list has jumped to the top and the selection may be gone. With locking, it happens even if you unlock right away.

## How it works

When the session locks (including when going to sleep), Windows broadcasts `WM_SETTINGCHANGE("ImmersiveColorSet")` to all windows, even though no color or theme setting changed. Explorer folder windows react by rebuilding their item view, and that resets scroll position and selection.

The mod subclasses Explorer folder windows (`CabinetWClass`). For each window it keeps a snapshot of the color settings: light/dark mode, accent color, transparency, accent on title bars, current theme, high contrast scheme, and system colors.

- If a notification arrives and the snapshot is unchanged, the mod drops it.
- If something actually changed, the notification is passed through, so real theme and accent switches still apply.
- When a notification is dropped, the settings are checked again about 400 ms later. If they changed in the meantime, the notification is delivered then. This covers programs that broadcast before they write the new settings.

Only Explorer folder windows are affected. The taskbar, Start menu and other apps still receive the notification as usual.

## Install

1. Install [Windhawk](https://windhawk.net/).
2. In Windhawk, click **Create a new mod**.
3. Replace the editor contents with [`explorer-keep-view-on-lock.wh.cpp`](explorer-keep-view-on-lock.wh.cpp).
4. Click **Compile Mod**, then exit the editor. The mod is enabled immediately; no Explorer restart is needed.

To uninstall, disable or remove the mod in Windhawk.

## Settings

| Setting | Default | Description |
|---|---|---|
| Verbose log | off | Writes a line to the Windhawk log for every `ImmersiveColorSet` notification, saying whether it was passed or blocked. |

## Compatibility

Tested on Windows 11 25H2 (build 26200) with Windhawk 1.7.3. It works alongside other Explorer mods, such as tab or double-click mods.

## License

No license has been chosen yet.

---

## 中文说明

一个 [Windhawk](https://windhawk.net/) mod：Win+L 锁屏再解锁，或者睡眠唤醒后，资源管理器的滚动位置和选中的文件不再丢失。

**原因**：锁屏时（进入睡眠时也会锁屏）Windows 会向所有窗口广播 `WM_SETTINGCHANGE("ImmersiveColorSet")`，即使主题和颜色设置都没有变化。资源管理器收到后会重建文件列表，滚动位置和选中项随之被重置。

**做法**：mod 只作用于资源管理器的文件夹窗口，每个窗口各自保存一份颜色设置的快照。

- 收到通知时设置没变化，就拦下这条通知。
- 设置真的变了，就照常放行，切换深浅色和强调色不受影响。
- 拦下通知约 400 ms 后会再检查一次设置。如果这期间设置变了，就把通知补发给窗口。

**安装**：Windhawk → 创建新 Mod → 用 `explorer-keep-view-on-lock.wh.cpp` 的内容替换编辑器内容 → 编译 Mod → 退出编辑器。
