# =============================================================================
#  SourceGlo Pro - Production Intelligence for Better Mixes
#
#  Hand-rolled build. This machine has Command Line Tools but no Xcode, so the
#  usual JUCE build paths (Projucer -> xcodebuild) are unavailable. This
#  Makefile compiles the JUCE 9 module unity files directly with clang++ and
#  assembles the macOS bundles by hand. Same pattern as Bay2LA / MelodyGlo /
#  The Drum King / MasterGlo Pro.
#
#  IMPORTANT: cap parallelism at -j 2 on this machine. Higher counts run clang
#  out of memory on the heavy JUCE translation units (juce_graphics_Harfbuzz).
#
#  Targets:
#    make            - build VST3, AU and Standalone
#    make vst3/au/standalone
#    make juceobjs   - just the cached JUCE objects (plugin tree)
#    make syntax     - fast -fsyntax-only over project sources
#    make uishot     - render the editor to build/SourceGlo-ui.png headlessly
#    make dsptest    - deterministic DSP test suite
#    make install    - copy bundles into ~/Library/Audio/Plug-Ins
#    make universal  - x86_64 + arm64 fat build
#    make test       - probe the finished VST3 like a DAW would + dsptest
#    make clean / distclean
# =============================================================================

PROJECT      := SourceGloPro
VERSION      := 0.9.1
JUCE_DIR     := /Users/jokabeatz/Documents/JUCE
JUCE_MODULES := $(JUCE_DIR)/modules
VST3_SDK     := $(JUCE_MODULES)/juce_audio_processors_headless/format_types/VST3_SDK

ROOT   := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
SRC    := $(ROOT)/Source
BUILD  := $(ROOT)/build
DIST   := $(BUILD)/dist

ARCHS      ?= x86_64
ARCH_FLAGS := $(foreach a,$(ARCHS),-arch $(a))
MIN_MACOS  ?= 10.15
CXX_STD    ?= c++17

# Object files are keyed by architecture + deployment target. make compares
# timestamps, not compiler flags, so without this a switch to a universal build
# would silently reuse the single-arch objects.
empty      :=
space      := $(empty) $(empty)
CONFIG_TAG := $(subst $(space),_,$(ARCHS))-$(MIN_MACOS)
OBJ        := $(BUILD)/obj/$(CONFIG_TAG)

CXX := clang++
CC  := clang

# -----------------------------------------------------------------------------
#  Code signing: Developer ID if present, ad-hoc otherwise.
#  Override: make SIGN_ID="Developer ID Application: Name (TEAMID)"
# -----------------------------------------------------------------------------
SIGN_ID ?= $(shell security find-identity -v -p codesigning 2>/dev/null | \
             sed -n 's/.*"\(Developer ID Application: [^"]*\)".*/\1/p' | head -1)

ifeq ($(strip $(SIGN_ID)),)
  SIGN_ID    := -
  SIGN_FLAGS :=
  SIGN_KIND  := ad-hoc
else
  SIGN_FLAGS := --options runtime --timestamp
  SIGN_KIND  := $(SIGN_ID)
endif

# -----------------------------------------------------------------------------
#  Flags
# -----------------------------------------------------------------------------

# The AU wrappers include Apple's SDK as <AudioUnitSDK/...>, so the folder that
# *contains* AudioUnitSDK has to be on the include path too.
INCLUDES := -I$(ROOT) -I$(SRC) -I$(JUCE_MODULES) -I$(VST3_SDK) \
            -I$(JUCE_MODULES)/juce_audio_plugin_client/AU

MODULE_DEFS := \
  -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 \
  -DJUCE_MODULE_AVAILABLE_juce_audio_basics=1 \
  -DJUCE_MODULE_AVAILABLE_juce_audio_devices=1 \
  -DJUCE_MODULE_AVAILABLE_juce_audio_formats=1 \
  -DJUCE_MODULE_AVAILABLE_juce_audio_plugin_client=1 \
  -DJUCE_MODULE_AVAILABLE_juce_audio_processors=1 \
  -DJUCE_MODULE_AVAILABLE_juce_audio_processors_headless=1 \
  -DJUCE_MODULE_AVAILABLE_juce_audio_utils=1 \
  -DJUCE_MODULE_AVAILABLE_juce_core=1 \
  -DJUCE_MODULE_AVAILABLE_juce_data_structures=1 \
  -DJUCE_MODULE_AVAILABLE_juce_dsp=1 \
  -DJUCE_MODULE_AVAILABLE_juce_events=1 \
  -DJUCE_MODULE_AVAILABLE_juce_graphics=1 \
  -DJUCE_MODULE_AVAILABLE_juce_gui_basics=1 \
  -DJUCE_MODULE_AVAILABLE_juce_gui_extra=1

CONFIG_DEFS := \
  -DJUCE_USE_CURL=0 \
  -DJUCE_WEB_BROWSER=0 \
  -DJUCE_USE_CAMERA=0 \
  -DJUCE_LOAD_CURL_SYMBOLS_LAZILY=0 \
  -DJUCE_STRICT_REFCOUNTEDPOINTER=1 \
  -DJUCE_VST3_CAN_REPLACE_VST2=0 \
  -DJUCE_PLUGINHOST_VST3=0 \
  -DJUCE_PLUGINHOST_AU=0 \
  -DJUCE_PLUGINHOST_LV2=0 \
  -DJUCE_PLUGINHOST_ARA=0 \
  -DJUCE_REPORT_APP_USAGE=0 \
  -DJUCE_USE_FLAC=1 \
  -DNDEBUG=1

