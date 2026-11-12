# Memory: Keep State Out of Internal RAM

Internal RAM is scarce, especially without PSRAM. Allocate state with external RAM as the desired capability. Without PSRAM it falls back to internal RAM automatically.

- Don't use file-scope or function-local `static` variables for state of more than ~64 bytes, or that hold buffers or containers.
- Bundle the state in a `struct` and allocate it with `memory_alloc_with_policy()` and `MEMORY_CAPABILITY_EXTERNAL`.
  In C++ use `tt::newObject<T>()` and `tt::deleteObject()` (`<tactility/Memory.hpp>`). Check for `nullptr`.
- With a lifecycle (start and stop): allocate on start, free on stop.
- Without one: allocate once on first use and never free:
  ```cpp
  static State& state() {
      static State* instance = tt::newObject<State>();
      check(instance != nullptr);
      return *instance;
  }
  ```
- Containers that live long use the `tt::std` replacements from TactilityKernel: `tt::std::vector`, `tt::std::deque`, `tt::std::map` and `tt::std::string` (`<TactilityCpp/std/*.h>`).
  They prefer external RAM. For other types use `tt::OptExternalAllocator<T>` (`<TactilityCpp/Allocator.h>`).
- Large buffers in functions with small stacks go on the heap. Don't share a static buffer between callers that can run at the same time.

Stays in internal RAM: `const` data (flash), DMA buffers, interrupt handler data, code that runs with the flash cache off, task stacks, and the mutex that guards lazy allocation.

Find large globals with `nm -S --size-sort -C -l <elf>`. Only `.data` and `.bss` count, `const` tables are in flash.
