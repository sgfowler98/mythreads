ALL= mythreads libmythreads.a
CXX=clang++
CFLAGS=-Wall -g
CXXFLAGS=-std=c++14 -g

#made clean before every make due to ar causing bugs when updating a library
all:clean $(ALL)

mythreads: mythreads.cc
	$(CXX) $(CXXFLAGS) -c -o $@ $<

libmythreads.a:
	ar -cvr libmythreads.a mythreads


clean:
	$(RM) $(ALL) $(*.o) $(*.a)
