# NotifyIconPromoteSvc

This is a Windows service that auto-promotes all newly registered and unconfigured notification icons to be visible by default in the Windows 11 notification area.

This somewhat emulates the behavior of `Always show all icons and notifications on the taskbar` from prior Windows versions, but also allows for individual apps to be toggled if so desired.

## Notes

* Supports shared machines, as the service is installed as a per-user service.
* Only affects unconfigured applications; does not touch notification icons the user have already modified the behaviour of.
* No performance impact to speak of as the code is using registry change notifications to only wake when a change is detected.
* Makes use of [Efficiency Mode](https://devblogs.microsoft.com/performance-diagnostics/reduce-process-interference-with-task-manager-efficiency-mode/) and thread background processing mode to further ensure the app never interferes with any foreground activity.
* Requires an up-to-date copy of Windows 11. **Does not work on Windows 11 v21H1**

## Installation

1. Go to the [Releases](https://github.com/Aemony/NotifyIconPromote/releases/latest) page.
2. Download the installer `NotifyIconPromoteSetup.exe` and run it.
3. Once the installer has finished, restart the system to finalize the install.
   * This is required to start up the per-user service that the installer created.
4. Note the following:
   * The service runs hidden in the background with no visual indicators.
   * It can be controlled through `services.msc` -> `Notification Icon Promoter`.
   * Run the application manually to perform a one-time promotion of any new icons.
     * Note that any existing icons must be controlled through Windows:
         Settings -> Personalization -> Taskbar -> Other system tray icons

## Credits

* Inspired by Liub0myr's [win11-all-icons-on-taskbar](https://github.com/Liub0myr/win11-all-icons-on-taskbar).
* [Microchip icon](https://www.iconfinder.com/icons/6380/chip_memory_microchip_processor_ram_icon) is copyright (c) Alessandro Rei and licensed under [GPL-3.0](https://www.gnu.org/licenses/gpl-3.0.html)