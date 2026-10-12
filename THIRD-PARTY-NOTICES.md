# Third-Party Libraries

This project uses the following third-party libraries:

1. **mimalloc**
   - Copyright (c) 2018-2025 Microsoft Corporation, Daan Leijen
   - License: MIT
   - Repository: https://github.com/microsoft/mimalloc
   - License Location: [`third-party/mimalloc/LICENSE`](./third-party/mimalloc/LICENSE)
   - Notice: This project includes mimalloc, a high-performance memory allocator. The mimalloc library is distributed under the MIT License. Please refer to the mimalloc repository for the full license text.

2. **xxHash**
   - Copyright (c) 2012-2021 Yann Collet
   - License: BSD 2-Clause
   - Repository: https://github.com/Cyan4973/xxHash
   - License Location: [`third-party/xxHash/LICENSE`](./third-party/xxHash/LICENSE)
   - Notice: This project includes xxHash, an extremely fast non-cryptographic hash algorithm. The xxHash library is distributed under the BSD 2-Clause License. Please refer to the xxHash repository for the full license text.

3. **memAllocator**
   - Copyright (c) 2025-2026 IMSDcrueoft
   - License: MIT
   - Repository: https://github.com/IMSDcrueoft (authored in-repo)
   - License Location: [`third-party/memAllocator/LICENSE`](./third-party/memAllocator/LICENSE)
   - Notice: This project includes memAllocator (`palloc_os`, `arena_bump`, `bits`), a thin OS virtual-memory primitive layer (mmap/mprotect/madvise on POSIX, VirtualAlloc/VirtualFree on Windows) topped by a LIFO bump arena. It backs the fixed-base vm stack: virtual address space is reserved once and never moves, physical pages are committed lazily in chunk steps. Distributed under the MIT License; please refer to the LICENSE file for the full license text.