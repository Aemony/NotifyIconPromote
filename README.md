# NotifyIconPromote

This is a background application that auto-promotes all newly registered and unconfigured notification icons to be visible by default in the Windows 11 notification area.

This somewhat emulates the behavior of `Always show all icons and notifications on the taskbar` from prior Windows versions, but also allows for individual apps to be toggled if so desired.

## Notes

* Only affects unconfigured applications; does not touch notification icons the user have already modified the behaviour of.
* No performance impact to speak of as the code is using registry change notifications to only wake when a change is detected.
* Makes use of [Efficiency Mode](https://devblogs.microsoft.com/performance-diagnostics/reduce-process-interference-with-task-manager-efficiency-mode/) and thread background processing mode to further ensure the app never interferes with any foreground activity.

## Installation

1. Go to the [Releases](https://github.com/Aemony/NotifyIconPromote/releases/latest) page.
2. Download `NotifyIconPromote.exe` and put it a folder of your choice.
   * The executable is standalone and does not create any additional files.
3. Launch the executable for it to do its thing.
   * The executable runs hidden in the background with no visual indicators.
4. Note the following:
   * To close the background process, use Task Manager to end the `Notification Icon Promoter` task (the `NotifyIconPromote.exe` process).
   * If you want it to start it automatically with Windows, the easiest way is to open `shell:Startup` (type it in the address field of the File Explorer) and drop a shortcut to the executable in there.

## Credits

* Inspired by Liub0myr's [win11-all-icons-on-taskbar](https://github.com/Liub0myr/win11-all-icons-on-taskbar).
