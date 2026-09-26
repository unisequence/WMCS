#!/bin/sh

set -eu

wmcs_root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
openwrt_input=${1:-${OPENWRT_DIR:-}}
jobs=${WMCS_JOBS:-2}

if [ -z "$openwrt_input" ]; then
	echo "Usage: $0 /path/to/openwrt-buildroot" >&2
	exit 2
fi

case "$jobs" in
	''|*[!0-9]*|0)
		echo "WMCS_JOBS must be a positive integer" >&2
		exit 2
		;;
esac

if [ ! -d "$openwrt_input" ]; then
	echo "OpenWrt buildroot does not exist: $openwrt_input" >&2
	exit 2
fi

openwrt_root=$(CDPATH= cd -- "$openwrt_input" && pwd)
if [ ! -f "$openwrt_root/include/toplevel.mk" ]; then
	echo "Not an OpenWrt buildroot: $openwrt_root" >&2
	exit 2
fi

package_source=$wmcs_root/package/openwrt
package_link=$openwrt_root/package/wmcsd
created_link=0
marker=$(mktemp /tmp/wmcs-package-check.XXXXXX)

cleanup() {
	if [ "$created_link" -eq 1 ] && [ -L "$package_link" ]; then
		unlink "$package_link"
	fi
	rm -f "$marker"
}
trap cleanup EXIT INT TERM

if [ -e "$package_link" ] || [ -L "$package_link" ]; then
	if [ ! -L "$package_link" ] || [ "$(readlink -f "$package_link")" != "$package_source" ]; then
		echo "Refusing to replace existing package path: $package_link" >&2
		exit 2
	fi
else
	ln -s "$package_source" "$package_link"
	created_link=1
fi

make -C "$openwrt_root" \
	package/wmcsd/clean package/wmcsd/compile \
	CONFIG_PACKAGE_wmcsd=m \
	WMCS_ROOT="$wmcs_root" \
	-j"$jobs"

artifact=$(find "$openwrt_root/bin" -type f \
	\( -name 'wmcsd*.ipk' -o -name 'wmcsd*.apk' \) \
	-newer "$marker" -print | head -n 1)

if [ -z "$artifact" ]; then
	echo "OpenWrt build completed without a fresh wmcsd package" >&2
	exit 1
fi

case "$artifact" in
	*.ipk)
		control=$(tar -xzOf "$artifact" ./control.tar.gz | tar -xzOf - ./control)
		contents=$(tar -xzOf "$artifact" ./data.tar.gz | tar -tzf -)
		config=$(tar -xzOf "$artifact" ./data.tar.gz | tar -xzOf - ./etc/config/wmcs)
		acl=$(tar -xzOf "$artifact" ./data.tar.gz | tar -xzOf - ./usr/share/rpcd/acl.d/wmcs.json)
		printf '%s\n' "$control" | grep -q '^Package: wmcsd$'
		printf '%s\n' "$control" | grep -q '^Depends: .*libubus.*libubox'
		printf '%s\n' "$control" | grep -q '^Depends: .*libuci'
		printf '%s\n' "$control" | grep -q '^Depends: .*libmbedtls'
		;;
	*.apk)
		host_apk=$openwrt_root/staging_dir/host/bin/apk
		if [ ! -x "$host_apk" ]; then
			echo "OpenWrt host apk tool is missing: $host_apk" >&2
			exit 1
		fi
		metadata=$($host_apk adbdump --format yaml "$artifact")
		config=$(cat "$package_source/files/wmcs.config")
		acl=$(cat "$package_source/files/wmcs.rpcd-acl.json")
		printf '%s\n' "$metadata" | grep -q '^  name: wmcsd$'
		printf '%s\n' "$metadata" | grep -q 'libubus'
		printf '%s\n' "$metadata" | grep -q 'libubox'
		printf '%s\n' "$metadata" | grep -q 'libuci'
		printf '%s\n' "$metadata" | grep -q 'libmbedtls'
		printf '%s\n' "$metadata" | grep -q '^  - name: etc/config$'
		printf '%s\n' "$metadata" | grep -q '^  - name: etc/init.d$'
		printf '%s\n' "$metadata" | grep -q '^  - name: usr/sbin$'
		printf '%s\n' "$metadata" | grep -q '^      - name: wmcs.json$'
		printf '%s\n' "$metadata" | grep -q '^      - name: wmcs$'
		if [ "$(printf '%s\n' "$metadata" | grep -c '^      - name: wmcsd$')" -ne 2 ]; then
			echo "APK must contain exactly the wmcsd init script and executable" >&2
			exit 1
		fi
		contents='etc/config/wmcs
etc/init.d/wmcsd
etc/wmcs
usr/lib/wmcs/wmcsd.bin
usr/sbin/wmcsd
usr/share/rpcd/acl.d/wmcs.json'
		$host_apk verify --allow-untrusted "$artifact" >/dev/null
		;;
esac

for required_path in 'etc/config/wmcs' 'etc/init.d/wmcsd' 'etc/wmcs' \
	'usr/lib/wmcs/wmcsd.bin' 'usr/sbin/wmcsd' \
	'usr/share/rpcd/acl.d/wmcs.json'; do
	if ! printf '%s\n' "$contents" | grep -q "$required_path"; then
		echo "Package is missing $required_path" >&2
		exit 1
	fi
done

printf '%s\n' "$config" | grep -Eq "option[[:space:]]+enabled[[:space:]]+'0'"
printf '%s\n' "$config" | grep -Eq "option[[:space:]]+mutation_enabled[[:space:]]+'0'"
printf '%s\n' "$config" | grep -Eq "config[[:space:]]+roaming[[:space:]]+'policy'"
printf '%s\n' "$config" | grep -Eq "option[[:space:]]+enabled[[:space:]]+'0'"
printf '%s\n' "$config" | grep -Eq "option[[:space:]]+source_trigger_dbm[[:space:]]+'-68'"
printf '%s\n' "$config" | grep -Eq "option[[:space:]]+improvement_margin_db[[:space:]]+'8'"
printf '%s\n' "$config" | grep -Eq "option[[:space:]]+neighbor_sync_enabled[[:space:]]+'0'"
grep -Fq -- '--neighbor-sync-enabled' "$wmcs_root/package/openwrt/files/wmcsd.init"
grep -Fq -- '--roaming-improvement-margin-db' "$wmcs_root/package/openwrt/files/wmcsd.init"
printf '%s\n' "$acl" | grep -q '"forget_orphan"'
printf '%s\n' "$acl" | grep -q '"release_start"'

binary=$(find "$openwrt_root/build_dir" -type f \
	-name 'wmcsd.bin' -path '*/usr/lib/wmcs/wmcsd.bin' \
	-newer "$marker" -print | head -n 1)

if [ -z "$binary" ]; then
	echo "Fresh packaged wmcsd binary was not found" >&2
	exit 1
fi

file "$binary" | grep -q 'pie executable'
readelf -l "$binary" | grep -q 'GNU_RELRO'
readelf -d "$binary" | grep -q 'BIND_NOW'

echo "OpenWrt package check: ok"
echo "Artifact: $artifact"