FORMAT_DEFS := \
  -DJucePlugin_Build_VST=0 \
  -DJucePlugin_Build_VST3=1 \
  -DJucePlugin_Build_AU=1 \
  -DJucePlugin_Build_AUv3=0 \
  -DJucePlugin_Build_AAX=0 \
  -DJucePlugin_Build_Unity=0 \
  -DJucePlugin_Build_LV2=0

WARN := -Wall -Wno-unused-parameter -Wno-deprecated-declarations \
        -Wno-unused-variable -Wno-unknown-pragmas -Wno-deprecated-copy

CXXFLAGS_BASE := -std=$(CXX_STD) -O3 -MMD -MP -fvisibility=hidden -fvisibility-inlines-hidden \
                 -fPIC $(ARCH_FLAGS) -mmacosx-version-min=$(MIN_MACOS) \
                 $(WARN) $(INCLUDES) $(MODULE_DEFS) $(CONFIG_DEFS) $(FORMAT_DEFS) \
                 -include $(ROOT)/JucePluginDefines.h

CXXFLAGS := $(CXXFLAGS_BASE) -x objective-c++

CFLAGS := -O3 -fPIC $(ARCH_FLAGS) -mmacosx-version-min=$(MIN_MACOS) -w \
          $(INCLUDES) $(MODULE_DEFS) $(CONFIG_DEFS)

FRAMEWORKS := \
  -framework Accelerate -framework AudioToolbox -framework AudioUnit \
  -framework Cocoa -framework CoreAudio -framework CoreAudioKit \
  -framework CoreMIDI -framework CoreServices -framework CoreText \
  -framework CoreGraphics -framework CoreImage -framework DiscRecording \
  -framework Foundation -framework IOKit -framework Metal -framework MetalKit \
  -framework QuartzCore -framework Security -framework UniformTypeIdentifiers \
  -framework AVFoundation

LDFLAGS_BASE := $(ARCH_FLAGS) -mmacosx-version-min=$(MIN_MACOS) $(FRAMEWORKS)

# -----------------------------------------------------------------------------
#  Sources
# -----------------------------------------------------------------------------

JUCE_SRCS := \
  $(JUCE_MODULES)/juce_core/juce_core.mm \
  $(JUCE_MODULES)/juce_core/juce_core_CompilationTime.cpp \
  $(JUCE_MODULES)/juce_events/juce_events.mm \
  $(JUCE_MODULES)/juce_data_structures/juce_data_structures.mm \
  $(JUCE_MODULES)/juce_graphics/juce_graphics.mm \
  $(JUCE_MODULES)/juce_graphics/juce_graphics_Harfbuzz.cpp \
  $(JUCE_MODULES)/juce_gui_basics/juce_gui_basics.mm \
  $(JUCE_MODULES)/juce_gui_basics/juce_gui_basics_2.cpp \
  $(JUCE_MODULES)/juce_gui_basics/juce_gui_basics_3.cpp \
  $(JUCE_MODULES)/juce_gui_basics/juce_gui_basics_4.cpp \
  $(JUCE_MODULES)/juce_gui_basics/juce_gui_basics_5.cpp \
  $(JUCE_MODULES)/juce_gui_extra/juce_gui_extra.mm \
  $(JUCE_MODULES)/juce_audio_basics/juce_audio_basics.mm \
  $(JUCE_MODULES)/juce_audio_formats/juce_audio_formats.mm \
  $(JUCE_MODULES)/juce_audio_devices/juce_audio_devices.mm \
  $(JUCE_MODULES)/juce_audio_processors_headless/juce_audio_processors_headless.mm \
  $(JUCE_MODULES)/juce_audio_processors_headless/juce_audio_processors_headless_ara.cpp \
  $(JUCE_MODULES)/juce_audio_processors_headless/juce_audio_processors_headless_lv2_libs.cpp \
  $(JUCE_MODULES)/juce_audio_processors/juce_audio_processors.mm \
  $(JUCE_MODULES)/juce_audio_utils/juce_audio_utils.mm \
  $(JUCE_MODULES)/juce_dsp/juce_dsp.mm

JUCE_C_SRCS := \
  $(JUCE_MODULES)/juce_core/juce_core_zlib.c \
  $(JUCE_MODULES)/juce_audio_formats/juce_audio_formats_flac_1.c \
  $(JUCE_MODULES)/juce_audio_formats/juce_audio_formats_flac_2.c \
  $(JUCE_MODULES)/juce_graphics/juce_graphics_Sheenbidi.c \
  $(JUCE_MODULES)/juce_graphics/juce_graphics_libjpg_1.c \
  $(JUCE_MODULES)/juce_graphics/juce_graphics_libjpg_2.c \
  $(JUCE_MODULES)/juce_graphics/juce_graphics_libjpg_3.c \
  $(JUCE_MODULES)/juce_graphics/juce_graphics_libpng.c \
  $(JUCE_MODULES)/juce_graphics/juce_graphics_lunasvg.c

