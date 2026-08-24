CXX = g++
CXXFLAGS := -g -Wall -Wextra -std=c++17
OBJS = builtin.o executor.o parser.o run_npshell.o

all: npshell np_simple

npshell: npshell.cpp $(OBJS)
	$(CXX) $(CXXFLAGS) npshell.cpp $(OBJS) -o npshell
	
np_simple: np_simple.cpp $(OBJS)
	$(CXX) $(CXXFLAGS) np_simple.cpp $(OBJS) -o np_simple

run_npshell.o: run_npshell.cpp run_npshell.hpp
	$(CXX) $(CXXFLAGS) -c run_npshell.cpp

builtin.o: builtin.cpp builtin.hpp types.hpp
	$(CXX) $(CXXFLAGS) -c builtin.cpp

executor.o: executor.cpp executor.hpp types.hpp
	$(CXX) $(CXXFLAGS) -c executor.cpp

parser.o: parser.cpp parser.hpp types.hpp
	$(CXX) $(CXXFLAGS) -c parser.cpp

clean:
	rm -f *.o npshell np_simple