CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -MMD -MP -pthread
CXXFLAGS += -Iinc -I$(THIRD_PARTY) -I$(RMQ)/include
LDFLAGS = -pthread

CDR_GEN = generator

BUILD     = build
OBJDIR    = $(BUILD)/obj
BIN       = $(BUILD)/main
TEST_BIN  = $(BUILD)/tests

SRC = $(shell find src -name '*.cpp')
OBJ = $(patsubst src/%.cpp,$(OBJDIR)/%.o,$(SRC))

LIB_OBJ = $(filter-out $(OBJDIR)/main.o,$(OBJ))
TEST_SRC = $(shell find tests -name '*.cpp')
TEST_OBJ = $(patsubst tests/%.cpp,$(OBJDIR)/tests/%.o,$(TEST_SRC))
DEPS = $(OBJ:.o=.d) $(TEST_OBJ:.o=.d) $(RMQ_OBJS:.o=.d)

THIRD_PARTY = third_party
RMQ = $(THIRD_PARTY)/rabbitmq-c
RMQ_SRCS = $(wildcard $(RMQ)/src/*.c)
RMQ_OBJS = $(patsubst $(RMQ)/src/%.c,$(BUILD)/rmq/%.o,$(RMQ_SRCS))

.PHONY: all build run test clean debug release gen

all: $(BIN) $(TEST_BIN)

build: $(BIN)

$(BUILD)/rmq/%.o: $(RMQ)/src/%.c
	@mkdir -p $(@D)
	$(CC) -I$(RMQ)/include -DAMQP_STATIC -DHAVE_CONFIG_H -O2 -w -MMD -MP -c $< -o $@

$(BIN): $(OBJ) $(RMQ_OBJS)
	@mkdir -p $(@D)
	$(CXX) $^ -o $@ $(LDFLAGS)

$(TEST_BIN): $(TEST_OBJ) $(LIB_OBJ) $(RMQ_OBJS)
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

gen:
	@python3 -m $(CDR_GEN) $(ARGS)

debug:   CXXFLAGS += -g -O0 -fsanitize=address,undefined
debug:   LDFLAGS  += -fsanitize=address,undefined
debug:   build

release: CXXFLAGS += -O2 -DNDEBUG
release: build

clean:
	rm -rf $(BUILD)

-include $(DEPS)
