# NotifyIconPromote

This is a background application that auto-promotes all newly registered and unconfigured notification icons to be visible by default in the Windows 11 notification area.

## Notes

* Only affects unconfigured applications; does not touch notification icons the user have already modified the behaviour of.
* No performance impact to speak of as the code is using registry change notifications to only wake when a change is detected.
* Automatically engages [Efficiency Mode](https://devblogs.microsoft.com/performance-diagnostics/reduce-process-interference-with-task-manager-efficiency-mode/) for itself.

## Credits

* Inspired by Liub0myr's [win11-all-icons-on-taskbar](https://github.com/Liub0myr/win11-all-icons-on-taskbar).