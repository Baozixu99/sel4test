# Top-level build helper for this workspace

PLATFORM ?= hifive-p550
KERNEL_SEL4_ARCH ?= riscv64
SEL4TEST_APP ?= hyperamp-server
BUILD_DIR ?= cbuild
INIT_BUILD ?= ../init-build.sh
NINJA ?= ninja

.PHONY: all clean configure build rebuild run hyperamp

all: build

clean:
	@echo "Removing $(BUILD_DIR)"
	@rm -rf $(BUILD_DIR)

configure:
	@mkdir -p $(BUILD_DIR)
	@echo "Configuring PLATFORM=$(PLATFORM) KERNEL_SEL4_ARCH=$(KERNEL_SEL4_ARCH) SEL4TEST_APP=$(SEL4TEST_APP)"
	@cd $(BUILD_DIR) && $(INIT_BUILD) -DPLATFORM=$(PLATFORM) -DKernelSel4Arch=$(KERNEL_SEL4_ARCH) -DSel4testApp=$(SEL4TEST_APP)

build: configure
	@echo "Building in $(BUILD_DIR)"
	@$(NINJA) -C $(BUILD_DIR)

rebuild: clean all

hyperamp:
	@$(MAKE) build SEL4TEST_APP=hyperamp-server

run: build
	@echo "Build finished. Artifacts are in $(BUILD_DIR)."