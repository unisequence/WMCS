.PHONY: check check-repository check-simulator check-daemon-contract check-controller-journal check-forget-journal check-rpcd-acl check-luci-app-wmcs check-discovery-wire check-pairing-wire check-control-wire check-result-store check-operation-store check-atomic-file check-snapshot check-wmcsd-cross check-openwrt-package check-runtime-spike

check: check-repository check-simulator check-daemon-contract check-controller-journal check-forget-journal check-rpcd-acl check-luci-app-wmcs check-discovery-wire check-pairing-wire check-control-wire check-result-store check-operation-store check-atomic-file check-snapshot

check-repository:
	@sh scripts/check-repository.sh

check-simulator:
	@PYTHONPATH=simulator python3 -m unittest discover -s simulator/tests -v

check-daemon-contract:
	@sh scripts/check-daemon-contract.sh

check-controller-journal:
	@python3 scripts/check-controller-journal.py

check-forget-journal:
	@python3 scripts/check-forget-journal.py

check-rpcd-acl:
	@sh scripts/check-rpcd-acl.sh

check-luci-app-wmcs:
	@sh scripts/check-luci-app-wmcs.sh

check-discovery-wire:
	@sh scripts/check-discovery-wire.sh

check-pairing-wire:
	@sh scripts/check-pairing-wire.sh

check-control-wire:
	@sh scripts/check-control-wire.sh

check-result-store:
	@sh scripts/check-result-store.sh

check-operation-store:
	@sh scripts/check-operation-store.sh

check-atomic-file:
	@sh scripts/check-atomic-file.sh

check-snapshot:
	@sh tools/snapshot/test-snapshot.sh

check-wmcsd-cross:
	@./scripts/build-wmcsd.sh aarch64
	@./scripts/build-wmcsd.sh mipsel

check-openwrt-package:
	@./scripts/check-openwrt-package.sh "$(OPENWRT_DIR)"

check-runtime-spike:
	@./spikes/runtime/build-c.sh aarch64
	@./spikes/runtime/build-c.sh mipsel
