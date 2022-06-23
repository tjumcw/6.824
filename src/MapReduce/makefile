libmrFunc.so:map_reduceFun.o
	@g++ -shared map_reduceFun.o -o libmrFunc.so
map_reduceFun.o:map_reduceFun.cpp
	@g++ -fpic -c map_reduceFun.cpp

.PHONY:clean
clean:
	rm *.o *.so