# Project sources are globbed so new modules join the build without Makefile
# edits.
PLUGIN_SRCS := $(wildcard $(SRC)/*.cpp) $(wildcard $(SRC)/dsp/*.cpp) \
               $(wildcard $(SRC)/params/*.cpp) $(wildcard $(SRC)/ui/*.cpp) \
               $(wildcard $(SRC)/state/*.cpp) $(wildcard $(SRC)/presets/*.cpp)

VST3_WRAPPER := $(JUCE_MODULES)/juce_audio_plugin_client/juce_audio_plugin_client_VST3.mm
AU_WRAPPERS  := $(JUCE_MODULES)/juce_audio_plugin_client/juce_audio_plugin_client_AU_1.mm \
                $(JUCE_MODULES)/juce_audio_plugin_client/juce_audio_plugin_client_AU_2.mm
SA_WRAPPER   := $(JUCE_MODULES)/juce_audio_plugin_client/juce_audio_plugin_client_Standalone.cpp

obj_of = $(OBJ)/$(1)/$(subst /,_,$(patsubst $(JUCE_MODULES)/%,juce_%,$(patsubst $(SRC)/%,app_%,$(2)))).o

PLUG_OBJS := $(foreach s,$(JUCE_SRCS) $(JUCE_C_SRCS) $(PLUGIN_SRCS),$(call obj_of,plugin,$(s)))
SA_OBJS   := $(foreach s,$(JUCE_SRCS) $(JUCE_C_SRCS) $(PLUGIN_SRCS),$(call obj_of,standalone,$(s)))

VST3_OBJ := $(call obj_of,plugin,$(VST3_WRAPPER))
AU_OBJS  := $(foreach s,$(AU_WRAPPERS),$(call obj_of,plugin,$(s)))
SA_OBJ   := $(call obj_of,standalone,$(SA_WRAPPER))

VST3_BUNDLE := $(DIST)/$(PROJECT).vst3
AU_BUNDLE   := $(DIST)/$(PROJECT).component
SA_BUNDLE   := $(DIST)/$(PROJECT).app

# Runtime artwork and factory data ship inside each bundle at
# Contents/Resources/Assets.
ASSET_DIR   := $(ROOT)/Assets
ASSET_FILES := $(shell find $(ASSET_DIR) -type f -not -name '.DS_Store')

# Wipe dist when the arch config changes so a single-arch build can never be
# mistaken for a universal one.
CONFIG_STAMP := $(DIST)/.config-$(CONFIG_TAG)

# -----------------------------------------------------------------------------
#  Build rules
# -----------------------------------------------------------------------------

.PHONY: all vst3 au standalone install universal clean distclean \
        juceobjs syntax uishot dsptest test sanitize installer notarize icon video reel assets

all: vst3 au standalone

juceobjs: $(foreach s,$(JUCE_SRCS) $(JUCE_C_SRCS),$(call obj_of,plugin,$(s)))

vst3: $(VST3_BUNDLE)
au: $(AU_BUNDLE)
standalone: $(SA_BUNDLE)

# -----------------------------------------------------------------------------
#  Universal build: one architecture at a time, then lipo.
#
#  Passing both -arch flags to clang in a single invocation DOES NOT WORK on this
#  machine. The dual-arch frontend runs out of memory on the heavier translation
#  units and clang dies with "frontend command failed due to signal" - it killed
#  a full build on PluginEditor.cpp for arm64, at -j 1, with memory to spare.
#  It looks like a compiler bug and is not: the same file compiles every time
#  when only one -arch is in play.
#
#  So each slice is built on the known-good single-arch path and the binaries are
#  merged afterwards. Same result, and it is the difference between a build that
#  works and one that does not.
#
#  Both slices use MIN_MACOS=11.0 - arm64 requires Big Sur, and mismatched
#  deployment targets across slices are a support problem waiting to happen.
# -----------------------------------------------------------------------------
SLICE_DIR := $(BUILD)/slices

universal:
	@echo "=== slice 1 of 2: x86_64 ==========================================="
	@$(MAKE) ARCHS=x86_64 MIN_MACOS=11.0 all
	@rm -rf $(SLICE_DIR)/x86_64
	@mkdir -p $(SLICE_DIR)/x86_64
	@cp -R $(DIST)/$(PROJECT).vst3 $(DIST)/$(PROJECT).component $(DIST)/$(PROJECT).app \
	   $(SLICE_DIR)/x86_64/
	@echo
	@echo "=== slice 2 of 2: arm64 ============================================"
	@$(MAKE) ARCHS=arm64 MIN_MACOS=11.0 all
	@echo
	@echo "=== merging slices ================================================="
	@for b in $(PROJECT).vst3 $(PROJECT).component $(PROJECT).app; do \
	   lipo -create "$(SLICE_DIR)/x86_64/$$b/Contents/MacOS/$(PROJECT)" \
	                "$(DIST)/$$b/Contents/MacOS/$(PROJECT)" \
	        -output "$(DIST)/$$b/Contents/MacOS/$(PROJECT).universal" || exit 1; \
	   mv "$(DIST)/$$b/Contents/MacOS/$(PROJECT).universal" \
	      "$(DIST)/$$b/Contents/MacOS/$(PROJECT)"; \
	   echo "  LIPO             $$b -> $$(lipo -archs "$(DIST)/$$b/Contents/MacOS/$(PROJECT)")"; \
	 done
	@echo
	@# lipo rewrites the executable, which invalidates the signature that was
	@# applied to the arm64-only bundle. Everything must be signed again.
	@for b in $(PROJECT).vst3 $(PROJECT).component; do \
	   codesign --force $(SIGN_FLAGS) --sign "$(SIGN_ID)" "$(DIST)/$$b" >/dev/null 2>&1 \
	     && echo "  RESIGN           $$b ($(SIGN_KIND))" \
	     || echo "  RESIGN           FAILED for $$b"; \
	 done
	@codesign --force $(SIGN_FLAGS) --entitlements $(ROOT)/packaging/standalone.entitlements \
	   --sign "$(SIGN_ID)" "$(DIST)/$(PROJECT).app" >/dev/null 2>&1 \
	  && echo "  RESIGN           $(PROJECT).app ($(SIGN_KIND))" \
	  || echo "  RESIGN           FAILED for $(PROJECT).app"
	@echo
	@for b in $(PROJECT).vst3 $(PROJECT).component $(PROJECT).app; do \
	   codesign --verify --deep --strict "$(DIST)/$$b" 2>&1 \
	     && echo "  VERIFY           $$b ok" \
	     || echo "  VERIFY           $$b FAILED"; \
	 done

# Wipes dist when the architecture configuration changes, so a single-arch build
# can never be mistaken for a universal one.
#
# Two things this has to get right, both learned the hard way:
#
#  * A plain rm is not enough. macOS App Management (TCC) can refuse to delete a
#    signed .app, and a DAW or Finder holding a handle on the folder makes the
#    whole rm fail - which killed a universal build mid-flight. Falling back to
#    a move means a transient lock cannot stop the build.
#
#  * RELEASE ARTIFACTS ARE RESCUED FIRST. This rule once deleted a signed,
#    notarized installer because a routine `make ARCHS=... vst3` changed the
#    config tag. Installers are expensive to reproduce - a full universal build
#    plus a round trip to Apple - so any .pkg is moved to build/released/ before
#    the wipe rather than being destroyed with the bundles.
$(CONFIG_STAMP):
	@if ls $(DIST)/*.pkg >/dev/null 2>&1; then \
	   mkdir -p $(BUILD)/released; \
	   for p in $(DIST)/*.pkg; do \
	     mv "$$p" $(BUILD)/released/ 2>/dev/null \
	       && echo "  RESCUED          $$(basename $$p) -> build/released/"; \
	   done; \
	 fi
	@rm -rf $(DIST) 2>/dev/null \
	  || mv $(DIST) "$(DIST).stale-$$(date +%s)" 2>/dev/null \
	  || true
	@mkdir -p $(DIST)
	@touch $@

# --- compile: plugin tree (JUCE_STANDALONE_APPLICATION=0) --------------------
define PLUGIN_RULE
$(call obj_of,plugin,$(1)): $(1) $(ROOT)/JucePluginDefines.h
	@mkdir -p $$(dir $$@)
	@echo "  CXX [plugin]     $$(notdir $(1))"
	@$$(CXX) $$(CXXFLAGS) -DJUCE_STANDALONE_APPLICATION=0 -DJucePlugin_Build_Standalone=0 -c $(1) -o $$@
endef
$(foreach s,$(JUCE_SRCS) $(PLUGIN_SRCS) $(VST3_WRAPPER) $(AU_WRAPPERS),$(eval $(call PLUGIN_RULE,$(s))))

# --- compile: standalone tree (JUCE_STANDALONE_APPLICATION=1) ----------------
define SA_RULE
$(call obj_of,standalone,$(1)): $(1) $(ROOT)/JucePluginDefines.h
	@mkdir -p $$(dir $$@)
	@echo "  CXX [standalone] $$(notdir $(1))"
	@$$(CXX) $$(CXXFLAGS) -DJUCE_STANDALONE_APPLICATION=1 -DJucePlugin_Build_Standalone=1 -c $(1) -o $$@
endef
$(foreach s,$(JUCE_SRCS) $(PLUGIN_SRCS) $(SA_WRAPPER),$(eval $(call SA_RULE,$(s))))

# --- compile: vendored C libraries -------------------------------------------
define PLUGIN_C_RULE
$(call obj_of,plugin,$(1)): $(1)
	@mkdir -p $$(dir $$@)
	@echo "  CC  [plugin]     $$(notdir $(1))"
	@$$(CC) $$(CFLAGS) -c $(1) -o $$@
endef
$(foreach s,$(JUCE_C_SRCS),$(eval $(call PLUGIN_C_RULE,$(s))))

define SA_C_RULE
$(call obj_of,standalone,$(1)): $(1)
	@mkdir -p $$(dir $$@)
	@echo "  CC  [standalone] $$(notdir $(1))"
	@$$(CC) $$(CFLAGS) -c $(1) -o $$@
endef
$(foreach s,$(JUCE_C_SRCS),$(eval $(call SA_C_RULE,$(s))))

# --- link + assemble bundles -------------------------------------------------

define COPY_ASSETS
	@rm -rf $(1)/Contents/Resources/Assets
	@cp -R $(ASSET_DIR) $(1)/Contents/Resources/Assets
	@chmod -R a+rX $(1)/Contents/Resources/Assets
endef

$(VST3_BUNDLE): $(CONFIG_STAMP) $(PLUG_OBJS) $(VST3_OBJ) $(ROOT)/packaging/VST3-Info.plist $(ASSET_FILES)
	@echo "  BUNDLE           $(PROJECT).vst3"
	@rm -rf $@
	@mkdir -p $@/Contents/MacOS $@/Contents/Resources
	@$(CXX) -bundle $(PLUG_OBJS) $(VST3_OBJ) $(LDFLAGS_BASE) -o $@/Contents/MacOS/$(PROJECT)
	@cp $(ROOT)/packaging/VST3-Info.plist $@/Contents/Info.plist
	$(call COPY_ASSETS,$@)
	@printf 'BNDL????' > $@/Contents/PkgInfo
	@codesign --force $(SIGN_FLAGS) --sign "$(SIGN_ID)" $@ >/dev/null 2>&1 && echo "  SIGN             $(notdir $@) ($(SIGN_KIND))" || echo "  SIGN             FAILED for $(notdir $@)"

$(AU_BUNDLE): $(CONFIG_STAMP) $(PLUG_OBJS) $(AU_OBJS) $(ROOT)/packaging/AU-Info.plist $(ASSET_FILES)
	@echo "  BUNDLE           $(PROJECT).component"
	@rm -rf $@
	@mkdir -p $@/Contents/MacOS $@/Contents/Resources
	@$(CXX) -bundle $(PLUG_OBJS) $(AU_OBJS) $(LDFLAGS_BASE) -o $@/Contents/MacOS/$(PROJECT)
	@cp $(ROOT)/packaging/AU-Info.plist $@/Contents/Info.plist
	$(call COPY_ASSETS,$@)
	@printf 'BNDL????' > $@/Contents/PkgInfo
	@codesign --force $(SIGN_FLAGS) --sign "$(SIGN_ID)" $@ >/dev/null 2>&1 && echo "  SIGN             $(notdir $@) ($(SIGN_KIND))" || echo "  SIGN             FAILED for $(notdir $@)"

$(SA_BUNDLE): $(CONFIG_STAMP) $(SA_OBJS) $(SA_OBJ) $(ROOT)/packaging/Standalone-Info.plist $(ASSET_FILES)
	@echo "  BUNDLE           $(PROJECT).app"
	@# macOS App Management (TCC) blocks deleting a signed .app unless the
	@# terminal has that permission, so move it aside when rm is refused.
	@rm -rf $@ 2>/dev/null || mv $@ $@.old-$$(date +%s) 2>/dev/null || true
	@mkdir -p $@/Contents/MacOS $@/Contents/Resources
	@$(CXX) $(SA_OBJS) $(SA_OBJ) $(LDFLAGS_BASE) -o $@/Contents/MacOS/$(PROJECT)
	@cp $(ROOT)/packaging/Standalone-Info.plist $@/Contents/Info.plist
	@# The icon has to be in Resources and named to match CFBundleIconFile, or
	@# the Dock silently falls back to the generic application icon.
	@test -f $(ROOT)/packaging/SourceGloPro.icns \
	  && cp $(ROOT)/packaging/SourceGloPro.icns $@/Contents/Resources/SourceGloPro.icns \
	  || echo "  WARNING          no SourceGloPro.icns - run 'make icon' (app will show a generic icon)"
	$(call COPY_ASSETS,$@)
	@printf 'APPL????' > $@/Contents/PkgInfo
	@# The standalone alone carries entitlements: it opens an audio input, and
	@# under the hardened runtime that needs the audio-input entitlement as well
	@# as the usage description in Info.plist. The plug-ins inherit the host's.
	@codesign --force $(SIGN_FLAGS) --entitlements $(ROOT)/packaging/standalone.entitlements \
	   --sign "$(SIGN_ID)" $@ >/dev/null 2>&1 && echo "  SIGN             $(notdir $@) ($(SIGN_KIND))" || echo "  SIGN             FAILED for $(notdir $@)"

# --- install -----------------------------------------------------------------

install: all
	@mkdir -p ~/Library/Audio/Plug-Ins/VST3 ~/Library/Audio/Plug-Ins/Components
	@rm -rf ~/Library/Audio/Plug-Ins/VST3/$(PROJECT).vst3
	@rm -rf ~/Library/Audio/Plug-Ins/Components/$(PROJECT).component
	@cp -R $(VST3_BUNDLE) ~/Library/Audio/Plug-Ins/VST3/
	@cp -R $(AU_BUNDLE) ~/Library/Audio/Plug-Ins/Components/
	@killall -9 AudioComponentRegistrar 2>/dev/null || true
	@rm -rf /Applications/$(PROJECT).app 2>/dev/null \
	  || mv /Applications/$(PROJECT).app "/Applications/$(PROJECT).app.old-$$(date +%s)" 2>/dev/null || true
	@cp -R $(SA_BUNDLE) /Applications/ 2>/dev/null \
	  && echo "Installed VST3, AU and /Applications/$(PROJECT).app" \
	  || echo "Installed VST3 and AU (could not write /Applications - grant App Management or copy the .app by hand)"

# --- fast feedback -----------------------------------------------------------

# -MMD is stripped here on purpose: with -fsyntax-only there is no -o for clang
# to derive a path from, so it writes the .d file into the working directory and
# litters the repository root.
SYNTAX_FLAGS := $(filter-out -MMD -MP,$(CXXFLAGS))

syntax:
	@for f in $(PLUGIN_SRCS); do \
	  echo "  SYNTAX           $$(basename $$f)"; \
	  $(CXX) $(SYNTAX_FLAGS) -DJUCE_STANDALONE_APPLICATION=0 -DJucePlugin_Build_Standalone=0 \
	     -fsyntax-only $$f || exit 1; \
	done
	@echo "  syntax OK"

TOOLS := $(BUILD)/tools

$(TOOLS)/uishot: $(PLUG_OBJS) $(ROOT)/tools/UIShot.cpp $(ROOT)/JucePluginDefines.h
	@mkdir -p $(TOOLS)
	@echo "  CXX [tools]      UIShot.cpp"
	@$(CXX) $(CXXFLAGS) -DJUCE_STANDALONE_APPLICATION=0 -DJucePlugin_Build_Standalone=0 \
	   -c $(ROOT)/tools/UIShot.cpp -o $(TOOLS)/UIShot.o
	@$(CXX) $(PLUG_OBJS) $(TOOLS)/UIShot.o $(LDFLAGS_BASE) -o $@

uishot: $(TOOLS)/uishot
	@cd $(BUILD) && $(TOOLS)/uishot $(ARGS)

$(TOOLS)/dsptest: $(PLUG_OBJS) $(ROOT)/tools/DspTest.cpp $(ROOT)/JucePluginDefines.h
	@mkdir -p $(TOOLS)
	@echo "  CXX [tools]      DspTest.cpp"
	@$(CXX) $(CXXFLAGS) -DJUCE_STANDALONE_APPLICATION=0 -DJucePlugin_Build_Standalone=0 \
	   -c $(ROOT)/tools/DspTest.cpp -o $(TOOLS)/DspTest.o
	@$(CXX) $(PLUG_OBJS) $(TOOLS)/DspTest.o $(LDFLAGS_BASE) -o $@

dsptest: $(TOOLS)/dsptest
	@cd $(BUILD) && $(TOOLS)/dsptest $(ARGS)

# LAYOUT=stacked (default) | full | mono
LAYOUT ?= stacked

$(TOOLS)/makeicon: $(PLUG_OBJS) $(ROOT)/tools/MakeIcon.cpp $(ROOT)/JucePluginDefines.h
	@mkdir -p $(TOOLS)
	@echo "  CXX [tools]      MakeIcon.cpp"
	@$(CXX) $(CXXFLAGS) -DJUCE_STANDALONE_APPLICATION=0 -DJucePlugin_Build_Standalone=0 \
	   -c $(ROOT)/tools/MakeIcon.cpp -o $(TOOLS)/MakeIcon.o
	@$(CXX) $(PLUG_OBJS) $(TOOLS)/MakeIcon.o $(LDFLAGS_BASE) -o $@

icon: $(TOOLS)/makeicon
	@$(TOOLS)/makeicon $(LAYOUT) $(BUILD)/SourceGloPro.iconset
	@iconutil -c icns $(BUILD)/SourceGloPro.iconset -o $(ROOT)/packaging/SourceGloPro.icns
	@echo "  ICNS             packaging/SourceGloPro.icns ($$(du -h $(ROOT)/packaging/SourceGloPro.icns | cut -f1), layout=$(LAYOUT))"

# Encodes straight to H.264 via AVFoundation. There is no ffmpeg on this machine
# and no package manager to install one, but AVFoundation is already linked.
$(TOOLS)/makevideo: $(PLUG_OBJS) $(ROOT)/tools/MakeVideo.cpp $(ROOT)/JucePluginDefines.h
	@mkdir -p $(TOOLS)
	@echo "  CXX [tools]      MakeVideo.cpp"
	@$(CXX) $(CXXFLAGS) -DJUCE_STANDALONE_APPLICATION=0 -DJucePlugin_Build_Standalone=0 \
	   -c $(ROOT)/tools/MakeVideo.cpp -o $(TOOLS)/MakeVideo.o
	@$(CXX) $(PLUG_OBJS) $(TOOLS)/MakeVideo.o $(LDFLAGS_BASE) \
	   -framework CoreVideo -framework CoreMedia -o $@

# Source material for the demo. Passed as separate quoted variables rather than
# folded into ARGS, because ARGS is word-split by the shell and real stem paths
# have spaces in them:
#
#   make video BEAT="~/Music/My Beat.wav" VOX="~/Music/My Vocal.wav"
#
# A '#' in a path still breaks this - make starts a comment there even inside a
# command-line assignment. Symlink such files to a plain name first.
BEAT ?=
VOX  ?=

video: $(TOOLS)/makevideo
	@mkdir -p $(ROOT)/marketing
	@cd $(ROOT)/marketing && $(TOOLS)/makevideo \
	   $(if $(ARGS),$(ARGS),SourceGlo-Pro-Demo.mp4 30) \
	   $(if $(BEAT),"$(BEAT)") $(if $(VOX),"$(VOX)")

# Same film, 9:16, for Reels / Shorts / TikTok.
# Validates the PNGs at the container level rather than through a decoder.
# macOS ImageIO accepts a truncated IDAT and says nothing; Windows libpng
# refuses it and takes the editor down. See tools/check-assets.py.
assets:
	@python3 $(ROOT)/tools/check-assets.py $(ROOT)/Assets

reel: $(TOOLS)/makevideo
	@mkdir -p $(ROOT)/marketing
	@cd $(ROOT)/marketing && $(TOOLS)/makevideo \
	   $(if $(ARGS),$(ARGS),SourceGlo-Pro-Reel.mp4 30) --vertical \
	   $(if $(BEAT),"$(BEAT)") $(if $(VOX),"$(VOX)")

$(TOOLS)/vst3probe: $(ROOT)/tools/VST3Probe.cpp
	@mkdir -p $(TOOLS)
	@echo "  CXX [tools]      VST3Probe.cpp"
	@$(CXX) -std=c++17 -O2 $(ARCH_FLAGS) -mmacosx-version-min=$(MIN_MACOS) \
	   -I$(VST3_SDK) $< -framework CoreFoundation -o $@

# Does not depend on $(VST3_BUNDLE) on purpose - see the note on `installer`.
# Listing it makes make relink under the DEFAULT single-arch config, which
# silently replaces a universal build with an x86_64-only one.
test: $(TOOLS)/vst3probe $(TOOLS)/dsptest
	@test -d $(VST3_BUNDLE) || { echo "No VST3 in $(DIST). Run 'make all' or 'make universal'."; exit 1; }
	@echo "--- asset integrity ---------------------------------------------"
	@python3 $(ROOT)/tools/check-assets.py $(ROOT)/Assets
	@echo "--- VST3 bundle probe -------------------------------------------"
	@$(TOOLS)/vst3probe $(VST3_BUNDLE)
	@echo
	@cd $(BUILD) && $(TOOLS)/dsptest

# -----------------------------------------------------------------------------
#  Retail installer.
#
#    make installer     - signed .pkg from whatever is in build/dist
#    make notarize      - submit it to Apple (needs stored credentials)
#
#  Three component packages so a customer can pick formats. VST3 and AU install
#  system-wide, which is what a paid product should do: a per-user install is
#  invisible to a DAW running as another user.
#
#  Refuses to build from a non-universal binary. Shipping an Intel-only plugin
#  to an Apple Silicon customer is a support nightmare, and it is exactly the
#  kind of thing that is easy to miss until someone reports it.
# -----------------------------------------------------------------------------
PKG_DIR      := $(BUILD)/pkg
INSTALLER    := $(DIST)/SourceGlo-Pro-$(VERSION).pkg
PKG_SIGN_ID  ?= $(shell security find-identity -v 2>/dev/null | \
                  sed -n 's/.*"\(Developer ID Installer: [^"]*\)".*/\1/p' | head -1)

# Deliberately depends on NOTHING. Listing the bundle targets here made make
# rebuild them under the DEFAULT single-arch config, quietly destroying the
# universal binaries it was about to package - the architecture guard below
# caught it, which is the only reason it was not shipped that way. The installer
# packages whatever is in dist and refuses if that is not universal.
installer:
	@test -d $(VST3_BUNDLE) || { echo "No bundles in $(DIST). Run 'make universal' first."; exit 1; }
	@echo "--- checking architectures ---"
	@archs=$$(lipo -archs $(VST3_BUNDLE)/Contents/MacOS/$(PROJECT)); \
	  echo "  VST3 contains: $$archs"; \
	  case "$$archs" in \
	    *x86_64*arm64*|*arm64*x86_64*) echo "  universal, ok" ;; \
	    *) echo ""; \
	       echo "  REFUSING: retail installer needs a universal binary."; \
	       echo "  Run 'make universal' first."; \
	       exit 1 ;; \
	  esac
	@echo "--- building component packages ---"
	@rm -rf $(PKG_DIR)
	@mkdir -p $(PKG_DIR)/root-vst3/Library/Audio/Plug-Ins/VST3
	@mkdir -p $(PKG_DIR)/root-au/Library/Audio/Plug-Ins/Components
	@mkdir -p $(PKG_DIR)/root-app/Applications
	@# ditto --norsrc --noextattr, not cp -R. cp carries extended attributes
	@# across, and pkgbuild then emits AppleDouble "._SourceGlo Pro.vst3" siblings
	@# into the payload - clutter in a paid product, and something plug-in
	@# scanners have been known to trip over. The embedded code signatures are
	@# unaffected; they live in the binaries, not in xattrs.
	@ditto --norsrc --noextattr $(VST3_BUNDLE) "$(PKG_DIR)/root-vst3/Library/Audio/Plug-Ins/VST3/SourceGlo Pro.vst3"
	@ditto --norsrc --noextattr $(AU_BUNDLE)   "$(PKG_DIR)/root-au/Library/Audio/Plug-Ins/Components/SourceGlo Pro.component"
	@ditto --norsrc --noextattr $(SA_BUNDLE)   "$(PKG_DIR)/root-app/Applications/SourceGlo Pro.app"
	@# The signatures must still be valid after staging, or the customer gets a
	@# Gatekeeper refusal that no amount of notarizing will fix.
	@codesign --verify --deep --strict "$(PKG_DIR)/root-vst3/Library/Audio/Plug-Ins/VST3/SourceGlo Pro.vst3" \
	  || { echo "  staged VST3 signature is broken"; exit 1; }
	@codesign --verify --deep --strict "$(PKG_DIR)/root-app/Applications/SourceGlo Pro.app" \
	  || { echo "  staged app signature is broken"; exit 1; }
	@pkgbuild --root $(PKG_DIR)/root-vst3 --identifier com.diamondloopz.sourceglopro.vst3 \
	   --version $(VERSION) --install-location / $(PKG_DIR)/SourceGloPro-VST3.pkg >/dev/null
	@pkgbuild --root $(PKG_DIR)/root-au --identifier com.diamondloopz.sourceglopro.au \
	   --version $(VERSION) --install-location / $(PKG_DIR)/SourceGloPro-AU.pkg >/dev/null
	@pkgbuild --root $(PKG_DIR)/root-app --identifier com.diamondloopz.sourceglopro.app \
	   --version $(VERSION) --install-location / $(PKG_DIR)/SourceGloPro-App.pkg >/dev/null
	@echo "  VST3, AU and App components built"
	@echo "--- assembling and signing ---"
	@if [ -z "$(strip $(PKG_SIGN_ID))" ]; then \
	   echo "  no Developer ID Installer certificate - building UNSIGNED"; \
	   productbuild --distribution $(ROOT)/packaging/installer/distribution.xml \
	     --resources $(ROOT)/packaging/installer/resources \
	     --package-path $(PKG_DIR) $(INSTALLER) >/dev/null; \
	 else \
	   productbuild --distribution $(ROOT)/packaging/installer/distribution.xml \
	     --resources $(ROOT)/packaging/installer/resources \
	     --package-path $(PKG_DIR) --sign "$(PKG_SIGN_ID)" \
	     --timestamp $(INSTALLER) >/dev/null; \
	   echo "  signed with $(PKG_SIGN_ID)"; \
	 fi
	@echo
	@echo "  $(INSTALLER)"
	@echo "  $$(du -h $(INSTALLER) | cut -f1)"
	@pkgutil --check-signature $(INSTALLER) 2>&1 | head -4
	@echo
	@echo "  NOT NOTARIZED YET. Gatekeeper will block this on another Mac."
	@echo "  Store credentials once, then run 'make notarize':"
	@echo "    xcrun notarytool store-credentials SourceGlo --apple-id YOU@EXAMPLE.COM \\"
	@echo "      --team-id 922D43C6FJ --password APP-SPECIFIC-PASSWORD"

