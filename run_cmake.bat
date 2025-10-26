@echo off
pushd build
cmake .. -DCMAKE_BUILD_TYPE=Debug -G Ninja
popd
@echo on
