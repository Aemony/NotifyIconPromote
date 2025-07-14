# Notification Icon Promoter

This is a Windows service/standalone tool that auto-promotes all newly registered and unconfigured notification icons to be visible by default in the Windows 11 notification area. This somewhat emulates the behavior of `Always show all icons and notifications on the taskbar` from prior Windows versions, but also allows for individual apps to be toggled if so desired (through the Settings app in Windows).

This is the app for you if you:

* Prefer to have new notification icons automatically be visible by default.
* Have disabled the `Hidden icon menu` but want to ensure that only select icons are hidden -- not new notification icons that may appear.
* Use an app that regularly sees updates that are registered as a new notification icon, meaning it is constantly moved back to the `Hidden icon menu` after each update (e.g. Discord and other apps like that).

Note that the app requires a somewhat newer version of Windows 11, and *does not work on Windows 11 v21H1.*

## Two solutions

The tool is provided in two forms: as a [per-user service](https://learn.microsoft.com/en-us/windows/application-management/per-user-services-in-windows) and as a standalone executable.

* The service solution supports shared machines as the tool is installed as a per-user service that starts alongside Windows for all current and future users and is the recommended solution. This however requires administrative privileges to install. Users can control the behavior through the `Notification Icon Promoter` app in their start menu.

* The standalone executable is the original legacy solution that consists of an extremely tiny executable with no visual or user-facing parts. This is currently *not* provided through an installer, and the user have to manually configure it to auto-start alongside Windows and setting it up for any new users on the system.

## Notes

* The tool only affects unconfigured apps; it does not touch any notification icons the user have already modified the behaviour of through the Settings app in Windows.
* It also have no performance impact to speak of as it uses registry change notifications to only run when Windows detects that a completely new notification icon have appeared, or the user toggles the visibility of an existing icon.
* Finally it makes use of [Efficiency Mode](https://devblogs.microsoft.com/performance-diagnostics/reduce-process-interference-with-task-manager-efficiency-mode/) and thread background processing mode to further ensure the app never interferes with any foreground activity.

## Installation

1. Go to the [Releases](https://github.com/Aemony/NotifyIconPromote/releases/latest) page.
2. Download the installer `NotifyIconPromote_Setup.exe` and run it to install the service based solution.
3. Once the installer has finished, restart the system to finalize the install.
   * This is required to start up the per-user service that the installer created.
4. Note the following:
   * The service runs hidden in the background with no visual indicators.
   * Run the `Notification Icon Promoter` app to enable/disable the automatic promotion of new icons.
     * Note that any existing icons must be controlled through Windows:
         Settings -> Personalization -> Taskbar -> Other system tray icons

## Credits

* Inspired by Liub0myr's [win11-all-icons-on-taskbar](https://github.com/Liub0myr/win11-all-icons-on-taskbar).
* [Microchip icon](https://www.iconfinder.com/icons/6380/chip_memory_microchip_processor_ram_icon) is copyright (c) Alessandro Rei and licensed under [GPL-3.0](https://www.gnu.org/licenses/gpl-3.0.html)
