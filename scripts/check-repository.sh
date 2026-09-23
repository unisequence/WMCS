#!/bin/sh

set -eu

required_files='README.md
LICENSE
PLAN.md
ARCHITECTURE.md
SECURITY.md
CONTRIBUTING.md
docs/compatibility/MATRIX.md
docs/experiments/TEMPLATE.md
docs/requirements/ROAMING.md
docs/protocol/UBUS_API_V0.md
docs/protocol/NATIVE_V0.md'

printf '%s\n' "$required_files" | while IFS= read -r path; do
	if [ ! -f "$path" ]; then
		echo "Missing required foundation file: $path" >&2
		exit 1
	fi
done

git ls-files --cached --others --exclude-standard | while IFS= read -r path; do
	case "$path" in
		*.cap|*.pcap|*.pcapng|*.img|*.trx|*.ubi|*.uImage|*.key|*.pem|*.p12|*.pfx|*.ipk|*.apk)
			echo "Forbidden binary or sensitive artifact is tracked: $path" >&2
			exit 1
			;;
	esac

	if [ -L "$path" ]; then
		echo "Symlinks require an explicit policy decision: $path" >&2
		exit 1
	fi

	if [ -f "$path" ]; then
		size=$(wc -c < "$path")
		if [ "$size" -gt 2097152 ]; then
			echo "Tracked file exceeds the 2 MiB foundation limit: $path" >&2
			exit 1
		fi
	fi
done

# Validate each shell file according to its declared interpreter. Most project
# scripts are POSIX sh; lab helpers may explicitly require Bash features.
find . -type f \( -name '*.sh' -o -name '*.init' \) -not -path './.git/*' -print |
while IFS= read -r path; do
	interpreter=$(sed -n '1p' "$path")
	case "$interpreter" in
		'#!/usr/bin/env bash'|'#!/bin/bash') bash -n "$path" ;;
		*) sh -n "$path" ;;
	esac
done

if [ -d .github/workflows ]; then
	workflow_uses=$(grep -RhoE '^[[:space:]]*uses:[[:space:]]*[^[:space:]]+' .github/workflows || true)
	if [ -n "$workflow_uses" ]; then
		printf '%s\n' "$workflow_uses" | while IFS= read -r use; do
			ref=${use##*@}
			if ! printf '%s\n' "$ref" | grep -Eq '^[0-9a-f]{40}$'; then
				echo "GitHub Action is not pinned to a full commit SHA: $use" >&2
				exit 1
			fi
		done
	fi
fi

echo "Repository foundation checks: ok"
