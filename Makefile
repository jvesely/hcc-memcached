SRCS= \
	main.cpp \
	process.cpp

BIN=echo-client

OBJS=$(SRCS:.cpp=.o)
DEPS=$(OBJS:.o=.d)


CPPFLAGS=-Wall
CXXFLAGS=-g -O3 -Wall -pthread
LDFLAGS=-pthread

all: $(BIN)

$(BIN): $(OBJS)
	$(CXX) $^ -o $@ $(LDFLAGS)

%.o: %.cpp
	$(CXX) -c $< $(CPPFLAGS) $(CXXFLAGS) -o $@

%.d: %.cpp
	$(CXX) -MMD -MF $@ $(CPPFLAGS) $< -E > /dev/null

-include $(DEPS)

clean:
	rm -vf $(OBJS) $(BINS) $(DEPS)
