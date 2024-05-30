.PHONY: clean release debug all

all: build

clean:
	rm -rf cmake-build*

relwithdebinfo: | cmake-build-relwithdebinfo
	cmake --build cmake-build-relwithdebinfo --config RelWithDebInfo

cmake-build-relwithdebinfo:
	cmake -S . -G "Ninja" -B cmake-build-relwithdebinfo -DCMAKE_BUILD_TYPE=RelWithDebInfo

release: | cmake-build-release
	cmake --build cmake-build-release --config Release

cmake-build-release:
	cmake -S . -G "Ninja" -B cmake-build-release -DCMAKE_BUILD_TYPE=Release

debug: | cmake-build-debug
	cmake --build cmake-build-debug --config Debug

cmake-build-debug:
	cmake -S . -G "Ninja" -B cmake-build-debug -DCMAKE_BUILD_TYPE=Debug

build: release relwithdebinfo debug