notarize: $(INSTALLER)
	@echo "Submitting to Apple. This usually takes a few minutes."
	xcrun notarytool submit $(INSTALLER) --keychain-profile SourceGlo --wait
	@echo "Stapling the ticket so it validates offline..."
	xcrun stapler staple $(INSTALLER)
	@xcrun stapler validate $(INSTALLER)
	@spctl --assess --type install -vv $(INSTALLER) 2>&1 | head -3

# -----------------------------------------------------------------------------
#  Sanitizer build of the test suite.
#
#  AddressSanitizer and UndefinedBehaviorSanitizer over the whole JUCE tree, in
#  their own object directory so they never mix with the release objects. Slow
#  to build and slow to run - this is a release-validation step, not something
#  to run every edit.
# -----------------------------------------------------------------------------
SAN_FLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined
SAN_OBJ   := $(BUILD)/obj/sanitize
SAN_CXX   := $(subst -O3,-O1,$(CXXFLAGS)) $(SAN_FLAGS)
SAN_CC    := $(subst -O3,-O1,$(CFLAGS)) $(SAN_FLAGS)

san_obj_of = $(SAN_OBJ)/$(subst /,_,$(patsubst $(JUCE_MODULES)/%,juce_%,$(patsubst $(SRC)/%,app_%,$(1)))).o

