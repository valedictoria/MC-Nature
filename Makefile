CXX      ?= clang++
CXXFLAGS ?= -std=c++20 -O3 -Wall -Wextra -Wno-unused-parameter \
            -fno-exceptions -fno-rtti -flto -mcpu=apple-m1 -Isrc -Isrc/syzygy
LDFLAGS  ?= -flto

SRC      := $(wildcard src/*.cpp) src/syzygy/syzygy.cpp src/nnue/nnue.cpp
OBJ      := $(SRC:.cpp=.o)
# Fathom (Syzygy probing) is vendored C — built separately, no LTO/C++ flags.
TB_OBJ   := src/syzygy/tbprobe.o
# Embedded default NNUE weights (incbin.s pulls in src/nnue/net.bin).
NNUE_OBJ := src/nnue/incbin.o
BIN      := build/carelesschess
INSTALL_DIR := ../lichess-bot/engines

.PHONY: all clean install perft debug

all: $(BIN)

$(BIN): $(OBJ) $(TB_OBJ) $(NNUE_OBJ)
	@mkdir -p build
	$(CXX) $(LDFLAGS) $(OBJ) $(TB_OBJ) $(NNUE_OBJ) -o $@
	@echo "Built $@"

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Fathom probing library: compiled as C11, single-threaded, no LTO.
$(TB_OBJ): src/syzygy/tbprobe.c
	$(CC) -O3 -std=c11 -DTB_NO_THREADS -Isrc/syzygy -c $< -o $@

# Embedded NNUE weights (incbin references src/nnue/net.bin at assemble time).
$(NNUE_OBJ): src/nnue/incbin.s src/nnue/net.bin
	$(CC) -c $< -o $@

install: $(BIN)
	@mkdir -p $(INSTALL_DIR)
	cp $(BIN) $(INSTALL_DIR)/carelesschess
	@# macOS Sequoia flags freshly-compiled binaries with com.apple.provenance,
	@# which causes lichess-bot's subprocess spawn to be SIGKILL'd. Strip attrs
	@# and re-sign so the OS is happy.
	@if [ "$$(uname)" = "Darwin" ]; then \
		xattr -c $(INSTALL_DIR)/carelesschess 2>/dev/null || true; \
		codesign --force -s - $(INSTALL_DIR)/carelesschess 2>/dev/null || true; \
	fi
	@echo "Installed to $(INSTALL_DIR)/carelesschess"

perft: $(BIN)
	./$(BIN) perft

debug: CXXFLAGS := -std=c++20 -O0 -g -Wall -Wextra -Isrc
debug: LDFLAGS  :=
debug: clean $(BIN)

clean:
	rm -f $(OBJ) $(TB_OBJ) $(NNUE_OBJ) $(BIN)
