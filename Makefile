JSR_BUILD_DIR = build/event_horizon/
JSR_SRC_DIR   = event_horizon/

JSR_FILES = $(wildcard $(JSR_SRC_DIR)/*jsr) $(wildcard $(JSR_SRC_DIR)/**/*jsr)
JSC_FILES = $(patsubst $(JSR_SRC_DIR)%.jsr, $(JSR_BUILD_DIR)%.jsc, $(JSR_FILES))

.PHONY all:
all: $(JSC_FILES)
	@[ -f ./build/CMakeCache.txt ] || (mkdir build; cd build; cmake ../; cd ../)
	$(MAKE) -C build

$(JSR_BUILD_DIR)%.jsc: $(JSR_SRC_DIR)%.jsr
	@mkdir -p $(dir $@)
	@jstarc $< -o $@

.PHONY: clean
clean:
	rm -rf $(JSR_BUILD_DIR)
	$(MAKE) -C build clean
