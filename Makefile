# FM-1 development shortcuts. See README.md and docs/TOOLCHAIN.md.
PY      := $(CURDIR)/.venv/bin/python
BASE    := firmware/base
APP     := firmware/app
NAME    := myfm1
export DYLD_FALLBACK_LIBRARY_PATH := /opt/homebrew/lib

.PHONY: venv toolchain fw host web install sim sim-serve flash-open inspect identity

venv:                       ## Python env for the build and host tools
	python3 -m venv .venv && .venv/bin/pip install -q pillow python-rtmidi mido

toolchain:                  ## (path A) JieLi Linux toolchain + 3 SDK files, hash-pinned
	cd $(BASE) && tools/setup_toolchain.sh --only jieli,ac79-sdk,python

fw:                         ## (path A) firmware/app/build/$(NAME).fwsc  (Docker must be running)
	cd firmware && $(PY) base/tools/build.py app

host:                       ## (path A) host simulator + scripted test
	cd firmware && $(PY) base/tools/build.py app --host && app/build/host/$(NAME)_host app/tests/basic.txt app/build/host

web:                        ## (path A) browser emulator (needs emcc)
	cd firmware && $(PY) base/tools/build.py app --web

install:                    ## (path A) flash over USB-MIDI
	$(PY) $(BASE)/tools/fm1_install.py $(APP)/build/$(NAME).fwsc

sim:                        ## (path B) FM1 Open WASM simulator, native macOS build
	scripts/build-sim-mac.sh

sim-serve: sim
	python3 -m http.server 8173 --bind 127.0.0.1 --directory fm1_open/build/preview

flash-open:                 ## (path B) flash a Windows-built FM1 Open package
	$(PY) tools/fm1tool.py flash out/latest.fwsc --identity-json out/latest-build-identity.json

inspect:                    ## make inspect F=some.fwsc
	$(PY) tools/fm1tool.py inspect $(F)

identity:
	$(PY) tools/fm1tool.py identity
