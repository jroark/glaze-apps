# Installable Glaze apps, built against the Glaze application SDK.
#
#   make                  out/pkg/<app>.gzp for every app
#   make test             native tests (host cc, ASan/UBSan; uses ../glaze-os sources)
#
# GLAZE_SDK defaults to the SDK exported by a sibling glaze-os checkout
# (`make sdk` there). Each packages/<app> is an SDK project; this file packs
# it with its icon, which lives beside the sources rather than in res/.
GLAZE_SDK ?= $(abspath ../glaze-os/out/sdk/glaze-sdk)
CROSS ?= mipsel-linux-gnu-
PYTHON ?= python3
OUT ?= out
APPS := $(notdir $(patsubst %/,%,$(wildcard packages/*/)))

.DEFAULT_GOAL := all
.PHONY: all test clean FORCE sdk-check
all: $(foreach a,$(APPS),$(OUT)/pkg/$(a).gzp)

sdk-check:
	@test -f "$(GLAZE_SDK)/share/app.mk" || { echo "GLAZE_SDK=$(GLAZE_SDK) is not a Glaze SDK;" \
	    "run 'make sdk' in ../glaze-os or set GLAZE_SDK" >&2; exit 1; }

# The SDK's app.mk tracks sources and headers, so always ask it.
$(OUT)/build/%/app: FORCE | sdk-check
	$(MAKE) -C packages/$* GLAZE_SDK=$(GLAZE_SDK) CROSS=$(CROSS) BUILD=$(abspath $(OUT)/build/$*) $(abspath $@)

RES = $(wildcard packages/$(1)/res/*)

.SECONDEXPANSION:
$(OUT)/pkg/%.gzp: $(OUT)/build/%/app packages/%/Info packages/%/icon.gzi $$(call RES,$$*)
	@mkdir -p $(dir $@)
	$(PYTHON) $(GLAZE_SDK)/tools/mkromfs.py --package $@ Info=packages/$*/Info app=$< \
	    res/icon.gzi=packages/$*/icon.gzi $(foreach f,$(call RES,$*),res/$(notdir $(f))=$(f))
	$(PYTHON) $(GLAZE_SDK)/bin/glaze-sdk check $@

test:
	$(MAKE) -C tests check

clean:
	rm -rf $(OUT)