SAN_OBJS := $(foreach s,$(JUCE_SRCS) $(JUCE_C_SRCS) $(PLUGIN_SRCS),$(call san_obj_of,$(s)))

define SAN_RULE
$(call san_obj_of,$(1)): $(1) $(ROOT)/JucePluginDefines.h
	@mkdir -p $$(dir $$@)
	@echo "  CXX [sanitize]   $$(notdir $(1))"
	@$$(CXX) $$(SAN_CXX) -DJUCE_STANDALONE_APPLICATION=0 -DJucePlugin_Build_Standalone=0 -c $(1) -o $$@
endef
$(foreach s,$(JUCE_SRCS) $(PLUGIN_SRCS),$(eval $(call SAN_RULE,$(s))))

define SAN_C_RULE
$(call san_obj_of,$(1)): $(1)
	@mkdir -p $$(dir $$@)
	@echo "  CC  [sanitize]   $$(notdir $(1))"
	@$$(CC) $$(SAN_CC) -c $(1) -o $$@
endef
$(foreach s,$(JUCE_C_SRCS),$(eval $(call SAN_C_RULE,$(s))))

$(TOOLS)/dsptest-san: $(SAN_OBJS) $(ROOT)/tools/DspTest.cpp $(ROOT)/JucePluginDefines.h
	@mkdir -p $(TOOLS)
	@echo "  CXX [sanitize]   DspTest.cpp"
	@$(CXX) $(SAN_CXX) -DJUCE_STANDALONE_APPLICATION=0 -DJucePlugin_Build_Standalone=0 \
	   -c $(ROOT)/tools/DspTest.cpp -o $(TOOLS)/DspTest-san.o
	@$(CXX) $(SAN_OBJS) $(TOOLS)/DspTest-san.o $(LDFLAGS_BASE) $(SAN_FLAGS) -o $@

sanitize: $(TOOLS)/dsptest-san
	@cd $(BUILD) && ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=print_stacktrace=1 \
	   $(TOOLS)/dsptest-san $(ARGS)

clean:
	@rm -rf $(DIST) $(BUILD)/tools
	@echo "Cleaned build products (JUCE objects kept - use distclean to drop those too)"

distclean:
	@rm -rf $(BUILD)
	@echo "Cleaned everything"

-include $(wildcard $(OBJ)/plugin/*.d) $(wildcard $(OBJ)/standalone/*.d)
