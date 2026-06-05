# bash completion for xrandr-profile
# Install as: $BASH_COMPLETION_USER_DIR/completions/xrandr-profile
#         or: /usr/share/bash-completion/completions/xrandr-profile
# Requires the bash-completion package (for _init_completion).

_xrandr_profile()
{
	local cur prev words cword split
	_init_completion -s || return

	case "$prev" in
	--load)
		# Only profiles that match the current hardware can be applied.
		local IFS=$'\n'
		COMPREPLY=( $(compgen -W \
			"$(xrandr-profile --list --names 2>/dev/null)" -- "$cur") )
		return
		;;
	--delete|--save)
		# Any saved profile, regardless of current hardware.
		local IFS=$'\n'
		COMPREPLY=( $(compgen -W \
			"$(xrandr-profile --list-all --names 2>/dev/null)" -- "$cur") )
		return
		;;
	--fallback)
		COMPREPLY=( $(compgen -W 'horizontal vertical clone off' -- "$cur") )
		return
		;;
	esac

	# Handled a --opt=value split that matched no case above.
	$split && return

	if [[ $cur == -* ]]; then
		COMPREPLY=( $(compgen -W '--save --load --delete --list --list-all
			--list-current --watch --fallback --names --version --help' \
			-- "$cur") )
	fi
}
complete -F _xrandr_profile xrandr-profile
