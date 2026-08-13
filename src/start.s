// Minimal NRO homebrew entry point -- no libnx.
//
// Per the Homebrew ABI (switchbrew.org/wiki/Homebrew_ABI), hbloader calls
// the entry point with x0=env context ptr, x1=0xFFFFFFFFFFFFFFFF, lr=return
// address into the loader. A well-behaved app returns to that lr with
// x0=an error code (0 = success) once it's done -- there's no need to call
// svcExitProcess as long as lr is valid, which it always is on normal NRO
// entry.
//
// libnx's own crt0 (source/runtime/switch_crt0.s in the libnx repo) does
// self-relocation via a MOD0 header before calling into C, because a real
// program has data/GOT/rela.dyn references that need fixing up once loaded
// at ASLR-chosen base. This program has none of that -- no globals, no
// calls to other addresses, nothing position-dependent -- so there is
// nothing to relocate and MOD0 can be safely omitted. This is deliberately
// the smallest possible thing elf2nro will accept, to validate the
// packaging pipeline (Milestone 2) before any real libnx-based runtime
// exists.
.section .text.start, "ax", %progbits
.global _start
.align 2
_start:
    mov x0, xzr
    ret
