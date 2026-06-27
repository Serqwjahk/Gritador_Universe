.RECIPEPREFIX := >

.PHONY: all linux win export-linux export-win zip-linux zip-win zip-all run run-win clean clean-linux clean-win rebuild-linux rebuild-win status check-win-dlls

PROJECT_NAME := gritador_universe

BUILD_LINUX := build
BUILD_WIN := build-win

EXPORT_LINUX := export-linux
EXPORT_WIN := export-windows

ZIP_LINUX := Gritador_Universe_Linux.zip
ZIP_WIN := Gritador_Universe_Windows.zip

WIN_TOOLCHAIN := cmake/toolchains/mingw64.cmake

JOBS ?= $(shell nproc)

all: linux win

linux:
>if [ ! -f "$(BUILD_LINUX)/CMakeCache.txt" ]; then \
>    cmake -S . -B $(BUILD_LINUX) -DCMAKE_BUILD_TYPE=Release; \
>else \
>    cmake -S . -B $(BUILD_LINUX) -DCMAKE_BUILD_TYPE=Release; \
>fi
>cmake --build $(BUILD_LINUX) -j$(JOBS)

win:
>if [ ! -f "$(BUILD_WIN)/CMakeCache.txt" ]; then \
>    cmake -S . -B $(BUILD_WIN) -DCMAKE_TOOLCHAIN_FILE=$(WIN_TOOLCHAIN) -DCMAKE_BUILD_TYPE=Release; \
>else \
>    cmake -S . -B $(BUILD_WIN) -DCMAKE_BUILD_TYPE=Release; \
>fi
>cmake --build $(BUILD_WIN) -j$(JOBS)

rebuild-linux:
>rm -rf $(BUILD_LINUX)
>$(MAKE) linux

rebuild-win:
>rm -rf $(BUILD_WIN)
>$(MAKE) win

export-linux: linux
>rm -rf $(EXPORT_LINUX)
>mkdir -p $(EXPORT_LINUX)
>cp -u $(BUILD_LINUX)/$(PROJECT_NAME) $(EXPORT_LINUX)/
>cp -u *.jpg *.jpeg *.png *.bmp *.tga $(EXPORT_LINUX)/ 2>/dev/null || true

export-win: win
>rm -rf $(EXPORT_WIN)
>mkdir -p $(EXPORT_WIN)
>cp -u $(BUILD_WIN)/$(PROJECT_NAME).exe $(EXPORT_WIN)/
>cp -u *.jpg *.jpeg *.png *.bmp *.tga $(EXPORT_WIN)/ 2>/dev/null || true

zip-linux: export-linux
>rm -f $(ZIP_LINUX)
>cd $(EXPORT_LINUX) && cmake -E tar cfv ../$(ZIP_LINUX) --format=zip .

zip-win: export-win
>rm -f $(ZIP_WIN)
>cd $(EXPORT_WIN) && cmake -E tar cfv ../$(ZIP_WIN) --format=zip .

zip-all: zip-linux zip-win

run: linux
>./$(BUILD_LINUX)/$(PROJECT_NAME)

run-win: win
>wine ./$(BUILD_WIN)/$(PROJECT_NAME).exe

check-win-dlls: win
>x86_64-w64-mingw32-objdump -p $(BUILD_WIN)/$(PROJECT_NAME).exe | grep "DLL Name" || true

clean:
>rm -rf $(BUILD_LINUX) $(BUILD_WIN) $(EXPORT_LINUX) $(EXPORT_WIN)
>rm -f $(ZIP_LINUX) $(ZIP_WIN)

clean-linux:
>rm -rf $(BUILD_LINUX) $(EXPORT_LINUX)
>rm -f $(ZIP_LINUX)

clean-win:
>rm -rf $(BUILD_WIN) $(EXPORT_WIN)
>rm -f $(ZIP_WIN)

status:
>@echo "Linux build:"
>@ls -lh $(BUILD_LINUX)/$(PROJECT_NAME) 2>/dev/null || true
>@echo ""
>@echo "Windows build:"
>@ls -lh $(BUILD_WIN)/$(PROJECT_NAME).exe 2>/dev/null || true
>@echo ""
>@file $(BUILD_WIN)/$(PROJECT_NAME).exe 2>/dev/null || true
>@echo ""
>@echo "Export Windows:"
>@find $(EXPORT_WIN) -maxdepth 2 -type f 2>/dev/null || true
>@echo ""
>@echo "Packages:"
>@ls -lh $(ZIP_LINUX) $(ZIP_WIN) 2>/dev/null || true