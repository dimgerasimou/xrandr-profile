# xrandr-profile

A lightweight, feature rich **XRandR** profile manager. Saves and restores display configurations based on connected hardware, with automatic profile matching via EDID.
Useful when you frequently switch between setups or power profiles, thus changing the output names and want your layout restored without heavy, complex and
difficult to maintain `XRandR` scripts.

**xrandr-profile** can apply the matching profile automatically when monitors are connected or disconnected. It also supports the execution of global and per profile hooks.

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

Behavior is configured via flags (see `xrandr-profile --help`).

Quick example:
```bash
xrandr-profile --save work        # save current layout as "work"
xrandr-profile --load work        # restore layout "work" if hardware matches
xrandr-profile --list             # print saved profiles matching current hardware
xrandr-profile --list-all         # print all saved profiles
xrandr-profile --list-current     # print current active layout
xrandr-profile --delete work      # delete saved profile "work"
xrandr-profile --watch            # daemon: auto-apply on monitor hotplug
xrandr-profile --watch --fallback=horizontal  # daemon + auto-arrange unknown monitor sets
xrandr-profile                    # auto-match and apply last used matching profile

xrandr-profile --list --names | fzf | xargs xrandr-profile --load # select with fzf profile to load
```

## Features

- **EDID-based matching**: Profiles are matched by monitor identity, not output port name
- **Full layout capture**: Saves position, resolution, refresh rate, rotation, reflection, panning, and scaling transform per monitor
- **Automatic profile application**: Run without arguments to auto-apply the last used matching profile
- **XDG-compliant storage**: Profiles stored at `$XDG_CONFIG_HOME/xrandr-profile/profiles` (defaults to `~/.config/xrandr-profile/profiles`)
- **Hotplug watcher**: Applies the matching profile automatically when monitors are connected or disconnected.
- **Fallback layouts**: With `--fallback=horizontal|vertical|clone`, unknown monitor sets are arranged automatically instead of being left untouched (`off` by default).
- **Hooks**: Supports executing global and per profile hooks.

## Running automatically

`--watch` applies the current layout once on startup, then idles until a
monitor is plugged in or removed. Start it from your session.

Using a window manager autostart or `~/.xinitrc`:

```sh
xrandr-profile --watch &
```

Send `SIGHUP` (`pkill -HUP xrandr-profile`) to force an immediate re-apply —
handy right after saving a new profile.

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

## License

This project is licensed under the GNU General Public License v3.
See the LICENSE file for details.

© 2026 Dimitris Gerasimou

## See Also

- [xrandr](https://man.archlinux.org/man/xrandr.1) - The underlying X11 RandR utility
- [autorandr](https://github.com/phillipberndt/autorandr) - Python-based alternative with similar goals
