SHELL:=/bin/bash
	CPPFLAGS=-static -g
	CC=gcc-9.1
	CPP=g++-9.1
linker:linker.cc
	(module unload $(CC);\
	module load $(CC);\
	$(CPP) $(CPPFLAGS) -o linker linker.cc)
	
clean:
	rm -f linker  

