CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -MMD -MP -pthread 
CXXFLAGS += -Iinc -Iinc/third_party
LDFLAGS = -pthread

CDR_GEN = scripts/generate_cdrs.py

BUILD     = build
OBJDIR    = $(BUILD)/obj
BIN       = $(BUILD)/main
TEST_BIN  = $(BUILD)/tests

SRC = $(wildcard src/*.cpp)
OBJ = $(patsubst src/%.cpp,$(OBJDIR)/%.o,$(SRC))

LIB_OBJ = $(filter-out $(OBJDIR)/main.o,$(OBJ))
TEST_SRC = $(wildcard tests/*.cpp)
TEST_OBJ = $(patsubst tests/%.cpp,$(OBJDIR)/tests/%.o,$(TEST_SRC))
DEPS = $(OBJ:.o=.d) $(TEST_OBJ:.o=.d)

.PHONY: all build run test clean debug release

all: build

build: $(BIN)

$(BIN): $(OBJ)
	@mkdir -p $(@D)
	$(CXX) $^ -o $@ $(LDFLAGS)

$(TEST_BIN): $(TEST_OBJ) $(LIB_OBJ)
	@mkdir -p $(@D)
	$(CXX) $^ -o $@ $(LDFLAGS)

$(OBJDIR)/%.o: src/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJDIR)/tests/%.o: tests/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c $< -o $@

run: $(BIN)
	./$(BIN)

test: $(TEST_BIN)
	./$(TEST_BIN)

generate:
	@python3 $(CDR_GEN) $(ARGS)

debug:   CXXFLAGS += -g -O0 -fsanitize=address,undefined
debug:   LDFLAGS  += -fsanitize=address,undefined
debug:   build

release: CXXFLAGS += -O2 -DNDEBUG
release: build

clean:
	rm -rf $(BUILD)

-include $(DEPS)
