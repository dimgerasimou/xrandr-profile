# xrandr-profile

A lightweight, feature-rich **XRandR** profile manager that saves and restores monitor layouts
based on monitor identity (EDID), not only output names.

Useful when output names change between docking stations, GPUs, power
profiles, or kernel/XRandR reconfiguration.

**xrandr-profile** can apply the matching profile automatically when the monitor configuration changes. It also supports global and per-profile hooks, plus fallback layouts for unknown
monitor sets.

## Build / install

Edit `Makefile` to match your intended configuration. Then run:
```bash
make
sudo make install
```

### Dependencies

- C compiler (gcc/clang)
- make
- X11 development libraries (`libX11`, `libXrandr`, `libXrender`)

## Usage

Behavior is configured via flags (see `xrandr-profile --help` or `man xrandr-profile`).

Quick examples:
```bash
xrandr-profile --save work         # save current layout as "work"
xrandr-profile --load work         # restore layout "work" if hardware matches
xrandr-profile --load work --force # re-apply even if "work" is already active
xrandr-profile                     # auto-match and apply last used matching profile
xrandr-profile --list              # print saved profiles matching current hardware
xrandr-profile --list-all          # print all saved profiles
xrandr-profile --list-current      # print current active layout
xrandr-profile --delete work       # delete saved profile "work"
xrandr-profile --watch &           # daemon: auto-apply on monitor configuration changes
xrandr-profile --watch --fallback=horizontal &  # daemon + auto-arrange unknown monitor sets

xrandr-profile --list --names | fzf | xargs -d '\n' xrandr-profile --load # load a profile selected with fzf
```

## Features

- **EDID-based matching**: Profiles are matched by the physical monitor rather than just the output port name.
- **Full layout capture**: Saves position, resolution, refresh rate, rotation, reflection, panning, and scaling transform per monitor.
- **Automatic profile application**: Auto-apply the most recently used profile matching the currently connected monitors.
- **XDG-compliant storage**: Profiles are stored at `$XDG_CONFIG_HOME/xrandr-profile/profiles` (defaults to `~/.config/xrandr-profile/profiles`), as a plain file.
- **Monitor watcher**: Applies the matching profile automatically when monitors are connected or disconnected.
- **Fallback layouts**: Unknown monitor sets can be arranged automatically instead of being left untouched.
- **Hooks**: Supports executing global and per profile hooks.

## Running automatically

`--watch` applies the current layout once on startup, then waits for monitor configuration changes.
Start it once when your graphical session begins.

Using a window manager autostart or `~/.xinitrc`:

```sh
xrandr-profile --watch &
```

Send `SIGHUP` (`pkill -HUP xrandr-profile`) to force an immediate re-apply.

When you plug in a monitor combination you've never saved, `--watch` normally
leaves the layout alone. Add `--fallback=MODE` to have it arrange the
connected outputs automatically instead:

```sh
xrandr-profile --watch --fallback=horizontal &
```

`horizontal` places outputs left to right at each output's preferred mode,
`vertical` stacks them top to bottom, and `clone` mirrors every output onto
the largest one (scaling smaller outputs to fit). Outputs are ordered by name
so the result is deterministic, and the largest output becomes primary. A
matching saved profile always takes priority, the fallback only fires on a
no-match, and never overrides an explicit `--load`. Its hooks run under the
profile name `fallback`.

## Hooks

`xrandr-profile` supports executing custom scripts before (`pre`) and after (`post`) a profile is applied.
Hooks can be defined globally for all profiles, or specifically for individual profiles. 

Scripts are executed in alphabetical order. They must be regular executable files.

### Directory Structure

Place your executable scripts in the following directories inside `$XDG_CONFIG_HOME/xrandr-profile/`:

- **Global Pre-hooks:** `hooks/pre/`
- **Global Post-hooks:** `hooks/post/`
- **Profile Pre-hooks:** `hooks/<profile_name>/pre/`
- **Profile Post-hooks:** `hooks/<profile_name>/post/`

When a profile is applied, the execution order is:
1. Global `pre` hooks
2. Profile-specific `pre` hooks
3. *[Profile is applied by XRandR]*
4. Global `post` hooks
5. Profile-specific `post` hooks

### Environment Variables

When a hook is executed, `xrandr-profile` exports the following environment variables to the script:

- `XRANDR_PROFILE`: The name of the profile being applied.
- `XRANDR_HOOK`: The current execution phase (`pre` or `post`).

### Example Hook

A common use case is resetting your wallpaper after a monitor layout changes. You could place this script at `~/.config/xrandr-profile/hooks/post/10-wallpaper.sh`:

```bash
#!/bin/sh
# Re-apply wallpaper using feh
feh --bg-fill /path/to/my/wallpaper.jpg
```

## Why xrandr-profile?

Compared to autorandr:

- Is written in C with minimal dependencies
- Uses XRandR APIs directly
- Includes an embedded monitor configuration watcher

## License

This project is licensed under the GNU General Public License v3.
See the `LICENSE` file for details.

© 2026 Dimitris Gerasimou

## See Also

- [xrandr](https://man.archlinux.org/man/xrandr.1) - The underlying X11 RandR utility
- [autorandr](https://github.com/phillipberndt/autorandr) - Python-based alternative with similar goals
