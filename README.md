# Hera

Hera is an [eWASM](https://github.com/ethereum/evm2.0-design) virtual machine implemented in C++ conforming to the [Ethereum VM API](https://github.com/ethereum/EIPs/issues/56).

It can be used with [cpp-ethereum](https://github.com/ethereum/cpp-ethereum) and perhaps in the future with other implementations through appropriate bindings.

Currently it uses [Binaryen](https://github.com/webassembly/binaryen)'s interpreter for running WebAssembly bytecode and it should be improved to support the [WASM JIT Prototype](https://github.com/WebAssembly/wasm-jit-prototype) as a backend.

## Caveats

Although Hera enables the execution of eWASM bytecode, there are more elements to eWASM an Ethereum node must be aware of:

- [backwards compatibility](https://github.com/ethereum/evm2.0-design/blob/master/backwardsCompatibility.md) provisions
- injecting metering code to eWASM contracts
- transcompiling EVM1 contracts to eWASM if desired

All of the above must be implemented outside of Hera.

## Author(s)

Alex Beregszaszi

## License

MIT
