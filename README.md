# Src2SrcTransform

Using clang libraries for source 2 source transformations

### Build LLVM & clang by using MinGW

* Unpack LLVM sources
		
		cd LLVM

		mkdir build 

* From build directory invoke:
		
		cmake -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release ..\..\llvm-4.0.0.src
		
		mingw32-make

### Build LLVM & clang by using Visual Studio 2015

		cmake -G "Visual Studio 14" ..\..\llvm-4.0.0.src		
