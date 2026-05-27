# xrandr-profile

A lightweight **XRandr** profile manager. Saves and restores display configurations based on connected hardware, with automatic profile matching via EDID.
Useful when you frequently switch between setups or power profiles, thus changing the output names and want your layout restored without heavy, complex and
difficult to maintain `XRandr` scripts.

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
xrandr-profile                    # auto-match and apply last used matching profile

xrandr-profile --list --name | fzf | xargs xrandr-profile --load # select with fzf profile to load
```

## Features

- **EDID-based matching**: Profiles are matched by monitor identity, not output port name
- **Full layout capture**: Saves position, resolution, refresh rate, rotation, reflection, panning, and scaling transform per monitor
- **Automatic profile application**: Run without arguments to auto-apply the last used matching profile
- **XDG-compliant storage**: Profiles stored at `$XDG_CONFIG_HOME/xrandr-profile/profiles` (defaults to `~/.config/xrandr-profile/profiles`)

## License

This project is licensed under the GNU General Public License v3.
See the LICENSE file for details.

© 2026 Dimitris Gerasimou

## See Also

- [xrandr](https://man.archlinux.org/man/xrandr.1) - The underlying X11 RandR utility
- [autorandr](https://github.com/phillipberndt/autorandr) - Python-based alternative with similar goals
