# fish completion for xrandr-profile
# Install as: ~/.config/fish/completions/xrandr-profile.fish
#         or: /usr/share/fish/vendor_completions.d/xrandr-profile.fish

function __xrandr_profile_names
    xrandr-profile --list-all --names 2>/dev/null
end

function __xrandr_profile_matching
    xrandr-profile --list --names 2>/dev/null
end

# No positional arguments take files.
complete -c xrandr-profile -f

complete -c xrandr-profile -l save         -d 'Save current layout as NAME' -r -a '(__xrandr_profile_names)'
complete -c xrandr-profile -l load         -d 'Load a saved profile'        -x -a '(__xrandr_profile_matching)'
complete -c xrandr-profile -l delete       -d 'Delete a saved profile'      -x -a '(__xrandr_profile_names)'
complete -c xrandr-profile -l list         -d 'List profiles matching current hardware'
complete -c xrandr-profile -l list-all     -d 'List all saved profiles'
complete -c xrandr-profile -l list-current -d 'Print current active layout'
complete -c xrandr-profile -l watch        -d 'Watch for hotplug and auto-apply'
complete -c xrandr-profile -l fallback     -d 'Auto-arrange unknown monitor sets' -x -a 'horizontal vertical clone off'
complete -c xrandr-profile -l force        -d 'Force apply profile'
complete -c xrandr-profile -l names        -d 'Print only profile names'
complete -c xrandr-profile -l version      -d 'Print version'
complete -c xrandr-profile -l help         -d 'Print help'
