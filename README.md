# Hera ![Status](https://circleci.com/gh/ewasm/hera.svg?style=shield&circle-token=:circle-token)

Hera is an [ewasm] (revision 4) virtual machine implemented in C++ conforming to [EVMC] ABIv5.

It is design to leverage various Wasm backends, both interpreters and AOT/JITs.

## Client support

Hera has been tested with [aleth]. It should however work with any client with compliant [EVMC] support.

## Build options

- `-DHERA_DEBUGGING=ON` will turn on debugging features and messages
- `-DBUILD_SHARED_LIBS=ON` is a standard CMake option to build libraries as shared. This will build Hera shared library that can be then dynamically loaded by EVMC compatible Clients (e.g. `aleth` from [aleth]). **This is the preferred way of compilation.**

### Binaryen support

*Complete support.*

[Binaryen] is always built and needs no build options.

### wabt support

*Limited support, work in progress.*

[wabt] support needs to be enabled via the following build option and requested at runtime with `engine=wabt`:

- `-DHERA_WABT=ON` will request the compilation of wabt support

### WAVM support

*Unfinished support, work in progress.*

[WAVM] support needs to be enabled via the following build option and requested at runtime with `engine=wavm`:

- `-DHERA_WAVM=ON` will request the compilation of WAVM support
- `-DLLVM_DIR=...` one will need to specify the path to LLVM's CMake file. In most installations this has to be within the `lib/cmake/llvm` directory, such as `/usr/local/Cellar/llvm/6.0.1/lib/cmake/llvm` on Homebrew.

## Runtime options

These are to be used via EVMC `set_option`:

- `engine=<engine>` will select the underlying WebAssembly engine, where the only accepted values currently are `binaryen` and `wabt`
- `metering=true` will enable metering of bytecode at deployment using the [Sentinel system contract] (set to `false` by default)
- `evm1mode=<evm1mode>` will select how EVM1 bytecode is handled
- `sys:<alias/address>=file.wasm` will override the code executing at the specified address with code loaded from a filepath at runtime. This option supports aliases for system contracts as well, such that `sys:sentinel=file.wasm` and `sys:evm2wasm=file.wasm` are both valid. **This option is intended for debugging purposes.**

### evm1mode

- `reject` will reject any EVM1 bytecode with an error (the default setting)
- `fallback` will allow EVM1 bytecode to be passed through to the client for execution
- `evm2wasm` will enable transformation of bytecode using the [EVM Transcompiler]
- `evm2wasm.js` will use a `evm2wasm.js` as an external commandline tool instead of the system contract
- `evm2wasm.js-trace` will use `evm2wasm.js` with tracing option turned on
- `evm2wasm.cpp` will use a `evm2wasm` as a compiled-in dependency instead of the system contract
- `evm2wasm.cpp-trace` will turn use `evm2wasm` with tracing option turned on

## Interfaces

Hera implements two interfaces: [EEI] and a debugging module.

### Debugging module

- `debug::print32(value: i32)` - print value
- `debug::print64(value: i64)` - print value
- `debug::printMem(offset: i32, len: i32)` - print memory segment as printable characters
- `debug::printMemHex(offset: i32, len: i32)` - print memory segment as hex
- `debug::printStorage(pathOffset: i32)` - print storage value as printable characters
- `debug::printStorageHex(pathOffset: i32)` - print storage value as hex

These are only enabled if Hera is compiled with debugging on.

### EVM Tracing

- `debug::evmTrace(pc: i32, opcode: i32, cost: i32, sp: i32)`

This is useful to trace the translated code by [EVM Transcompiler]. This is only enabled if Hera is compiled with debugging on.

**Note:** it is valid to invoke `evmTrace` with a negative value for `sp`.  In this case, no stack values will be printed.

## Author(s)

Alex Beregszaszi, Jake Lang

## License

Apache 2.0

[ewasm]: https://github.com/ewasm/design
[EVMC]: https://github.com/ethereum/evmc
[aleth]: https://github.com/ethereum/aleth
[Binaryen]: https://github.com/webassembly/binaryen
[wabt]: https://github.com/webassembly/wabt
[WAVM]: https://github.com/AndrewScheidecker/WAVM
[Sentinel system contract]: https://github.com/ewasm/design/blob/master/system_contracts.md#sentinel-contract
[EVM Transcompiler]: https://github.com/ewasm/design/blob/master/system_contracts.md#evm-transcompiler
[EEI]: https://github.com/ewasm/design/blob/master/eth_interface.md
