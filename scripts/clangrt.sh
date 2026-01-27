set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RTLIB_DIR="$SCRIPT_DIR/../tools/rt"
mkdir -p "$RTLIB_DIR"

CLANG_VERSION=$(clang -dumpversion 2>/dev/null)
RTLIB_VERSION_FILE="${RTLIB_DIR}/.libclang_rt-ver"

if [[ -f "$RTLIB_VERSION_FILE" ]] && [[ "$(cat "$RTLIB_VERSION_FILE")" == "$CLANG_VERSION" ]]; then
	exit 0
fi

MAJOR=$(echo "$CLANG_VERSION" | cut -d. -f1)
MINOR=$(echo "$CLANG_VERSION" | cut -d. -f2)

if ! git clone https://github.com/llvm/llvm-project.git --branch=release/${MAJOR}.${MINOR}.x --depth=1; then
	if ! git clone https://github.com/llvm/llvm-project.git --branch=release/${MAJOR}.x --depth=1; then
		echo "Failed to clone LLVM"
		exit 1
	fi
fi

mkdir -p llvm-project/compiler-rt-build
cd llvm-project/compiler-rt-build

cmake ../compiler-rt -G Ninja \
	-DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_SYSTEM_NAME=Generic \
	-DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
	-DCOMPILER_RT_BUILD_BUILTINS=ON \
	-DCOMPILER_RT_BUILD_SANITIZERS=OFF \
	-DCOMPILER_RT_BUILD_XRAY=OFF \
	-DCOMPILER_RT_BUILD_PROFILE=OFF \
	-DCOMPILER_RT_BAREMETAL_BUILD=ON \
	-DCOMPILER_RT_DEFAULT_TARGET_ONLY=ON \
	-DCMAKE_C_COMPILER_TARGET=x86_64-unknown-elf -DCMAKE_CXX_COMPILER_TARGET=x86_64-unknown-elf \
	-DCMAKE_C_FLAGS="-mno-red-zone -fPIC" -DCMAKE_CXX_FLAGS="-mno-red-zone -fPIC" \
	-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY
ninja

cp lib/generic/libclang_rt.builtins-x86_64.a "$RTLIB_DIR"
echo "$CLANG_VERSION" > $RTLIB_VERSION_FILE

cd ../..
rm -rf ./llvm-project
