.PHONY: all native linux win mac Mac export-linux export-win export-mac zip-linux zip-win zip-mac zip-native zip-all run run-win run-mac clean clean-linux clean-win clean-mac rebuild-linux rebuild-win rebuild-mac status check-win-dlls

PROJECT_NAME := gritador_universe

BUILD_LINUX := build
BUILD_WIN := build-win
BUILD_MAC := build-mac

EXPORT_LINUX := export-linux
EXPORT_WIN := export-windows
EXPORT_MAC := export-mac

ZIP_LINUX := Gritador_Universe_Linux.zip
ZIP_WIN := Gritador_Universe_Windows.zip
ZIP_MAC := Gritador_Universe_macOS.zip

WIN_TOOLCHAIN := cmake/toolchains/mingw64.cmake

JOBS ?= $(shell getconf _NPROCESSORS_ONLN || echo 4)

all: native

native: ; if [ "$$(uname)" = "Darwin" ]; then $(MAKE) mac; elif [ "$$(uname)" = "Linux" ]; then $(MAKE) linux; else echo "Sistema no soportado para build nativo."; exit 1; fi

linux: ; if [ "$$(uname)" = "Darwin" ]; then echo "Este target es para Linux. En macOS usa: make mac"; exit 1; fi; cmake -S . -B $(BUILD_LINUX) -DCMAKE_BUILD_TYPE=Release; cmake --build $(BUILD_LINUX) -j$(JOBS)

win: ; if [ "$$(uname)" = "Darwin" ]; then echo "Este target es para cross-build Windows desde Linux con MinGW. En macOS usa: make mac"; exit 1; fi; if [ ! -f "$(BUILD_WIN)/CMakeCache.txt" ]; then cmake -S . -B $(BUILD_WIN) -DCMAKE_TOOLCHAIN_FILE=$(WIN_TOOLCHAIN) -DCMAKE_BUILD_TYPE=Release; else cmake -S . -B $(BUILD_WIN) -DCMAKE_BUILD_TYPE=Release; fi; cmake --build $(BUILD_WIN) -j$(JOBS)

mac: ; if [ "$$(uname)" != "Darwin" ]; then echo "Este target debe ejecutarse en macOS."; exit 1; fi; cmake -S . -B $(BUILD_MAC) -DCMAKE_BUILD_TYPE=Release; cmake --build $(BUILD_MAC) -j$(JOBS)

Mac: mac

rebuild-linux: ; rm -rf $(BUILD_LINUX); $(MAKE) linux

rebuild-win: ; rm -rf $(BUILD_WIN); $(MAKE) win

rebuild-mac: ; rm -rf $(BUILD_MAC); $(MAKE) mac

export-linux: linux ; rm -rf $(EXPORT_LINUX); mkdir -p $(EXPORT_LINUX); cp -f $(BUILD_LINUX)/$(PROJECT_NAME) $(EXPORT_LINUX)/; for f in *.jpg *.jpeg *.png *.bmp *.tga; do if [ -e "$$f" ]; then cp -f "$$f" $(EXPORT_LINUX)/; fi; done

export-win: win ; rm -rf $(EXPORT_WIN); mkdir -p $(EXPORT_WIN); cp -f $(BUILD_WIN)/$(PROJECT_NAME).exe $(EXPORT_WIN)/; for f in *.jpg *.jpeg *.png *.bmp *.tga; do if [ -e "$$f" ]; then cp -f "$$f" $(EXPORT_WIN)/; fi; done

export-mac: mac ; rm -rf $(EXPORT_MAC); mkdir -p $(EXPORT_MAC); cp -f $(BUILD_MAC)/$(PROJECT_NAME) $(EXPORT_MAC)/; for f in *.jpg *.jpeg *.png *.bmp *.tga; do if [ -e "$$f" ]; then cp -f "$$f" $(EXPORT_MAC)/; fi; done

zip-linux: export-linux ; rm -f $(ZIP_LINUX); cd $(EXPORT_LINUX); cmake -E tar cfv ../$(ZIP_LINUX) --format=zip .

zip-win: export-win ; rm -f $(ZIP_WIN); cd $(EXPORT_WIN); cmake -E tar cfv ../$(ZIP_WIN) --format=zip .

zip-mac: export-mac ; rm -f $(ZIP_MAC); cd $(EXPORT_MAC); cmake -E tar cfv ../$(ZIP_MAC) --format=zip .

zip-native: ; if [ "$$(uname)" = "Darwin" ]; then $(MAKE) zip-mac; elif [ "$$(uname)" = "Linux" ]; then $(MAKE) zip-linux; else echo "Sistema no soportado para export nativo."; exit 1; fi

zip-all: zip-native

run: linux ; ./$(BUILD_LINUX)/$(PROJECT_NAME)

run-win: win ; wine ./$(BUILD_WIN)/$(PROJECT_NAME).exe

run-mac: mac ; ./$(BUILD_MAC)/$(PROJECT_NAME)

check-win-dlls: win ; x86_64-w64-mingw32-objdump -p $(BUILD_WIN)/$(PROJECT_NAME).exe | grep "DLL Name" || true

clean: ; rm -rf $(BUILD_LINUX) $(BUILD_WIN) $(BUILD_MAC) $(EXPORT_LINUX) $(EXPORT_WIN) $(EXPORT_MAC); rm -f $(ZIP_LINUX) $(ZIP_WIN) $(ZIP_MAC)

clean-linux: ; rm -rf $(BUILD_LINUX) $(EXPORT_LINUX); rm -f $(ZIP_LINUX)

clean-win: ; rm -rf $(BUILD_WIN) $(EXPORT_WIN); rm -f $(ZIP_WIN)

clean-mac: ; rm -rf $(BUILD_MAC) $(EXPORT_MAC); rm -f $(ZIP_MAC)

status: ; echo "Linux build:"; ls -lh $(BUILD_LINUX)/$(PROJECT_NAME) || true; echo ""; echo "Windows build:"; ls -lh $(BUILD_WIN)/$(PROJECT_NAME).exe || true; file $(BUILD_WIN)/$(PROJECT_NAME).exe || true; echo ""; echo "macOS build:"; ls -lh $(BUILD_MAC)/$(PROJECT_NAME) || true; file $(BUILD_MAC)/$(PROJECT_NAME) || true; echo ""; echo "Export Linux:"; find $(EXPORT_LINUX) -maxdepth 2 -type f || true; echo ""; echo "Export Windows:"; find $(EXPORT_WIN) -maxdepth 2 -type f || true; echo ""; echo "Export macOS:"; find $(EXPORT_MAC) -maxdepth 2 -type f || true; echo ""; echo "Packages:"; ls -lh $(ZIP_LINUX) $(ZIP_WIN) $(ZIP_MAC) || true