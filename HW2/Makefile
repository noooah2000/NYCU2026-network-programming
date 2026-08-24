CXX = g++
CXXFLAGS := -g -Wall -Wextra -std=c++17
LDFLAGS = -lrt

OBJDIR = obj

COMMON_OBJS = $(OBJDIR)/parser.o
SINGLE_OBJS = $(OBJDIR)/executor_single.o $(OBJDIR)/builtin_single.o
MULTI_OBJS = $(OBJDIR)/executor_multi.o $(OBJDIR)/builtin_multi.o $(OBJDIR)/run_npshell.o

all: np_single_proc np_multi_proc

np_single_proc: np_single_proc.cpp $(COMMON_OBJS) $(SINGLE_OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $@

np_multi_proc: np_multi_proc.cpp $(COMMON_OBJS) $(MULTI_OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

$(OBJDIR)/run_npshell.o: run_npshell.cpp run_npshell.hpp types.hpp
	@mkdir -p $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJDIR)/parser.o: parser.cpp parser.hpp types.hpp
	@mkdir -p $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJDIR)/executor_single.o: executor_single.cpp executor.hpp types.hpp
	@mkdir -p $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJDIR)/executor_multi.o: executor_multi.cpp executor.hpp types.hpp
	@mkdir -p $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJDIR)/builtin_single.o: builtin_single.cpp builtin.hpp types.hpp
	@mkdir -p $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJDIR)/builtin_multi.o: builtin_multi.cpp builtin.hpp types.hpp
	@mkdir -p $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(OBJDIR) np_single_proc np_multi_proc